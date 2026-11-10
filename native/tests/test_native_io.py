import sys
from pathlib import Path

import cv2
import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "bridge"))
from native_io import decode_frame


@pytest.mark.parametrize("fmt,channels", [("RGB3", 3), ("RGB4", 4)])
def test_bottom_up_and_stride(fmt, channels):
    raw = np.zeros((2, 12), dtype=np.uint8)
    raw[0, :3] = [1, 2, 3]
    raw[1, :3] = [4, 5, 6]
    image = decode_frame(dict(width=2, height=2, stride=12, format=fmt, bottom_up=True), raw.tobytes())
    assert image.shape == (2, 2, 3)
    assert image[0, 0].tolist() == [4, 5, 6]


@pytest.mark.parametrize("fmt", ["NV12", "I420", "P010"])
def test_padded_planar_yuv(fmt):
    width, height, stride = 4, 4, 8
    raw = np.full((height * 3 // 2, stride), 128, np.uint8)
    raw[:height] = 16
    if fmt == "P010":
        raw = (raw.astype(np.uint16) << 8)
        stride *= 2
    image = decode_frame(dict(width=width, height=height, stride=stride, format=fmt), raw.tobytes())
    assert image.shape == (height, width, 3)
    assert not image.any()


def test_truncated_frame_rejected():
    with pytest.raises(ValueError, match="不完整"):
        decode_frame(dict(width=4, height=4, stride=4, format="NV12"), bytes(2))


def test_mjpeg():
    source = np.full((16, 24, 3), 100, np.uint8)
    success, encoded = cv2.imencode(".jpg", source)
    assert success
    result = decode_frame(dict(width=24, height=16, stride=24, format="MJPG"), encoded.tobytes())
    np.testing.assert_allclose(result, source, atol=1)
