"""Optional text-line advance for NPC dialogue without a skip button."""
from functools import wraps
import re

import cv2
import numpy as np

ADVANCE_NPC = "继续普通NPC对白"


def has_continue_marker(frame):
    height, width = frame.shape[:2]
    area = frame[round(height * 0.925):round(height * 0.98),
                 round(width * 0.475):round(width * 0.525)]
    minimum = area.min(axis=2)
    spread = area.max(axis=2).astype(np.int16) - minimum
    white = ((minimum >= 180) & (spread < 65)).astype(np.uint8) * 255
    contours, _ = cv2.findContours(white, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    for contour in contours:
        _, _, w, h = cv2.boundingRect(contour)
        if (width * 0.006 <= w <= width * 0.023
                and height * 0.012 <= h <= height * 0.04
                and 0.5 <= w / h <= 1.9
                and 0.12 <= cv2.contourArea(contour) / (w * h) <= 0.8):
            return True
    return False


def advance_npc_line(task):
    if not task.config.get(ADVANCE_NPC, False) or task.in_team_and_world():
        return False
    frame = task.frame
    if not has_continue_marker(frame):
        return False
    height, width = frame.shape[:2]
    speaker_region = frame[round(height * 0.73):round(height * 0.79),
                           round(width * 0.35):round(width * 0.65)]
    yellow = cv2.inRange(cv2.cvtColor(speaker_region, cv2.COLOR_BGR2HSV), (10, 90, 110), (45, 255, 255))
    if cv2.countNonZero(yellow) < width * height * 0.00008:
        return False
    # A menu footer diamond alone must never cause a click. Require both the
    # centered speaker name and a separate subtitle line, with no party HUD.
    speaker = task.ocr(0.35, 0.73, 0.65, 0.79, frame=frame, threshold=0.85,
                       match=re.compile(r"^[\u4e00-\u9fffA-Za-z· :]{1,24}$"))
    if not speaker:
        return False
    subtitle = task.ocr(0.12, 0.79, 0.88, 0.92, frame=frame, threshold=0.85)
    if not any(len(box.name.strip()) >= 2 for box in subtitle):
        return False
    task.click(0.5, 0.945, after_sleep=0.3)
    task.log_info("普通 NPC 对白：继续当前台词；分支选项由用户选择")
    return True


def install_dialogue_hook():
    from src.task.SkipDialogTask import AutoDialogTask
    original_init = AutoDialogTask.__init__
    if not getattr(original_init, "_dualpc_npc_option", False):
        @wraps(original_init)
        def init(task, *args, **kwargs):
            original_init(task, *args, **kwargs)
            task.default_config[ADVANCE_NPC] = False
            task.config_description[ADVANCE_NPC] = (
                "没有跳过按钮时继续普通 NPC 台词；遇到分支选项停下，不代选、不购买。默认关闭。")
        init._dualpc_npc_option = True
        AutoDialogTask.__init__ = init

    original_run = AutoDialogTask.run
    if getattr(original_run, "_dualpc_npc_advance", False):
        return

    @wraps(original_run)
    def run(task):
        result = original_run(task)
        if result or not getattr(task.executor.interaction, "is_remote", False):
            return result
        return advance_npc_line(task)

    run._dualpc_npc_advance = True
    AutoDialogTask.run = run
