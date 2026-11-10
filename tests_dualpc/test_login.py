from types import SimpleNamespace
from unittest.mock import Mock

import pytest

from dualpc.login import monthly_text_confirmed, monthly_visible, reward_visible, claim_monthly, install_login_hook


@pytest.mark.parametrize('lines', [
    ['剩余14天', '点击领取今日月相观测卡奖励'],
    ['剩 餘 14 天', '點擊領取今日月相觀測卡獎勵'],
    ['Remaining 14 days', "Click to claim today's Lunite Subscription rewards"],
])
def test_monthly_reward_requires_both_daily_claim_and_remaining_days(lines):
    assert monthly_text_confirmed(lines)


@pytest.mark.parametrize('lines', [[], ['月相观测卡', '剩余14天', '立即购买'],
    ['点击领取今日奖励', '剩余14天'], ['点击领取今日月相观测卡奖励'],
    ['Remaining 14 days', 'Lunite Subscription', 'Purchase'], ['剩余14天', '充值']])
def test_shop_and_generic_reward_text_do_not_trigger_clicks(lines):
    assert not monthly_text_confirmed(lines)


def fake_task():
    return SimpleNamespace(find_monthly_card=Mock(return_value=None),
        ocr=Mock(return_value=[]), box_of_screen=Mock(return_value='box'),
        click_relative=Mock(), next_frame=Mock(), log_info=Mock(),
        set_check_monthly_card=Mock())


def test_capture_ocr_fallback_and_original_template():
    task = fake_task()
    task.ocr.return_value = [SimpleNamespace(name=t) for t in ['剩余14天', '点击领取今日月相观测卡奖励']]
    assert monthly_visible(task)
    task.find_monthly_card.return_value = object()
    task.ocr.reset_mock()
    assert monthly_visible(task)
    task.ocr.assert_not_called()


def test_verified_overlay_click_stops_as_soon_as_it_is_gone(monkeypatch):
    task = fake_task()
    monkeypatch.setattr('dualpc.login.monthly_visible', Mock(side_effect=[True, False]))
    monkeypatch.setattr('dualpc.login.reward_visible', Mock(return_value=False))
    assert claim_monthly(task)
    task.click_relative.assert_called_once_with(.5, .89, after_sleep=.8)
    task.set_check_monthly_card.assert_called_once_with(next_day=True)


def test_claim_result_is_verified_before_dismissal(monkeypatch):
    task = fake_task()
    monkeypatch.setattr('dualpc.login.monthly_visible', Mock(side_effect=[True, False, False]))
    monkeypatch.setattr('dualpc.login.reward_visible', Mock(side_effect=[True, False]))
    assert claim_monthly(task)
    assert task.click_relative.call_count == 2


def test_current_short_reward_heading_and_continue_footer():
    task = fake_task()
    task.ocr.return_value = [SimpleNamespace(name=text) for text in ['获得', '90', '点击空白处继续']]
    assert reward_visible(task)
    task.ocr.return_value = [SimpleNamespace(name=text) for text in ['购买可获得奖励', '点击空白处继续']]
    assert not reward_visible(task)


def test_receipt_already_visible_when_application_restarts(monkeypatch):
    task = fake_task()
    monkeypatch.setattr('dualpc.login.monthly_visible', lambda _: False)
    monkeypatch.setattr('dualpc.login.reward_visible', Mock(side_effect=[True, False]))
    assert claim_monthly(task)
    task.click_relative.assert_called_once_with(.5, .89, after_sleep=.8)


def test_monthly_claim_is_bounded_and_stop_is_not_swallowed(monkeypatch):
    task = fake_task()
    monkeypatch.setattr('dualpc.login.monthly_visible', lambda _: True)
    assert claim_monthly(task)
    assert task.click_relative.call_count == 3
    task.click_relative.side_effect = RuntimeError('paused')
    with pytest.raises(RuntimeError, match='paused'):
        claim_monthly(task)


@pytest.mark.parametrize('remote,enabled', [(True, True), (False, True), (True, False)])
def test_logged_in_gate_no_longer_skips_monthly_but_local_and_disabled_stay_unchanged(monkeypatch, remote, enabled):
    from src.task.AutoLoginTask import AutoLoginTask
    old = Mock(return_value=False)
    monkeypatch.setattr(AutoLoginTask, 'run', lambda task: old(task))
    visible = Mock(return_value=True)
    claim = Mock(return_value=True)
    monkeypatch.setattr('dualpc.login.monthly_visible', visible)
    monkeypatch.setattr('dualpc.login.claim_monthly', claim)
    install_login_hook()
    task = SimpleNamespace(logged_in=True, executor=SimpleNamespace(interaction=SimpleNamespace(is_remote=remote)),
                           monthly_card_config={'Check Monthly Card': enabled})
    assert AutoLoginTask.run(task) is (remote and enabled)
    assert claim.call_count == int(remote and enabled)
    assert old.call_count == int(not (remote and enabled))
