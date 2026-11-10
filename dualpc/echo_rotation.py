"""Echo-only dungeon rotation; saved single-boss configuration stays intact."""
from dataclasses import dataclass


SINGLE = "Single Boss"
CYCLE = "Cycle All Dungeons"
ALL = "All Challenges"
WEEKLY = "Weekly Challenge"
BOSS = "Boss Challenge"


def configure_rotation(task):
    # Place the mode before its child settings without changing existing keys.
    task.default_config = {"Farm Mode": SINGLE, **task.default_config,
                           "Cycle Scope": ALL, "Cycle Start Category": BOSS,
                           "Cycle Start Index": 1, "Rounds per Dungeon": 1}
    task.config_description.update({
        "Farm Mode": "Choose one boss or rotate through the guidebook dungeons.",
        "Cycle Scope": "Follow the guidebook order, starting with the selected category when cycling all challenges.",
        "Cycle Start Category": "Choose which category to visit first. Both categories still run in All Challenges mode.",
        "Cycle Start Index": "Number from the top of the starting category, beginning at 1. Continue through the remaining dungeons, then wrap around without omitting earlier entries.",
        "Rounds per Dungeon": "Complete this many combat rounds before switching. No Echo drop still counts as one round.",
        "Repeat Farm Count": "Total combat rounds across all dungeons. The final dungeon may run fewer rounds.",
    })
    task.config_type["Farm Mode"] = {
        "type": "drop_down", "options": [SINGLE, CYCLE],
        "sub_configs": {SINGLE: ["Teleport to Boss", "Boss"],
                        CYCLE: ["Cycle Scope", "Cycle Start Index", "Rounds per Dungeon"]},
    }
    task.config_type["Cycle Scope"] = {"type": "drop_down", "options": [ALL, BOSS, WEEKLY],
                                      "sub_configs": {ALL: ["Cycle Start Category"]}}
    task.config_type["Cycle Start Category"] = {"type": "drop_down", "options": [BOSS, WEEKLY]}
    task.config_type["Cycle Start Index"] = {"min": 1, "max": max(task.total_boss_number, task.total_weekly_number)}
    task.config_type["Rounds per Dungeon"] = {"min": 1, "max": 100000}
    # Level is shared by both modes, rendered once instead of under two parents.
    for fields in task.config_type["Teleport to Boss"]["sub_configs"].values():
        if "Boss Level" in fields:
            fields.remove("Boss Level")


@dataclass
class RotationStep:
    category: str
    index: int
    rounds: int
    total_before: int
    completed: int = 0


def farm_setting(task, key, default=None):
    step = getattr(task, "_echo_rotation_step", None)
    if step is not None:
        overrides = {"Teleport to Boss": step.category, "Repeat Farm Count": step.rounds,
                     "Which Weekly Boss to Teleport": step.index,
                     "Which Boss Challenge to Teleport": step.index, "Boss": "Other"}
        if key in overrides:
            return overrides[key]
    return task.config.get(key, default)


def rotation_enabled(task):
    return task.config.get("Farm Mode", SINGLE) == CYCLE


def rotation_targets(task):
    scope = task.config.get("Cycle Scope", ALL)
    if scope not in {ALL, BOSS, WEEKLY}:
        raise ValueError("Unknown dungeon cycle scope")
    categories = ((BOSS, task.total_boss_number), (WEEKLY, task.total_weekly_number))
    start = task.config.get("Cycle Start Category", BOSS) if scope == ALL else scope
    if start not in {BOSS, WEEKLY}:
        raise ValueError("Unknown dungeon cycle start category")
    index = task.config.get("Cycle Start Index", 1)
    count = dict(categories)[start]
    if type(index) is not int or not 1 <= index <= count:
        raise ValueError(f"{task.tr(start)}：起刷序号必须为 1～{count}，当前值为 {index}。请按索拉指南从上到下计数。")
    targets = tuple((category, number) for category, count in categories if scope in {ALL, category}
                    for number in range(1, count + 1))
    offset = targets.index((start, index))
    return targets[offset:] + targets[:offset]


def rotation_round_completed(task):
    step = getattr(task, "_echo_rotation_step", None)
    if step is None:
        return
    step.completed += 1
    task.info_set("Dungeon Rounds", f"{step.completed}/{step.rounds}")
    task.info_set("Rotation Progress", f"{step.total_before + step.completed}/{task.config.get('Repeat Farm Count')}")


def run_rotation(task, run_single):
    # Validate before teleporting or sending input. Rotation never claims rewards.
    if task.config.get("Advanced Skill Material Mode", False):
        raise ValueError("循环刷声骸时请关闭“技能进阶材料模式”；该模式只拾取声骸。")
    total = task.config.get("Repeat Farm Count", 0)
    per_dungeon = task.config.get("Rounds per Dungeon", 1)
    if type(total) is not int or total < 0 or type(per_dungeon) is not int or per_dungeon < 1:
        raise ValueError("总刷取次数不能小于 0，每个副本刷取轮数必须至少为 1。")
    targets = rotation_targets(task)
    if not targets:
        raise ValueError("No dungeons available for rotation")
    completed = 0
    position = 0
    try:
        while completed < total:
            category, index = targets[position % len(targets)]
            rounds = min(per_dungeon, total - completed)
            step = RotationStep(category, index, rounds, completed)
            task._echo_rotation_step = step
            task.aim_boss = None
            task.treat_as_not_in_realm = False
            task._just_entered_boss_realm = False
            task.info_set("Rotation Cycle", position // len(targets) + 1)
            task.info_set("Current Dungeon", f"{task.tr(category)} · {index}")
            task.info_set("Dungeon Rounds", f"0/{rounds}")
            task.log_info(f"声骸轮刷：{task.tr(category)} #{index}，本副本 {rounds} 轮，总进度 {completed}/{total}")
            run_single()
            if step.completed != rounds:
                raise RuntimeError(f"当前副本只完成 {step.completed}/{rounds} 轮，停止轮刷，请检查战斗或复活状态。")
            completed += step.completed
            position += 1
    finally:
        task._echo_rotation_step = None
