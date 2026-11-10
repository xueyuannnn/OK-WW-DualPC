"""Resume recoverable steps without restarting whole tasks or material actions."""
from functools import wraps

from ok import ConfigOption, Icon, TriggerTask
from ok.task.exceptions import CannotFindException, WaitFailedException

OPTION_NAME = 'Task Recovery'


def recovery_option():
    return ConfigOption(OPTION_NAME, {
        'Resume Interrupted Tasks': True,
        'Recovery Retry Limit': 3,
    }, description='Recover temporary interface interruptions at the current task step.',
        config_description={
            'Resume Interrupted Tasks': 'Retry the current wait or navigation step. Manual pauses, stops and resource decisions are preserved.',
            'Recovery Retry Limit': 'Maximum consecutive retries for one interrupted step.',
        }, config_type={'Recovery Retry Limit': {'min': 1, 'max': 10}},
        show_at_tab=True, icon=Icon.SYNC)


def options(task):
    executor = getattr(task, 'executor', None)
    if not executor or not getattr(getattr(executor, 'interaction', None), 'is_remote', False):
        return {}
    global_config = getattr(executor, 'global_config', None)
    return global_config.get_config(OPTION_NAME) if global_config else {}


def retry_limit(task, key):
    # Background combat keeps its original waits and control flow.
    if (isinstance(task, TriggerTask) or getattr(task, '_in_combat', False)
            or getattr(task, '_dualpc_combat_scope', False)):
        return 0
    if not getattr(getattr(getattr(task, 'executor', None), 'interaction', None), 'is_remote', False):
        return 0
    settings = options(task)
    if not settings.get(key, True):
        return 0
    return max(1, min(10, int(settings.get('Recovery Retry Limit', 3))))


def show_recovery(task, step, attempt, limit):
    task.info_set('Interruption Recovery', f'{task.tr(step)} · {attempt}/{limit}')
    task.log_info(f'中断自动继续：{step}，重试当前步骤 {attempt}/{limit}')


def recoverable(error):
    from src.task.BaseCombatTask import CharDeadException
    if isinstance(error, CharDeadException):
        return False
    return (isinstance(error, (WaitFailedException, CannotFindException))
            or type(error) is Exception and str(error) in {
                'Please start in game world and in team!', 'can not walk to treasure!'})


def retry_checkpoint(task, operation, step, *, before_retry=None):
    limit = retry_limit(task, 'Resume Interrupted Tasks')
    if not limit or getattr(task, '_dualpc_retrying_step', False):
        return operation()
    task._dualpc_retrying_step = True
    try:
        for attempt in range(limit + 1):
            try:
                return operation()
            except Exception as error:
                if not recoverable(error) or attempt >= limit:
                    raise
                show_recovery(task, step, attempt + 1, limit)
                task.sleep(.5)
                task.next_frame()
                if before_retry:
                    before_retry()
    finally:
        task._dualpc_retrying_step = False


def install_wait_continuation():
    from ok.task.TaskExecutor import TaskExecutor
    original = TaskExecutor.wait_condition
    if getattr(original, '_dualpc_resume_wait', False):
        return

    @wraps(original)
    def wait_condition(executor, condition, time_out=0, pre_action=None, post_action=None,
                       settle_time=-1, raise_if_not_found=False):
        task = getattr(executor, 'current_task', None)
        if (task is None or not raise_if_not_found or not retry_limit(task, 'Resume Interrupted Tasks')
                or getattr(task, '_dualpc_retrying_step', False)):
            return original(executor, condition, time_out, pre_action, post_action,
                            settle_time, raise_if_not_found)
        calls = 0
        def wait():
            nonlocal calls
            initial = calls == 0
            calls += 1
            # A retry observes the expected screen only. Never repeat a resource
            # click or a supplied pre/post action after an uncertain timeout.
            return original(executor, condition, time_out if initial else 5,
                            pre_action if initial else None, post_action if initial else None,
                            settle_time, True)
        return retry_checkpoint(task, wait, 'Wait for interface')

    wait_condition._dualpc_resume_wait = True
    TaskExecutor.wait_condition = wait_condition


def install_navigation_continuation():
    from src.task.BaseWWTask import BaseWWTask
    for name in ('openF2Book', 'open_boss_book', 'wait_click_travel', 'wait_in_team_and_world',
                 'ensure_main', 'walk_to_treasure', 'click_team_challenge'):
        original = getattr(BaseWWTask, name)
        if getattr(original, '_dualpc_resume_navigation', False):
            continue
        @wraps(original)
        def navigate(task, *args, _original=original, _name=name, **kwargs):
            return retry_checkpoint(task, lambda: _original(task, *args, **kwargs), _name)
        navigate._dualpc_resume_navigation = True
        setattr(BaseWWTask, name, navigate)

    from src.task.GardenTask import GardenTask
    original = GardenTask.run_garden_loop
    if not getattr(original, '_dualpc_resume_garden_steps', False):
        @wraps(original)
        def garden_loop(task):
            return retry_checkpoint(task, lambda: original(task), 'Garden current stage')
        garden_loop._dualpc_resume_garden_steps = True
        GardenTask.run_garden_loop = garden_loop


def install_continuation_hooks():
    install_wait_continuation()
    install_navigation_continuation()
