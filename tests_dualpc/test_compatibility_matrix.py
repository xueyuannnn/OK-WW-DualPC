"""Software compatibility checks; these do not certify untested hardware."""
import struct
import re

import cv2
import numpy as np
import pytest

from dualpc.capture import CaptureReader
from dualpc.combat import shifted_bar_candidates
from dualpc.interaction import KMBoxInteraction
from dualpc.kmbox import MOVE
from dualpc.settings import BoxSettings, CaptureSettings


# Source HDMI size, inference size, desktop size, crop, known point and its
# desktop location. Include downscaling and a 16:9 game inside an ultrawide feed.
GEOMETRIES = [
    ((1280, 720), (1280, 720), (1280, 720), None, (960, 540), (960, 540)),
    ((1600, 900), (1600, 900), (1600, 900), None, (1200, 675), (1200, 675)),
    ((1920, 1080), (1920, 1080), (2560, 1440), None, (1440, 810), (1920, 1080)),
    ((2560, 1440), (1920, 1080), (2560, 1440), None, (1440, 810), (1920, 1080)),
    ((3840, 2160), (1920, 1080), (3840, 2160), None, (1440, 810), (2880, 1620)),
    ((3440, 1440), (1920, 1080), (3440, 1440), [440, 0, 2560, 1440],
     (1440, 810), (2360, 1080)),
]


@pytest.mark.parametrize("protocol", ["kmbox_net"])
@pytest.mark.parametrize("raw,inference,desktop,crop,point,expected", GEOMETRIES)
def test_menu_target_reaches_same_desktop_location_across_protocols(
        protocol, raw, inference, desktop, crop, point, expected):
    reader = CaptureReader(CaptureSettings(output_width=inference[0], output_height=inference[1], crop=crop))
    reader.raw_size = raw

    class Capture:
        def connected(self):
            return True

    capture = Capture()
    capture.reader = reader
    interaction = KMBoxInteraction(capture, BoxSettings(
        protocol=protocol, desktop_width=desktop[0], desktop_height=desktop[1],
        counts_per_pixel_x=1.25, counts_per_pixel_y=0.75, home_wait=0, move_interval=0))
    try:
        interaction.move(*point)
        packets = interaction.client.history
        if protocol == "kmbox_net":
            moves = [struct.unpack_from("<4i", packet, 16) for packet in packets
                     if struct.unpack_from("<I", packet, 12)[0] == MOVE]
            # Discard connection release and corner homing. The endpoint must
            # include desktop scaling, crop offsets and per-axis calibration.
            assert moves[1][1:3] == (-32767, -32767)
            assert (sum(move[1] for move in moves[2:]), sum(move[2] for move in moves[2:])) == (
                round(expected[0] * 1.25), round(expected[1] * 0.75))
    finally:
        interaction.on_destroy()
        reader.close()


@pytest.mark.parametrize("size", [(1280, 720), (1600, 900), (1920, 1080), (2560, 1440)])
@pytest.mark.parametrize("codec", ["NV12", "YUY2", "MJPEG"])
def test_thin_enemy_bar_survives_resolution_and_format_combination(size, codec):
    width, height = size
    rng = np.random.default_rng(17)
    frame = rng.integers(15, 45, (height, width, 3), dtype=np.uint8)
    x, y = round(width * 0.355), round(height * 0.327)
    bar_width, bar_height = round(width * 0.078), max(4, round(height * 0.0083))
    frame[y:y + bar_height, x:x + bar_width] = (60, 75, 207)
    if codec == "NV12":
        planar = cv2.cvtColor(frame, cv2.COLOR_BGR2YUV_I420).reshape(-1)
        y_size = width * height
        chroma = y_size // 4
        uv = np.empty(y_size // 2, dtype=np.uint8)
        uv[0::2], uv[1::2] = planar[y_size:y_size + chroma], planar[y_size + chroma:]
        packed = np.concatenate((planar[:y_size], uv)).reshape(height * 3 // 2, width)
        frame = cv2.cvtColor(packed, cv2.COLOR_YUV2BGR_NV12)
    elif codec == "YUY2":
        frame = cv2.cvtColor(cv2.cvtColor(frame, cv2.COLOR_BGR2YUV_YUY2), cv2.COLOR_YUV2BGR_YUY2)
    else:
        encoded, data = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, 65])
        assert encoded
        frame = cv2.imdecode(data, cv2.IMREAD_COLOR)
    candidates = shifted_bar_candidates(frame)
    assert any(abs(box.x - x) <= 3 and abs(box.y - y) <= 3 for box in candidates)
