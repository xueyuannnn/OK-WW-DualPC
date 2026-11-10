from types import SimpleNamespace
from unittest.mock import Mock

import cv2
import numpy as np

from dualpc.dialogue import ADVANCE_NPC, advance_npc_line, has_continue_marker, install_dialogue_hook


def dialogue_frame():
    frame = np.zeros((1080, 1920, 3), dtype=np.uint8)
    cv2.fillPoly(frame, [np.array([[960, 1008], [967, 1020], [975, 1024],
                                   [967, 1028], [960, 1040], [953, 1028],
                                   [945, 1024], [953, 1020]])], (240, 240, 245))
    frame[820:832, 935:985] = (30, 210, 245)
    return frame


def task_for(frame):
    return SimpleNamespace(config={ADVANCE_NPC: True}, frame=frame,
                           in_team_and_world=Mock(return_value=False), click=Mock(), log_info=Mock(),
                           ocr=Mock(side_effect=[[SimpleNamespace(name="凌风")],
                                                 [SimpleNamespace(name="你好，有什么事吗？")]]))


def test_opt_in_advances_a_confirmed_npc_line_once():
    task = task_for(dialogue_frame())
    assert has_continue_marker(task.frame)
    assert advance_npc_line(task)
    task.click.assert_called_once_with(0.5, 0.945, after_sleep=0.3)


def test_option_disabled_and_world_hud_never_advance_text():
    task = task_for(dialogue_frame())
    task.config[ADVANCE_NPC] = False
    assert not advance_npc_line(task)
    task.config[ADVANCE_NPC] = True
    task.in_team_and_world.return_value = True
    assert not advance_npc_line(task)
    task.ocr.assert_not_called()
    task.click.assert_not_called()


def test_menu_footer_or_choice_without_continue_marker_never_clicks():
    task = task_for(dialogue_frame())
    task.frame[999:] = 0
    assert not advance_npc_line(task)
    task.ocr.assert_not_called()
    task.click.assert_not_called()
    task = task_for(dialogue_frame())
    task.frame[780:860] = 0
    assert not advance_npc_line(task)
    task.ocr.assert_not_called()
    task = task_for(dialogue_frame())
    task.ocr.side_effect = [[], []]
    assert not advance_npc_line(task)
    task.click.assert_not_called()


def test_hook_defaults_off_and_keeps_native_dialogue_behavior(monkeypatch):
    from dualpc import dialogue
    from src.task.SkipDialogTask import AutoDialogTask
    def init(task):
        task.default_config = {"_enabled": True}
        task.config_description = {}
    def run(task):
        return None
    monkeypatch.setattr(AutoDialogTask, "__init__", init)
    monkeypatch.setattr(AutoDialogTask, "run", run)
    advance = Mock(return_value=True)
    monkeypatch.setattr(dialogue, "advance_npc_line", advance)
    install_dialogue_hook()
    installed = AutoDialogTask.run
    install_dialogue_hook()
    assert AutoDialogTask.run is installed
    task = SimpleNamespace(executor=SimpleNamespace(interaction=SimpleNamespace(is_remote=False)))
    AutoDialogTask.__init__(task)
    assert task.default_config[ADVANCE_NPC] is False
    assert installed(task) is None
    advance.assert_not_called()
    task.executor.interaction.is_remote = True
    assert installed(task) is True
    advance.assert_called_once_with(task)
