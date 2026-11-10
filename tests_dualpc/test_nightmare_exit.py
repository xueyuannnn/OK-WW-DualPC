from types import SimpleNamespace
from unittest.mock import Mock

import pytest

from dualpc import nightmare
from ok.task.exceptions import CannotFindException


def box(text, x=1100):
    return SimpleNamespace(name=text, x=x, width=150)


def task():
    return SimpleNamespace(ocr=Mock(return_value=[]), find_one=Mock(return_value=None),
        f_search_box='f-region', in_world=Mock(return_value=False),
        in_team=Mock(return_value=(True, 0, 3)), width_of_screen=lambda f: 1920*f,
        next_frame=Mock(), sleep=Mock(), send_key=Mock(), click=Mock(), log_info=Mock(),
        wait_until=Mock(return_value=False))


@pytest.mark.parametrize('text', ['该聚落梦魔已被除', '该聚落梦魇已破除', '此聚落夢魘已淨化', 'Nightmare nest cleared'])
def test_completed_nest_text(text):
    value = task()
    value.ocr.return_value = [box(text)]
    assert nightmare.completed_nest(value)


@pytest.mark.parametrize('text', ['吸收', '领取奖励', '开启挑战', '已击败残象：3/36', '购买'])
def test_non_exit_interactions_are_not_exit_prompts(text):
    value = task()
    value.find_one.return_value = object()
    value.ocr.return_value = [box(text)]
    assert not nightmare.exit_prompt(value)


def test_f_exit_requires_the_icon_and_exact_text():
    value = task()
    value.ocr.return_value = [box('F 离开副本')]
    assert not nightmare.exit_prompt(value)
    value.find_one.return_value = object()
    assert nightmare.exit_prompt(value)


@pytest.mark.parametrize('body,allowed', [
    ('是否离开当前副本？退出后将不保留进度', True),
    ('是否退出副本？', True),
    ('是否离开副本并消耗60结晶波片领取奖励？', False),
    ('是否确认复苏角色？', False),
    ('是否确认重构声骸？', False),
])
def test_confirm_button_needs_explicit_exit_body_and_no_resource_action(body, allowed):
    value = task()
    button = box('确认')
    value.ocr.return_value = [box(body, 600), button]
    assert (nightmare.exit_confirmation(value) is button) is allowed


def test_f_portal_exit_must_verify_open_world_afterwards(monkeypatch):
    value = task()
    outside = Mock(side_effect=[False, False, True])
    monkeypatch.setattr(nightmare, 'outside_nest', outside)
    monkeypatch.setattr(nightmare, 'exit_confirmation', lambda _: None)
    monkeypatch.setattr(nightmare, 'exit_prompt', lambda _: True)
    value.wait_until.side_effect = lambda condition, **kwargs: condition()
    assert nightmare.leave_nest(value)
    value.send_key.assert_called_once_with('f', after_sleep=1)
    value.click.assert_not_called()
    assert outside.call_count == 3


def test_team_hud_inside_instance_is_not_open_world():
    value = task()
    assert not nightmare.outside_nest(value)
    value.in_world.return_value = True
    value.ocr.return_value = [box('该聚落梦魔已被除')]
    assert not nightmare.outside_nest(value)
    value.ocr.return_value = []
    assert nightmare.outside_nest(value)


def test_exit_menu_then_confirmation_then_loading_is_bounded(monkeypatch):
    value = task()
    button = box('确认')
    monkeypatch.setattr(nightmare, 'outside_nest', Mock(side_effect=[False, False, False]))
    monkeypatch.setattr(nightmare, 'exit_prompt', lambda _: False)
    monkeypatch.setattr(nightmare, 'exit_button', lambda _: None)
    monkeypatch.setattr(nightmare, 'exit_confirmation', Mock(side_effect=[None, button]))
    value.wait_until.return_value = True
    assert nightmare.leave_nest(value)
    value.send_key.assert_called_once_with('esc', after_sleep=1)
    value.click.assert_called_once_with(button, after_sleep=1)


def test_unknown_dialog_never_gets_generic_confirm_click(monkeypatch):
    value = task()
    monkeypatch.setattr(nightmare, 'outside_nest', lambda _: False)
    monkeypatch.setattr(nightmare, 'exit_prompt', lambda _: False)
    monkeypatch.setattr(nightmare, 'exit_button', lambda _: None)
    monkeypatch.setattr(nightmare, 'exit_confirmation', lambda _: None)
    with pytest.raises(CannotFindException, match='尚未确认回到大世界'):
        nightmare.leave_nest(value)
    value.click.assert_not_called()
    assert value.send_key.call_count == 1
    assert value.next_frame.call_count == 4


def test_pause_is_never_swallowed_during_exit(monkeypatch):
    value = task()
    monkeypatch.setattr(nightmare, 'outside_nest', lambda _: False)
    value.next_frame.side_effect = RuntimeError('paused')
    with pytest.raises(RuntimeError, match='paused'):
        nightmare.leave_nest(value)


@pytest.mark.parametrize('remote,finished', [(True, True), (True, False), (False, True)])
def test_task_restart_only_leaves_completed_remote_nest(monkeypatch, remote, finished):
    from src.task.NightmareNestTask import NightmareNestTask
    original_run = Mock(return_value='run')
    original_exit = Mock(return_value='local exit')
    monkeypatch.setattr(NightmareNestTask, 'run', lambda value: original_run(value))
    monkeypatch.setattr(NightmareNestTask, 'esc_world_confirm', lambda value, **kwargs: original_exit(value, **kwargs))
    monkeypatch.setattr(NightmareNestTask, '_dualpc_nest_exit', False, raising=False)
    leave = Mock(return_value=True)
    monkeypatch.setattr(nightmare, 'outside_nest', lambda _: False)
    monkeypatch.setattr(nightmare, 'completed_nest', lambda _: finished)
    monkeypatch.setattr(nightmare, 'leave_nest', leave)
    nightmare.install_nightmare_hook()
    value = task()
    value.executor = SimpleNamespace(interaction=SimpleNamespace(is_remote=remote))
    assert NightmareNestTask.run(value) == 'run'
    assert leave.call_count == int(remote and finished)
    if not remote:
        assert NightmareNestTask.esc_world_confirm(value) == 'local exit'
        original_exit.assert_called_once_with(value, send_esc=True)
