from __future__ import annotations

import threading
import time
from collections import deque

import cv2

from .settings import CaptureSettings


def fourcc_label(value):
    value = int(value) & 0xFFFFFFFF
    # DirectShow reports RGB media-subtype GUID Data1 values, not text FOURCC.
    if value in {0xE436EB7D, 0xE436EB7E}:
        return "RGB3" if value == 0xE436EB7D else "RGB4"
    label = "".join(chr((value >> (8 * i)) & 255) for i in range(4)).strip("\x00")
    return label if all(32 <= ord(char) <= 126 for char in label) else f"0x{value:08X}"


class CaptureReader:
    """Continuously drain DirectShow into one latest frame, without a growing queue."""

    def __init__(self, settings: CaptureSettings, factory=None):
        self.settings = settings
        self._factory = factory or cv2.VideoCapture
        self._custom_factory = factory is not None
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._ready = threading.Event()
        self._thread = None
        self._frame = None
        self._stamp = 0.0
        self.sequence = 0
        self.raw_size = (0, 0)
        self.error = "Not connected"
        self.actual = {}
        self._capture = None
        self._arrivals = deque(maxlen=240)
        self._processing_ms = deque(maxlen=240)
        self._delivery_ms = deque(maxlen=240)

    def start(self, timeout=5.0):
        if self._thread is None:
            self._thread = threading.Thread(target=self._read, name="GC573Capture", daemon=True)
            self._thread.start()
        self._ready.wait(timeout)
        if not self.connected():
            raise RuntimeError(f"External video capture unavailable: {self.error}")

    def _read(self):
        capture = None
        try:
            c = self.settings
            backend = cv2.CAP_DSHOW if c.backend == "DSHOW" else cv2.CAP_MSMF
            selected_index, selected_name = c.device_index, c.device_name
            if not self._custom_factory:
                from .video_devices import select_video
                selected_index, backend, selected_name = select_video(c)
            if c.backend == "FFMPEG":
                from .ffmpeg_capture import FFmpegCamera
                from dataclasses import replace
                capture = FFmpegCamera(replace(c, device_name=selected_name))
            else:
                capture = self._factory(selected_index, backend)
            self._capture = capture
            if not capture.isOpened():
                raise RuntimeError("采集卡无法打开。请先关闭旧版本，或在旧版本点击“终止线程”，并关闭占用采集卡的 OBS / RECentral 等软件；同一采集卡只保留一个连接实例，再重新启动线程。")
            capture.set(cv2.CAP_PROP_FRAME_WIDTH, c.width)
            capture.set(cv2.CAP_PROP_FRAME_HEIGHT, c.height)
            capture.set(cv2.CAP_PROP_FPS, c.fps)
            # OpenCV DirectShow's FPS setter rebuilds the graph with its default
            # subtype (RGB24), discarding a previously selected NV12. Apply the
            # requested format last, retaining the configured frame interval.
            if c.fourcc:
                code = cv2.VideoWriter_fourcc(*c.fourcc)
                if c.backend == "DSHOW" and c.fourcc in {"RGB3", "RGB4"}:
                    code = {"RGB3": 0xE436EB7D, "RGB4": 0xE436EB7E}[c.fourcc] - (1 << 32)
                capture.set(cv2.CAP_PROP_FOURCC, code)
            # DirectShow may ignore this property. The reader thread, rather than
            # this setting, is what prevents a Python-side frame backlog.
            capture.set(cv2.CAP_PROP_BUFFERSIZE, 1)
            self.actual = {"width": capture.get(cv2.CAP_PROP_FRAME_WIDTH),
                           "height": capture.get(cv2.CAP_PROP_FRAME_HEIGHT),
                           "fps": capture.get(cv2.CAP_PROP_FPS), "backend": c.backend,
                           "device_index": selected_index, "device_name": selected_name,
                           "fps_scope": "backend property; DirectShow may echo the requested frame interval"}
            self.actual["fourcc"] = fourcc_label(capture.get(cv2.CAP_PROP_FOURCC))
            while not self._stop.is_set():
                success, frame = capture.read()
                received_at = time.perf_counter()
                if not success or frame is None:
                    self.error = "Video read failed or input signal was lost"
                    self._stop.wait(0.01)
                    continue
                if frame.ndim != 3 or frame.shape[2] < 3:
                    raise ValueError(f"Expected a BGR image, received shape {frame.shape}")
                raw_height, raw_width = frame.shape[:2]
                self.actual["frame_width"], self.actual["frame_height"] = raw_width, raw_height
                if c.crop:
                    x, y, width, height = c.crop
                    if x + width > raw_width or y + height > raw_height:
                        raise ValueError("Configured crop extends beyond the captured image")
                    frame = frame[y:y + height, x:x + width]
                if abs(frame.shape[1] / frame.shape[0] - c.output_width / c.output_height) > 0.02:
                    raise ValueError("Capture/crop aspect ratio differs from inference resolution; configure crop instead of stretching")
                if frame.shape[2] == 4:
                    frame = cv2.cvtColor(frame, cv2.COLOR_BGRA2BGR)
                elif frame.shape[2] != 3:
                    raise ValueError("Capture must deliver BGR or BGRA pixels")
                if frame.shape[1::-1] != (c.output_width, c.output_height):
                    frame = cv2.resize(frame, (c.output_width, c.output_height), interpolation=cv2.INTER_AREA)
                with self._lock:
                    # VideoCapture.read()/resize return independently owned
                    # arrays. Retain that frame and copy only for the consumer.
                    self._frame = frame
                    self.raw_size = (raw_width, raw_height)
                    self._stamp = received_at
                    self._arrivals.append(received_at)
                    self._processing_ms.append((time.perf_counter() - received_at) * 1000)
                    self.sequence += 1
                    self.error = ""
                self._ready.set()
        except Exception as error:
            self.error = str(error)
            self._ready.set()
        finally:
            if capture is not None:
                capture.release()
            self._stop.set()

    def connected(self):
        with self._lock:
            return self._frame is not None and not self._stop.is_set() and time.perf_counter() - self._stamp <= self.settings.stale_after

    def get_frame(self):
        with self._lock:
            if self._frame is None or self._stop.is_set() or time.perf_counter() - self._stamp > self.settings.stale_after:
                return None
            frame, stamp = self._frame, self._stamp
        # Retain a local reference, so copying a full image cannot block the
        # producer from publishing its next frame under the shared lock.
        result = frame.copy()
        with self._lock:
            self._delivery_ms.append((time.perf_counter() - stamp) * 1000)
        return result

    def preview_frame(self, width, height, after_sequence=None):
        """Resize only a fresh, new frame for display; keep task copies separate."""
        with self._lock:
            if (self._frame is None or self._stop.is_set()
                    or time.perf_counter() - self._stamp > self.settings.stale_after
                    or self.sequence == after_sequence):
                return None
            frame, sequence = self._frame, self.sequence
        # Published frames are never modified by the reader. Retain a reference
        # and resize outside the lock, without first copying a full-size image.
        scale = min(1.0, max(1, width) / frame.shape[1], max(1, height) / frame.shape[0])
        size = (max(1, round(frame.shape[1] * scale)), max(1, round(frame.shape[0] * scale)))
        result = cv2.resize(frame, size, interpolation=cv2.INTER_AREA) if scale < 1 else frame.copy()
        return sequence, result

    def statistics(self):
        """Software measurements start after the driver has returned a frame."""
        import numpy as np
        with self._lock:
            times = list(self._arrivals)
            processing, delivery = list(self._processing_ms), list(self._delivery_ms)
        return {"measured_fps": round((len(times) - 1) / (times[-1] - times[0]), 2) if len(times) > 1 and times[-1] > times[0] else None,
                "preprocess_p95_ms": round(float(np.percentile(processing, 95)), 3) if processing else None,
                "delivery_age_p95_ms": round(float(np.percentile(delivery, 95)), 3) if delivery else None,
                "latest_age_ms": round(self.age_ms, 3) if self.age_ms is not None else None,
                "frames_received": self.sequence,
                "scope": "driver-return-to-consumer only; HDMI/driver buffering and display latency are excluded"}

    @property
    def age_ms(self):
        return (time.perf_counter() - self._stamp) * 1000 if self._stamp else None

    def screen_point(self, x, y, desktop_width, desktop_height):
        c = self.settings
        if not 0 <= x < c.output_width or not 0 <= y < c.output_height:
            raise ValueError(f"Point outside inference image: {(x, y)}")
        raw_width, raw_height = self.raw_size
        if not raw_width or not raw_height:
            raise RuntimeError("No capture dimensions available")
        cx, cy, cw, ch = c.crop or (0, 0, raw_width, raw_height)
        px = (cx + x * cw / c.output_width) * desktop_width / raw_width
        py = (cy + y * ch / c.output_height) * desktop_height / raw_height
        return min(desktop_width - 1, round(px)), min(desktop_height - 1, round(py))

    def close(self):
        self._stop.set()
        if self.settings.backend == "FFMPEG" and self._capture is not None:
            # Closing the child process interrupts a blocked raw-pipe read.
            self._capture.release()
        if self._thread is not None and self._thread is not threading.current_thread():
            self._thread.join(timeout=2)

    @property
    def closed(self):
        return self._thread is None or not self._thread.is_alive()
