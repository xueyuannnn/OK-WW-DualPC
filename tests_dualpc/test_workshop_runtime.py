import ast
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock

import cv2
import numpy as np
import pytest

from dualpc.workshop_runtime import (compile_import, shifted_con_fraction,
                                     read_con, message_backend, MessageBackend)
from src.task.BaseCombatTask import con_colors, BaseCombatTask


def ring(size, index, fraction):
    image = np.full((size, size, 3), 24, np.uint8)
    ref = np.uint8([[[sum(con_colors[index][c]) // 2 for c in ('b', 'g', 'r')]]])
    hsv = cv2.cvtColor(ref, cv2.COLOR_BGR2HSV)
    hsv[0, 0, 1:] = (190, 248)
    color = tuple(int(c) for c in cv2.cvtColor(hsv, cv2.COLOR_HSV2BGR)[0, 0])
    cv2.ellipse(image, (size // 2, size // 2), (round(size * .39),) * 2,
                0, 90, 90 + 360 * fraction, color, max(3, round(size * .06)))
    return image


@pytest.mark.parametrize('size', [42, 63, 84])
@pytest.mark.parametrize('index', range(6))
def test_capture_shifted_partial_full_empty_rings_across_elements_and_sizes(size, index):
    assert shifted_con_fraction(ring(size, index, .72), con_colors[index]) == pytest.approx(.72, abs=.09)
    assert shifted_con_fraction(ring(size, index, 1), con_colors[index]) == 1
    assert shifted_con_fraction(np.full((size, size, 3), 24, np.uint8), con_colors[index]) == 0


def test_actual_capture_ring_was_zero_with_original_rgb_but_is_three_quarters():
    image = cv2.imread(str(Path(__file__).parent / 'fixtures/workshop-con-spectro-075.png'))
    task = SimpleNamespace(logger=Mock(), screen_width=1920)
    assert BaseCombatTask.count_rings(task, image, con_colors[0], 375) == (0, False)
    assert shifted_con_fraction(image, con_colors[0]) == pytest.approx(.75, abs=.04)


def test_flat_tinted_background_and_center_icon_are_not_a_full_ring():
    flat = np.full((63, 63, 3), (20, 235, 245), np.uint8)
    assert shifted_con_fraction(flat, con_colors[0]) is None
    glyph = np.zeros((63, 63, 3), np.uint8)
    cv2.circle(glyph, (31, 31), 12, (20, 235, 245), -1)
    assert shifted_con_fraction(glyph, con_colors[0]) == 0
    assert shifted_con_fraction(None, con_colors[0]) is None


def task_for_ring(remote=True, combat=True, hud=True):
    current = SimpleNamespace(ring_index=0)
    task = SimpleNamespace(get_current_con=lambda: 0., _in_combat=combat,
        executor=SimpleNamespace(interaction=SimpleNamespace(is_remote=remote)),
        in_team=lambda: (hud, 0, 3), get_current_char=lambda: current,
        get_con_box=lambda: SimpleNamespace(crop_frame=lambda frame: frame),
        frame=ring(63, 0, .72))
    current.task = task
    current.get_current_con = lambda: 0.
    return task, current


def test_only_current_remote_imported_character_gets_capture_fallback():
    task, current = task_for_ring()
    assert read_con(task) == pytest.approx(.72, abs=.06)
    assert read_con(current) == pytest.approx(.72, abs=.06)
    other = SimpleNamespace(task=task, get_current_con=lambda: .2)
    assert read_con(other) == .2
    task.get_current_con = lambda: .9
    assert read_con(task) == .9


@pytest.mark.parametrize('remote,combat,hud', [(False, True, True), (True, False, True), (True, True, False)])
def test_no_fallback_for_single_pc_noncombat_or_hidden_hud(remote, combat, hud):
    task, _ = task_for_ring(remote, combat, hud)
    assert read_con(task) == 0


def test_runtime_compile_adapts_calls_without_wrapping_task_or_rewriting_file(tmp_path):
    text = '''def run(task):
    a = task.get_current_con()
    b = task.executor.interaction
    c = getattr(task.executor, "interaction", None)
    task.interaction = "stored"
    return a, b, c
'''
    path = tmp_path / 'code.py'
    path.write_text(text)
    env = {'_dualpc_read_con': lambda obj: .73,
           '_dualpc_message_backend': lambda obj: ('facade', obj)}
    exec(compile_import(text, str(path)), env)
    task = SimpleNamespace(executor=SimpleNamespace(interaction='actual'))
    assert env['run'](task) == (.73, ('facade', 'actual'), ('facade', 'actual'))
    assert task.interaction == 'stored'
    assert path.read_text() == text


def test_message_facade_preserves_left_hold_when_dodging_without_positioning(monkeypatch):
    import threading
    from dualpc.workshop_compat import scoped_importer, NativeBridge
    from dualpc.interaction import KMBoxInteraction
    from dualpc.settings import BoxSettings
    capture = SimpleNamespace(connected=lambda: True)
    backend = KMBoxInteraction(capture, BoxSettings(dry_run=True))
    monkeypatch.setattr('dualpc.workshop_compat.current_interaction', lambda: backend)
    try:
        backend.mouse_down()
        backend.mouse_pos = (1700, 900)
        facade = message_backend(backend)
        native = scoped_importer(NativeBridge(lambda: backend))('win32api')
        lparam = native.MAKELONG(*facade.mouse_pos)
        backend.move = Mock(side_effect=AssertionError('held attack must not move'))
        facade.post(0x204, 3, lparam)
        assert backend.client.buttons == 3
        facade.post(0x205, 1, lparam)
        assert backend.client.buttons == 1
        assert backend.mouse_pos == (1700, 900)
        backend.mouse_up()
        assert backend.client.buttons == 0
        backend.mouse_pos = None
        assert facade.mouse_pos == (0, 0)
        assert backend.mouse_pos is None
        assert message_backend(facade) is facade
    finally:
        backend.on_destroy()


def test_stale_message_facade_refuses_new_backend(monkeypatch):
    from dualpc.workshop_compat import CompatibilityError
    backend = SimpleNamespace(is_remote=True)
    facade = message_backend(backend)
    monkeypatch.setattr('dualpc.workshop_compat.current_interaction', lambda: object())
    with pytest.raises(CompatibilityError, match='no longer active'):
        facade.post(0x204, 3, 0)
    local = SimpleNamespace(is_remote=False)
    assert message_backend(local) is local
