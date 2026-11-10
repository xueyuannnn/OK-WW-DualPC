"""Small explicit compatibility contract, checked before upgrading or starting."""
import ast
from pathlib import Path

PATCH_MARKER = "# dualpc: USB HID input does not need the local PostMessage cursor workaround"
GARDEN_MARKER = "# dualpc: keep active Garden progress when resuming"


def apply_game_patch(root):
    changed = _apply_cursor_patch(root)
    path = Path(root) / "src/task/GardenTask.py"
    text = path.read_text(encoding="utf-8")
    if GARDEN_MARKER not in text:
        before = "        self.click(0.246, 0.486, after_sleep=1)\n        while True:"
        after = ("        self.click(0.246, 0.486, after_sleep=1)\n        self.run_garden_loop()\n\n"
                 "    def run_garden_loop(self):\n        " + GARDEN_MARKER + "\n        while True:")
        if text.count(before) != 1:
            raise RuntimeError("Upstream GardenTask changed: review the Garden resume patch before upgrading")
        text = text.replace(before, after, 1)
        ast.parse(text)
        path.write_text(text, encoding="utf-8", newline="\n")
        changed = True
    return changed


def _apply_cursor_patch(root):
    path = Path(root) / "src/combat/CombatCheck.py"
    text = path.read_text(encoding="utf-8")
    if PATCH_MARKER in text:
        return False
    replacements = [
        ("        old = win32api.GetCursorPos()", f"        {PATCH_MARKER}\n        remote_input = getattr(self.executor.interaction, 'is_remote', False)\n        old = None if remote_input else win32api.GetCursorPos()"),
        ("        abs_pos = self.executor.interaction.capture.get_abs_cords(levitator.x, levitator.y)\n        win32api.SetCursorPos(abs_pos)",
         "        if not remote_input:\n            abs_pos = self.executor.interaction.capture.get_abs_cords(levitator.x, levitator.y)\n            win32api.SetCursorPos(abs_pos)"),
        ("        win32api.SetCursorPos(old)", "        if not remote_input:\n            win32api.SetCursorPos(old)"),
    ]
    for before, after in replacements:
        if text.count(before) != 1:
            raise RuntimeError("Upstream CombatCheck changed: review the local-cursor compatibility patch before upgrading")
        text = text.replace(before, after, 1)
    ast.parse(text)
    path.write_text(text, encoding="utf-8", newline="\n")
    return True


def check_contract(root):
    root = Path(root)
    framework = root / "vendor/ok-script"
    checks = {
        framework / "ok/__init__.py": {"OK": {"init_device_manager", "do_init", "start"}},
        framework / "ok/device/DeviceManager.py": {"DeviceManager": {"get_devices", "get_preferred_device", "refresh", "set_preferred_device"}},
        framework / "ok/device/interaction_methods/base.py": {"BaseInteraction": {"send_key", "click", "on_run", "on_destroy"}},
        framework / "ok/device/capture_methods/base.py": {"BaseCaptureMethod": {"get_frame", "do_get_frame", "connected"}},
        framework / "ok/task/TaskExecutor.py": {"TaskExecutor": {"check_frame_and_resolution", "get_task_by_class", "get_all_tasks", "pause", "start"}},
        framework / "ok/util/windows_schedule.py": {"WindowsScheduleManager": {"_generate_task_xml"}},
        root / "src/task/GardenTask.py": {"GardenTask": {"run", "run_garden_loop"}},
    }
    errors = []
    for path, classes in checks.items():
        try:
            tree = ast.parse(path.read_text(encoding="utf-8"))
            for name, required in classes.items():
                found = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == name)
                names = {n.name for n in found.body if isinstance(n, ast.FunctionDef)}
                if required - names:
                    errors.append(f"{name}: missing methods {sorted(required - names)}")
        except (OSError, SyntaxError, StopIteration) as error:
            errors.append(f"{path.relative_to(root)}: {error or 'class not found'}")
    for relative in ("config.py", "assets/coco_annotations.json", "src/task/DailyTask2.py",
                     "src/task/TacetTask2.py", "src/task/MouseResetTask.py", "src/combat/CombatCheck.py"):
        if not (root / relative).is_file():
            errors.append(f"Required enhanced-version resource is missing: {relative}")
    # New native input call sites deserve review rather than silently falling
    # through to controlling the inference PC after a future upstream update.
    allowed = {"src/combat/CombatCheck.py", "src/task/MouseResetTask.py", "src/task/MultiAccountDailyTask.py"}
    for path in (root / "src").rglob("*.py"):
        source = path.read_text(encoding="utf-8-sig")
        relative = path.relative_to(root).as_posix()
        if any(token in source for token in ("SetCursorPos(", "mouse_event(", "keybd_event(", "SendInput(", "PostMessage(")) and relative not in allowed:
            errors.append(f"Review new native input usage: {relative}")
    if errors:
        raise RuntimeError("Dual-PC compatibility check failed:\n" + "\n".join(errors))
    return {"framework_interfaces": "passed", "enhanced_tasks": "present", "native_input_scan": "passed"}
