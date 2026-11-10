import os
os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
import builtins
import ctypes
from pathlib import Path
import threading
from types import SimpleNamespace
from unittest.mock import Mock

import pytest

from dualpc.workshop_compat import (NativeBridge, CompatibilityError, scoped_importer,
    inspect_code, load_bridged_class, LOCAL_LISTENER, EXTERNAL_INPUT, UNKNOWN_NATIVE, current_interaction)


class Mouse(ctypes.Structure):
    _fields_ = [('dx', ctypes.c_long), ('dy', ctypes.c_long), ('mouseData', ctypes.c_ulong),
                ('dwFlags', ctypes.c_ulong), ('time', ctypes.c_ulong), ('dwExtraInfo', ctypes.c_size_t)]


class Keyboard(ctypes.Structure):
    _fields_ = [('wVk', ctypes.c_ushort), ('wScan', ctypes.c_ushort), ('dwFlags', ctypes.c_ulong),
                ('time', ctypes.c_ulong), ('dwExtraInfo', ctypes.c_size_t)]


class Union(ctypes.Union):
    _fields_ = [('mi', Mouse), ('ki', Keyboard)]


class Input(ctypes.Structure):
    _fields_ = [('type', ctypes.c_ulong), ('u', Union)]


@pytest.fixture
def remote():
    interaction = SimpleNamespace(_action_lock=threading.RLock(), PRESS_MINIMUM=0,
        send_key_down=Mock(), send_key_up=Mock(), mouse_down=Mock(), mouse_up=Mock(),
        move=Mock(), _relative_counts=Mock(), _require_active=Mock(), client=SimpleNamespace(wheel=Mock()))
    return NativeBridge(lambda: interaction), interaction


def test_sendinput_mouse_hold_uses_box_without_mouse_positioning(remote):
    bridge, interaction = remote
    packet = Input(type=0, u=Union(mi=Mouse(0, 0, 0, 2, 0, 0)))
    assert bridge.SendInput(1, ctypes.byref(packet), ctypes.sizeof(Input)) == 1
    interaction.mouse_down.assert_called_once_with(key='left')
    packet.u.mi.dwFlags = 4
    assert bridge.SendInput(1, ctypes.byref(packet), ctypes.sizeof(Input)) == 1
    interaction.mouse_up.assert_called_once_with(key='left')
    interaction._relative_counts.assert_not_called()


def test_sendinput_keyboard_array_preserves_order_and_held_state(remote):
    bridge, interaction = remote
    calls = []
    interaction.send_key_down.side_effect = lambda key: calls.append(('down', key))
    interaction.send_key_up.side_effect = lambda key: calls.append(('up', key))
    packets = (Input * 2)(Input(type=1, u=Union(ki=Keyboard(0x45, 0, 0, 0, 0))),
                          Input(type=1, u=Union(ki=Keyboard(0x45, 0, 2, 0, 0))))
    assert bridge.SendInput(2, packets, ctypes.sizeof(Input)) == 2
    assert calls == [('down', 'e'), ('up', 'e')]


@pytest.mark.parametrize('flags', [0x8000, 0x1000, 0x80])
def test_unsupported_mouse_batch_sends_nothing(remote, flags):
    bridge, interaction = remote
    packets = (Input * 2)(Input(type=0, u=Union(mi=Mouse(0, 0, 0, 2, 0, 0))),
                          Input(type=0, u=Union(mi=Mouse(0, 0, 0, flags, 0, 0))))
    with pytest.raises(CompatibilityError):
        bridge.SendInput(2, packets, ctypes.sizeof(Input))
    interaction.mouse_down.assert_not_called()


@pytest.mark.parametrize('inputs,size', [(12345, ctypes.sizeof(Input)),
                                       (ctypes.byref(Input()), 1)])
def test_bad_pointers_and_structure_sizes_never_reach_hardware(remote, inputs, size):
    bridge, interaction = remote
    with pytest.raises(CompatibilityError):
        bridge.SendInput(1, inputs, size)
    interaction.mouse_down.assert_not_called()


def test_scan_codes_window_keys_and_relative_mouse_events(remote):
    bridge, interaction = remote
    bridge.keybd_event(0, 0x12, 8, 0)
    bridge.PostMessageW(0, 0x101, 0x45, 0)
    bridge.mouse_event(1 | 8, 40, 40, 0, 0)
    interaction.send_key_down.assert_called_once_with('e')
    interaction.send_key_up.assert_called_once_with('e')
    interaction._relative_counts.assert_called_once_with(40, 40)
    interaction.mouse_down.assert_called_once_with(key='right')


def test_window_mouse_message_positions_menu_targets_and_keeps_combat_center_in_place(remote):
    bridge, interaction = remote
    bridge.PostMessageW(0, 0x201, 1, (800 << 16) | 1600)
    interaction.mouse_down.assert_called_once_with(1600, 800, key='left')
    interaction.mouse_down.reset_mock()
    task = SimpleNamespace(_in_combat=True, width_of_screen=lambda f:1920*f, height_of_screen=lambda f:1080*f)
    interaction.manager = SimpleNamespace(executor=SimpleNamespace(current_task=task))
    bridge.PostMessageW(0, 0x201, 1, (540 << 16) | 960)
    interaction.move.assert_not_called()
    interaction.mouse_down.assert_called_once_with(key='left')


def test_short_native_pulses_are_visible_to_hardware_and_long_holds_are_preserved(remote, monkeypatch):
    bridge, interaction = remote
    interaction.PRESS_MINIMUM = .02
    clock = [1.0]
    waits = []
    monkeypatch.setattr('dualpc.workshop_compat.time.monotonic', lambda: clock[0])
    monkeypatch.setattr('dualpc.workshop_compat.time.sleep', lambda seconds: waits.append(seconds))
    bridge.keybd_event(0x45)
    clock[0] += .005
    bridge.keybd_event(0x45, 0, 2)
    assert waits == [pytest.approx(.015)]
    bridge.mouse_event(2)
    clock[0] += .1
    bridge.mouse_event(4)
    assert len(waits) == 1


def test_press_metadata_recovers_after_external_release(remote, monkeypatch):
    bridge, interaction = remote
    interaction.PRESS_MINIMUM = .02
    clock, waits = [1.0], []
    monkeypatch.setattr('dualpc.workshop_compat.time.monotonic', lambda: clock[0])
    monkeypatch.setattr('dualpc.workshop_compat.time.sleep', lambda seconds: waits.append(seconds))
    bridge.mouse_event(2)
    interaction.client.buttons = 0  # Pause watchdog released the previous hold.
    clock[0] = 10.0
    bridge.mouse_event(2)
    clock[0] += .005
    bridge.mouse_event(4)
    assert waits == [pytest.approx(.015)]


def test_physical_keyboard_monitor_is_not_faked_or_read_locally(remote):
    bridge, interaction = remote
    bridge.GetAsyncKeyState.restype = ctypes.c_short
    assert bridge.GetAsyncKeyState(0x46) == 0
    interaction.send_key_down.assert_not_called()


@pytest.mark.parametrize('remote,task_thread,task', [(False, True, object()),
                                                   (True, False, object()), (True, True, None)])
def test_native_input_cannot_run_during_import_or_detached_worker(monkeypatch, remote, task_thread, task):
    from ok import og
    executor = SimpleNamespace(interaction=SimpleNamespace(is_remote=remote), current_task=task,
                               is_executor_thread=lambda:task_thread)
    monkeypatch.setattr(og, 'executor', executor)
    with pytest.raises(CompatibilityError):
        current_interaction()


def test_scoped_import_does_not_patch_shared_modules(remote):
    bridge, interaction = remote
    original = ctypes.windll.user32
    importer = scoped_importer(bridge)
    cloned = importer('ctypes')
    assert cloned is not ctypes and cloned.Structure is ctypes.Structure
    assert cloned.windll.user32 is bridge and cloned.WinDLL('user32.dll') is bridge
    assert cloned.CDLL('user32.dll') is bridge
    assert cloned.WinDLL(b'USER32.dll') is bridge
    assert cloned.CDLL(r'C:\Windows\System32\user32.dll') is bridge
    assert ctypes.windll.user32 is original
    assert builtins.__import__('ctypes') is ctypes
    assert importer('win32api', fromlist=('GetAsyncKeyState',)).GetAsyncKeyState(0x46) == 0


@pytest.mark.parametrize('code,notice', [
    ('import ctypes\nu = ctypes.windll.user32\nu.GetAsyncKeyState(70)', LOCAL_LISTENER),
    ('from win32api import GetAsyncKeyState\nGetAsyncKeyState(70)', LOCAL_LISTENER),
    ('import pyautogui\npyautogui.click()', EXTERNAL_INPUT),
    ('import ctypes\nu = ctypes.windll.user32\nu.SetCursorPos(1, 1)', UNKNOWN_NATIVE),
])
def test_import_report_identifies_concrete_review_requirements(code, notice):
    assert notice in inspect_code(code).notices


def test_imported_class_uses_scoped_native_bridge_without_rewriting_source(remote, tmp_path):
    from src.char.Mortefi import Mortefi
    bridge, interaction = remote
    path = tmp_path / 'Mortefi.py'
    code = '''import ctypes
from src.char.Mortefi import Mortefi as Builtin
user32 = ctypes.windll.user32
class Mortefi(Builtin):
    def do_perform(self):
        user32.keybd_event(0x45, 0, 0, 0)
        user32.keybd_event(0x45, 0, 2, 0)
'''
    path.write_text(code, encoding='utf-8')
    before = path.read_bytes()
    cls = load_bridged_class(Mortefi, path, 'test_imported_bridge', bridge)
    cls.do_perform(cls.__new__(cls))
    interaction.send_key_down.assert_called_once_with('e')
    interaction.send_key_up.assert_called_once_with('e')
    assert path.read_bytes() == before
    assert cls._dualpc_import_report.native_calls == ('keybd_event',)


def test_unadapted_external_input_import_is_explicitly_blocked(remote, tmp_path):
    from src.char.Mortefi import Mortefi
    bridge, _ = remote
    path = tmp_path / 'Mortefi.py'
    path.write_text('import pyautogui\nclass Mortefi: pass\n', encoding='utf-8')
    with pytest.raises(CompatibilityError, match='pyautogui'):
        load_bridged_class(Mortefi, path, 'test_blocked_bridge', bridge)


@pytest.mark.parametrize('protocol', ['kmbox_net', 'makcu_serial'])
def test_native_mouse_and_keyboard_bridge_uses_each_existing_controller(protocol):
    from dualpc.capture import CaptureReader
    from dualpc.interaction import KMBoxInteraction
    from dualpc.settings import BoxSettings, CaptureSettings
    reader = CaptureReader(CaptureSettings())
    reader.raw_size = (1920, 1080)
    capture = SimpleNamespace(reader=reader, connected=lambda: True)
    interaction = KMBoxInteraction(capture, BoxSettings(protocol=protocol, dry_run=True))
    bridge = NativeBridge(lambda: interaction)
    try:
        bridge.mouse_event(2)
        assert interaction.client.buttons == 1
        bridge.keybd_event(0x45)
        assert interaction.key_code('e') in interaction.client.keys
        bridge.mouse_event(4)
        bridge.keybd_event(0x45, 0, 2)
        assert interaction.client.buttons == 0 and not interaction.client.keys
        assert interaction.mouse_pos is None
    finally:
        interaction.on_destroy()


def test_compatibility_status_covers_other_members_in_the_selected_team(tmp_path, monkeypatch):
    from PySide6.QtWidgets import QApplication
    from ok.util.config import Config
    from src.char.CustomCharLoader import create_custom_team, save_team_char_code
    from src.char.Mortefi import Mortefi
    from src.char.Chixia import Chixia
    from src.char.Verina import Verina
    from src.gui.CharacterCodeTab import CharacterCodeTab
    from dualpc.workshop_compat import install_workshop_ui, CHECK
    app = QApplication.instance() or QApplication([])
    monkeypatch.setattr(Config, 'config_folder', str(tmp_path))
    team = (Mortefi, Chixia, Verina)
    create_custom_team(team)
    save_team_char_code(team, Mortefi, '''import ctypes
from src.char.Mortefi import Mortefi as Builtin
class Mortefi(Builtin):
    def read_key(self):
        return ctypes.windll.user32.GetAsyncKeyState(70)
''')
    install_workshop_ui()
    tab = CharacterCodeTab()
    try:
        index = next(i for i in range(tab.member_combo.count()) if tab.member_combo.itemData(i) == 'Chixia')
        tab.member_combo.setCurrentIndex(index)
        app.processEvents()
        assert tab._dualpc_compat_label.text() == CHECK
        assert LOCAL_LISTENER in tab._dualpc_compat_label.toolTip()
    finally:
        tab.close()
        tab.deleteLater()
