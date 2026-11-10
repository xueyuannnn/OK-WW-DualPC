from types import SimpleNamespace
from unittest.mock import Mock

import pytest
from ok.task.exceptions import CannotFindException, TaskDisabledException, WaitFailedException
from dualpc.continuation import retry_checkpoint, install_wait_continuation, install_continuation_hooks


def make_task(settings=None, remote=True):
    return SimpleNamespace(executor=SimpleNamespace(interaction=SimpleNamespace(is_remote=remote),
        global_config=SimpleNamespace(get_config=lambda name: settings or {})),
        info_set=Mock(), log_info=Mock(), tr=str, sleep=Mock(), next_frame=Mock())


def test_retry_resumes_only_failed_step_and_keeps_progress():
    task = make_task()
    task.completed_rounds = 23
    step = Mock(side_effect=[WaitFailedException(), 'expected screen'])
    assert retry_checkpoint(task, step, 'wait') == 'expected screen'
    assert task.completed_rounds == 23
    assert step.call_count == 2 and not task._dualpc_retrying_step


@pytest.mark.parametrize('error', [TaskDisabledException(), ValueError('bad config'),
                                  RuntimeError('Capture signal is unavailable')])
def test_cancel_configuration_and_hardware_errors_are_never_retried(error):
    task = make_task()
    step = Mock(side_effect=error)
    with pytest.raises(type(error)):
        retry_checkpoint(task, step, 'wait')
    assert step.call_count == 1


@pytest.mark.parametrize('settings,remote', [({'Resume Interrupted Tasks': False}, True), ({}, False)])
def test_disabled_or_native_tasks_keep_original_behavior(settings, remote):
    task = make_task(settings, remote)
    step = Mock(side_effect=WaitFailedException())
    with pytest.raises(WaitFailedException):
        retry_checkpoint(task, step, 'wait')
    assert step.call_count == 1


def test_consecutive_retries_are_bounded_and_nested_steps_share_budget():
    task = make_task({'Recovery Retry Limit': 2})
    operation = Mock(side_effect=CannotFindException())
    def outer():
        return retry_checkpoint(task, operation, 'inner')
    with pytest.raises(CannotFindException):
        retry_checkpoint(task, outer, 'outer')
    assert operation.call_count == 3


def test_wait_retries_observation_without_replaying_actions(monkeypatch):
    from ok.task.TaskExecutor import TaskExecutor
    calls = []
    def original(executor, condition, time_out, pre, post, settle, required):
        calls.append((time_out, pre, post))
        if len(calls) == 1:
            pre()
            post()
            raise WaitFailedException()
        return 'ready'
    monkeypatch.setattr(TaskExecutor, 'wait_condition', original)
    install_wait_continuation()
    task = make_task()
    executor = SimpleNamespace(current_task=task)
    before, after = Mock(), Mock()
    assert TaskExecutor.wait_condition(executor, lambda: True, 12, before, after, .1, True) == 'ready'
    assert calls == [(12, before, after), (5, None, None)]
    before.assert_called_once()
    after.assert_called_once()


def test_task_recovery_install_keeps_original_auto_combat_and_character_methods():
    from src.task.AutoCombatTask import AutoCombatTask
    from src.char.BaseChar import BaseChar
    before = AutoCombatTask.run, AutoCombatTask.__init__, BaseChar.perform, BaseChar.sleep
    install_continuation_hooks()
    assert (AutoCombatTask.run, AutoCombatTask.__init__, BaseChar.perform, BaseChar.sleep) == before


def test_background_combat_does_not_retry_its_waits():
    from src.task.AutoCombatTask import AutoCombatTask
    executor = make_task().executor
    executor.scene, executor.text_fix = None, {}
    task = AutoCombatTask(executor, None)
    operation = Mock(side_effect=WaitFailedException())
    with pytest.raises(WaitFailedException):
        retry_checkpoint(task, operation, 'original combat wait')
    operation.assert_called_once()


@pytest.mark.parametrize('flag', ['_in_combat', '_dualpc_combat_scope'])
def test_farming_combat_keeps_original_wait_behavior(flag):
    task = make_task()
    setattr(task, flag, True)
    operation = Mock(side_effect=WaitFailedException())
    with pytest.raises(WaitFailedException):
        retry_checkpoint(task, operation, 'combat wait')
    operation.assert_called_once()


