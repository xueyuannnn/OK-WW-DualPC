"""Python task facade; only the C++ host opens capture/HID devices."""
from __future__ import annotations

import json
import mmap
import os
import socket
import threading
import time

import cv2
import numpy as np


class NativeChannel:
    def __init__(self):
        self.lock = threading.RLock()
        self.token = os.environ["OKWW_NATIVE_TOKEN"]
        self.socket = socket.create_connection(("127.0.0.1", int(os.environ["OKWW_NATIVE_PORT"])), timeout=12)
        self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.stream = self.socket.makefile("rb")
        self.memory = mmap.mmap(-1, 64 * 1024 * 1024, tagname=os.environ["OKWW_NATIVE_MAPPING"], access=mmap.ACCESS_READ)
        self.state = {}
        self.state_time = 0.0

    def request(self, op, **values):
        with self.lock:
            packet = dict(values, op=op, token=self.token)
            self.socket.sendall(json.dumps(packet, separators=(",", ":")).encode() + b"\n")
            line = self.stream.readline(65537)
            if not line or len(line) > 65536:
                raise ConnectionError("C++ 主程序的连接已关闭")
            reply = json.loads(line)
            if not reply.get("ok"):
                raise RuntimeError(reply.get("error", "原生通道调用失败"))
            result = reply["result"]
            if op in {"state", "heartbeat", "connect", "probe"}:
                self.state, self.state_time = result, time.monotonic()
            return result

    def frame(self):
        # The server writes the map only for this request, and the lock covers
        # the copy. Other task threads cannot replace it halfway through a read.
        with self.lock:
            meta = self.request("frame")
            if not meta.get("available"):
                return None, None
            size = meta["size"]
            if not 0 < size <= len(self.memory):
                raise ValueError("无效原生帧大小")
            return meta, self.memory[:size]

    def fresh(self):
        return time.monotonic() - self.state_time < 1.5

    def close(self):
        self.memory.close()
        self.stream.close()
        self.socket.close()


def decode_frame(meta, data):
    width, height, stride = (int(meta[k]) for k in ("width", "height", "stride"))
    fmt = meta["format"]
    if min(width, height, stride) <= 0:
        raise ValueError("无效图像尺寸")
    raw = np.frombuffer(data, dtype=np.uint8)
    if fmt == "MJPG":
        image = cv2.imdecode(raw, cv2.IMREAD_COLOR)
        if image is None or image.shape[:2] != (height, width):
            raise ValueError("MJPEG 解码尺寸不匹配")
        return image
    rows = height * 3 // 2 if fmt in {"NV12", "I420", "P010"} else height
    if len(raw) < rows * stride:
        raise ValueError("采集图像数据不完整")
    packed = raw[:rows * stride].reshape(rows, stride)
    if fmt in {"RGB3", "RGB4"}:
        channels = 3 if fmt == "RGB3" else 4
        image = packed[:height, :width * channels].reshape(height, width, channels)
        if meta.get("bottom_up"):
            image = image[::-1]
        return np.ascontiguousarray(image[:, :, :3])
    if width % 2 or (fmt in {"NV12", "I420", "P010"} and height % 2):
        raise ValueError("YUV 格式需要偶数尺寸")
    if fmt in {"YUY2", "UYVY"}:
        return cv2.cvtColor(np.ascontiguousarray(packed[:, :width * 2]).reshape(height, width, 2),
                            cv2.COLOR_YUV2BGR_YUY2 if fmt == "YUY2" else cv2.COLOR_YUV2BGR_UYVY)
    if fmt == "P010":
        packed = (packed.copy().view(np.uint16) >> 8).astype(np.uint8)
        fmt = "NV12"
    if fmt == "NV12":
        return cv2.cvtColorTwoPlane(np.ascontiguousarray(packed[:height, :width]),
                                   np.ascontiguousarray(packed[height:, :width]).reshape(height // 2, width // 2, 2),
                                   cv2.COLOR_YUV2BGR_NV12)
    if fmt == "I420":
        y = packed[:height, :width].reshape(-1)
        tail = raw[height * stride:]
        uv_size = height * stride // 4
        u = tail[:uv_size].reshape(height // 2, stride // 2)[:, :width // 2].reshape(-1)
        v = tail[uv_size:uv_size * 2].reshape(height // 2, stride // 2)[:, :width // 2].reshape(-1)
        return cv2.cvtColor(np.concatenate((y, u, v)).reshape(height * 3 // 2, width), cv2.COLOR_YUV2BGR_I420)
    raise ValueError(f"不支持的原生图像格式：{fmt}")


def install_native_io(channel, direct_input=False):
    from dualpc.capture import CaptureReader
    from dualpc.kmbox import KMBoxNet
    import dualpc.capture
    import dualpc.controllers

    class NativeReader(CaptureReader):
        def __init__(self, settings, **kwargs):
            self.settings = settings
            self.raw_size = (0, 0)
            self.actual = {}
            self.sequence = 0
            self.error = ""
            self._closed = True

        def start(self, timeout=5):
            state = channel.request("state")
            if not state.get("capture_connected"):
                raise RuntimeError("请在 C++ 双机页启动连接线程")
            self._closed = False
            self.get_frame()

        def connected(self):
            return not self._closed and channel.fresh() and channel.state.get("capture_connected", False)

        def get_frame(self):
            if self._closed:
                return None
            meta, raw = channel.frame()
            if meta is None:
                return None
            image = decode_frame(meta, raw)
            self.raw_size = (meta["width"], meta["height"])
            self.sequence = int(meta["sequence"])
            self.actual = dict(channel.state.get("actual", {}), frame_width=meta["width"], frame_height=meta["height"])
            if self.settings.crop:
                x, y, w, h = self.settings.crop
                if x < 0 or y < 0 or min(w, h) <= 0 or x + w > image.shape[1] or y + h > image.shape[0]:
                    raise ValueError("裁剪范围超出采集画面")
                image = image[y:y+h, x:x+w]
            if abs(image.shape[1] / image.shape[0] - self.settings.output_width / self.settings.output_height) > .02:
                raise ValueError("采集画面与识别画面比例不同，请设置裁剪")
            size = (self.settings.output_width, self.settings.output_height)
            return cv2.resize(image, size, interpolation=cv2.INTER_AREA) if image.shape[1::-1] != size else image

        def close(self):
            self._closed = True

        @property
        def closed(self):
            return self._closed

        @property
        def age_ms(self):
            return channel.state.get("age_ms")

        def statistics(self):
            return dict(channel.state.get("actual", {}), frames_received=self.sequence, latest_age_ms=self.age_ms)

    class NativeController(KMBoxNet):
        def __init__(self, settings):
            # Reuse only the controller facade/state, never open its UDP socket.
            super().__init__(settings)
            self.supports_absolute = False
            self.supports_keyboard = True
            self.max_keys = 10

        def connect(self):
            state = channel.request("connect")["box"]
            if not state.get("connected"):
                raise RuntimeError("C++ 盒子尚未连接")
            self.connected = True
            self.supports_keyboard = state.get("keyboard", False)
            self.capability_status = state.get("capability", "")

        def move(self, x, y):
            channel.request("move", x=int(x), y=int(y))

        def absolute(self, x, y, width, height):
            channel.request("absolute", x=int(x), y=int(y), width=int(width), height=int(height))

        def key(self, code, down):
            state = channel.request("key", code=int(code), down=bool(down))
            self.keys = set(state["keys"])
            self.modifiers = state["modifiers"]

        def button(self, name, down):
            state = channel.request("button", name=name, down=bool(down))
            self.buttons = state["buttons"]

        def wheel(self, count):
            channel.request("wheel", steps=int(count))

        def probe(self):
            state = channel.request("probe")
            self.connected = state["box"].get("connected", False)

        def release_all(self):
            channel.request("release")
            self.buttons = self.modifiers = 0
            self.keys.clear()

        def close(self, release=True):
            if release and self.connected:
                self.release_all()
            self.connected = False

    dualpc.capture.CaptureReader = NativeReader
    dualpc.controllers.create_controller = NativeController
    # Modules may already have imported these names during compatibility hooks.
    import dualpc.device
    import dualpc.interaction
    dualpc.device.CaptureReader = NativeReader
    dualpc.interaction.create_controller = NativeController
    if direct_input:
        class NativeInteraction(dualpc.interaction.KMBoxInteraction):
            def _native_input(self, operation, **values):
                self._require_active()
                state = channel.request('script_input', operation=operation, **values)
                self.client.keys = set(state.get('keys', []))
                self.client.buttons = state.get('buttons', 0)
                self.client.modifiers = state.get('modifiers', 0)

            def move(self, x, y):
                self._native_input('move', x=x, y=y)
                self.mouse_pos = (x, y)

            def mouse_down(self, x=-1, y=-1, name=None, key='left'):
                self._native_input('mouse_down', x=x, y=y, key=key)

            def mouse_up(self, name=None, key='left'):
                self._native_input('mouse_up', key=key)

            def click(self, x=-1, y=-1, move_back=False, name=None, move=True, down_time=.05, key='left'):
                previous = self.mouse_pos
                positioned = move and x >= 0 and y >= 0
                self._native_input('click', x=x if positioned else -1, y=y if positioned else -1,
                                   seconds=down_time, key=key)
                if positioned:
                    self.mouse_pos = (x, y)
                if move_back and previous is not None:
                    self.move(*previous)

            def send_key_down(self, key):
                self._native_input('key_down', key=key)

            def send_key_up(self, key):
                self._native_input('key_up', key=key)

            def send_key(self, key, down_time=.02):
                self._native_input('key', key=key, seconds=down_time)

            def scroll(self, x, y, scroll_amount):
                self._native_input('scroll', x=x, y=y, steps=scroll_amount)

            def swipe(self, from_x, from_y, to_x, to_y, duration, settle_time=0):
                self._native_input('swipe', x=from_x, y=from_y, to_x=to_x, to_y=to_y,
                                   seconds=duration, settle=settle_time)

            def input_text(self, text):
                self._native_input('text', text=text)

        dualpc.device.KMBoxInteraction = NativeInteraction
    return NativeReader, NativeController
