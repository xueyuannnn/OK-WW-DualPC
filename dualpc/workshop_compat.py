"""Module-local input bridges for imported character code, without rewriting it.

This is a compatibility adapter, not a Python sandbox or a rotation validator.
Only imports made by the character module are redirected; other modules and
the process-wide ctypes/win32 functions are never patched.
"""
import ast
import builtins
import ctypes
from dataclasses import dataclass
from functools import lru_cache, wraps
import importlib.util
import ntpath
import os
import time
from types import ModuleType

from ok import Logger, og
from .interaction import KEYS

logger = Logger.get_logger(__name__)
LOCAL_LISTENER = 'Physical game-PC key monitoring is unavailable. Local keyboard monitoring is disabled; normal combat detection still works.'
EXTERNAL_INPUT = 'This code uses an input library outside the automatic bridge and needs manual adaptation.'
UNKNOWN_NATIVE = 'Some native or dynamic input calls need manual review before use.'
READY = 'Dual-PC import: common input interfaces supported'
CHECK = 'Dual-PC import: review required'
DETAIL = 'Input is routed to the selected box when this code runs. Recognition, resources and rotation timing still require game testing.'
SUPPORTED = {'SendInput', 'mouse_event', 'keybd_event', 'PostMessage', 'PostMessageA', 'PostMessageW',
             'SendMessage', 'SendMessageA', 'SendMessageW', 'GetAsyncKeyState', 'GetKeyState'}
EXTERNAL_LIBRARIES = {'pyautogui', 'pydirectinput', 'pynput', 'keyboard', 'mouse'}


@dataclass(frozen=True)
class CodeReport:
    native_calls: tuple = ()
    notices: tuple = ()
    blocked_imports: tuple = ()


@lru_cache(maxsize=128)
def inspect_code(code):
    try:
        tree = ast.parse(code)
    except SyntaxError:
        return CodeReport(notices=('Python syntax error',))
    imports, calls, native_aliases = set(), set(), {'user32'}
    uncertain = False
    for node in ast.walk(tree):
        if isinstance(node, ast.Assign) and 'user32' in ast.unparse(node.value).lower():
            native_aliases.update(target.id for target in node.targets if isinstance(target, ast.Name))
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            imports.update(alias.name.split('.')[0] for alias in node.names)
        elif isinstance(node, ast.ImportFrom):
            root = (node.module or '').split('.')[0]
            imports.add(root)
            if root in {'win32api', 'win32gui'}:
                for alias in node.names:
                    if alias.name in SUPPORTED:
                        calls.add(alias.name)
                    else:
                        uncertain = True
        elif isinstance(node, ast.Attribute):
            if node.attr in SUPPORTED:
                calls.add(node.attr)
            elif (isinstance(node.value, ast.Name) and node.value.id in native_aliases
                  or 'windll.user32' in ast.unparse(node.value)) and node.attr not in {'restype', 'argtypes'}:
                uncertain = True
        elif isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id in {'exec', 'eval', '__import__'}:
            uncertain = True
        elif isinstance(node, ast.Name) and any(flag in node.id for flag in (
                'MOUSEEVENTF_ABSOLUTE', 'MOUSEEVENTF_HWHEEL', 'MOUSEEVENTF_XDOWN',
                'MOUSEEVENTF_XUP', 'KEYEVENTF_UNICODE')):
            uncertain = True
    notices = []
    if calls & {'GetAsyncKeyState', 'GetKeyState'}:
        notices.append(LOCAL_LISTENER)
    blocked = tuple(sorted(imports & EXTERNAL_LIBRARIES))
    if blocked:
        notices.append(EXTERNAL_INPUT)
    if uncertain or 'importlib' in imports:
        notices.append(UNKNOWN_NATIVE)
    return CodeReport(tuple(sorted(calls)), tuple(notices), blocked)


class CompatibilityError(RuntimeError):
    pass


def current_interaction():
    executor = getattr(og, 'executor', None)
    interaction = getattr(executor, 'interaction', None)
    if not getattr(interaction, 'is_remote', False):
        raise CompatibilityError('No dual-PC controller is available for imported input')
    if getattr(executor, 'current_task', None) is None:
        raise CompatibilityError('Imported input requires an executing task')
    if not executor.is_executor_thread():
        raise CompatibilityError('Native input outside the task thread needs manual adaptation')
    return interaction


def virtual_key(vk, scan=0, flags=0):
    vk, scan, flags = int(vk), int(scan), int(flags)
    if flags & ~0x0B:
        raise CompatibilityError('Unsupported keyboard input flags; use the software key API')
    if flags & 8:
        letters = {**dict(zip(range(0x10, 0x1A), 'qwertyuiop')),
                   **dict(zip(range(0x1E, 0x27), 'asdfghjkl')),
                   **dict(zip(range(0x2C, 0x33), 'zxcvbnm')),
                   **dict(zip(range(2, 12), '1234567890'))}
        letters.update({1: 'esc', 14: 'backspace', 15: 'tab', 28: 'enter', 57: 'space',
                        29: 'rctrl' if flags & 1 else 'lctrl', 42: 'lshift', 54: 'rshift',
                        56: 'ralt' if flags & 1 else 'lalt'})
        key = letters.get(scan)
    else:
        special = {8: 'backspace', 9: 'tab', 13: 'enter', 16: 'lshift', 17: 'lctrl',
                   18: 'lalt', 27: 'esc', 32: 'space', 33: 'pageup', 34: 'pagedown',
                   35: 'end', 36: 'home', 37: 'left', 38: 'up', 39: 'right', 40: 'down',
                   45: 'insert', 46: 'delete', 160: 'lshift', 161: 'rshift',
                   162: 'lctrl', 163: 'rctrl', 164: 'lalt', 165: 'ralt'}
        key = chr(vk).lower() if 48 <= vk <= 57 or 65 <= vk <= 90 else (
            f'f{vk - 111}' if 112 <= vk <= 123 else special.get(vk))
    if key not in KEYS:
        raise CompatibilityError(f'Unsupported imported key: VK={vk}, scan={scan}')
    return key


class NativeFunction:
    """Accept ctypes argtypes/restype assignments on module-local functions."""
    def __init__(self, callback):
        self.callback = callback
        self.argtypes = self.restype = None

    def __call__(self, *args):
        return self.callback(*args)


class NativeBridge:
    def __init__(self, resolve=current_interaction):
        self.resolve = resolve
        callbacks = {'SendInput': self.send_input, 'mouse_event': self.mouse_event,
                     'keybd_event': self.keybd_event, 'GetAsyncKeyState': self.key_state,
                     'GetKeyState': self.key_state}
        callbacks.update({name: self.message for name in SUPPORTED if 'Message' in name})
        self.functions = {name: NativeFunction(callback) for name, callback in callbacks.items()}

    def __getattr__(self, name):
        if name in self.functions:
            return self.functions[name]
        raise CompatibilityError(f'Dual-PC import cannot translate native API {name}')

    def key_state(self, vk):
        # Outgoing HID state is not the physical keyboard state on the game PC.
        # Never read the operator's keyboard or mislabel synthetic keys as human.
        return 0

    @staticmethod
    def emit_press(interaction, kind, key, down):
        times = getattr(interaction, '_workshop_press_times', None)
        if times is None:
            times = interaction._workshop_press_times = {}
        identity = (kind, key)
        client = interaction.client
        if kind == 'key':
            code = KEYS[key]
            held = bool(getattr(client, 'modifiers', 0) & (1 << (code - 0xE0))) if 0xE0 <= code <= 0xE7 else code in getattr(client, 'keys', ())
        else:
            held = bool(getattr(client, 'buttons', 0) & {'left': 1, 'right': 2, 'middle': 4}[key])
        if not down and identity in times:
            remaining = getattr(interaction, 'PRESS_MINIMUM', .02) - (time.monotonic() - times[identity])
            if remaining > 0:
                event = getattr(interaction, '_suspended', None)
                event.wait(remaining) if event is not None else time.sleep(remaining)
        if kind == 'key':
            (interaction.send_key_down if down else interaction.send_key_up)(key)
        else:
            (interaction.mouse_down if down else interaction.mouse_up)(key=key)
        if down and not held:
            times[identity] = time.monotonic()
        elif not down:
            times.pop(identity, None)

    @staticmethod
    def mouse_operations(flags, dx=0, dy=0, data=0):
        flags, dx, dy, data = int(flags), int(dx), int(dy), int(data)
        if flags & ~0x087F:
            raise CompatibilityError('Absolute, horizontal-wheel and extra-button SendInput need manual adaptation')
        operations = []
        if flags & 1:
            operations.append(('move', dx, dy))
        for bit, key, down in ((2, 'left', True), (4, 'left', False), (8, 'right', True),
                               (16, 'right', False), (32, 'middle', True), (64, 'middle', False)):
            if flags & bit:
                operations.append(('button', key, down))
        if flags & 0x800:
            signed = ctypes.c_int32(data).value
            if signed % 120:
                raise CompatibilityError('High-resolution wheel input needs manual adaptation')
            operations.append(('wheel', signed // 120))
        return operations

    def dispatch(self, operations):
        interaction = self.resolve()
        # Validate batch before sending anything, and serialize it with ordinary
        # task input. Hardware pause/disconnect guards remain authoritative.
        with interaction._action_lock:
            for op in operations:
                if op[0] == 'key':
                    self.emit_press(interaction, 'key', op[1], op[2])
                elif op[0] == 'button':
                    self.emit_press(interaction, 'mouse', op[1], op[2])
                elif op[0] == 'move':
                    interaction._relative_counts(op[1], op[2])
                elif op[0] == 'point_button':
                    task = getattr(getattr(getattr(interaction, 'manager', None), 'executor', None), 'current_task', None)
                    combat_center = (task is not None and getattr(task, '_in_combat', False)
                        and ((op[1] == 0 and op[2] == 0) or
                             (abs(op[1] - task.width_of_screen(.5)) <= .5
                              and abs(op[2] - task.height_of_screen(.5)) <= .5)))
                    if combat_center:
                        self.emit_press(interaction, 'mouse', op[3], True)
                    else:
                        interaction.mouse_down(op[1], op[2], key=op[3])
                        times = getattr(interaction, '_workshop_press_times', None)
                        if times is None:
                            times = interaction._workshop_press_times = {}
                        times[('mouse', op[3])] = time.monotonic()
                elif op[0] == 'wheel':
                    interaction._require_active()
                    interaction.client.wheel(op[1])

    def mouse_event(self, flags, dx=0, dy=0, data=0, extra=0):
        self.dispatch(self.mouse_operations(flags, dx, dy, data))

    def keybd_event(self, vk, scan=0, flags=0, extra=0):
        self.dispatch([('key', virtual_key(vk, scan, flags), not int(flags) & 2)])

    def send_input(self, count, inputs, size):
        count = int(count)
        if count == 0:
            return 0
        if not 1 <= count <= 256:
            raise CompatibilityError('Invalid imported input batch size')
        # Read only Python-owned ctypes objects, never dereference raw addresses.
        obj = getattr(inputs, '_obj', inputs)
        if isinstance(obj, ctypes.Array) and len(obj) >= count:
            items = list(obj)[:count]
        elif count == 1 and isinstance(obj, ctypes.Structure):
            items = [obj]
        else:
            raise CompatibilityError('SendInput requires an owned INPUT object or array')
        operations = []
        for item in items:
            if int(size) != ctypes.sizeof(item):
                raise CompatibilityError('SendInput structure size does not match')
            union = item
            for field in item._fields_:
                if issubclass(field[1], ctypes.Union):
                    union = getattr(item, field[0])
                    break
            if item.type == 0:
                mouse = union.mi
                operations += self.mouse_operations(mouse.dwFlags, mouse.dx, mouse.dy, mouse.mouseData)
            elif item.type == 1:
                key = union.ki
                operations.append(('key', virtual_key(key.wVk, key.wScan, key.dwFlags), not key.dwFlags & 2))
            else:
                raise CompatibilityError('Hardware INPUT packets need manual adaptation')
        self.dispatch(operations)
        return count

    def message(self, hwnd, message, wparam=0, lparam=0, *, in_place=False):
        message = int(message)
        if message in {0x100, 0x101, 0x104, 0x105}:
            self.dispatch([('key', virtual_key(wparam), message in {0x100, 0x104})])
        elif message in {0x201, 0x202, 0x204, 0x205, 0x207, 0x208}:
            key = {0x201: 'left', 0x202: 'left', 0x204: 'right', 0x205: 'right',
                   0x207: 'middle', 0x208: 'middle'}[message]
            down = message in {0x201, 0x204, 0x207}
            operations = [('button', key, down)] if in_place or not down else [
                ('point_button', ctypes.c_short(int(lparam) & 0xFFFF).value,
                 ctypes.c_short((int(lparam) >> 16) & 0xFFFF).value, key)]
            self.dispatch(operations)
        else:
            raise CompatibilityError(f'Unsupported imported window message: {message:#x}')
        return 1


class ScopedModule(ModuleType):
    def __init__(self, name, delegate=None):
        super().__init__(name)
        self._delegate = delegate

    def __getattr__(self, name):
        if name.startswith('__'):
            raise AttributeError(name)
        if self._delegate is not None:
            return getattr(self._delegate, name)
        raise CompatibilityError(f'Imported input module does not support {name}')


def scoped_importer(bridge):
    ctypes_module = ScopedModule('ctypes', ctypes)
    ctypes_module.__dict__.update(vars(ctypes))
    ctypes_module.windll = ScopedModule('ctypes.windll')
    ctypes_module.windll.user32 = bridge
    original_windll = getattr(ctypes, 'WinDLL', None)
    def is_user32(name):
        return name is not None and ntpath.basename(os.fsdecode(name)).lower().removesuffix('.dll') == 'user32'

    def win_dll(name, *args, **kwargs):
        if is_user32(name):
            return bridge
        if original_windll is None:
            raise CompatibilityError('Native DLL is unavailable')
        return original_windll(name, *args, **kwargs)
    ctypes_module.WinDLL = win_dll
    original_cdll = ctypes.CDLL
    def c_dll(name, *args, **kwargs):
        if is_user32(name):
            return bridge
        return original_cdll(name, *args, **kwargs)
    ctypes_module.CDLL = c_dll
    ctypes_module.cdll = ScopedModule('ctypes.cdll')
    ctypes_module.cdll.user32 = bridge
    windows = {name: ScopedModule(name, bridge) for name in ('win32api', 'win32gui')}
    # Pure integer packing helper used by legacy window-message macros.
    windows['win32api'].MAKELONG = lambda low, high: (int(low) & 0xFFFF) | ((int(high) & 0xFFFF) << 16)

    def import_module(name, globals=None, locals=None, fromlist=(), level=0):
        if not level:
            root = name.split('.')[0]
            if root in EXTERNAL_LIBRARIES:
                raise CompatibilityError(f'{root} needs a separate dual-PC adapter')
            if root in windows:
                return windows[root]
            if root == 'ctypes':
                if name != 'ctypes':
                    imported = builtins.__import__(name, globals, locals, fromlist, level)
                    return imported if fromlist else ctypes_module
                return ctypes_module
        return builtins.__import__(name, globals, locals, fromlist, level)
    return import_module


def load_bridged_class(char_cls, path, module_name, bridge=None):
    code = path.read_text(encoding='utf-8-sig')
    report = inspect_code(code)
    if report.blocked_imports:
        raise CompatibilityError(f"Dual-PC import needs adaptation: {', '.join(report.blocked_imports)}")
    spec = importlib.util.spec_from_file_location(module_name, path)
    if spec is None or spec.loader is None:
        raise CompatibilityError(f'Cannot load imported character: {path}')
    module = importlib.util.module_from_spec(spec)
    module.__dict__['__builtins__'] = {**vars(builtins), '__import__': scoped_importer(bridge or NativeBridge())}
    from .workshop_runtime import compile_import, read_con, message_backend
    module.__dict__.update(_dualpc_read_con=read_con, _dualpc_message_backend=message_backend)
    exec(compile_import(code, str(path)), module.__dict__)
    cls = getattr(module, char_cls.__name__, None)
    from src.char.BaseChar import BaseChar
    if not isinstance(cls, type) or not issubclass(cls, BaseChar):
        raise CompatibilityError(f'{char_cls.__name__} must inherit BaseChar')
    cls._dualpc_import_report = report
    for notice in report.notices:
        logger.warning(f'{char_cls.__name__}: {notice}')
    return cls


def install_workshop_compat():
    from src.char import CustomCharLoader as loader
    original = loader._load_class_from_file
    if getattr(original, '_dualpc_workshop', False):
        return
    @wraps(original)
    def load(char_cls, path, module_name):
        return load_bridged_class(char_cls, path, module_name)
    load._dualpc_workshop = True
    loader._load_class_from_file = load
    loader.clear_team_char_cache()
    loader.clear_custom_char_cache()
    install_workshop_ui()


def install_workshop_ui():
    from src.gui.CharacterCodeTab import CharacterCodeTab
    from qfluentwidgets import BodyLabel
    original = CharacterCodeTab.__init__
    if getattr(original, '_dualpc_workshop', False):
        return
    def refresh(tab):
        label = getattr(tab, '_dualpc_compat_label', None)
        if label is None:
            return
        code = tab.editor.toPlainText()
        label.setVisible(bool(code))
        reports = [inspect_code(code)]
        if tab.current_team and tab.current_char_cls:
            from src.char.CustomCharLoader import read_team_char_code
            for name in tab.current_team:
                if name != tab.current_char_cls.__name__:
                    reports.append(inspect_code(read_team_char_code(tab.current_team, tab.char_by_name[name])))
        notices = tuple(dict.fromkeys(notice for report in reports for notice in report.notices))
        calls = sorted({call for report in reports for call in report.native_calls})
        label.setText(tab.tr(CHECK if notices else READY))
        details = [tab.tr(DETAIL), *(tab.tr(notice) for notice in notices)]
        if calls:
            details.append(', '.join(calls))
        label.setToolTip('\n\n'.join(details))
    @wraps(original)
    def init(tab, *args, **kwargs):
        original(tab, *args, **kwargs)
        parent = tab.editor.parentWidget()
        label = BodyLabel(parent)
        label.setWordWrap(True)
        tab._dualpc_compat_label = label
        layout = parent.layout()
        layout.insertWidget(layout.indexOf(tab.editor), label)
        tab.editor.setMinimumHeight(min(420, tab.editor.minimumHeight()))
        tab.editor.textChanged.connect(lambda: refresh(tab))
        refresh(tab)
    init._dualpc_workshop = True
    CharacterCodeTab.__init__ = init
