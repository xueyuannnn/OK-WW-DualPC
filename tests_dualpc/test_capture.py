import time

import numpy as np
import pytest
import cv2

from dualpc.capture import CaptureReader
from dualpc.settings import CaptureSettings


def test_directshow_applies_nv12_after_fps_graph_rebuild():
    class DirectShow(FakeCamera):
        def __init__(self):
            super().__init__()
            self.properties = {}
        def set(self, key, value):
            self.properties[key] = value
            if key == cv2.CAP_PROP_FPS:
                self.properties[cv2.CAP_PROP_FOURCC] = 0xE436EB7D
            return True
        def get(self, key):
            return self.properties.get(key, 0)
    camera = DirectShow()
    reader = CaptureReader(CaptureSettings(fourcc="NV12", fps=240), lambda *args: camera)
    try:
        reader.start()
        assert reader.actual["fourcc"] == "NV12" and reader.actual["fps"] == 240
    finally:
        reader.close()


@pytest.mark.parametrize("format,expected", [("RGB3", 0xE436EB7D), ("RGB4", 0xE436EB7E)])
def test_directshow_rgb_selection_uses_media_guid_and_decodes_label(format, expected):
    from dualpc.capture import fourcc_label
    class Camera(FakeCamera):
        subtype = None
        def set(self, key, value):
            if key == cv2.CAP_PROP_FOURCC:
                self.subtype = value
            return True
        def get(self, key):
            return self.subtype if key == cv2.CAP_PROP_FOURCC else 0
    camera = Camera()
    reader = CaptureReader(CaptureSettings(fourcc=format), lambda *args: camera)
    try:
        reader.start()
        assert camera.subtype == expected - (1 << 32)
        assert reader.actual["fourcc"] == format
        assert fourcc_label(expected) == format
        assert fourcc_label(0x01020304) == "0x01020304"
    finally:
        reader.close()


class FakeCamera:
    def __init__(self, shape=(180, 320, 3)):
        self.frame = np.full(shape, 45, dtype=np.uint8)
        self.lost = False
        self.released = False

    def isOpened(self):
        return True

    def set(self, *args):
        return False  # A backend may ignore requested capture properties.

    def get(self, key):
        return 0

    def read(self):
        time.sleep(0.005)
        return (False, None) if self.lost else (True, self.frame)

    def release(self):
        self.released = True


def test_latest_frame_copy_and_signal_loss():
    camera = FakeCamera()
    reader = CaptureReader(CaptureSettings(output_width=1280, output_height=720, stale_after=0.03), lambda *args: camera)
    try:
        reader.start()
        first = reader.get_frame()
        assert first.shape == (720, 1280, 3)
        first[:] = 0
        assert reader.get_frame().mean() == 45
        first_sequence = reader.sequence
        time.sleep(0.03)
        assert reader.sequence > first_sequence
        camera.lost = True
        time.sleep(0.09)
        assert reader.get_frame() is None and not reader.connected()
    finally:
        reader.close()
    assert camera.released


def test_crop_and_coordinate_mapping():
    c = CaptureSettings(crop=[160, 90, 960, 540], output_width=1280, output_height=720)
    reader = CaptureReader(c)
    reader.raw_size = (1920, 1080)
    assert reader.screen_point(640, 360, 3840, 2160) == (1280, 720)
    with pytest.raises(ValueError):
        reader.screen_point(1280, 360, 3840, 2160)


@pytest.mark.parametrize("shape,crop", [((180, 240, 3), None), ((180, 320, 3), [300, 0, 100, 100])])
def test_invalid_capture_geometry_rejected(shape, crop):
    camera = FakeCamera(shape)
    reader = CaptureReader(CaptureSettings(crop=crop), lambda *args: camera)
    try:
        with pytest.raises(RuntimeError):
            reader.start()
    finally:
        reader.close()


def test_close_releases_even_after_read_failure():
    camera = FakeCamera()
    camera.lost = True
    reader = CaptureReader(CaptureSettings(), lambda *args: camera)
    with pytest.raises(RuntimeError):
        reader.start(timeout=0.03)
    reader.close()
    assert camera.released


def test_preview_uses_newest_frame_without_affecting_task_delivery_statistics():
    reader = CaptureReader(CaptureSettings(stale_after=10))
    reader._frame = np.full((1080, 1920, 3), (3, 17, 220), dtype=np.uint8)
    reader._stamp = time.perf_counter()
    reader.sequence = 4
    sequence, preview = reader.preview_frame(720, 405)
    assert sequence == 4 and preview.shape == (405, 720, 3)
    assert tuple(preview[0, 0]) == (3, 17, 220)
    preview[:] = 0
    assert reader._frame[0, 0, 2] == 220
    assert reader.preview_frame(720, 405, after_sequence=4) is None
    assert reader.statistics()["delivery_age_p95_ms"] is None
    reader.sequence = 9  # Intermediate frames are dropped instead of queued.
    assert reader.preview_frame(720, 405, after_sequence=4)[0] == 9
    reader._stamp -= 20
    assert reader.preview_frame(720, 405) is None
    reader.close()


def test_full_size_preview_is_an_independent_copy_and_stop_invalidates_it():
    reader = CaptureReader(CaptureSettings())
    reader._frame = np.full((180, 320, 3), 45, dtype=np.uint8)
    reader._stamp = time.perf_counter()
    reader.sequence = 1
    _, preview = reader.preview_frame(1920, 1080)
    assert preview.shape == (180, 320, 3)  # Do not upscale small sources.
    preview[:] = 0
    assert reader._frame.mean() == 45
    reader.close()
    assert reader.preview_frame(720, 405) is None
