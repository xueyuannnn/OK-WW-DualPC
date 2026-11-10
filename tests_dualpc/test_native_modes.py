import ctypes as ct
import struct

import pytest

from dualpc.native_modes import GUID, MediaType, VIDEO_INFO, VIDEO_INFO2, _fps, _parse_media
from dualpc.settings import Settings


@pytest.mark.parametrize("kind,offset", [(VIDEO_INFO, 48), (VIDEO_INFO2, 72)])
def test_native_media_type_decodes_resolution_format_and_fractional_fps(kind, offset):
    raw = bytearray(offset + 40)
    struct.pack_into("<q", raw, 40, 166833)
    struct.pack_into("<ii", raw, offset + 4, 1920, -1080)
    raw[offset + 16:offset + 20] = b"YUY2"
    buffer = ct.create_string_buffer(bytes(raw))
    media = MediaType()
    media.format_type = GUID(kind)
    media.format, media.format_size = ct.cast(buffer, ct.c_void_p), len(raw)
    assert _parse_media(media) == {"width": 1920, "height": 1080, "fourcc": "YUY2", "fps": 59.94}


def test_integer_rate_rounding_only_removes_reference_clock_quantization():
    assert _fps(69444) == 144
    assert _fps(41666) == 240
    assert _fps(166833) == 59.94


def test_fractional_fps_round_trips_and_nonfinite_fps_is_rejected(tmp_path):
    settings = Settings()
    settings.capture.fps = 59.94
    destination = tmp_path / "settings.json"
    settings.save(destination)
    assert Settings.load(destination).capture.fps == 59.94
    settings.capture.fps = float("nan")
    with pytest.raises(ValueError, match="finite"):
        settings.validate()
