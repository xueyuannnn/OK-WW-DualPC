import io
from types import SimpleNamespace

import numpy as np
import pytest

from dualpc.ffmpeg_capture import FFmpegCamera
from dualpc.settings import CaptureSettings


class FragmentedPipe(io.BytesIO):
    def readinto(self, target):
        return super().readinto(target[:7])


class FakeProcess:
    def __init__(self, *args, **kwargs):
        self.command = args[0]
        self.stdout = FragmentedPipe(bytes(range(36)))
        self.code = None

    def poll(self):
        return self.code

    def terminate(self):
        self.code = 0

    def wait(self, timeout):
        return self.code


def test_ffmpeg_pipe_handles_partial_reads_and_device_name_spaces():
    settings = CaptureSettings(backend="FFMPEG", width=4, height=3, device_name="Capture Card (Video) 1")
    camera = FFmpegCamera(settings, popen_factory=FakeProcess)
    success, frame = camera.read()
    assert success and frame.shape == (3, 4, 3)
    assert frame.reshape(-1).tolist() == list(range(36))
    assert "video=Capture Card (Video) 1" in camera.command
    assert camera.command[camera.command.index("-thread_queue_size") + 1] == "1"
    assert camera.read() == (False, None)
    camera.release()
    assert not camera.isOpened()


@pytest.mark.parametrize("fourcc,argument,value", [("YUY2", "-pixel_format", "yuyv422"),
                                                 ("NV12", "-pixel_format", "nv12"),
                                                 ("MJPG", "-vcodec", "mjpeg")])
def test_selected_input_format_is_passed_to_ffmpeg(fourcc, argument, value):
    camera = FFmpegCamera(CaptureSettings(backend="FFMPEG", fourcc=fourcc), popen_factory=FakeProcess)
    try:
        assert camera.command[camera.command.index(argument) + 1] == value
    finally:
        camera.release()


def test_unknown_ffmpeg_format_is_rejected_instead_of_ignored():
    with pytest.raises(ValueError, match="not mapped"):
        FFmpegCamera(CaptureSettings(backend="FFMPEG", fourcc="ABCD"), popen_factory=FakeProcess)
