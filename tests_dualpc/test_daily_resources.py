from datetime import datetime
import json

import pytest

from dualpc.daily_resources import CubeBudget
from types import SimpleNamespace
from unittest.mock import Mock
from ok import Box
from dualpc import daily_resources as resources


def test_both_daily_tasks_and_restarts_share_persisted_four_cube_budget(tmp_path):
    path = tmp_path / 'usage.json'
    for n in range(4):
        budget = CubeBudget(path, 99, datetime(2026, 10, 22, 15))
        assert budget.remaining == 4-n
        budget.reserve()
        assert json.loads(path.read_text())['pending']
        budget.verified()
    reopened = CubeBudget(path, 99, datetime(2026, 10, 22, 23))
    assert reopened.remaining == 0
    with pytest.raises(RuntimeError):
        reopened.reserve()
    assert json.loads(path.read_text())['verified'] == 4


def test_crash_or_uncertain_click_blocks_repeated_consumption(tmp_path):
    path = tmp_path / 'usage.json'
    budget = CubeBudget(path)
    budget.reserve()
    with pytest.raises(RuntimeError, match='unverified'):
        CubeBudget(path)
    with pytest.raises(RuntimeError):
        budget.reserve()


def test_game_day_rollover_at_four_am_and_user_can_lower_limit(tmp_path):
    path = tmp_path / 'usage.json'
    budget = CubeBudget(path, 1, datetime(2026, 10, 22, 3, 59))
    budget.reserve()
    budget.verified()
    assert CubeBudget(path, 1, datetime(2026, 10, 22, 3, 59)).remaining == 0
    assert CubeBudget(path, 1, datetime(2026, 10, 22, 4)).remaining == 1


@pytest.mark.parametrize('data', ['bad json', '{}', '{"reserved":-1}', '{"reserved":5}'])
def test_invalid_or_corrupt_budget_does_not_reset_to_allow_more(tmp_path, data):
    path = tmp_path / 'usage.json'
    path.write_text(data)
    with pytest.raises(ValueError):
        CubeBudget(path)


def test_zero_limit_never_reserves_material(tmp_path):
    budget = CubeBudget(tmp_path/'usage.json', 0)
    assert budget.remaining == 0
    with pytest.raises(RuntimeError):
        budget.reserve()


def daily_task(tmp_path, monkeypatch, progress='160/180', stamina=(60, 0, 60)):
    budget = CubeBudget(tmp_path/'usage.json')
    monkeypatch.setattr(resources, 'budget_for', lambda task: budget)
    domain = SimpleNamespace(farm_tacet=Mock())
    task = SimpleNamespace(config={resources.ENABLE: True}, info_set=Mock(), log_info=Mock(), log_error=Mock(),
        ensure_main=Mock(), open_daily=Mock(side_effect=[(160, False), (180, True)]),
        ocr=Mock(return_value=[Box(200, 400, 100, 30, name=progress)]),
        openF2Book=Mock(), get_stamina=Mock(return_value=stamina),
        get_task_by_class=Mock(return_value=domain))
    return task, domain, budget


def test_daily_topup_uses_current_and_reserve_before_any_cube(tmp_path, monkeypatch):
    task, domain, _ = daily_task(tmp_path, monkeypatch, stamina=(30, 30, 60))
    refill = Mock(side_effect=AssertionError('No cube is needed'))
    monkeypatch.setattr(resources, 'use_one_cube', refill)
    resources.complete_daily_stamina(task)
    refill.assert_not_called()
    domain.farm_tacet.assert_called_once_with(daily=True, used_stamina=120, config=task.config)
    assert 'use_stamina' not in domain.__dict__


def test_daily_topup_consumes_only_one_when_one_is_enough(tmp_path, monkeypatch):
    task, domain, budget = daily_task(tmp_path, monkeypatch, stamina=(0, 0, 0))
    task.get_stamina.side_effect = [(0, 0, 0), (60, 0, 60)]
    def refill(*args):
        budget.reserve()
        budget.verified()
        return True
    consume = Mock(side_effect=refill)
    monkeypatch.setattr(resources, 'use_one_cube', consume)
    resources.complete_daily_stamina(task)
    assert consume.call_count == 1 and budget.data['reserved'] == 1
    domain.farm_tacet.assert_called_once()


def test_four_cube_limit_cannot_be_bypassed_when_stamina_does_not_increase(tmp_path, monkeypatch):
    task, domain, budget = daily_task(tmp_path, monkeypatch, stamina=(0, 0, 0))
    def refill(*args):
        budget.reserve()
        budget.verified()
        return True
    consume = Mock(side_effect=refill)
    monkeypatch.setattr(resources, 'use_one_cube', consume)
    resources.complete_daily_stamina(task)
    assert consume.call_count == 4
    domain.farm_tacet.assert_not_called()


@pytest.mark.parametrize('ready,progress', [(True, '160/180'), (False, 'unreadable'), (False, '180/180')])
def test_no_material_for_completed_or_unknown_daily_objective(tmp_path, monkeypatch, ready, progress):
    task, domain, _ = daily_task(tmp_path, monkeypatch, progress=progress)
    task.open_daily.side_effect = None
    task.open_daily.return_value = (160, ready)
    consume = Mock(side_effect=AssertionError('No material should be used'))
    monkeypatch.setattr(resources, 'use_one_cube', consume)
    resources.complete_daily_stamina(task)
    consume.assert_not_called()
    domain.farm_tacet.assert_not_called()


def test_unknown_progress_after_farming_stops_further_spending_on_claim_retries(tmp_path, monkeypatch):
    task, domain, _ = daily_task(tmp_path, monkeypatch)
    task.open_daily.side_effect = None
    task.open_daily.return_value = (160, False)
    task.ocr.side_effect = [[Box(200, 400, 100, 30, name='160/180')], []]
    resources.complete_daily_stamina(task)
    resources.complete_daily_stamina(task)
    domain.farm_tacet.assert_called_once()


def solvent_flow(description='消耗1结晶溶剂补充60结晶波片', verify_stock=True):
    import numpy as np
    state = {'dialog': False, 'selected': False, 'current': 35, 'stock': 75, 'used': 0}
    confirm = Box(1300, 760, 60, 30, name='确认')
    cancel = Box(550, 760, 60, 30, name='取消')
    def ocr(x, y, ex, ey, **kw):
        if x == .18:
            return [Box(350, 190, 200, 40, name='补充结晶波片')] if state['dialog'] else []
        if x == .49:
            return [Box(1335, 52, 100, 30, name=f"{state['current']}/240")]
        if x == .37:
            return [Box(750, 580, 400, 30, name=description)]
        if x == .50:
            value = state['stock'] if verify_stock or not state['used'] else 75
            return [Box(990, 540, 30, 30, name=str(value))]
        if x == .60:
            return [confirm]
        if x == .22:
            return [cancel]
        raise AssertionError((x, y, ex, ey))
    def click(target, *args, **kw):
        if target is confirm:
            state.update(current=state['current']+60, stock=state['stock']-1, used=state['used']+1, dialog=False)
        elif target is cancel:
            state['dialog'] = False
        elif target == .497:
            state['selected'] = True
        else:
            state['dialog'] = True
    task = SimpleNamespace(frame=np.zeros((1080, 1920, 3), dtype=np.uint8), ocr=ocr,
        click=Mock(side_effect=click), next_frame=Mock(), info_set=Mock(), log_info=Mock(),
        get_stamina=lambda: (state['current'], 10, state['current']+10),
        wait_until=lambda f, **kw: f())
    return task, state


def test_one_cube_requires_both_stamina_increase_and_inventory_decrease(tmp_path):
    task, state = solvent_flow()
    budget = CubeBudget(tmp_path/'usage.json')
    assert resources.use_one_cube(task, budget)
    assert state['used'] == 1 and state['current'] == 95 and state['stock'] == 74
    assert budget.data == {'day': budget.day, 'reserved': 1, 'verified': 1, 'pending': False}
    assert not state['dialog']


def test_premium_or_multiple_cube_description_cannot_be_confirmed(tmp_path):
    for description in ('消耗50星声补充60结晶波片', '消耗2结晶溶剂补充120结晶波片'):
        task, state = solvent_flow(description)
        budget = CubeBudget(tmp_path/description)
        assert not resources.use_one_cube(task, budget)
        assert state['used'] == 0 and budget.data['reserved'] == 0


def test_uncertain_inventory_keeps_pending_record_and_prevents_more_cubes(tmp_path):
    task, state = solvent_flow(verify_stock=False)
    budget = CubeBudget(tmp_path/'usage.json')
    with pytest.raises(resources.CannotFindException):
        resources.use_one_cube(task, budget)
    assert state['used'] == 1 and budget.data['pending']
    assert not resources.use_one_cube(task, budget)
    assert state['used'] == 1
