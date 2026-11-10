"""Conservative health-bar fallback for color-shifted external capture."""
import re
import time

import cv2
import numpy as np

from ok import Box, Logger

logger = Logger.get_logger(__name__)
LEVEL_TEXT = re.compile(r"(?<![A-Za-z])Lv[\s.·]*\d{1,3}(?!\d)", re.IGNORECASE)


def shifted_bar_candidates(frame):
    height, width = frame.shape[:2]
    scale = height / 1080
    # Capture range/matrix conversion and chroma subsampling can change RGB
    # values. Use red/orange hue (including hue wraparound), with enough chroma
    # and brightness to exclude gray/dark outlines. OCR remains mandatory.
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
    # Bridge only tiny horizontal gaps from compressed/subsampled edges.
    kernel = np.ones((1, max(3, round(3 * width / 1920))), dtype=np.uint8)
    boxes = []
    # Try several brightness cutoffs so dim bars remain eligible while bright
    # bars can be separated from their tinted outlines and background.
    for value in (75, 110, 150, 190, 230):
        mask = cv2.inRange(hsv, (0, 70, value), (18, 255, 255))
        mask |= cv2.inRange(hsv, (170, 70, value), (179, 255, 255))
        joined = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel)
        contours, _ = cv2.findContours(joined, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        for contour in contours:
            x, y, w, h = cv2.boundingRect(contour)
            if not (round(50 * width / 1920) <= w <= round(345 * width / 1920)
                    and max(2, round(4 * scale)) <= h <= max(3, round(14 * scale))
                    and w >= h * 6 and height * 0.08 < y < height * 0.85):
                continue
            filled = cv2.countNonZero(mask[y:y + h, x:x + w]) / (w * h)
            core = mask[y + 1:y + h - 1, x + 1:x + w - 1] if h > 2 else mask[y:y + h, x:x + w]
            if filled >= 0.80 and cv2.countNonZero(core) / core.size >= 0.95:
                boxes.append(Box(x, y, w, h, confidence=filled))
    unique = []
    for box in sorted(boxes, key=lambda b: b.confidence, reverse=True):
        def overlaps(other):
            intersection = (max(0, min(box.x + box.width, other.x + other.width) - max(box.x, other.x))
                            * max(0, min(box.y + box.height, other.y + other.height) - max(box.y, other.y)))
            return intersection / min(box.width * box.height, other.width * other.height) > 0.8
        if not any(overlaps(other) for other in unique):
            unique.append(box)
        if len(unique) == 6:
            break
    return unique


def shifted_health_bar(task, frame):
    """Color/shape alone never starts combat: require the level text above it."""
    height, width = frame.shape[:2]
    scale = height / 1080
    # Some new bosses have no shield icon. Their full-width orange bar also
    # shifts out of the original RGB range after HDMI capture. Limit this
    # fallback to the top-center boss HUD and require an adjacent Lv label.
    top = frame[round(height * 0.025):round(height * 0.09), round(width * 0.28):round(width * 0.72)]
    hsv = cv2.cvtColor(top, cv2.COLOR_BGR2HSV)
    mask = cv2.inRange(hsv, (0, 90, 120), (35, 255, 255))
    mask |= cv2.inRange(hsv, (170, 90, 120), (179, 255, 255))
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    for contour in contours:
        x, y, w, h = cv2.boundingRect(contour)
        if (w >= width * 0.15 and max(2, 4 * scale) <= h <= 16 * scale
                and w / h >= 20 and cv2.contourArea(contour) / (w * h) >= 0.7):
            area = Box(round(width * 0.3), 0, round(width * 0.4), round(height * 0.052),
                       name="dualpc_boss_level")
            levels = task.ocr(box=area, frame=frame, match=LEVEL_TEXT, threshold=0.65)
            if any(LEVEL_TEXT.search(level.name) for level in levels):
                return True
    for bar in shifted_bar_candidates(frame):
        x, y = max(0, bar.x - round(12 * scale)), max(0, bar.y - round(48 * scale))
        right = min(width, bar.x + bar.width + round(55 * scale))
        bottom = max(y + 1, bar.y - round(3 * scale))
        area = Box(x, y, right - x, bottom - y, name="dualpc_enemy_level")
        levels = task.ocr(box=area, frame=frame, match=LEVEL_TEXT, threshold=0.65)
        if any(LEVEL_TEXT.search(level.name) for level in levels):
            task.draw_boxes("dualpc_enemy_health_bar", [bar], color="blue")
            return True
    return False


def install_combat_hook():
    install_visible_enemy_retarget_hook()
    from src.combat.CombatCheck import CombatCheck
    original = CombatCheck.has_health_bar
    if getattr(original, "_dualpc_color_compat", False):
        return

    def has_health_bar(task):
        if original(task):
            return True
        if not getattr(task.executor.interaction, "is_remote", False):
            return False
        if not task.in_team()[0]:
            return False
        # OCR runs only when the original detector misses. Limit repeat scans
        # while waiting for combat, and invalidate cached results on a new reader.
        reader = task.executor.device_manager.capture_method.reader
        now = time.monotonic()
        cached = getattr(task, "_dualpc_health_cache", None)
        if cached is not None and cached[0] is reader and now - cached[1] < 0.2:
            return cached[2]
        result = shifted_health_bar(task, task.frame)
        task._dualpc_health_cache = (reader, time.monotonic(), result)
        if result and not getattr(task, "_dualpc_color_notified", False):
            logger.info("External capture health bar confirmed using level text and shifted-color geometry")
            task._dualpc_color_notified = True
        return result

    has_health_bar._dualpc_color_compat = True
    has_health_bar._dualpc_original = original
    CombatCheck.has_health_bar = has_health_bar


def install_visible_enemy_retarget_hook():
    """A temporary lock-on loss cannot end a fight with a verified health bar."""
    from src.combat.CombatCheck import CombatCheck
    original = CombatCheck.target_enemy
    if getattr(original, "_dualpc_visible_enemy", False):
        return

    def target_enemy(task, wait=True):
        if (getattr(task.executor.interaction, "is_remote", False) and
                getattr(task, "_in_combat", False) and task.in_team()[0] and task.has_health_bar()):
            return True
        return original(task, wait=wait)

    target_enemy._dualpc_visible_enemy = True
    CombatCheck.target_enemy = target_enemy
