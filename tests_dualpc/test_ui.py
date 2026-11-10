"""Settings interactions: preservation, device identity, mode linkage and visibility."""
import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
import sys
import time

from copy import deepcopy
from types import SimpleNamespace

import pytest
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import Qt
from PySide6.QtTest import QTest

from ok import og
from dualpc.settings import Settings
from dualpc.ui import DualPCTab, _fill


@pytest.fixture(scope="module")
def qt_app():
    return QApplication.instance() or QApplication([])


@pytest.fixture
def tab(qt_app, monkeypatch, tmp_path):
    settings = Settings()
    settings.capture.crop = [0, 0, 1920, 1080]
    settings.kmbox.host, settings.kmbox.port, settings.kmbox.uuid = "192.168.2.2", 12345, "1234abcd"
    settings.kmbox.serial_port = "COM7"
    monkeypatch.setattr(og, "config", {"dualpc_settings": settings, "dualpc_settings_path": str(tmp_path / "dualpc.json")})
    monkeypatch.setattr(og, "device_manager", None)
    callback_errors = []
    monkeypatch.setattr(sys, "excepthook", lambda kind, error, traceback: callback_errors.append(error))
    widget = DualPCTab(auto_discover=False)
    widget.timer.stop()
    monkeypatch.setattr(widget, "_probe", lambda *unused: None)
    yield widget
    widget._close_probes()
    widget.close()
    widget.deleteLater()
    qt_app.processEvents()
    assert not callback_errors, f"Unhandled Qt callback errors: {callback_errors}"


def select(combo, value):
    combo.setCurrentIndex(next(i for i in range(combo.count()) if combo.itemData(i) == value))


@pytest.mark.parametrize("protocol,visible", [("kmbox_net", "network"), ("makcu_serial", "serial")])
def test_protocol_only_exposes_its_connection_fields(tab, protocol, visible):
    select(tab.controls["kmbox.protocol"], protocol)
    for key in ("network", "serial"):
        assert getattr(tab, key + "_panel").isHidden() == (key != visible)
    assert tab.calibration.body.isHidden()  # Movement-test calibration stays collapsed for every protocol.


def test_protocol_menu_has_only_supported_boxes_and_no_usb_controls(tab):
    combo = tab.controls["kmbox.protocol"]
    assert [combo.itemData(i) for i in range(combo.count())] == ["kmbox_net", "makcu_serial"]
    assert not any(key.startswith("kmbox.hid_") for key in tab.controls)
    assert not hasattr(tab, "hid_panel")


@pytest.mark.parametrize("panel", ["capture_advanced", "calibration"])
def test_advanced_options_expand_and_collapse_without_callback_errors(tab, panel):
    section = getattr(tab, panel)
    assert section.body.isHidden()
    section.button.click()
    assert not section.body.isHidden()
    section.button.click()
    assert section.body.isHidden()


def test_numeric_fields_show_their_values_and_accept_keyboard_edits_after_resize(tab, qt_app):
    tab.resize(1000, 1200)
    tab.show()
    tab.capture_advanced.button.click()
    tab.manual_capture.setChecked(True)
    qt_app.processEvents()
    for key in ("capture.width", "capture.height", "capture.fps", "kmbox.port"):
        assert tab.controls[key].lineEdit().width() > 50
    editor = tab.controls["capture.fps"].lineEdit()
    editor.setFocus()
    QTest.keyClick(editor, Qt.Key_A, Qt.ControlModifier)
    QTest.keyClicks(editor, "144")
    QTest.keyClick(editor, Qt.Key_Return)
    assert tab.controls["capture.fps"].value() == 144
    tab.resize(800, 1200)
    qt_app.processEvents()
    assert editor.width() > 50


def test_protocol_switch_and_save_preserve_other_protocols_and_advanced_settings(tab):
    original = deepcopy(og.config["dualpc_settings"])
    for protocol in ("makcu_serial", "kmbox_net"):
        select(tab.controls["kmbox.protocol"], protocol)
    tab.controls["kmbox.host"].setText("192.168.2.3")
    assert tab.save()
    saved = Settings.load(og.config["dualpc_settings_path"])
    assert saved.kmbox.host == "192.168.2.3"
    assert saved.kmbox.serial_port == "COM7"
    assert saved.capture.crop == original.capture.crop
    assert og.config["dualpc_settings"] == original  # Saving does not silently hot-apply output.


def test_makcu_baud_rate_survives_switching_to_network_and_back(tab):
    select(tab.controls["kmbox.protocol"], "makcu_serial")
    assert tab.baudrate.currentText() == "4000000"
    tab.baudrate.setText("115200")
    select(tab.controls["kmbox.protocol"], "kmbox_net")
    select(tab.controls["kmbox.protocol"], "makcu_serial")
    assert tab.baudrate.currentText() == "115200"


def test_device_refresh_preserves_missing_selection_and_never_picks_camera_zero(tab):
    tab.controls["capture.device_path"].setText("saved-card-path")
    tab._devices_ready({"video": [{"index": 0, "name": "Webcam", "path": "webcam"}],
                        "serial": [], "hid": [], "warnings": []})
    assert tab.video_device.currentData() is None
    assert tab.collect_settings().capture.device_path == "saved-card-path"
    assert tab.collect_settings().kmbox.serial_port == "COM7"


def test_serial_refresh_preserves_unplugged_port_and_never_autoselects_unrelated_port(tab):
    tab.serial_port.setText("")
    tab._devices_ready({"video": [], "serial": [{"port": "COM1", "description": "System port"}],
                        "hid": [], "warnings": []})
    assert tab.collect_settings().kmbox.serial_port == ""
    tab.serial_port.setCurrentIndex(0)
    assert tab.collect_settings().kmbox.serial_port == "COM1"


def test_selected_device_is_saved_by_stable_path_and_old_modes_are_removed(tab):
    device = {"index": 3, "name": "USB Capture", "path": "stable-card-path"}
    tab._modes_ready({"modes": [{"width": 1920, "height": 1080, "fps": 120, "fourcc": "YUY2"}]})
    _fill(tab.video_device, [("Choose", None), (device["name"], device)])
    tab.video_device.setCurrentIndex(1)
    saved = tab.collect_settings()
    assert saved.capture.device_path == device["path"]
    assert saved.capture.device_index == 3
    assert not tab._modes
    assert tab.frame_rate.count() == 1  # No formats leaked from the previous card.


def test_resolution_and_format_filter_frame_rates_including_fractional_rates(tab):
    tab._modes_ready({"modes": [
        {"width": 1920, "height": 1080, "fps": 60, "fourcc": "YUY2"},
        {"width": 1920, "height": 1080, "fps": 120, "fourcc": "YUY2"},
        {"width": 3840, "height": 2160, "fps": 59.94, "fourcc": "NV12"},
    ]})
    select(tab.pixel_format, "NV12")
    assert tab.resolution.currentData() == (3840, 2160)
    assert tab.frame_rate.count() == 1
    assert tab.frame_rate.currentData() == 59.94
    settings = tab.collect_settings()
    assert (settings.capture.width, settings.capture.height, settings.capture.fps) == (3840, 2160, 59.94)


def test_unknown_saved_mode_is_retained_until_user_changes_selection(tab):
    tab.controls["capture.fps"].setValue(75)
    tab._modes_ready({"modes": [{"width": 1920, "height": 1080, "fps": 60, "fourcc": "YUY2"}]})
    assert tab.collect_settings().capture.fps == 75
    assert "手动值" in tab.frame_rate.currentText()
    select(tab.frame_rate, 60)
    assert tab.collect_settings().capture.fps == 60


def test_manual_capture_clears_invisible_device_override(tab):
    tab.controls["capture.device_path"].setText("old-card-path")
    tab.manual_capture.setChecked(True)
    tab.controls["capture.device_name"].setText("")
    tab.controls["capture.device_index"].setValue(5)
    settings = tab.collect_settings()
    assert settings.capture.device_path == ""
    assert settings.capture.device_index == 5




def test_ffmpeg_path_only_shown_for_ffmpeg_and_device_setup_is_prioritized(tab):
    assert tab.ffmpeg_row.isHidden()
    select(tab.controls["capture.backend"], "FFMPEG")
    assert not tab.ffmpeg_row.isHidden()
    assert not tab.add_after_default_tabs


def test_invalid_save_reports_error_without_overwriting_configuration(tab):
    assert tab.save()
    before = open(og.config["dualpc_settings_path"], "rb").read()
    tab.dry_run.setChecked(False)
    tab.controls["kmbox.host"].setText("invalid-address")
    assert not tab.save()
    assert open(og.config["dualpc_settings_path"], "rb").read() == before


def test_emergency_stop_releases_and_pauses_tasks(tab, monkeypatch):
    calls = []
    monkeypatch.setattr(og, "device_manager", SimpleNamespace(interaction=SimpleNamespace(emergency_stop=lambda: calls.append("stop"))))
    monkeypatch.setattr(og, "executor", SimpleNamespace(pause=lambda: calls.append("pause")))
    tab.stop()
    assert calls == ["stop", "pause"]


@pytest.fixture
def live_manager(tab, monkeypatch):
    snapshot = {"phase": "running", "worker_alive": True, "revision": 1, "applied_revision": 1,
                "last_test": None, "controller_error": "", "last_check": time.monotonic(), "error": ""}
    reader = SimpleNamespace(connected=lambda: True, age_ms=2.5, error="", get_frame=lambda: None,
                             actual={"frame_width": 1920, "frame_height": 1080, "fps": 120,
                                     "fourcc": "YUY2", "backend": "DSHOW", "device_name": "Test Capture"},
                             statistics=lambda: {"measured_fps": 119.9})
    manager = SimpleNamespace(settings=deepcopy(og.config["dualpc_settings"]), error="",
                              executor=SimpleNamespace(paused=True), session_snapshot=lambda: snapshot,
                              capture_method=SimpleNamespace(reader=reader),
                              interaction=SimpleNamespace(client=SimpleNamespace(connected=True)))
    manager.calls = []

    def start(settings):
        manager.calls.append(("start", settings))
        snapshot.update(phase="starting", revision=2)
        return 2

    def stop(emergency=False):
        manager.calls.append(("stop", emergency))
        snapshot["phase"] = "stopping"

    manager.start_session, manager.stop_session = start, stop
    manager.test_mouse_move = lambda: manager.calls.append(("test",)) or 7
    monkeypatch.setattr(og, "device_manager", manager)
    return manager, snapshot


def test_start_thread_uses_current_form_without_saving_or_restarting_app(tab, live_manager):
    manager, snapshot = live_manager
    tab.controls["capture.fps"].setValue(144)
    assert tab.start_connection() == 2
    assert manager.calls[0][0] == "start" and manager.calls[0][1].capture.fps == 144
    assert snapshot["phase"] == "starting" and not tab.start_thread_button.isEnabled()
    assert not __import__("pathlib").Path(og.config["dualpc_settings_path"]).exists()


def test_stop_thread_buttons_follow_asynchronous_state(tab, live_manager):
    manager, snapshot = live_manager
    tab.tick()
    assert tab.stop_thread_button.isEnabled()
    tab.stop_connection()
    assert manager.calls == [("stop", False)]
    assert not tab.stop_thread_button.isEnabled()
    snapshot.update(phase="stopped", worker_alive=False)
    tab.tick()
    assert tab.start_thread_button.isEnabled() and "已终止" in tab.session_state.text()


def test_unapplied_box_changes_disable_and_block_movement_test(tab, live_manager):
    manager, snapshot = live_manager
    tab.controls["kmbox.host"].setText("192.168.2.3")
    tab.tick()
    assert not tab.test_move_button.isEnabled()
    assert tab.test_connection_move() is None and not manager.calls
    assert "先点击" in tab.message.text()
    manager.settings = tab.collect_settings()
    tab.tick()
    assert tab.test_move_button.isEnabled()
    assert tab.test_connection_move() == 7
    assert manager.calls == [("test",)]


def test_running_ui_shows_negotiated_video_and_real_kmbox_status(tab, live_manager):
    manager, snapshot = live_manager
    manager.settings.kmbox.dry_run = False
    tab.dry_run.setChecked(False)
    tab.tick()
    assert "KMBox 已确认" == tab.controller_state.text()
    assert tab.actual_state.text() == "1920 × 1080"
    assert "120 FPS" in tab.status.text() and "YUY2" in tab.status.text()
    assert "119.9" in tab.status.text() and "DSHOW" in tab.status.text()
    manager.interaction.client.connected = False
    snapshot["controller_error"] = "ACK timeout"
    tab.tick()
    assert tab.controller_state.text() == "KMBox 未连接"
    assert "ACK timeout" in tab.status.text() and not tab.test_move_button.isEnabled()


def test_dry_run_ui_does_not_claim_physical_kmbox_is_connected(tab, live_manager):
    tab.tick()
    assert "未连接实物" in tab.controller_state.text()


def test_makcu_detection_does_not_open_an_active_session_port(tab, live_manager, monkeypatch):
    select(tab.controls["kmbox.protocol"], "makcu_serial")
    monkeypatch.setattr(tab, "_probe", lambda *unused: pytest.fail("Active port must not be probed"))
    tab.detect_makcu()
    assert "终止连接线程" in tab.message.text()


def test_makcu_detect_applies_only_matching_selection_and_shows_capabilities(tab, monkeypatch):
    select(tab.controls["kmbox.protocol"], "makcu_serial")
    calls = []
    monkeypatch.setattr(tab, "_probe", lambda key, args, callback: calls.append((key, args, callback)))
    tab.detect_makcu()
    assert calls[0][:2] == ("makcu", ["makcu", "COM7"])
    assert not tab.start_thread_button.isEnabled()
    calls[0][2]({"baudrate": 115200, "mouse": True, "keyboard": False})
    assert tab.baudrate.currentText() == "115200" and "未检测到键盘" in tab.message.text()
    assert tab.start_thread_button.isEnabled()
    tab.serial_port.setText("COM9")
    tab._makcu_ready("COM7", {"baudrate": 4000000, "mouse": True, "keyboard": True})
    assert tab.baudrate.currentText() == "115200" and "未应用" in tab.message.text()


def test_makcu_route_capability_is_visible_in_running_status(tab, live_manager):
    manager, snapshot = live_manager
    manager.settings.kmbox.protocol = "makcu_serial"
    manager.settings.kmbox.dry_run = False
    manager.interaction.client.capability_status = "MAKCU V4：仅鼠标路由"
    tab.tick()
    assert "仅鼠标" in tab.controller_state.text()
    assert not tab.makcu_probe_button.isEnabled()


def test_one_time_combat_status_shows_the_running_task_instead_of_trigger_waiting(tab, live_manager):
    manager, snapshot = live_manager
    task = SimpleNamespace(name="4C 声骸", running=True, _in_combat=True)
    manager.executor.paused = False
    manager.executor.current_task = task
    manager.executor.onetime_tasks = [task]
    tab.tick()
    assert "4C 声骸" in tab.automation_state.text() and "正在战斗" in tab.automation_state.text()


def test_preview_drops_duplicates_and_hidden_frames_and_tracks_reader_replacement(tab, live_manager, qt_app, monkeypatch):
    import numpy as np
    from dualpc.capture import CaptureReader
    from dualpc.settings import CaptureSettings
    manager, _ = live_manager

    def source(color):
        reader = CaptureReader(CaptureSettings(stale_after=10))
        reader._frame = np.full((1080, 1920, 3), color, dtype=np.uint8)
        reader._stamp = time.perf_counter()
        reader.sequence = 1
        monkeypatch.setattr(reader, "get_frame", lambda: pytest.fail("Preview copied a full task frame"))
        return reader

    reader = manager.capture_method.reader = source((0, 0, 255))
    tab.resize(1000, 900)
    tab.show()
    tab.toggle_preview()
    tab.preview_timer.stop()  # Exercise rendering deterministically.
    qt_app.processEvents()
    tab.verticalScrollBar().setValue(tab.verticalScrollBar().maximum())
    qt_app.processEvents()
    assert tab.render_preview()
    assert tab.preview.pixmap().toImage().pixelColor(0, 0).red() == 255
    presented = tab.preview_frames_presented
    assert not tab.render_preview() and tab.preview_frames_presented == presented
    reader.sequence = 17
    assert tab.render_preview()  # No queue of intermediate frames.
    manager.capture_method.reader = source((255, 0, 0))
    assert tab.render_preview()  # A new reader can reuse the same sequence.
    assert tab.preview.pixmap().toImage().pixelColor(0, 0).blue() == 255
    tab.resize(800, 900)
    qt_app.processEvents()
    assert tab.render_preview()  # Repaint an unchanged frame at the new size.
    manager.capture_method.reader.close()
    assert not tab.render_preview() and tab.preview.pixmap().isNull()
    assert "暂无" in tab.preview.text()
    tab.hide()
    assert not tab.preview_timer.isActive()
    assert not tab.render_preview()
    tab.show()
    assert tab.preview_timer.isActive()
    tab.toggle_preview()
    assert not tab.preview_timer.isActive()


def test_independent_preview_keeps_rendering_after_leaving_tab_and_shares_capture(tab, live_manager, qt_app, monkeypatch):
    import numpy as np
    from dualpc.capture import CaptureReader
    from dualpc.settings import CaptureSettings
    manager, _ = live_manager
    reader = CaptureReader(CaptureSettings(stale_after=10))
    reader._frame = np.full((1080, 1920, 3), (0, 200, 0), dtype=np.uint8)
    reader._stamp = time.perf_counter()
    reader.sequence = 1
    manager.capture_method.reader = reader
    monkeypatch.setattr(reader, "get_frame", lambda: pytest.fail("Preview used full task frame"))
    tab.open_preview_window()
    window = tab.preview_window
    qt_app.processEvents()
    tab.hide()
    assert tab.preview_timer.isActive()
    tab.preview_timer.stop()
    assert tab.render_preview()
    assert window.image.pixmap().toImage().pixelColor(0, 0).green() == 200
    assert manager.capture_method.reader is reader
    assert not tab.render_preview()
    window.resize(700, 500)
    qt_app.processEvents()
    tab.preview_timer.stop()
    assert tab.render_preview()
    window.close()
    assert not tab.preview_timer.isActive()
    assert not tab.render_preview()


def test_preview_fps_size_topmost_are_independent_from_capture_and_persist(tab, qt_app):
    before = deepcopy(tab.collect_settings().capture)
    tab.open_preview_window()
    window = tab.preview_window
    assert window.windowFlags() & Qt.WindowStaysOnTopHint
    tab.preview_fps.setValue(30)
    assert tab.preview_target_fps == 30 and tab.preview_timer.interval() == 33
    assert window.fps.value() == 30
    window.fps.setValue(120)
    assert tab.preview_fps.value() == 120 and tab.preview_timer.interval() == 8
    select(tab.preview_size, 540)
    assert tab.preview.height() == 540
    window.topmost.setChecked(False)
    assert not window.windowFlags() & Qt.WindowStaysOnTopHint
    assert not tab.preview_topmost.isChecked()
    assert tab.collect_settings().capture == before
    from dualpc.preview_window import load_preview_options
    assert load_preview_options(tab._preview_options_path) == {"fps": 120, "height": 540, "topmost": False}


def test_embedded_preview_card_grows_and_keeps_controls_and_picture_inside(tab, qt_app):
    from PySide6.QtCore import QPoint
    tab.resize(1200, 800)
    tab.show()
    tab.toggle_preview()
    card = tab.preview.parentWidget()
    for height in (540, 270, 720, 405):
        select(tab.preview_size, height)
        qt_app.processEvents()
        tab.verticalScrollBar().setValue(tab.verticalScrollBar().maximum())
        qt_app.processEvents()
        picture_top = tab.preview.mapTo(card, QPoint(0, 0)).y()
        rate_bottom = tab.preview_rate.mapTo(card, tab.preview_rate.rect().bottomLeft()).y()
        assert card.height() >= rate_bottom + 1, (card.height(), picture_top, rate_bottom)
        assert picture_top + tab.preview.height() <= tab.preview_rate.mapTo(card, QPoint(0, 0)).y()
        assert tab.preview.height() == height
        for widget in (tab.preview_fps, tab.preview_size, tab.preview_topmost):
            assert widget.mapTo(card, widget.rect().bottomLeft()).y() < picture_top
    tab.toggle_preview()
    qt_app.processEvents()
    assert card.height() < 500


@pytest.mark.parametrize("rate", [30, 60, 120, 144, 165, 240, 0])
def test_preview_rate_dropdown_matches_requested_presets_and_synchronizes_window(tab, qt_app, rate):
    from qfluentwidgets import ComboBox
    assert isinstance(tab.preview_fps, ComboBox)
    assert [tab.preview_fps.itemData(i) for i in range(tab.preview_fps.count())] == [30, 60, 120, 144, 165, 240, 0]
    tab.open_preview_window()
    select(tab.preview_fps, rate)
    assert tab.preview_window.fps.value() == rate
    assert tab.preview_timer.interval() == (round(1000 / rate) if rate else 0)
    from dualpc.preview_window import load_preview_options
    if rate != 60:
        assert load_preview_options(tab._preview_options_path)["fps"] == rate


def test_unlimited_preview_waits_when_connection_is_not_available(tab, qt_app):
    tab.open_preview_window()
    select(tab.preview_fps, 0)
    assert not tab.render_preview()
    assert tab.preview_timer.interval() > 0


@pytest.mark.parametrize("protocol", ["kmbox_net", "makcu_serial"])
def test_corner_wait_control_only_shows_for_kmbox(tab, protocol):
    select(tab.controls["kmbox.protocol"], protocol)
    assert not tab.homing_wait_row.isHidden()


def test_automation_status_distinguishes_connection_from_paused_or_disabled_combat(tab, live_manager):
    manager, _ = live_manager
    tab.tick()
    assert "自动任务已暂停" in tab.automation_state.text()
    manager.executor.paused = False
    task = type("AutoCombatTask", (), {"enabled": False, "_in_combat": False})()
    manager.executor.trigger_tasks = [task]
    tab.tick()
    assert "自动战斗未开启" in tab.automation_state.text()
    task.enabled = True
    tab.tick()
    assert "等待识别战斗" in tab.automation_state.text()
    task._in_combat = True
    tab.tick()
    assert "正在自动战斗" in tab.automation_state.text()
