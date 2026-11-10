from types import SimpleNamespace
from unittest.mock import Mock

import pytest

from src.task.DailyTask import DailyTask
from src.task.DailyTask2 import DailyTask2
from src.task.WWOneTimeTask import WWOneTimeTask
from ok import TaskDisabledException


def fake_daily(monkeypatch, kind, order='Tacet First', all_nests=True):
    events = []
    monkeypatch.setattr(WWOneTimeTask, 'run', lambda self: events.append('login'))
    monkeypatch.setattr('dualpc.daily_resources.complete_daily_stamina', lambda task: events.append('topup'))
    config = {'Run Tacet or Forgery First': order, 'Which to Farm': 'Tacet Suppression',
              'Task Retry': 1, 'Exit with Error': False, 'Farm Nightmare Nest for Daily Echo': True,
              'Additional Tasks to Run After Daily Task': ['Check Weekly Garden'] +
                  (['Auto Farm all Nightmare Nest'] if all_nests else [])}
    task = SimpleNamespace(config=config, support_tasks=['Tacet Suppression', 'Forgery Challenge', 'Simulation Challenge'],
        validate_additional_tasks=Mock(), ensure_main=Mock(), open_daily=Mock(return_value=(0, False)),
        info_set=Mock(), sleep=Mock(), log_info=Mock(), log_debug=Mock(), screenshot=Mock(),
        log_error=Mock(side_effect=AssertionError('No retry should be needed')),
        claim_daily=lambda: events.append('claim'), claim_mail=lambda: events.append('mail'),
        claim_battle_pass=lambda: events.append('pass'), check_weekly_garden=lambda: events.append('weekly'))
    def get_task(cls):
        return SimpleNamespace(farm_tacet=lambda **kw: events.append('tacet'),
            farm_forgery=lambda **kw: events.append('forgery'), farm_simulation=lambda **kw: events.append('simulation'),
            run_capture_mode=lambda: events.append('nest'), ensure_main=Mock())
    task.get_task_by_class = get_task
    task.run_task_by_class = lambda cls: events.append('nest')
    task.run_additional_tasks = lambda: DailyTask.run_additional_tasks(task)
    return task, events


@pytest.mark.parametrize('order,farms', [('Tacet First', ['tacet', 'forgery', 'simulation']),
                                        ('Forgery First', ['forgery', 'tacet', 'simulation'])])
@pytest.mark.parametrize('all_nests', [True, False])
def test_enhanced_daily_finishes_stamina_before_nest_and_weekly(monkeypatch, order, farms, all_nests):
    task, events = fake_daily(monkeypatch, DailyTask2, order, all_nests)
    DailyTask2.run(task)
    assert events == ['login', *farms, 'topup', 'nest', 'claim', 'mail', 'pass', 'weekly']


def test_base_daily_uses_same_stamina_nest_weekly_order(monkeypatch):
    task, events = fake_daily(monkeypatch, DailyTask)
    DailyTask.run(task)
    assert events == ['login', 'tacet', 'topup', 'nest', 'claim', 'mail', 'pass', 'weekly']


def test_stopping_daily_during_stamina_does_not_start_later_tasks(monkeypatch):
    task, events = fake_daily(monkeypatch, DailyTask2)
    task.get_task_by_class = lambda cls: SimpleNamespace(farm_tacet=Mock(side_effect=TaskDisabledException()))
    with pytest.raises(TaskDisabledException):
        DailyTask2.run(task)
    assert events == ['login']
