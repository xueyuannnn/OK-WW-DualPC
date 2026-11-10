from pathlib import Path
import threading

from dualpc.bootstrap import make_config
from dualpc.compat import PATCH_MARKER, apply_game_patch, check_contract
from dualpc.settings import Settings

ROOT = Path(__file__).absolute().parents[1]


def test_pinned_upstream_contract_and_idempotent_patch():
    assert check_contract(ROOT)["enhanced_tasks"] == "present"
    assert apply_game_patch(ROOT) is False
    assert PATCH_MARKER in (ROOT / "src/combat/CombatCheck.py").read_text(encoding="utf-8")


def test_enhanced_tasks_preserved_and_local_targets_removed():
    from config import config as original
    before = list(original["onetime_tasks"])
    cfg = make_config(Settings(), ROOT)
    names = {x[1] for x in cfg["onetime_tasks"]}
    assert {"DailyTask2", "TacetTask2", "ForgeryTask2", "SimulationTask2", "FarmEchoTask2"} <= names
    assert "MultiAccountDailyTask" not in names
    assert original["onetime_tasks"] == before
    assert "git_update" not in cfg and "update_pyappify" not in cfg
    assert cfg["windows"]["interaction"] == ["HardwareHID"]


def test_device_manager_never_constructs_a_local_game_window(monkeypatch, tmp_path):
    from ok.util.config import Config
    from ok.device import DeviceManager as module
    from dualpc.device import DualPCDeviceManager

    def forbidden(*args, **kwargs):
        raise AssertionError("Native game-window lookup attempted")
    monkeypatch.setattr(module, "HwndWindow", forbidden)
    monkeypatch.setattr(Config, "config_folder", str(tmp_path))
    from ok.util.handler import ExitEvent
    event = ExitEvent()
    manager = DualPCDeviceManager(make_config(Settings(), ROOT), event)
    try:
        assert manager.get_preferred_device()["imei"] == "dualpc"
        assert manager.hwnd_window.hwnd == 0
        assert manager.hwnd_window.hwnd_title == "鸣潮"
        assert not manager.device_connected()
        assert manager.get_exe_path(manager.get_preferred_device()) is None
    finally:
        event.set()
        manager.close()


def test_remote_mouse_reset_placeholder_never_polls_or_moves_local_cursor(monkeypatch):
    import win32api
    from types import SimpleNamespace
    from dualpc.task_hooks import RemoteMouseResetTask
    from src.task.MouseResetTask import MouseResetTask

    def forbidden(*args):
        raise AssertionError("Dual-PC placeholder touched the local cursor")

    monkeypatch.setattr(win32api, "GetCursorPos", forbidden)
    monkeypatch.setattr(win32api, "SetCursorPos", forbidden)
    task = RemoteMouseResetTask(SimpleNamespace(scene=None), None)
    assert isinstance(task, MouseResetTask)
    assert task.default_config["_enabled"] is False
    assert "无需开启" in task.description
    assert task.run() is None and task.mouse_reset() is None
