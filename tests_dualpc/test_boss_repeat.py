from types import SimpleNamespace
from unittest.mock import Mock

import pytest
from ok import Box
from ok.task.exceptions import CannotFindException, TaskDisabledException

from dualpc import boss_repeat


def boxes(*names):
    return [Box(x=700 + i * 100, y=650, width=80, height=30, name=name)
            for i, name in enumerate(names)]


@pytest.mark.parametrize('text', ['重新挑战', '再次挑战', '重新挑戰', '再次挑戰', 'Challenge Again'])
def test_restart_button_is_clicked_at_its_own_text(text):
    task = SimpleNamespace(ocr=Mock(return_value=boxes('退出副本', text)))
    kind, button = boss_repeat.dialog(task)
    assert kind == 'restart' and button.name == text


@pytest.mark.parametrize('text', ['确认消耗60结晶波片领取奖励？', '是否消耗结晶波片领取奖励后重新挑战？',
                                 '確認消耗體力領取獎勵？', 'Confirm claim reward with Waveplates?'])
def test_resource_dialog_is_always_cancelled_even_if_it_mentions_retry(text):
    task = SimpleNamespace(ocr=Mock(return_value=boxes(text, '取消', '确认')))
    kind, button = boss_repeat.dialog(task)
    assert kind == 'resource' and button.name == '取消'


def test_confirmation_requires_explicit_restart_context():
    task = SimpleNamespace(ocr=Mock(return_value=boxes('是否重新挑战当前副本？', '取消', '确认')))
    assert boss_repeat.dialog(task)[0] == 'confirm'
    task.ocr.return_value = boxes('是否退出当前副本？', '取消', '确认')
    assert boss_repeat.dialog(task) == (None, None)


def flow(states, transitions):
    task = SimpleNamespace(state='hud', clicks=[], keys=[], tick=0)
    task.info_set = Mock()
    task.log_info = Mock()
    task.next_frame = Mock()
    task.in_team = lambda: (states[task.state][0], None, None)
    task.in_realm = lambda: task.state == 'ready'
    task.ocr = lambda *a, **kw: boxes(*states[task.state][1])
    def click(button, **kwargs):
        task.clicks.append(button.name)
        task.state = transitions.get((task.state, button.name), task.state)
    def key(name, **kwargs):
        task.keys.append(name)
        task.state = transitions.get((task.state, name), task.state)
    def sleep(seconds):
        task.tick += 1
        if task.state == 'loading' and task.tick % 2 == 0:
            task.state = 'ready'
    task.click, task.send_key, task.sleep = click, key, sleep
    return task


BASE = {'hud': (True, []), 'menu': (False, ['退出副本', '重新挑战']),
        'claim': (False, ['是否消耗结晶波片领取奖励？', '取消', '确认']),
        'loading': (False, []), 'ready': (True, []), 'unknown': (False, ['未知弹窗', '确认'])}


def test_restart_verifies_loading_then_realm_hud():
    task = flow(BASE, {('hud', 'esc'): 'menu', ('menu', '重新挑战'): 'loading'})
    assert boss_repeat.restart_boss_domain(task)
    assert task.keys == ['esc'] and task.clicks == ['重新挑战'] and task.state == 'ready'


def test_claim_overlay_is_cancelled_in_place_before_restart():
    task = flow(BASE, {('claim', '取消'): 'hud', ('hud', 'esc'): 'menu', ('menu', '重新挑战'): 'loading'})
    task.state = 'claim'
    assert boss_repeat.restart_boss_domain(task)
    assert task.clicks == ['取消', '重新挑战']


def test_restart_confirmation_is_handled_before_loading():
    states = {**BASE, 'confirm': (False, ['确定重新挑战当前副本？', '取消', '确认'])}
    task = flow(states, {('hud', 'esc'): 'menu', ('menu', '重新挑战'): 'confirm', ('confirm', '确认'): 'loading'})
    assert boss_repeat.restart_boss_domain(task)
    assert task.clicks == ['重新挑战', '确认']


def test_missing_click_is_retried_at_most_three_times():
    task = flow(BASE, {('hud', 'esc'): 'menu'})
    with pytest.raises(CannotFindException):
        boss_repeat.restart_boss_domain(task)
    assert task.clicks == ['重新挑战'] * 3


def test_old_hud_without_loading_never_counts_as_a_new_round():
    task = flow(BASE, {('hud', 'esc'): 'menu', ('menu', '重新挑战'): 'hud'})
    with pytest.raises(CannotFindException):
        boss_repeat.restart_boss_domain(task)
    assert task.clicks == ['重新挑战']


def test_unknown_dialog_is_never_confirmed_or_mistaken_for_loading():
    task = flow(BASE, {})
    task.state = 'unknown'
    with pytest.raises(CannotFindException):
        boss_repeat.restart_boss_domain(task)
    assert not task.clicks and not task.keys


def test_resource_modal_hiding_the_hud_cannot_verify_a_restart():
    task = flow(BASE, {('hud', 'esc'): 'menu', ('menu', '重新挑战'): 'claim', ('claim', '取消'): 'hud'})
    with pytest.raises(CannotFindException):
        boss_repeat.restart_boss_domain(task)
    assert '确认' not in task.clicks


def test_pause_propagates_immediately():
    task = flow(BASE, {})
    task.next_frame.side_effect = TaskDisabledException()
    with pytest.raises(TaskDisabledException):
        boss_repeat.restart_boss_domain(task)
    assert not task.clicks and not task.keys
