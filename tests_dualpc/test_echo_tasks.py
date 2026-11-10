from importlib import import_module
from types import SimpleNamespace

from dualpc.echo_tasks import install_echo_task_hooks, install_boss_combat_hook, install_main_stat_region_hook
from src.task.ChangeEchoTask import ChangeEchoTask


def test_live_health_bar_prevents_echo_model_false_end_and_restores_condition(monkeypatch):
    from unittest.mock import Mock
    from src.task.FarmEchoTask import FarmEchoTask
    from src.task.FarmEchoTask2 import FarmEchoTask2
    model = Mock(return_value=True)
    def combat(task, **kwargs):
        assert not task.combat_end_condition()  # Model thinks an Echo is present, but boss is alive.
        model.assert_not_called()
        task.has_health_bar = lambda: False
        assert task.combat_end_condition()
        return "finished"
    monkeypatch.setattr(FarmEchoTask, "combat_once", combat)
    monkeypatch.setattr(FarmEchoTask2, "combat_once", combat)
    install_boss_combat_hook()
    task = SimpleNamespace(executor=SimpleNamespace(interaction=SimpleNamespace(is_remote=True)),
                           combat_end_condition=model, has_health_bar=lambda: True)
    assert FarmEchoTask2.combat_once(task) == "finished"
    assert task.combat_end_condition is model


def test_main_stat_crop_includes_complete_label_without_changing_other_ocr_or_local_mode(monkeypatch):
    from unittest.mock import Mock
    observed = Mock()
    observed._dualpc_main_stat_region = False
    monkeypatch.setattr(ChangeEchoTask, "wait_ocr", observed)
    install_main_stat_region_hook()
    installed = ChangeEchoTask.wait_ocr
    install_main_stat_region_hook()
    assert ChangeEchoTask.wait_ocr is installed
    task = SimpleNamespace(executor=SimpleNamespace(interaction=SimpleNamespace(is_remote=True)))
    installed(task, 0.09, 0.20, 0.15, 0.26, time_out=2)
    observed.assert_called_with(task, 0.10, 0.23, 0.35, 0.275, time_out=2)
    installed(task, match="声骸强化")
    observed.assert_called_with(task, match="声骸强化")
    task.executor.interaction.is_remote = False
    installed(task, 0.09, 0.20, 0.15, 0.26)
    observed.assert_called_with(task, 0.09, 0.20, 0.15, 0.26)


def test_enhancement_start_preserves_existing_diagnostic_images(monkeypatch, tmp_path):
    monkeypatch.chdir(tmp_path)
    folder = tmp_path / "screenshots"
    folder.mkdir()
    prior = folder / "unrelated-diagnostic.png"
    prior.write_bytes(b"existing capture")
    module = import_module("src.task.EnhanceEchoTask")
    monkeypatch.setattr(module, "clear_folder", module.clear_folder)
    monkeypatch.setattr(ChangeEchoTask, "run", ChangeEchoTask.run)
    install_echo_task_hooks()
    module.clear_folder("screenshots")
    assert prior.read_bytes() == b"existing capture"


def test_main_stat_task_finishes_when_no_level_zero_echo_is_available(monkeypatch):
    module = import_module("src.task.EnhanceEchoTask")
    monkeypatch.setattr(module, "clear_folder", module.clear_folder)
    # Exercise the actual original task's empty-selection branch, where adding
    # the formerly missing counter to the success count used to raise TypeError.
    original = getattr(ChangeEchoTask.run, "__wrapped__", ChangeEchoTask.run)
    monkeypatch.setattr(ChangeEchoTask, "run", original)
    install_echo_task_hooks()
    installed = ChangeEchoTask.run
    install_echo_task_hooks()
    assert ChangeEchoTask.run is installed
    info = {"失败声骸数量": 7}
    messages = []
    task = SimpleNamespace(info_set=info.__setitem__, info_get=info.get,
                           find_echo_enhance=lambda: True, is_0_level=lambda: False,
                           debug=False, log_info=lambda message, **kwargs: messages.append(message))
    assert installed(task) is None
    assert info == {"失败声骸数量": 0, "成功声骸数量": 0}
    assert "任务结束" in messages[0]


def test_hardware_boss_round_requires_a_confirmed_combat_start(monkeypatch):
    from src.task.FarmEchoTask import FarmEchoTask
    from src.task.FarmEchoTask2 import FarmEchoTask2
    calls = []
    def wait_for_spawn(task, **kwargs):
        calls.append(kwargs)
        if kwargs["raise_if_not_found"]:
            raise RuntimeError("boss did not spawn")
        return False
    monkeypatch.setattr(FarmEchoTask, "combat_once", wait_for_spawn)
    monkeypatch.setattr(FarmEchoTask2, "combat_once", wait_for_spawn)
    install_boss_combat_hook()
    task = SimpleNamespace(executor=SimpleNamespace(interaction=SimpleNamespace(is_remote=True)))
    import pytest
    for cls in (FarmEchoTask, FarmEchoTask2):
        installed = cls.combat_once
        install_boss_combat_hook()
        assert cls.combat_once is installed
        with pytest.raises(RuntimeError, match="did not spawn"):
            installed(task, wait_combat_time=5, raise_if_not_found=False)
    assert all(call["wait_combat_time"] == 20 for call in calls)
    task.executor.interaction.is_remote = False
    assert FarmEchoTask.combat_once(task, wait_combat_time=5, raise_if_not_found=False) is False
    assert calls[-1]["wait_combat_time"] == 5
