from types import SimpleNamespace
from unittest.mock import Mock

import pytest

from dualpc.recovery import has_death_dialog, install_combat_recovery_hook
from src.task.BaseCombatTask import BaseCombatTask, CharDeadException, CharRevivedException
from ok.task.exceptions import TaskDisabledException


def test_death_dialog_requires_both_its_title_and_confirmation():
    task = SimpleNamespace(ocr=Mock(side_effect=[[SimpleNamespace(name="选择复苏物品")],
                                               [SimpleNamespace(name="确认")]]))
    assert has_death_dialog(task)
    task.ocr = Mock(return_value=[SimpleNamespace(name="声骸强化")])
    assert not has_death_dialog(task)
    task.ocr = Mock(side_effect=[[SimpleNamespace(name="失去意识")], []])
    assert not has_death_dialog(task)


@pytest.mark.parametrize("recovered,exception", [(True, CharRevivedException), (False, CharDeadException)])
def test_death_is_not_a_successful_round(monkeypatch, recovered, exception):
    from dualpc import recovery
    monkeypatch.setattr(BaseCombatTask, "combat_once", lambda task: True)
    monkeypatch.setattr(recovery, "has_death_dialog", Mock(side_effect=[False, True]))
    install_combat_recovery_hook()
    installed = BaseCombatTask.combat_once
    install_combat_recovery_hook()
    assert BaseCombatTask.combat_once is installed
    task = SimpleNamespace(executor=SimpleNamespace(interaction=SimpleNamespace(is_remote=True)),
                           log_info=Mock(), revive_action=Mock(return_value=recovered))
    with pytest.raises(exception, match="本轮未完成"):
        installed(task)
    task.revive_action.assert_called_once_with()


def test_native_combat_does_not_use_captured_dialog_detection(monkeypatch):
    from dualpc import recovery
    monkeypatch.setattr(BaseCombatTask, "combat_once", lambda task: "native result")
    monkeypatch.setattr(recovery, "has_death_dialog", Mock(side_effect=AssertionError("local OCR fallback")))
    install_combat_recovery_hook()
    task = SimpleNamespace(executor=SimpleNamespace(interaction=SimpleNamespace(is_remote=False)))
    assert BaseCombatTask.combat_once(task) == "native result"


@pytest.mark.parametrize('escaped', [True, False])
def test_item_dialog_is_closed_without_pressing_confirm_or_consuming_items(monkeypatch, escaped):
    from dualpc import recovery
    monkeypatch.setattr(BaseCombatTask, 'close_revive_popup', lambda task: 'native')
    monkeypatch.setattr(recovery, 'has_death_dialog', lambda task: True)
    recovery.install_revive_popup_close_hook()
    task = SimpleNamespace(executor=SimpleNamespace(interaction=SimpleNamespace(is_remote=True)),
                           send_key=Mock(), click_relative=Mock(),
                           wait_until=Mock(side_effect=[True] if escaped else [False, True]))
    assert BaseCombatTask.close_revive_popup(task)
    task.send_key.assert_called_once_with('esc', after_sleep=.5)
    if escaped:
        task.click_relative.assert_not_called()
    else:
        task.click_relative.assert_called_once_with(.803, .196, after_sleep=.5)


def test_popup_must_be_confirmed_closed_before_recovery_continues(monkeypatch):
    from dualpc import recovery
    monkeypatch.setattr(BaseCombatTask, 'close_revive_popup', lambda task: None)
    monkeypatch.setattr(recovery, 'has_death_dialog', lambda task: True)
    recovery.install_revive_popup_close_hook()
    task = SimpleNamespace(executor=SimpleNamespace(interaction=SimpleNamespace(is_remote=True)),
                           send_key=Mock(), click_relative=Mock(), wait_until=Mock(return_value=False))
    with pytest.raises(RuntimeError, match='避免误用'):
        BaseCombatTask.close_revive_popup(task)


def test_enhanced_echo_task_can_use_free_domain_recovery_instead_of_failing_immediately(monkeypatch):
    from src.task.FarmEchoTask2 import FarmEchoTask2
    from src.task.DomainTask import DomainTask
    recover = Mock(return_value=True)
    monkeypatch.setattr(DomainTask, 'revive_action', recover)
    task = SimpleNamespace(_in_realm=True, teleport_to_boss_enabled=lambda: True,
                           executor=SimpleNamespace(interaction=SimpleNamespace(is_remote=True)))
    assert FarmEchoTask2.revive_action(task)
    recover.assert_called_once_with(task)


def test_recovered_death_retries_same_dungeon_without_counting_failed_round(monkeypatch):
    from src.task.FarmEchoTask2 import FarmEchoTask2
    import importlib
    module = importlib.import_module('src.task.FarmEchoTask2')
    completed = Mock()
    monkeypatch.setattr(module, 'rotation_round_completed', completed)
    task = SimpleNamespace(config={'Repeat Farm Count': 1}, in_realm=lambda: True,
                           manage_boss_parameters=Mock(), init_parameters=Mock(),
                           teleport_to_boss_enabled=lambda: True, in_realm_check=Mock(),
                           log_info=Mock(), log_debug=Mock(), manage_boss_interactions=Mock(),
                           in_combat=lambda: True, sleep=Mock(), combat_wait_time=0,
                           check_boss_name=Mock(), combat_once=Mock(side_effect=[CharRevivedException(), True]),
                           pick_echo=Mock(return_value=True), incr_drop=Mock(), bypass_end_wait=True)
    task.teleport_to_configured_boss_and_prepare = Mock(side_effect=lambda: setattr(task, '_just_entered_boss_realm', True))
    FarmEchoTask2._do_run_single(task)
    assert task.combat_once.call_count == 2
    assert task.teleport_to_configured_boss_and_prepare.call_count == 2
    task.pick_echo.assert_called_once()
    completed.assert_called_once_with(task)


def test_imported_rotation_is_interrupted_inside_its_skipped_combat_wait(monkeypatch):
    from dualpc import recovery
    # Reproduce an imported rotation retrying a dead slot without returning.
    # Its sleep(check_combat=False) still runs the executor's sleep_check.
    attempts = []
    visible = [False]
    def rotation(task):
        task.skip_combat_check = True
        for attempt in range(20):
            attempts.append(attempt)
            visible[0] = True
            task.sleep_check()
        pytest.fail('dead character loop was not interrupted')

    monkeypatch.setattr(BaseCombatTask, 'combat_once', rotation)
    monkeypatch.setattr(BaseCombatTask, 'sleep_check', lambda task: None)
    monkeypatch.setattr(recovery, 'has_death_dialog', lambda task: visible[0])
    install_combat_recovery_hook()
    task = SimpleNamespace(executor=SimpleNamespace(interaction=SimpleNamespace(is_remote=True)),
                           skip_combat_check=False, log_info=Mock(), reset_to_false=Mock(),
                           revive_action=Mock(return_value=True))
    task.sleep_check = lambda: BaseCombatTask.sleep_check(task)
    with pytest.raises(CharRevivedException):
        BaseCombatTask.combat_once(task)
    assert attempts == [0]
    task.revive_action.assert_called_once_with()
    assert not task.skip_combat_check
    assert not task._dualpc_combat_scope
    assert not task._dualpc_dialog_checking


def test_dialog_watchdog_is_throttled_and_recovery_waits_do_not_reenter_it(monkeypatch):
    from dualpc import recovery
    original = Mock()
    detector = Mock(return_value=False)
    monkeypatch.setattr(BaseCombatTask, 'sleep_check', lambda task: original(task))
    monkeypatch.setattr(recovery, 'has_death_dialog', detector)
    monkeypatch.setattr(recovery.time, 'monotonic', Mock(side_effect=[1., 1.2, 2.1]))
    recovery.install_combat_dialog_watchdog()
    task = SimpleNamespace(executor=SimpleNamespace(interaction=SimpleNamespace(is_remote=True)),
                           _dualpc_combat_scope=True)
    for _ in range(3):
        BaseCombatTask.sleep_check(task)
    assert detector.call_count == 2
    task._dualpc_combat_scope = False
    BaseCombatTask.sleep_check(task)
    task._dualpc_combat_scope = True
    task._dualpc_dialog_checking = True
    BaseCombatTask.sleep_check(task)
    assert detector.call_count == 2
    assert original.call_count == 5


@pytest.mark.parametrize('exception', [TaskDisabledException, CharDeadException, CharRevivedException])
def test_combat_recognition_does_not_swallow_stop_or_death_control_flow(exception):
    from src.combat.CombatCheck import CombatCheck
    task = SimpleNamespace(do_check_in_combat=Mock(side_effect=exception()))
    with pytest.raises(exception):
        CombatCheck.in_combat(task)
    assert not task.in_sleep_check


def test_stopping_combat_does_not_start_recovery_even_when_death_dialog_exists(monkeypatch):
    from dualpc import recovery
    def stopped(task):
        raise TaskDisabledException()
    monkeypatch.setattr(BaseCombatTask, 'combat_once', stopped)
    detector = Mock(return_value=False)
    monkeypatch.setattr(recovery, 'has_death_dialog', detector)
    install_combat_recovery_hook()
    task = SimpleNamespace(executor=SimpleNamespace(interaction=SimpleNamespace(is_remote=True)),
                           revive_action=Mock())
    with pytest.raises(TaskDisabledException):
        BaseCombatTask.combat_once(task)
    assert detector.call_count == 1
    task.revive_action.assert_not_called()


def test_character_skipped_check_flag_restores_after_an_interruption():
    from src.char.BaseChar import BaseChar
    task = SimpleNamespace(skip_combat_check=False, sleep=Mock(side_effect=CharDeadException()))
    with pytest.raises(CharDeadException):
        BaseChar.sleep(SimpleNamespace(task=task), .1, check_combat=False)
    assert not task.skip_combat_check


def test_stop_during_free_healing_is_not_reported_as_recovery_failure():
    task = SimpleNamespace(close_revive_popup=Mock(side_effect=TaskDisabledException()))
    with pytest.raises(TaskDisabledException):
        BaseCombatTask.revive_action(task)
