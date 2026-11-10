"""Recognize captured death dialogs before reporting a combat round complete."""
from functools import wraps
import re
import time

from ok.task.exceptions import TaskDisabledException

DEATH_TITLE = re.compile(r"^(?:选择复苏物品|選擇復甦物品|失去意识|失去意識|Select\s+Revival\s+Item|Unconscious)$", re.IGNORECASE)
DEATH_BUTTON = re.compile(r"^(?:复苏|復甦|确认|確認|Revive|Confirm)$", re.IGNORECASE)


def has_death_dialog(task):
    titles = task.ocr(0.15, 0.14, 0.85, 0.40, match=DEATH_TITLE, threshold=0.8)
    if not any(DEATH_TITLE.fullmatch(box.name.strip()) for box in titles):
        return False
    buttons = task.ocr(0.35, 0.65, 0.82, 0.93, match=DEATH_BUTTON, threshold=0.8)
    return any(DEATH_BUTTON.fullmatch(box.name.strip()) for box in buttons)


def install_combat_recovery_hook():
    from src.task.BaseCombatTask import BaseCombatTask, CharDeadException, CharRevivedException
    install_revive_popup_close_hook()
    install_combat_dialog_watchdog()
    original = BaseCombatTask.combat_once
    if getattr(original, "_dualpc_death_dialog", False):
        return

    def recover(task):
        task.log_info("双机战斗：识别到死亡弹窗，恢复后重新执行本轮")
        if hasattr(task, "reset_to_false"):
            task.reset_to_false(reason="复苏弹窗中断战斗")
        if task.revive_action():
            raise CharRevivedException("死亡后已恢复，本轮未完成")
        raise CharDeadException("死亡后恢复失败，本轮未完成")

    @wraps(original)
    def combat_once(task, *args, **kwargs):
        if not getattr(task.executor.interaction, "is_remote", False):
            return original(task, *args, **kwargs)
        if has_death_dialog(task):
            recover(task)
        previous_scope = getattr(task, "_dualpc_combat_scope", False)
        previous_skip = getattr(task, "skip_combat_check", False)
        task._dualpc_combat_scope = True
        try:
            try:
                result = original(task, *args, **kwargs)
            finally:
                task._dualpc_combat_scope = previous_scope
                task.skip_combat_check = previous_skip
        except (TaskDisabledException, CharRevivedException):
            raise
        except Exception:
            if has_death_dialog(task):
                recover(task)
            raise
        if has_death_dialog(task):
            recover(task)
        return result

    combat_once._dualpc_death_dialog = True
    BaseCombatTask.combat_once = combat_once


def install_combat_dialog_watchdog():
    """Check dialogs inside character waits, even if combat checks are skipped.

    Imported rotations can retry a dead slot indefinitely. The executor calls
    sleep_check during those waits; unwind to combat_once before doing recovery.
    """
    from src.task.BaseCombatTask import BaseCombatTask, CharDeadException
    original = BaseCombatTask.sleep_check
    if getattr(original, "_dualpc_dialog_watchdog", False):
        return

    @wraps(original)
    def sleep_check(task):
        if (getattr(task.executor.interaction, "is_remote", False)
                and getattr(task, "_dualpc_combat_scope", False)
                and not getattr(task, "_dualpc_dialog_checking", False)):
            now = time.monotonic()
            if now - getattr(task, "_dualpc_dialog_checked_at", -float("inf")) >= 1:
                task._dualpc_dialog_checked_at = now
                task._dualpc_dialog_checking = True
                try:
                    if has_death_dialog(task):
                        raise CharDeadException("复苏弹窗中断角色切换循环")
                finally:
                    task._dualpc_dialog_checking = False
        return original(task)

    sleep_check._dualpc_dialog_watchdog = True
    BaseCombatTask.sleep_check = sleep_check


def install_revive_popup_close_hook():
    from src.task.BaseCombatTask import BaseCombatTask
    original = BaseCombatTask.close_revive_popup
    if getattr(original, "_dualpc_verified_close", False):
        return

    @wraps(original)
    def close_revive_popup(task):
        if not getattr(task.executor.interaction, "is_remote", False):
            return original(task)
        if not has_death_dialog(task):
            return False
        task.send_key("esc", after_sleep=.5)
        if task.wait_until(lambda: not has_death_dialog(task), time_out=2, raise_if_not_found=False):
            return True
        # This is the close icon of the confirmed 16:9 revival-item dialog.
        # Never click its bottom-right Confirm button: that consumes an item.
        task.click_relative(.803, .196, after_sleep=.5)
        if not task.wait_until(lambda: not has_death_dialog(task), time_out=2, raise_if_not_found=False):
            raise RuntimeError("复苏弹窗未关闭，已停止恢复，避免误用复苏道具")
        return True

    close_revive_popup._dualpc_verified_close = True
    BaseCombatTask.close_revive_popup = close_revive_popup


def recover_starting_popup(task):
    """A user can restart the farm while its previous death popup is still open."""
    if not getattr(task.executor.interaction, "is_remote", False) or not has_death_dialog(task):
        return
    task.log_info("开始刷取前关闭遗留复苏弹窗并免费回血")
    task.reset_to_false(reason="启动时存在复苏弹窗")
    task.close_revive_popup()
    if task.in_realm():
        from src.task.DomainTask import DomainTask
        if not DomainTask.revive_action(task):
            raise RuntimeError("启动前退出副本恢复失败")
    else:
        task.revive_at_tower_and_heal()
