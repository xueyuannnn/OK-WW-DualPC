from src.task.MouseResetTask import MouseResetTask


def install_task_lookup_hook():
    """Keep enhanced subclasses from shadowing registered original tasks."""
    from functools import wraps
    from ok.task.TaskExecutor import TaskExecutor
    original = TaskExecutor.get_task_by_class
    if getattr(original, "_dualpc_exact_task", False):
        return

    @wraps(original)
    def get_task_by_class(executor, cls):
        if getattr(executor.interaction, "is_remote", False):
            for task in executor.get_all_tasks():
                if type(task) is cls:
                    return task
        # RemoteMouseResetTask deliberately substitutes for MouseResetTask;
        # preserve subclass lookup when no exact registered instance exists.
        return original(executor, cls)

    get_task_by_class._dualpc_exact_task = True
    TaskExecutor.get_task_by_class = get_task_by_class


def install_task_input_hook():
    """Translate coordinate message-clicks to real pointer positioning.

    BaseWWTask defaults move=False because PostMessage can carry coordinates
    without moving the local cursor. A hardware mouse must move first. Bare
    combat clicks and the center middle-click used for targeting stay in place.
    """
    from functools import wraps
    from src.task.BaseWWTask import BaseWWTask
    install_macro_mouse_hook()
    original = BaseWWTask.click
    if getattr(original, "_dualpc_coordinate_click", False):
        return

    @wraps(original)
    def click(task, x=-1, y=-1, move_back=False, name=None, interval=-1, move=False,
              down_time=0.01, after_sleep=0, key="left"):
        if (getattr(task.executor.interaction, "is_remote", False)
                and (x != -1 or y != -1) and key != "middle"):
            move = True
        return original(task, x, y, move_back, name, interval, move, down_time, after_sleep, key)

    click._dualpc_coordinate_click = True
    BaseWWTask.click = click
    original_box = BaseWWTask.click_box

    @wraps(original_box)
    def click_box(task, box=None, relative_x=.5, relative_y=.5, raise_if_not_found=False,
                  move_back=False, move=True, down_time=.01, after_sleep=1):
        if not getattr(task.executor.interaction, "is_remote", False):
            return original_box(task, box, relative_x, relative_y, raise_if_not_found,
                                move_back, move, down_time, after_sleep)
        if isinstance(box, list):
            box = box[0] if box else None
        if isinstance(box, str):
            box = task.get_box_by_name(box)
        if box is None:
            if raise_if_not_found:
                raise ValueError("click_box box is None")
            return
        x, y = round(box.x + box.width * relative_x), round(box.y + box.height * relative_y)
        # Keep valid in-box requests inside tiny/edge boxes. Deliberate offsets
        # outside a label (e.g. relative_x=1.5 for a travel button) stay intact.
        if 0 <= relative_x <= 1:
            x = min(box.x + box.width - 1, max(box.x, x))
        if 0 <= relative_y <= 1:
            y = min(box.y + box.height - 1, max(box.y, y))
        return task.click(x, y, name=box.name, move_back=move_back, move=move,
                          down_time=down_time, after_sleep=after_sleep)

    BaseWWTask.click_box = click_box


def install_macro_mouse_hook():
    """Keep message-style center holds in imported combat macros in place.

    Imported macros pass the screen center to mouse_down for normal/heavy
    attacks. Rehoming a hardware pointer here rotates the camera and consumes
    the macro's short key-hold window. Menu coordinates and named targets keep
    their original positioning behavior.
    """
    from functools import wraps
    from src.task.BaseWWTask import BaseWWTask
    original = BaseWWTask.mouse_down
    if getattr(original, '_dualpc_macro_mouse', False):
        return

    @wraps(original)
    def mouse_down(task, x=-1, y=-1, name=None, key='left'):
        if (getattr(task.executor.interaction, 'is_remote', False)
                and getattr(task, '_in_combat', False)
                and getattr(task, 'skip_combat_check', False)
                and name is None and key in {'left', 'right'}
                and abs(x - task.width_of_screen(.5)) <= .5
                and abs(y - task.height_of_screen(.5)) <= .5):
            x = y = -1
        return original(task, x, y, name=name, key=key)

    mouse_down._dualpc_macro_mouse = True
    BaseWWTask.mouse_down = mouse_down


class RemoteMouseResetTask(MouseResetTask):
    """Keep get_task_by_class(MouseResetTask) valid without local cursor polling."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.default_config = {"_enabled": False}
        self.name = "双机模式：无需本机鼠标回正"
        self.description = ("双机兼容占位项，无需开启；启用也不会执行本机鼠标回正。"
                            "KMBox 定位在硬件输入层处理。\n"
                            "Dual-PC compatibility placeholder: leave disabled. "
                            "Cursor positioning is handled by the hardware input adapter.")

    def run(self):
        pass

    def mouse_reset(self):
        pass
