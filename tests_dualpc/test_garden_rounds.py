from inspect import unwrap
from types import SimpleNamespace
from unittest.mock import Mock

import pytest
from ok import Box, CannotFindException
from src.task.GardenTask import GardenTask
from src.task.WWOneTimeTask import WWOneTimeTask
from dualpc.garden import use_garden_blessing, blessing_controls


@pytest.mark.parametrize('progresses,rounds', [([0, 3000, 6000], 2), ([6000], 0)])
def test_round_always_checks_overview_before_starting_another(monkeypatch, progresses, rounds):
    events = []
    monkeypatch.setattr(WWOneTimeTask, 'run', lambda task: None)
    task = SimpleNamespace(ensure_main=Mock(), open_garden_weekly_page=lambda: events.append('overview'),
        read_weekly_garden_progress=Mock(side_effect=progresses), info_set=Mock(), log_info=Mock(),
        screenshot=Mock(), wait_until=lambda predicate, **kw: predicate(),
        click=lambda *a, **kw: events.append('enter'), run_garden_loop=lambda: events.append('round'))
    unwrap(GardenTask.run)(task)
    assert events == ['overview', *(['enter', 'round', 'overview']*rounds)]


def test_unreadable_weekly_counter_does_not_start_another_round(monkeypatch):
    monkeypatch.setattr(WWOneTimeTask, 'run', lambda task: None)
    task = SimpleNamespace(ensure_main=Mock(), open_garden_weekly_page=Mock(),
        read_weekly_garden_progress=lambda: None, wait_until=lambda predicate, **kw: predicate(),
        screenshot=Mock(), click=Mock())
    with pytest.raises(CannotFindException):
        unwrap(GardenTask.run)(task)
    task.click.assert_not_called()


@pytest.mark.parametrize('summary', ['4400/6000', '本周游历值 已达到上限', '识别不清'])
def test_result_always_returns_instead_of_pressing_restart(summary):
    buttons = {name: Box(700, 900, 100, 50, name=name) for name in ('a_garden_restart', 'a_garden_back')}
    task = SimpleNamespace(sleep=Mock(), find_best_garden_feature=lambda: None,
        find_one=lambda name: buttons[name], ocr=lambda *a, **kw: [SimpleNamespace(name=summary)],
        log_info=Mock(), click=Mock(), wait_feature=Mock(return_value=True), back=Mock(), info_set=Mock())
    GardenTask.run_garden_loop(task)
    assert task.click.call_args.args[0].name == 'a_garden_back'
    assert task.click.call_count == 1


def blessing_task():
    state = {'day': 1, 'uses': 0, 'opened': False, 'used': 0}
    button = Box(180, 840, 160, 40, name='使用祝福')
    def ocr(x, *args, **kw):
        if x == .025:
            return [SimpleNamespace(name=f"阶段：2/7 第{state['day']}天")]
        if state['opened']:
            return [button, SimpleNamespace(name='剩余次数：'), SimpleNamespace(name=str(state['uses']))]
        return []
    def click(target, *a, **kw):
        if target is button:
            state.update(uses=state['uses']-1, opened=False, used=state['used']+1)
        elif kw.get('name') == 'garden_blessing_open':
            state['opened'] = True
        else:
            state['opened'] = False
    return SimpleNamespace(ocr=ocr, click=Mock(side_effect=click), next_frame=Mock(), info_set=Mock()), state


def test_skill_unavailable_today_is_checked_again_next_day_in_same_stage():
    task, state = blessing_task()
    assert use_garden_blessing(task)
    assert state['used'] == 0
    assert not use_garden_blessing(task)
    state.update(day=2, uses=1)
    assert use_garden_blessing(task)
    assert state['used'] == 1
    assert not use_garden_blessing(task)


def test_skill_count_can_be_split_across_ocr_boxes():
    task, state = blessing_task()
    state.update(opened=True, uses=1)
    assert blessing_controls(task)[1] == 1
