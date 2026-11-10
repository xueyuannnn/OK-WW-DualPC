from copy import deepcopy
from types import SimpleNamespace
from unittest.mock import Mock

import pytest
from dualpc.echo_rotation import (ALL, BOSS, WEEKLY, CYCLE, SINGLE, configure_rotation,
                                  farm_setting, run_rotation, rotation_round_completed)
from dualpc.echo_rotation import rotation_targets


def task_for(total=9, per=2, scope=ALL):
    config = {"Farm Mode": CYCLE, "Cycle Scope": scope, "Rounds per Dungeon": per,
              "Repeat Farm Count": total, "Teleport to Boss": "No", "Boss": "Lorelei"}
    return SimpleNamespace(config=config, total_boss_number=2, total_weekly_number=1,
                           tr=str, info={}, log_info=Mock())


def test_rotation_finishes_each_batch_wraps_and_honors_total_without_saving_config():
    task = task_for()
    task.info_set = task.info.__setitem__
    original = deepcopy(task.config)
    visits = []
    def single():
        visits.append((farm_setting(task, "Teleport to Boss"), farm_setting(task, "Which Weekly Boss to Teleport"),
                       farm_setting(task, "Repeat Farm Count")))
        assert farm_setting(task, "Boss") == "Other"
        for _ in range(farm_setting(task, "Repeat Farm Count")):
            rotation_round_completed(task)
    run_rotation(task, single)
    assert visits == [(BOSS, 1, 2), (BOSS, 2, 2), (WEEKLY, 1, 2), (BOSS, 1, 2), (BOSS, 2, 1)]
    assert task.info["Rotation Progress"] == "9/9"
    assert task.info["Rotation Cycle"] == 2
    assert task.config == original and task._echo_rotation_step is None
    assert farm_setting(task, "Boss") == "Lorelei"


@pytest.mark.parametrize("scope,visits", [(WEEKLY, [(WEEKLY, 1), (WEEKLY, 1)]),
                                         (BOSS, [(BOSS, 1), (BOSS, 2)])])
def test_rotation_scope_and_rounds_without_echo_drop(scope, visits):
    task = task_for(2, 1, scope)
    task.info_set = task.info.__setitem__
    actual = []
    def single():
        actual.append((task._echo_rotation_step.category, task._echo_rotation_step.index))
        rotation_round_completed(task)  # A completed kill without an Echo drop still counts.
    run_rotation(task, single)
    assert actual == visits


@pytest.mark.parametrize("key,value", [("Rounds per Dungeon", 0), ("Rounds per Dungeon", -1),
                                      ("Repeat Farm Count", -1), ("Cycle Scope", "unknown"),
                                      ("Advanced Skill Material Mode", True)])
def test_invalid_or_resource_claiming_rotation_stops_before_teleport(key, value):
    task = task_for()
    task.config[key] = value
    single = Mock()
    with pytest.raises(ValueError):
        run_rotation(task, single)
    single.assert_not_called()


def test_failed_or_incomplete_round_does_not_advance_and_clears_runtime_overrides():
    task = task_for()
    task.info_set = task.info.__setitem__
    single = Mock()
    with pytest.raises(RuntimeError, match="0/2"):
        run_rotation(task, single)
    single.assert_called_once()
    assert task._echo_rotation_step is None
    with pytest.raises(InterruptedError):
        run_rotation(task, Mock(side_effect=InterruptedError("stopped")))
    assert task._echo_rotation_step is None


def test_starting_with_weekly_rotates_both_categories_and_wraps():
    task = task_for(7, 1)
    task.config['Cycle Start Category'] = WEEKLY
    task.info_set = task.info.__setitem__
    visits = []
    def single():
        visits.append((task._echo_rotation_step.category, task._echo_rotation_step.index))
        rotation_round_completed(task)
    original = deepcopy(task.config)
    run_rotation(task, single)
    assert visits == [(WEEKLY, 1), (BOSS, 1), (BOSS, 2), (WEEKLY, 1),
                      (BOSS, 1), (BOSS, 2), (WEEKLY, 1)]
    assert task.config == original


def test_invalid_start_category_is_rejected_before_input():
    task = task_for()
    task.config['Cycle Start Category'] = 'unknown'
    operation = Mock()
    with pytest.raises(ValueError, match='start category'):
        run_rotation(task, operation)
    operation.assert_not_called()


@pytest.mark.parametrize('scope,category,index,expected', [
    (ALL, BOSS, 2, [(BOSS, 2), (BOSS, 3), (WEEKLY, 1), (WEEKLY, 2), (BOSS, 1)]),
    (ALL, WEEKLY, 2, [(WEEKLY, 2), (BOSS, 1), (BOSS, 2), (BOSS, 3), (WEEKLY, 1)]),
    (BOSS, WEEKLY, 3, [(BOSS, 3), (BOSS, 1), (BOSS, 2)]),
    (WEEKLY, BOSS, 2, [(WEEKLY, 2), (WEEKLY, 1)]),
])
def test_selected_dungeon_starts_first_and_full_list_wraps(scope, category, index, expected):
    task = task_for(total=len(expected) + 1, per=1, scope=scope)
    task.total_boss_number, task.total_weekly_number = 3, 2
    task.config.update({'Cycle Start Category': category, 'Cycle Start Index': index})
    assert rotation_targets(task) == tuple(expected)
    task.info_set = task.info.__setitem__
    visits = []
    original = deepcopy(task.config)
    def single():
        visits.append((task._echo_rotation_step.category, task._echo_rotation_step.index))
        rotation_round_completed(task)
    run_rotation(task, single)
    assert visits == expected + expected[:1]
    assert task.config == original


@pytest.mark.parametrize('category,index', [(BOSS, 0), (BOSS, -1), (BOSS, 3),
                                           (WEEKLY, 2), (WEEKLY, True), (BOSS, 1.5)])
def test_invalid_start_number_never_sends_input(category, index):
    task = task_for()
    task.config.update({'Cycle Start Category': category, 'Cycle Start Index': index})
    operation = Mock()
    with pytest.raises(ValueError, match='起刷序号'):
        run_rotation(task, operation)
    operation.assert_not_called()


@pytest.mark.parametrize("name", ["FarmEchoTask", "FarmEchoTask2"])
def test_real_tasks_keep_single_mode_and_expose_cycle_metadata(name):
    from importlib import import_module
    cls = getattr(import_module("src.task." + name), name)
    task = cls(SimpleNamespace(scene=None, text_fix={}, global_config=SimpleNamespace(get_config=lambda name: {})), None)
    assert task.default_config["Farm Mode"] == SINGLE
    assert task.config_type["Rounds per Dungeon"]["min"] == 1
    for children in task.config_type["Teleport to Boss"]["sub_configs"].values():
        assert "Boss Level" not in children
    task.config = dict(task.default_config)
    task._do_run_single = Mock(return_value="single")
    assert task.do_run() == "single"
    task._do_run_single.assert_called_once()
