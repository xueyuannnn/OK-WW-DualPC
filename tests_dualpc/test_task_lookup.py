from types import SimpleNamespace

import pytest

from dualpc.task_hooks import install_task_lookup_hook, RemoteMouseResetTask
from ok.task.TaskExecutor import TaskExecutor
from src.task.MouseResetTask import MouseResetTask
from src.task.SimulationTask import SimulationTask
from src.task.SimulationTask2 import SimulationTask2
from src.task.TacetTask import TacetTask
from src.task.TacetTask2 import TacetTask2
from src.task.ForgeryTask import ForgeryTask
from src.task.ForgeryTask2 import ForgeryTask2


@pytest.fixture
def lookup(monkeypatch):
    # Restore the installed method after each test, even in a shared Qt suite.
    monkeypatch.setattr(TaskExecutor, "get_task_by_class", TaskExecutor.get_task_by_class)
    install_task_lookup_hook()
    installed = TaskExecutor.get_task_by_class
    install_task_lookup_hook()
    assert TaskExecutor.get_task_by_class is installed
    return installed


@pytest.mark.parametrize("base,enhanced", [(SimulationTask, SimulationTask2),
                                           (TacetTask, TacetTask2), (ForgeryTask, ForgeryTask2)])
def test_original_daily_resolves_exact_task_even_when_enhanced_is_first(lookup, base, enhanced):
    original, newer = base.__new__(base), enhanced.__new__(enhanced)
    executor = SimpleNamespace(interaction=SimpleNamespace(is_remote=True),
                               onetime_tasks=[newer, original], trigger_tasks=[])
    executor.get_all_tasks = lambda: executor.onetime_tasks + executor.trigger_tasks
    assert lookup(executor, base) is original
    assert lookup(executor, enhanced) is newer


def test_replacement_mouse_reset_and_native_lookup_keep_subclass_behavior(lookup):
    reset = RemoteMouseResetTask.__new__(RemoteMouseResetTask)
    executor = SimpleNamespace(interaction=SimpleNamespace(is_remote=True),
                               onetime_tasks=[], trigger_tasks=[reset])
    executor.get_all_tasks = lambda: executor.onetime_tasks + executor.trigger_tasks
    assert lookup(executor, MouseResetTask) is reset
    original = SimulationTask.__new__(SimulationTask)
    newer = SimulationTask2.__new__(SimulationTask2)
    executor.onetime_tasks = [newer, original]
    executor.interaction.is_remote = False
    assert lookup(executor, SimulationTask) is newer
