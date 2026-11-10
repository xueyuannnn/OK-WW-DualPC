"""Optional generic DirectShow capture through an FFmpeg raw-video pipe."""
import subprocess

import cv2
import numpy as np


class FFmpegCamera:
    def __init__(self, settings, popen_factory=subprocess.Popen):
        self.settings = settings
        c = settings
        command = [c.ffmpeg_path, "-hide_banner", "-loglevel", "error", "-nostdin",
                   "-fflags", "nobuffer", "-flags", "low_delay", "-probesize", "32", "-analyzeduration", "0",
                   "-f", "dshow", "-rtbufsize", str(max(8 * 1024 * 1024, c.width * c.height * 4 * 2)), "-thread_queue_size", "1",
                   "-video_size", f"{c.width}x{c.height}", "-framerate", str(c.fps)]
        if c.fourcc == "MJPG":
            command += ["-vcodec", "mjpeg"]
        elif c.fourcc:
            formats = {"YUY2": "yuyv422", "UYVY": "uyvy422", "NV12": "nv12", "P010": "p010le",
                       "I420": "yuv420p", "RGB3": "bgr24", "RGB4": "bgr0"}
            if c.fourcc not in formats:
                raise ValueError(f"FFmpeg input format {c.fourcc!r} is not mapped; select automatic or another format")
            command += ["-pixel_format", formats[c.fourcc]]
        command += ["-i", f"video={c.device_name}", "-an", "-sn", "-dn", "-vsync", "0", "-pix_fmt", "bgr24", "-f", "rawvideo", "pipe:1"]
        self.command = command
        self.process = popen_factory(command, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0,
                                     creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))

    def isOpened(self):
        return self.process.poll() is None

    def set(self, key, value):
        return False

    def get(self, key):
        return {cv2.CAP_PROP_FRAME_WIDTH: self.settings.width,
                cv2.CAP_PROP_FRAME_HEIGHT: self.settings.height,
                cv2.CAP_PROP_FPS: self.settings.fps}.get(key, 0)

    def read(self):
        expected = self.settings.width * self.settings.height * 3
        buffer = bytearray(expected)
        view = memoryview(buffer)
        offset = 0
        while offset < expected:
            count = self.process.stdout.readinto(view[offset:])
            if not count:
                return False, None
            offset += count
        return True, np.frombuffer(buffer, dtype=np.uint8).reshape(self.settings.height, self.settings.width, 3)

    def release(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=2)
        if self.process.stdout:
            self.process.stdout.close()
