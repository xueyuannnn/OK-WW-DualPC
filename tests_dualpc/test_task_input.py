"""Exercise the real upstream task click dispatch against the remote adapter."""
from types import SimpleNamespace

import pytest

from ok import Box
from ok.task.task import ExecutorOperation
from dualpc.task_hooks import install_task_input_hook, install_macro_mouse_hook
from src.task.BaseWWTask import BaseWWTask


@pytest.fixture
def task_clicks(monkeypatch):
    install_task_input_hook()
    recorded = []
    # Observe the input contract at the shared framework click boundary.
    def click(self, x, y, *args, **kwargs):
        recorded.append((x, y, kwargs))
    monkeypatch.setattr(ExecutorOperation, "click", click)
    task = BaseWWTask.__new__(BaseWWTask)
    task._executor = SimpleNamespace(interaction=SimpleNamespace(is_remote=True))
    task.width_of_screen = lambda fraction: 1920 * fraction
    task.height_of_screen = lambda fraction: 1080 * fraction
    return task, recorded


@pytest.mark.parametrize("target", [(1824, 510), (0.9, 0.94), (Box(1600, 480, 240, 60), -1)])
def test_explicit_menu_coordinates_move_hardware_even_with_upstream_move_false(task_clicks, target):
    task, recorded = task_clicks
    BaseWWTask.click(task, *target, move=False)
    assert recorded[-1][2]["move"] is True


@pytest.mark.parametrize("key", ["left", "right", "middle"])
def test_bare_combat_click_does_not_rehome_the_pointer(task_clicks, key):
    task, recorded = task_clicks
    BaseWWTask.click(task, key=key)
    assert recorded[-1] == (960, 540, {"move": False, "down_time": 0.01, "after_sleep": 0, "key": key})


def test_target_lock_middle_click_stays_in_place(task_clicks):
    task, recorded = task_clicks
    BaseWWTask.click(task, 0.5, 0.5, key="middle")
    assert recorded[-1][2]["move"] is False


def test_local_background_click_semantics_are_preserved(task_clicks):
    task, recorded = task_clicks
    task.executor.interaction.is_remote = False
    BaseWWTask.click(task, 1824, 510)
    assert recorded[-1][2]["move"] is False


@pytest.mark.parametrize('box,relative_x,expected', [
    (Box(1600, 480, 240, 60), .5, (1720, 510)),
    (Box(1919, 1079, 1, 1), .5, (1919, 1079)),
    (Box(1200, 700, 100, 40), 1.5, (1350, 720)),
])
def test_remote_box_click_uses_exact_center_and_preserves_intentional_label_offsets(task_clicks, monkeypatch, box, relative_x, expected):
    task, recorded = task_clicks
    observed = []
    monkeypatch.setattr(task, 'click', lambda x, y, **kwargs: observed.append((x, y)))
    task.click_box(box, relative_x=relative_x)
    assert observed == [expected]


@pytest.fixture
def macro_holds(monkeypatch):
    recorded = []
    def mouse_down(task, x, y, **kwargs):
        recorded.append((x, y, kwargs))
    monkeypatch.setattr(BaseWWTask, 'mouse_down', mouse_down)
    install_macro_mouse_hook()
    task = BaseWWTask.__new__(BaseWWTask)
    task._executor = SimpleNamespace(interaction=SimpleNamespace(is_remote=True))
    task._in_combat, task.skip_combat_check = True, True
    return task, recorded


@pytest.mark.parametrize('size', [(1280, 720), (1920, 1080), (2560, 1440)])
@pytest.mark.parametrize('key', ['left', 'right'])
def test_imported_macro_center_holds_do_not_position_hardware(macro_holds, size, key):
    task, recorded = macro_holds
    task.width_of_screen = lambda value: size[0] * value
    task.height_of_screen = lambda value: size[1] * value
    task.mouse_down(size[0] / 2, size[1] / 2, key=key)
    assert recorded == [(-1, -1, {'name': None, 'key': key})]


@pytest.mark.parametrize('case', ['local', 'menu', 'named_target', 'off_center', 'outside_macro', 'bare'])
def test_macro_fix_preserves_other_mouse_operations(macro_holds, case):
    task, recorded = macro_holds
    task.width_of_screen = lambda value: 1920 * value
    task.height_of_screen = lambda value: 1080 * value
    x, y, name = 960, 540, None
    if case == 'local': task.executor.interaction.is_remote = False
    elif case == 'menu': task._in_combat = False
    elif case == 'named_target': name = 'confirm'
    elif case == 'off_center': x, y = 1700, 950
    elif case == 'outside_macro': task.skip_combat_check = False
    elif case == 'bare': x = y = -1
    task.mouse_down(x, y, name=name)
    assert recorded == [(x, y, {'name': name, 'key': 'left'})]
