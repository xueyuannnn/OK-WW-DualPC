from types import SimpleNamespace

import pytest

from dualpc.navigation import install_navigation_hooks, install_team_challenge_hook, select_monster_by_name
from src.task.BaseCombatTask import BaseCombatTask


@pytest.mark.parametrize("text,expected", [
    ("提前到达目标位置可能影响剧情体验，是否确认前往？", True),
    ("提前到達目標位置可能影響劇情體驗，是否確認前往？", True),
    ("领取奖励需消耗60点结晶波片，请确认是否领取？", False),
    ("是否确认重构声骸？", False),
])
def test_early_access_only_confirms_exact_weekly_entry_notice(text, expected):
    from unittest.mock import Mock
    from dualpc.navigation import handle_early_access_warning
    task = SimpleNamespace(config={"Teleport to Boss": "Weekly Challenge"},
                           ocr=Mock(return_value=[SimpleNamespace(name=text)]),
                           log_info=Mock(), click_dialog_right_button=Mock())
    assert handle_early_access_warning(task) is expected
    assert task.click_dialog_right_button.call_count == int(expected)
    task.config["Teleport to Boss"] = "No"
    assert not handle_early_access_warning(task)


def test_early_access_wait_is_limited_to_book_transition_and_restores_instance(monkeypatch):
    from unittest.mock import Mock
    from src.task.BaseWWTask import BaseWWTask
    from dualpc.navigation import BOOK_TRANSITION_FEATURES, install_book_early_access_hook
    local_wait = Mock(return_value="other-result")
    def book(task, *args, **kwargs):
        assert task.wait_feature("other", time_out=1) == "other-result"
        return task.wait_feature(BOOK_TRANSITION_FEATURES, time_out=10, settle_time=0.5, raise_if_not_found=True)
    monkeypatch.setattr(BaseWWTask, "click_on_book_target", book)
    install_book_early_access_hook()
    task = SimpleNamespace(config={"Teleport to Boss": "Weekly Challenge"},
                           executor=SimpleNamespace(interaction=SimpleNamespace(is_remote=True)),
                           wait_feature=local_wait, find_one=Mock(side_effect=[None, None, "team-close"]),
                           ocr=Mock(return_value=[SimpleNamespace(name="提前到达目标位置可能影响剧情体验，是否确认前往？")]),
                           log_info=Mock(), click_dialog_right_button=Mock())
    def wait(ready, **options):
        assert ready() is None
        assert ready() is None
        return ready()
    task.wait_until = wait
    assert BaseWWTask.click_on_book_target(task, 1, 9) == "team-close"
    task.click_dialog_right_button.assert_called_once()
    assert task.wait_feature is local_wait


@pytest.mark.parametrize("materials", [True, False])
def test_low_level_warning_only_proceeds_for_explicit_material_mode(materials):
    from unittest.mock import Mock
    from ok import TaskDisabledException
    from dualpc.navigation import handle_low_level_warning
    task = SimpleNamespace(ocr=Mock(return_value=[SimpleNamespace(name="无法掉落任何声骸，是否确认进入？")]),
                           config={"Advanced Skill Material Mode": materials}, log_info=Mock(), log_error=Mock(),
                           click_dialog_right_button=Mock(), disable=Mock())
    if materials:
        assert handle_low_level_warning(task)
        task.click_dialog_right_button.assert_called_once_with()
        task.disable.assert_not_called()
    else:
        with pytest.raises(TaskDisabledException):
            handle_low_level_warning(task)
        task.disable.assert_called_once_with()
        task.click_dialog_right_button.assert_not_called()


def test_unrelated_confirmation_is_not_accepted_as_low_level_warning():
    from unittest.mock import Mock
    from dualpc.navigation import handle_low_level_warning
    task = SimpleNamespace(ocr=Mock(return_value=[SimpleNamespace(name="是否确认重构声骸？")]),
                           click_dialog_right_button=Mock())
    assert not handle_low_level_warning(task)
    task.click_dialog_right_button.assert_not_called()


@pytest.mark.parametrize("remote", [True, False])
def test_recovery_uses_map_on_hardware_and_preserves_local_search(monkeypatch, remote):
    from unittest.mock import Mock
    from dualpc import navigation
    called = []

    def local_search(task):
        called.append("local_search")
        return "local"

    monkeypatch.setattr(BaseCombatTask, "revive_at_tower_and_heal", local_search)
    monkeypatch.setattr(navigation, "select_monster_by_name", lambda task, name: called.append(name))
    install_navigation_hooks()
    installed = BaseCombatTask.revive_at_tower_and_heal
    install_navigation_hooks()
    assert BaseCombatTask.revive_at_tower_and_heal is installed
    task = SimpleNamespace(executor=SimpleNamespace(interaction=SimpleNamespace(is_remote=remote)),
                           log_info=lambda message: None,
                           ensure_main=Mock(), openF2Book=Mock(), get_revive_search_boss_name=lambda: "无冠者",
                           click=Mock(), wait_until=lambda ready, **kwargs: ready(),
                           find_best_match_in_box=Mock(return_value=True), box_of_screen=Mock(),
                           _travel_to_nearest_waypoint=lambda: called.append("map"), sleep=Mock())
    assert installed(task) == (None if remote else "local")
    assert called == (["无冠者", "map"] if remote else ["local_search"])


def test_hovered_start_button_uses_exact_text_in_its_expected_area(monkeypatch):
    from ok import Box
    from src.task.BaseWWTask import BaseWWTask
    from unittest.mock import Mock
    def local_challenge(task):
        raise AssertionError("Remote fallback called the local challenge path")
    monkeypatch.setattr(BaseWWTask, "click_team_challenge", local_challenge)
    install_team_challenge_hook()
    button = Box(1550, 965, 180, 40, name="开启挑战")
    task = SimpleNamespace(executor=SimpleNamespace(interaction=SimpleNamespace(is_remote=True)),
                           find_one=Mock(return_value=None), ocr=Mock(return_value=[button]),
                           box_of_screen=Mock(return_value="button area"),
                           wait_until=lambda ready, **kwargs: ready(), sleep=Mock(),
                           click_box=Mock(), wait_click_skip_dialog_confirm=Mock())
    BaseWWTask.click_team_challenge(task)
    task.box_of_screen.assert_called_once_with(0.64, 0.82, 0.99, 0.98)
    task.click_box.assert_called_once_with(button, after_sleep=1)
    task.wait_click_skip_dialog_confirm.assert_called_once_with()


def monster_list_task(pages):
    from unittest.mock import Mock
    return SimpleNamespace(click=Mock(), send_key_down=Mock(), send_key_up=Mock(), send_key=Mock(),
                           scroll_relative=Mock(), sleep=Mock(), next_frame=Mock(),
                           ocr=Mock(side_effect=pages), click_box=Mock())


def test_monster_selection_scrolls_and_requires_exact_name():
    from ok import Box
    wrong = Box(330, 230, 180, 40, name="梦魇·罗蕾莱")
    right = Box(330, 400, 100, 40, name="罗蕾莱")
    task = monster_list_task([[wrong], [right]])
    assert select_monster_by_name(task, "罗蕾莱") is right
    task.click_box.assert_called_once_with(right, after_sleep=0.5)
    task.scroll_relative.assert_any_call(0.25, 0.65, -4)
    # The path uses keyboard controls for clearing, never Unicode input.
    assert all(call.args[0].isascii() for call in task.send_key.call_args_list)


def test_monster_selection_stops_when_list_no_longer_moves():
    from ok import Box
    page = [Box(330, 230, 180, 40, name="无妄者")]
    task = monster_list_task([page] * 3)
    with pytest.raises(RuntimeError, match="找不到 罗蕾莱"):
        select_monster_by_name(task, "罗蕾莱")
    assert task.ocr.call_count == 3
    task.click_box.assert_not_called()


def test_monster_selection_releases_control_if_clearing_fails():
    task = monster_list_task([])
    task.send_key.side_effect = RuntimeError("disconnected")
    with pytest.raises(RuntimeError, match="disconnected"):
        select_monster_by_name(task, "罗蕾莱")
    task.send_key_up.assert_called_once_with("ctrl")
