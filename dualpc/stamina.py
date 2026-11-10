"""Read the two stamina counters without accepting performance-overlay digits."""
from functools import wraps
import re

COUNTER = re.compile(r"(\d+)\s*/\s*(\d+)")
RESERVE = re.compile(r"(?:MAX)?(\d{1,4})(?:MAX)?", re.IGNORECASE)


def stamina_values(boxes):
    counters = []
    for box in boxes:
        match = COUNTER.fullmatch(str(box.name).replace("／", "/").strip())
        if match and 20 <= int(match[2]) <= 9999:
            counters.append((box, int(match[1]), int(match[2])))
    if not counters:
        return -1, -1, -1
    counter, current, maximum = max(counters, key=lambda item: item[2])
    reserves = []
    for box in boxes:
        match = RESERVE.fullmatch(str(box.name).replace(" ", "").replace(",", ""))
        if (match and box.x + box.width <= counter.x
                and abs(box.y + box.height / 2 - counter.y - counter.height / 2)
                <= 1.5 * max(box.height, counter.height)):
            reserves.append((box, int(match[1])))
    backup = max(reserves, key=lambda item: item[0].x)[1] if reserves else 0
    return current, backup, current + backup


def install_stamina_hook():
    from src.task.BaseWWTask import BaseWWTask
    original = BaseWWTask.get_stamina
    if getattr(original, "_dualpc_stamina_counters", False):
        return

    @wraps(original)
    def get_stamina(task):
        if not getattr(task.executor.interaction, "is_remote", False):
            return original(task)
        boxes = task.wait_ocr(0.49, 0, 0.92, 0.10, raise_if_not_found=False, match=re.compile(r"\d"))
        current, backup, total = stamina_values(boxes or [])
        if current < 0:
            task.screenshot("stamina_error")
        else:
            task.info_set("current_stamina", current)
            task.info_set("back_up_stamina", backup)
        return current, backup, total

    get_stamina._dualpc_stamina_counters = True
    BaseWWTask.get_stamina = get_stamina
