from types import SimpleNamespace
from unittest.mock import Mock
import pytest
import numpy as np

from ok import Box
from dualpc import garden_shop as shop


def test_recommended_goods_first_then_original_order_and_only_affordable():
    items = [shop.Item(0, 2, False, None), shop.Item(1, 8, False, None),
             shop.Item(2, 4, True, None), shop.Item(3, 8, True, None)]
    assert [i.index for i in shop.affordable_order(items, 8)] == [2, 3, 0, 1]
    assert [i.index for i in shop.affordable_order(items, 3)] == [0]


def test_shop_rechecks_real_balance_and_leaves_when_no_goods_are_affordable(monkeypatch):
    state = {'money': 24, 'selected': None, 'bought': []}
    items = [shop.Item(i, price, rec, Box(100+i*100, 200, 50, 50, name=str(i)))
             for i, price, rec in ((0, 2, False), (1, 8, False), (2, 4, True), (3, 8, True))]
    buy = Box(1000, 700, 60, 30, name='购买')
    close = Box(1600, 70, 60, 30, name='close')
    monkeypatch.setattr(shop, 'read_money', lambda task: state['money'])
    monkeypatch.setattr(shop, 'inventory', lambda task: [i for i in items if i.index not in state['bought']])
    def click(box, **kw):
        if box is buy:
            state['money'] -= items[state['selected']].price
            state['bought'].append(state['selected'])
        elif box is not close:
            state['selected'] = int(box.name)
    task = SimpleNamespace(next_frame=Mock(), sleep=Mock(), info_set=Mock(), log_info=Mock(), click=Mock(side_effect=click),
        find_one=Mock(return_value=close), wait_until=lambda f, **kw: f(),
        ocr=lambda *a, **kw: [buy, Box(1100, 700, 30, 30, name=f"-{items[state['selected']].price}")])
    shop.run_shop(task)
    assert state['bought'] == [2, 3, 0]
    assert state['money'] == 10
    assert task.click.call_args.args == (close,)


def test_unknown_money_never_buys_or_refreshes(monkeypatch):
    monkeypatch.setattr(shop, 'read_money', lambda task: None)
    task = SimpleNamespace(next_frame=Mock(), sleep=Mock(), info_set=Mock(), log_info=Mock(), click=Mock(),
                           find_one=Mock(return_value=None))
    shop.run_shop(task)
    task.click.assert_not_called()


@pytest.mark.parametrize('labels', [['-4', '购买'], ['-4购买'], ['购买 4'], ['− 4 購買'], ['Buy -4']])
def test_purchase_price_can_be_merged_with_the_button(labels):
    buy, price = shop.purchase_controls([Box(100, 100, 80, 30, name=s) for s in labels])
    assert buy is not None and price == 4


def test_inventory_does_not_discard_a_recommended_slot_when_its_price_is_unreadable(monkeypatch):
    from dualpc import garden
    monkeypatch.setattr(garden, 'recommendation_score', Mock(side_effect=[0, 0, .95, 0, 0, 0, 0, 0]))
    task = SimpleNamespace(frame=np.zeros((1080, 1920, 3), dtype=np.uint8), ocr=Mock(return_value=[]))
    items = shop.inventory(task)
    assert [(i.index, i.price, i.recommended) for i in items] == [(2, None, True)]
    assert shop.affordable_order([shop.Item(0, 2, False, None)] + items, 10)[0] is items[0]


@pytest.mark.parametrize('mode,expected', [
    ('success', [2, 0]), ('unaffordable', [0]), ('sold', [0]),
    ('unreadable', []), ('click_failed', []), ('stale_detail', [])])
def test_unknown_recommended_price_is_resolved_before_normal_goods(monkeypatch, mode, expected):
    state = {'money': 16, 'selected': None, 'bought': []}
    items = [shop.Item(0, 2, False, Box(100, 200, 50, 50, name='0')),
             shop.Item(2, 4 if mode == 'stale_detail' else None, True, Box(300, 200, 50, 50, name='2'))]
    buy = Box(1000, 700, 90, 30, name='-4购买')
    close = Box(1600, 70, 60, 30, name='close')
    monkeypatch.setattr(shop, 'read_money', lambda task: state['money'])
    # Leave purchased badges visible: the runner must remember confirmed buys.
    monkeypatch.setattr(shop, 'inventory', lambda task: items)
    monkeypatch.setattr('dualpc.garden.modern_reward_choice', lambda task: None)
    def ocr(*args, **kwargs):
        if state['selected'] == 2:
            if mode == 'unreadable':
                return [Box(1000, 700, 90, 30, name='购买')]
            if mode == 'sold':
                return [Box(1000, 700, 90, 30, name='已售罄')]
            buy.name = '-8购买' if mode == 'unaffordable' else '-2购买' if mode == 'stale_detail' else '-4购买'
        else:
            buy.name = '-2购买'
        return [buy]
    def click(box, **kwargs):
        if box is buy:
            if mode != 'click_failed':
                state['money'] -= 4 if state['selected'] == 2 else 2
                state['bought'].append(state['selected'])
        elif box is not close:
            state['selected'] = int(box.name)
    task = SimpleNamespace(next_frame=Mock(), sleep=Mock(), info_set=Mock(), log_info=Mock(),
                           click=Mock(side_effect=click), find_one=Mock(return_value=close), ocr=ocr)
    shop.run_shop(task)
    assert state['bought'] == expected
    assert state['money'] >= 10
    selected = [int(c.args[0].name) for c in task.click.call_args_list if c.args[0] not in (buy, close)]
    assert selected[0] == 2
    if mode in ('unreadable', 'click_failed', 'stale_detail'):
        assert selected == [2]


def test_unexpected_currency_change_does_not_confirm_a_purchase(monkeypatch):
    monkeypatch.setattr(shop, 'read_money', lambda task: 7)
    task = SimpleNamespace(next_frame=Mock())
    assert not shop.wait_for_purchase(task, 10, 4)


@pytest.mark.parametrize('balance', [0, 8, 10])
def test_reserved_ten_coins_are_never_spent(monkeypatch, balance):
    monkeypatch.setattr(shop, 'read_money', lambda task: balance)
    inventory = Mock()
    monkeypatch.setattr(shop, 'inventory', inventory)
    task = SimpleNamespace(next_frame=Mock(), sleep=Mock(), info_set=Mock(), log_info=Mock(),
                           click=Mock(), find_one=Mock(return_value=None))
    shop.run_shop(task)
    inventory.assert_not_called()
    task.click.assert_not_called()
