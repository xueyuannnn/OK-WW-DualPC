from types import SimpleNamespace

import numpy as np
import cv2
import pytest

from dualpc.combat import shifted_bar_candidates, shifted_health_bar, install_combat_hook


@pytest.mark.parametrize("remote,active,health,expected", [(True, True, True, True),
    (True, True, False, False), (True, False, True, False), (False, True, True, False)])
def test_visible_enemy_survives_temporary_lock_loss(monkeypatch, remote, active, health, expected):
    from unittest.mock import Mock
    from src.combat.CombatCheck import CombatCheck
    from dualpc.combat import install_visible_enemy_retarget_hook
    calls = Mock(return_value=False)
    def original(task, wait=True):
        return calls(task, wait=wait)
    monkeypatch.setattr(CombatCheck, "target_enemy", original)
    install_visible_enemy_retarget_hook()
    task = SimpleNamespace(executor=SimpleNamespace(interaction=SimpleNamespace(is_remote=remote)),
                           _in_combat=active, in_team=lambda: (True, 0, 3), has_health_bar=lambda: health)
    assert CombatCheck.target_enemy(task) is expected
    assert calls.call_count == int(not expected)


def sample():
    frame = np.zeros((1080, 1920, 3), dtype=np.uint8)
    frame[352:361, 680:830] = (13, 72, 255)
    return frame


def test_shifted_color_needs_bar_geometry_and_confirmed_level_text():
    frame = sample()
    candidates = shifted_bar_candidates(frame)
    assert len(candidates) == 1
    checked = []

    def ocr(box, frame, match, threshold):
        checked.append(box)
        assert box.y + box.height < 352 and box.x <= 680 < box.x + box.width
        assert threshold >= 0.65
        return [SimpleNamespace(name="Lv.89")]

    task = SimpleNamespace(ocr=ocr, draw_boxes=lambda *args, **kwargs: None)
    assert shifted_health_bar(task, frame) and len(checked) == 1
    task.ocr = lambda **kwargs: [SimpleNamespace(name="89")]
    assert not shifted_health_bar(task, frame)
    task.ocr = lambda **kwargs: []
    assert not shifted_health_bar(task, frame)


def test_red_background_and_bottom_hud_are_not_enemy_bar_candidates():
    frame = np.zeros((1080, 1920, 3), dtype=np.uint8)
    frame[100:800, 100:1700] = (13, 72, 255)  # A broad red scene.
    frame[1030:1040, 700:1000] = (13, 72, 255)  # Player health bar.
    assert not shifted_bar_candidates(frame)


@pytest.mark.parametrize("bgr", [(0, 55, 255), (15, 140, 240), (45, 90, 210)])
def test_capture_boss_bar_needs_top_hud_geometry_and_level_text(bgr):
    frame = np.zeros((1080, 1920, 3), dtype=np.uint8)
    frame[56:67, 680:1248] = bgr
    checked = []
    def ocr(**kwargs):
        checked.append(kwargs["box"])
        return [SimpleNamespace(name="Lv.90天傀劫煞")]
    task = SimpleNamespace(ocr=ocr, draw_boxes=lambda *args, **kwargs: None)
    assert shifted_health_bar(task, frame)
    assert checked[0].y == 0 and checked[0].height < 60
    task.ocr = lambda **kwargs: [SimpleNamespace(name="90")]
    assert not shifted_health_bar(task, frame)
    task.ocr = ocr
    frame[56:67] = 0
    frame[700:711, 680:1248] = bgr
    assert not shifted_health_bar(task, frame)


@pytest.mark.parametrize("bgr", [(60, 75, 207), (13, 72, 255), (110, 120, 205),
                                 (40, 45, 125), (30, 130, 230), (95, 70, 210)])
def test_capture_color_variations_keep_the_same_bar_candidate(bgr):
    frame = sample()
    frame[352:361, 680:830] = bgr
    assert shifted_bar_candidates(frame)


@pytest.mark.parametrize("bgr", [(60, 207, 75), (207, 75, 60), (30, 230, 230),
                                 (210, 210, 210), (25, 25, 25)])
def test_unrelated_hues_and_gray_do_not_become_health_bars(bgr):
    frame = sample()
    frame[352:361, 680:830] = bgr
    assert not shifted_bar_candidates(frame)


def nv12_roundtrip(frame):
    height, width = frame.shape[:2]
    planar = cv2.cvtColor(frame, cv2.COLOR_BGR2YUV_I420).reshape(-1)
    y_size = height * width
    chroma_size = y_size // 4
    uv = np.empty(y_size // 2, dtype=np.uint8)
    uv[0::2] = planar[y_size:y_size + chroma_size]
    uv[1::2] = planar[y_size + chroma_size:]
    nv12 = np.concatenate((planar[:y_size], uv)).reshape(height * 3 // 2, width)
    return cv2.cvtColor(nv12, cv2.COLOR_YUV2BGR_NV12)


@pytest.mark.parametrize("codec", ["NV12", "YUY2", "MJPEG"])
def test_subsampled_and_compressed_frames_retain_bar_detection(codec):
    frame = sample()
    # A textured background and odd-aligned thin bar exercise chroma edges.
    rng = np.random.default_rng(17)
    frame[:] = rng.integers(15, 45, frame.shape, dtype=np.uint8)
    frame[353:362, 681:831] = (60, 75, 207)
    if codec == "NV12":
        frame = nv12_roundtrip(frame)
    elif codec == "YUY2":
        frame = cv2.cvtColor(cv2.cvtColor(frame, cv2.COLOR_BGR2YUV_YUY2), cv2.COLOR_YUV2BGR_YUY2)
    else:
        ok, encoded = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, 65])
        assert ok
        frame = cv2.imdecode(encoded, cv2.IMREAD_COLOR)
    assert shifted_bar_candidates(frame)


@pytest.mark.parametrize("size", [(1280, 720), (2560, 1440)])
def test_color_tolerance_scales_with_inference_resolution(size):
    frame = cv2.resize(sample(), size, interpolation=cv2.INTER_AREA)
    assert shifted_bar_candidates(frame)


def test_remote_fallback_preserves_native_detection_and_requires_team_hud(monkeypatch):
    from src.combat.CombatCheck import CombatCheck
    from dualpc import combat
    original = CombatCheck.has_health_bar
    monkeypatch.setattr(CombatCheck, "has_health_bar", lambda task: task.standard_hit)
    install_combat_hook()
    hook = CombatCheck.has_health_bar
    install_combat_hook()
    assert CombatCheck.has_health_bar is hook
    calls = []
    monkeypatch.setattr(combat, "shifted_health_bar", lambda task, frame: calls.append(frame) or True)
    reader = object()
    task = SimpleNamespace(standard_hit=True, frame=sample(), in_team=lambda: (True, 0, 3),
                           executor=SimpleNamespace(interaction=SimpleNamespace(is_remote=False),
                               device_manager=SimpleNamespace(capture_method=SimpleNamespace(reader=reader))))
    assert hook(task) and not calls  # Preserve standard hits for local and remote.
    task.standard_hit = False
    assert not hook(task) and not calls
    task.executor.interaction.is_remote = True
    task.in_team = lambda: (False, -1, 1)
    assert not hook(task) and not calls
    task.in_team = lambda: (True, 0, 3)
    assert hook(task) and len(calls) == 1
    assert hook(task) and len(calls) == 1  # Rate limit repeat OCR.
    task.executor.device_manager.capture_method.reader = object()
    assert hook(task) and len(calls) == 2  # Invalidate a previous session's result.
    monkeypatch.setattr(CombatCheck, "has_health_bar", original)
