"""Shared workshop recognition recovery; no executor or device is started."""
from types import SimpleNamespace
from unittest.mock import patch
import numpy as np

from src.task.BaseCombatTask import BaseCombatTask
from src.task.BaseWWTask import BaseWWTask


def test_workshop_party_falls_back_without_remembering_a_missing_slot():
    calls = []

    def find(name, **options):
        calls.append(options)
        if options.get('use_gray_scale') and name in {'char_2_text', 'char_3_text'}:
            return object()

    task = SimpleNamespace(find_one=find, logged_in=False)
    task.get_feature_by_name = lambda name: SimpleNamespace(mat=np.zeros((18,16,3),dtype=np.uint8))
    task.get_box_by_name = lambda name: SimpleNamespace(x=1740,y=340,width=16,height=18)
    task.width_of_screen = lambda fraction: 1920*fraction
    task.height_of_screen = lambda fraction: 1080*fraction
    assert BaseWWTask.in_team(task) == (True, 0, 3)
    assert task.logged_in
    assert any(options.get('frame_processor') for options in calls)
    task.find_one = lambda *args, **kwargs: None
    assert BaseWWTask.in_team(task)[0] is False


def test_workshop_cooldown_uses_raw_fallback_and_preserves_last_observation():
    now = [100.]
    visible = [True]
    task = SimpleNamespace(cds={}, scene=SimpleNamespace(cd_refreshed=False))
    task.get_current_char = lambda: SimpleNamespace(index=0)
    task.width_of_screen = lambda fraction: fraction * 1920
    task.height_of_screen = lambda fraction: fraction * 1080
    task.CD_OCR_REFERENCE_ASPECT_RATIO = BaseCombatTask.CD_OCR_REFERENCE_ASPECT_RATIO
    task.frame = np.zeros((1080,1920,3),dtype=np.uint8)
    task._cd_ocr_relative_x = lambda x: BaseCombatTask._cd_ocr_relative_x(task, x)
    task.log_debug = lambda *args: None
    task.ocr = lambda *args, **kwargs: [] if kwargs.get('frame_processor') or not visible[0] else [
        SimpleNamespace(name='12,3', x=40, y=80, width=40, height=40),
        SimpleNamespace(name='8', x=440, y=80, width=40, height=40),
        SimpleNamespace(name='2', x=40, y=10, width=20, height=20)]
    task.refresh_cd = lambda: BaseCombatTask.refresh_cd(task)
    task.time_elapsed_accounting_for_freeze = lambda stamp: now[0] - stamp
    with patch('src.task.BaseCombatTask.time.time', side_effect=lambda: now[0]):
        task.refresh_cd()
        assert task.cds[0]['resonance'] == 12.3
        assert BaseCombatTask.get_cd(task, 'liberation') == 8
        visible[0] = False
        now[0] = 101.
        task.scene.cd_refreshed = False
        task.refresh_cd()
        assert BaseCombatTask.get_cd(task, 'liberation') == 7
        now[0] = 110.
        task.scene.cd_refreshed = False
        assert BaseCombatTask.get_cd(task, 'liberation') <= 0
