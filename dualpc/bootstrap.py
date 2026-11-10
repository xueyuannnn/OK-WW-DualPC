from copy import deepcopy
from pathlib import Path

from .settings import Settings


def make_config(settings: Settings, root: Path):
    from config import config as upstream
    config = dict(upstream)
    config.update({"gui_title": "OK-WW-E 双机 · 外部采集 + 硬件键鼠", "version": "dualpc-0.1.0",
                   "dualpc_settings": settings, "dualpc_settings_path": str(root / "dualpc.json"),
                   "config_folder": str(root / "configs-dualpc"), "blur_area": None,
                   "check_mutex": False, "analytics": False,
                   "windows": {"start_exe": True, "capture_method": ["ExternalCapture"],
                               "interaction": ["HardwareHID"]}})
    # Updates are prepared as independent candidates; an upstream auto-updater
    # must not overwrite this adapter or its pinned framework.
    for key in ("git_update", "update_pyappify", "adb", "browser"):
        config.pop(key, None)
    config["onetime_tasks"] = [list(task) for task in upstream["onetime_tasks"]
                               if task[1] != "MultiAccountDailyTask"]
    config["trigger_tasks"] = [["dualpc.task_hooks", "RemoteMouseResetTask"] if task[1] == "MouseResetTask" else list(task)
                               for task in upstream["trigger_tasks"]]
    config["custom_tabs"] = [["dualpc.ui", "DualPCTab"]] + deepcopy(upstream.get("custom_tabs", []))
    config["global_configs"] = list(upstream["global_configs"])
    from .continuation import recovery_option
    config["global_configs"].append(recovery_option())
    # These local-window launch options do not control the game PC.
    basic = deepcopy(next(option for option in config["global_configs"] if option.name == "Basic Options"))
    basic.config_type["Auto Start Game When App Starts"] = {"hidden": True}
    config["global_configs"] = [basic if option.name == "Basic Options" else option for option in config["global_configs"]]
    # These options concern processes/windows on the local PC.
    config["supported_resolution"] = dict(upstream["supported_resolution"], force_ratio=True)
    return config


def create_runtime(settings, root):
    from ok import OK, og
    from .device import DualPCDeviceManager
    from .schedule import install_scheduler_hook
    from .combat import install_combat_hook
    from .config_ui import install_config_ui
    from .task_hooks import install_task_input_hook, install_task_lookup_hook
    from .navigation import install_navigation_hooks
    from .stamina import install_stamina_hook
    from .echo_tasks import install_echo_task_hooks, install_boss_combat_hook
    from .update_ui import install_about_update_hook
    from .recovery import install_combat_recovery_hook
    from .garden import install_garden_hook
    from .dialogue import install_dialogue_hook
    install_scheduler_hook()
    install_combat_hook()
    install_config_ui()
    install_task_input_hook()
    install_task_lookup_hook()
    install_navigation_hooks()
    install_stamina_hook()
    install_echo_task_hooks()
    install_combat_recovery_hook()
    install_boss_combat_hook()
    install_about_update_hook()
    install_garden_hook()
    install_dialogue_hook()
    from .continuation import install_continuation_hooks
    install_continuation_hooks()
    from .workshop_compat import install_workshop_compat
    install_workshop_compat()
    from .login import install_login_hook
    install_login_hook()
    from .nightmare import install_nightmare_hook
    install_nightmare_hook()

    class DualPCOK(OK):
        def init_device_manager(self):
            if self.device_manager is None:
                self.device_manager = DualPCDeviceManager(self.config, self.exit_event, self.global_config)
                og.device_manager = self.device_manager

    return DualPCOK(make_config(settings, root))
