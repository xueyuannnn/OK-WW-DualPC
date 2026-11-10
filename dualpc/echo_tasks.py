"""Small task repairs kept with the dual-PC adapter across upstream updates."""
from functools import wraps
from importlib import import_module
from pathlib import Path


def preserve_screenshots(folder):
    """Keep previous diagnostic images when starting an Echo enhancement run."""
    Path(folder).mkdir(parents=True, exist_ok=True)


def install_echo_task_hooks():
    install_main_stat_region_hook()
    enhance = import_module("src.task.EnhanceEchoTask")
    # This imported helper is only used at the beginning of run(). Clearing
    # the shared screenshots directory destroys unrelated diagnostic evidence.
    enhance.clear_folder = preserve_screenshots

    from src.task.ChangeEchoTask import ChangeEchoTask
    original = ChangeEchoTask.run
    if getattr(original, "_dualpc_echo_counters", False):
        return

    @wraps(original)
    def run(task):
        task.info_set("失败声骸数量", 0)
        return original(task)

    run._dualpc_echo_counters = True
    ChangeEchoTask.run = run


def install_main_stat_region_hook():
    """Read the complete main-stat label in the current cultivation layout."""
    from src.task.ChangeEchoTask import ChangeEchoTask
    original = ChangeEchoTask.wait_ocr
    if getattr(original, "_dualpc_main_stat_region", False):
        return

    @wraps(original)
    def wait_ocr(task, *args, **kwargs):
        if (getattr(task.executor.interaction, "is_remote", False)
                and tuple(args[:4]) == (0.09, 0.20, 0.15, 0.26)):
            # The old crop cuts through the first character of the label.
            # Stay on the first row, excluding the level and second main stat.
            args = (0.10, 0.23, 0.35, 0.275) + args[4:]
        return original(task, *args, **kwargs)

    wait_ocr._dualpc_main_stat_region = True
    ChangeEchoTask.wait_ocr = wait_ocr


def install_boss_combat_hook():
    """Do not finish a requested farm round before its boss has spawned."""
    from src.task.FarmEchoTask import FarmEchoTask
    from src.task.FarmEchoTask2 import FarmEchoTask2
    for cls in (FarmEchoTask, FarmEchoTask2):
        original = cls.combat_once
        if getattr(original, "_dualpc_boss_start", False):
            continue

        @wraps(original)
        def combat_once(task, wait_combat_time=200, raise_if_not_found=True, target=False,
                        _original=original):
            remote = getattr(task.executor.interaction, "is_remote", False)
            if remote and 0 < wait_combat_time < 20:
                wait_combat_time = 20
                raise_if_not_found = True
            end_condition = getattr(task, "combat_end_condition", None)
            if remote and end_condition is not None:
                # New boss effects can resemble an Echo to the older model.
                # A verified live enemy health bar takes precedence over YOLO.
                task.combat_end_condition = lambda: not task.has_health_bar() and end_condition()
            try:
                return _original(task, wait_combat_time=wait_combat_time,
                                 raise_if_not_found=raise_if_not_found, target=target)
            finally:
                if remote and end_condition is not None:
                    task.combat_end_condition = end_condition

        combat_once._dualpc_boss_start = True
        cls.combat_once = combat_once
