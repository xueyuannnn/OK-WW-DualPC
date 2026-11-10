"""Navigation paths that do not rely on the local PC clipboard or IME."""
from functools import wraps
import re
from .echo_rotation import farm_setting

START_CHALLENGE = re.compile(r"^(?:开启挑战|开始挑战|開啟挑戰|開始挑戰|Start\s*Challenge)$", re.IGNORECASE)
NO_ECHO_DROP = re.compile(r"无法掉落任何声骸|無法掉落任何聲骸|(?:no|cannot\s+drop)\s+(?:any\s+)?echo", re.IGNORECASE)
BOOK_TRANSITION_FEATURES = ["fast_travel_custom", "gray_teleport", "remove_custom", "team_close"]
EARLY_ACCESS_WARNING = re.compile(r"提前到[达達]目[标標]位置可能影[响響]剧情[体體][验驗].*是否[确確][认認]前往")


def handle_early_access_warning(task):
    if farm_setting(task, "Teleport to Boss") != "Weekly Challenge":
        return False
    texts = task.ocr(0.23, 0.40, 0.78, 0.57, threshold=0.8)
    text = re.sub(r"\s+", "", "".join(box.name for box in texts))
    # This is the story-preview notice on the explicitly selected weekly boss.
    # Do not treat arbitrary confirmation or resource dialogs as entry notices.
    if not EARLY_ACCESS_WARNING.search(text.replace("劇情", "剧情")):
        return False
    task.log_info("周本提前开放：确认前往当前配置的 Boss")
    task.click_dialog_right_button()
    return True


def install_book_early_access_hook():
    from src.task.BaseWWTask import BaseWWTask
    original = BaseWWTask.click_on_book_target
    if getattr(original, "_dualpc_early_access", False):
        return

    @wraps(original)
    def click_on_book_target(task, *args, **kwargs):
        if (not getattr(task.executor.interaction, "is_remote", False) or
                farm_setting(task, "Teleport to Boss") != "Weekly Challenge"):
            return original(task, *args, **kwargs)
        original_wait = task.wait_feature
        prior_override = task.__dict__.get("wait_feature")
        had_override = "wait_feature" in task.__dict__
        confirmed = [False]

        def wait_feature(features, *positional, **options):
            if (features != BOOK_TRANSITION_FEATURES or positional or
                    set(options) - {"time_out", "settle_time", "raise_if_not_found"}):
                return original_wait(features, *positional, **options)

            def ready():
                result = task.find_one(features)
                if result:
                    return result
                if not confirmed[0] and handle_early_access_warning(task):
                    confirmed[0] = True
                return None

            return task.wait_until(ready, **options)

        task.wait_feature = wait_feature
        try:
            return original(task, *args, **kwargs)
        finally:
            if had_override:
                task.wait_feature = prior_override
            else:
                del task.wait_feature

    click_on_book_target._dualpc_early_access = True
    BaseWWTask.click_on_book_target = click_on_book_target


def handle_low_level_warning(task):
    """Only acknowledge the no-Echo warning for a material-only run."""
    text = task.ocr(0.22, 0.40, 0.78, 0.57, threshold=0.8)
    if not any(NO_ECHO_DROP.search(box.name) for box in text):
        return False
    if task.config.get("Advanced Skill Material Mode", False):
        task.log_info("低等级周本：已确认本轮只领取材料，无法掉落声骸")
        task.click_dialog_right_button()
        return True
    from ok import TaskDisabledException
    task.log_error("当前挑战等级无法掉落声骸，请提高 Boss 等级后重新启动任务。已停止本轮。", notify=True)
    task.disable()
    raise TaskDisabledException()


def select_monster_by_name(task, name, max_pages=40):
    """Find the exact monster in the visible guide list without a remote IME."""
    task.click(0.13, 0.14, after_sleep=0.2)
    task.send_key_down("ctrl")
    try:
        task.send_key("a")
    finally:
        task.send_key_up("ctrl")
    task.send_key("backspace")
    task.click(0.354, 0.137, after_sleep=0.5)
    task.scroll_relative(0.25, 0.5, 80)
    task.sleep(0.6)
    normalize = lambda value: re.sub(r"\s+", "", value).casefold()
    expected = normalize(name)
    previous = None
    repeated = 0
    for page in range(max_pages):
        task.next_frame()
        boxes = task.ocr(0.10, 0.18, 0.365, 0.85, threshold=0.7)
        target = next((box for box in boxes if normalize(box.name) == expected), None)
        if target is not None:
            task.click_box(target, after_sleep=0.5)
            return target
        fingerprint = tuple((normalize(box.name), round(box.y / 8)) for box in boxes)
        repeated = repeated + 1 if fingerprint and fingerprint == previous else 0
        if repeated >= 2:
            break
        previous = fingerprint
        task.scroll_relative(0.25, 0.65, -4)
        task.sleep(0.45)
    raise RuntimeError(f"敌迹探寻列表中找不到 {name}，请确认目标已解锁及游戏语言")


def install_boss_navigation_hooks():
    from src.task.FarmEchoTask import FarmEchoTask
    from src.task.FarmEchoTask2 import FarmEchoTask2
    for cls in (FarmEchoTask, FarmEchoTask2):
        original = cls.teleport_to_nearest_boss
        if getattr(original, "_dualpc_monster_list", False):
            continue

        @wraps(original)
        def teleport_to_nearest_boss(task, _original=original):
            if (not getattr(task.executor.interaction, "is_remote", False)
                    or task.aim_boss is None):
                return _original(task)
            task.log_info(f"双机 Boss 定位：从敌迹列表选择 {task.aim_boss}")
            task.ensure_main(time_out=180)
            task.openF2Book("gray_book_all_monsters")
            select_monster_by_name(task, task.aim_boss)
            task.click(0.89, 0.92, after_sleep=1)
            task.click(0.89, 0.92)
            task.wait_in_team_and_world(time_out=30, raise_if_not_found=True)

        teleport_to_nearest_boss._dualpc_monster_list = True
        cls.teleport_to_nearest_boss = teleport_to_nearest_boss


def install_team_challenge_hook():
    from src.task.BaseWWTask import BaseWWTask
    original = BaseWWTask.click_team_challenge
    if getattr(original, "_dualpc_challenge_text", False):
        return

    @wraps(original)
    def click_team_challenge(task):
        if not getattr(task.executor.interaction, "is_remote", False):
            return original(task)

        def ready():
            button = task.find_one("team_start_challenge")
            if button:
                return button
            # Physical pointer hover changes the button background. Require
            # its exact label within the known start-button area as fallback.
            texts = task.ocr(box=task.box_of_screen(0.64, 0.82, 0.99, 0.98),
                             match=START_CHALLENGE, threshold=0.8)
            button = next((box for box in texts if START_CHALLENGE.fullmatch(box.name.strip())), None)
            if button:
                return button
            handle_low_level_warning(task)
            return None

        button = task.wait_until(ready, time_out=10, settle_time=0.2, raise_if_not_found=True)
        task.sleep(0.5)
        task.click_box(button, after_sleep=1)
        task.wait_click_skip_dialog_confirm()

    click_team_challenge._dualpc_challenge_text = True
    BaseWWTask.click_team_challenge = click_team_challenge


def install_navigation_hooks():
    install_team_challenge_hook()
    install_book_early_access_hook()
    install_boss_navigation_hooks()
    from src.task.BaseCombatTask import BaseCombatTask
    original_map_heal = BaseCombatTask.teleport_to_heal
    if not getattr(original_map_heal, "_dualpc_fixed_recovery", False):
        @wraps(original_map_heal)
        def teleport_to_heal(task):
            if getattr(task.executor.interaction, "is_remote", False):
                return task.revive_at_tower_and_heal()
            return original_map_heal(task)
        teleport_to_heal._dualpc_fixed_recovery = True
        BaseCombatTask.teleport_to_heal = teleport_to_heal
    original = BaseCombatTask.revive_at_tower_and_heal
    if getattr(original, "_dualpc_map_recovery", False):
        return

    @wraps(original)
    def revive_at_tower_and_heal(task):
        if not getattr(task.executor.interaction, "is_remote", False):
            return original(task)
        # Use the upstream recovery region near Crownless. A nearest beacon
        # at an arbitrary boss may only teleport, without reviving the party.
        task.log_info("双机恢复：从敌迹列表定位回血区域")
        task.ensure_main(time_out=120)
        task.openF2Book("gray_book_all_monsters")
        select_monster_by_name(task, task.get_revive_search_boss_name())
        task.click(0.89, 0.92, after_sleep=1)
        task.wait_until(lambda: task.find_best_match_in_box(
            task.box_of_screen(0.1, 0.1, 0.9, 0.9), ["map_way_point", "map_way_point_big"], 0.6),
            time_out=6, raise_if_not_found=True)
        task._travel_to_nearest_waypoint()
        task.sleep(5)

    revive_at_tower_and_heal._dualpc_map_recovery = True
    BaseCombatTask.revive_at_tower_and_heal = revive_at_tower_and_heal
