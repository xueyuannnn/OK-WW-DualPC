"""Developer smoke test; no physical capture or input device is opened."""
import json
from pathlib import Path
import time

import numpy as np

from .capture import CaptureReader
from .settings import Settings


def smoke_gui(root):
    from PySide6.QtCore import QTimer
    from . import device
    from .bootstrap import create_runtime
    from .ui import DualPCTab

    class SyntheticReader(CaptureReader):
        def __init__(self, settings):
            from tools_dualpc.benchmark_capture import SyntheticCamera
            camera = SyntheticCamera(settings.width, settings.height, settings.fps)
            camera.frame[:, :, 1] = 60
            super().__init__(settings, lambda *unused: camera)

        def start(self, timeout=5):
            super().start(timeout)
            self.actual.update(source="synthetic, no physical hardware", fps=self.settings.fps, fourcc="YUY2")

    device.CaptureReader = SyntheticReader
    original_probe = DualPCTab._probe

    def synthetic_probe(tab, key, arguments, callback):
        if key == "devices":
            callback({"video": [{"index": 1, "name": "GC573 · 模拟设备", "path": "synthetic-capture"}],
                      "serial": [], "hid": [], "warnings": []})
        else:
            callback({"modes": [{"width": 1920, "height": 1080, "fps": fps, "fourcc": fmt}
                                for fps in (60, 120) for fmt in ("YUY2", "NV12")]})

    DualPCTab._probe = synthetic_probe
    directory = Path(root) / "validation-dualpc"
    directory.mkdir(exist_ok=True)
    report = {"status": "running", "physical_hardware": "not used", "input": "dry-run only"}
    (directory / "gui-smoke.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    settings = Settings()
    runtime = create_runtime(settings, root)
    # Qt's offscreen Windows plugin has no system font database. Register the
    # system UI font for readable verification images without bundling a font.
    from PySide6.QtGui import QFont, QFontDatabase
    import os
    font_path = Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts/msyh.ttc"
    if font_path.exists():
        QFontDatabase.addApplicationFont(str(font_path))
        runtime.app.app.setFont(QFont("Microsoft YaHei", 10))

    def inspect():
        try:
            from dualpc.ui import DualPCTab
            window = runtime.app.main_window
            if window is None:
                raise RuntimeError("Main window was not created")
            tabs = window.findChildren(DualPCTab)
            if len(tabs) != 1:
                raise RuntimeError(f"Expected one dual-PC tab, got {len(tabs)}")
            if runtime.device_manager.session_snapshot()["phase"] == "idle":
                revision = tabs[0].start_connection()
                if revision is None:
                    raise RuntimeError(tabs[0].message.text())
                await_state(lambda: runtime.device_manager.session_snapshot()["applied_revision"] == revision,
                            inspect, time.monotonic() + 5)
                return
            if not runtime.device_manager.capture_method.connected():
                raise RuntimeError("Synthetic capture did not connect")
            tabs[0].tick()
            window.grab().save(str(directory / "gui-main.png"))
            # Show the adapter tab so layout can be visually checked separately.
            window.switchTo(tabs[0])
            QTimer.singleShot(400, inspect_tab)
        except Exception as error:
            report.update(status="failed", error=str(error))
            finish()

    def inspect_tab():
        try:
            tab = runtime.app.main_window.findChildren(DualPCTab)[0]
            for section in (tab.capture_advanced, tab.calibration):
                # Call directly as well as through signals: Qt otherwise logs
                # Python slot exceptions without failing this smoke test.
                section._toggle(True)
                if section.body.isHidden():
                    raise RuntimeError("Advanced device settings did not expand")
                section._toggle(False)
            report["device_settings_ui"] = "device mode selection and advanced panels loaded"
            from ok.util.config import Config
            from ok.ui.qt.tasks.ConfigCard import ConfigCard
            from qfluentwidgets import FluentIcon
            from .echo_rotation import CYCLE
            farm = next(task for task in runtime.task_executor.onetime_tasks if task.__class__.__name__ == "FarmEchoTask2")
            rotation_config = Config("rotation-preview", {**farm.default_config, "Farm Mode": CYCLE},
                                     folder=str(directory))
            rotation_card = ConfigCard(None, "声骸循环配置", rotation_config, "", rotation_config.default,
                                       farm.config_description, farm.config_type, FluentIcon.SETTING)
            rotation_card.resize(1100, 1400)
            rotation_card.show()
            rotation_card.setExpand(True)
            from PySide6.QtTest import QTest
            QTest.qWait(350)
            runtime.app.app.processEvents()
            rotation_card.grab().save(str(directory / "gui-echo-rotation.png"))
            assert not rotation_card.config_widget_by_key["Rounds per Dungeon"].isHidden()
            assert rotation_card.config_widget_by_key["Teleport to Boss"].isHidden()
            rotation_card.close()
            report["echo_rotation_form"] = "cycle controls present, single-boss controls hidden, no duplicate fields"
            garden = next(task for task in runtime.task_executor.onetime_tasks if type(task).__name__ == 'GardenTask')
            garden_config = Config('garden-preview', garden.default_config, folder=str(directory))
            garden_card = ConfigCard(None, '周常乐园配置', garden_config, '', garden_config.default,
                                     garden.config_description, garden.config_type, FluentIcon.SETTING)
            garden_card.resize(1100, 800)
            garden_card.show()
            garden_card.setExpand(True)
            QTest.qWait(150)
            assert garden_config.get('Garden Shop Reserve') == 10
            assert garden_card.config_widget_by_key['Garden Custom Style'].isHidden()
            combo = garden_card.config_widget_by_key['Preferred Garden Style'].combo_box
            combo.setCurrentIndex(garden.config_type['Preferred Garden Style']['options'].index('Custom Style'))
            runtime.app.app.processEvents()
            assert not garden_card.config_widget_by_key['Garden Custom Style'].isHidden()
            garden_card.grab().save(str(directory / 'gui-garden-settings.png'))
            garden_card.close()
            report['garden_form'] = 'style dropdown, conditional custom style, coin reserve default 10'
            window = runtime.app.main_window
            assert not any(card.has_key("Launch Game After Capture Starts") or card.has_key("Game PC Launch Path")
                           for card in window.setting_tab.config_groups)
            login = next(task for task in runtime.task_executor.trigger_tasks if type(task).__name__ == "AutoLoginTask")
            assert login.default_config == {"_enabled": True}
            assert not getattr(type(runtime.task_executor).start, "_dualpc_game_startup", False)
            from src.gui.CharacterCodeTab import CharacterCodeTab
            character_tabs = window.findChildren(CharacterCodeTab)
            assert len(character_tabs) == 1 and hasattr(character_tabs[0], '_dualpc_compat_label')
            report['workshop_compatibility_ui'] = 'automatic input bridge installed; compatibility label present'
            window.switchTo(window.setting_tab)
            runtime.app.app.processEvents()
            window.grab().save(str(directory / "gui-settings-no-auto-launch.png"))
            window.switchTo(tab)
            report["automatic_game_launch"] = "removed; no launch settings or executor hook; original in-game Auto Login retained"
            runtime.app.main_window.grab().save(str(directory / "gui-dualpc.png"))
            # Exercise the enhanced task's real input API, without running its
            # gameplay logic or opening a hardware input transport.
            executor = runtime.task_executor
            daily = next(task for task in executor.onetime_tasks if task.__class__.__name__ == "DailyTask2")
            executor.paused = False
            try:
                daily.send_key("e", down_time=0)
                daily.click_relative(0.5, 0.5, down_time=0)
                if not runtime.device_manager.interaction.client.history:
                    raise RuntimeError("Enhanced task input API did not reach the HID adapter")
            finally:
                executor.paused = True
                runtime.device_manager.interaction.client.release_all()
            report.update(onetime_tasks=[t.__class__.__name__ for t in runtime.task_executor.onetime_tasks],
                          triggers=[t.__class__.__name__ for t in runtime.task_executor.trigger_tasks],
                          connected=runtime.device_manager.device_connected(),
                          enhanced_task_input_api="send_key + click_relative passed; gameplay was not executed")
            revision = tab.start_connection()
            if revision is None:
                raise RuntimeError(tab.message.text())
            await_state(lambda: runtime.device_manager.session_snapshot()["applied_revision"] == revision,
                        inspect_running, time.monotonic() + 5)
            return
        except Exception as error:
            report.update(status="failed", error=str(error))
        finish()

    def await_state(predicate, callback, deadline):
        try:
            if predicate():
                callback()
                return
            if time.monotonic() >= deadline:
                raise RuntimeError("Connection-worker UI smoke test timed out")
            QTimer.singleShot(50, lambda: await_state(predicate, callback, deadline))
        except Exception as error:
            report.update(status="failed", error=str(error))
            finish()

    def inspect_running():
        tab = runtime.app.main_window.findChildren(DualPCTab)[0]
        tab.tick()
        runtime.app.main_window.grab().save(str(directory / "gui-dualpc-running.png"))
        tab.toggle_preview()
        QTimer.singleShot(50, inspect_preview_start)

    def inspect_preview_start():
        tab = runtime.app.main_window.findChildren(DualPCTab)[0]
        tab.verticalScrollBar().setValue(tab.verticalScrollBar().maximum())
        initial = tab.preview_frames_presented
        QTimer.singleShot(600, lambda: inspect_preview(initial))

    def inspect_preview(initial):
        tab = runtime.app.main_window.findChildren(DualPCTab)[0]
        try:
            frames = tab.preview_frames_presented - initial
            if frames < 2 or tab.preview.pixmap().isNull():
                raise RuntimeError("Live preview did not render new synthetic frames")
            tab.tick()
            runtime.app.main_window.grab().save(str(directory / "gui-dualpc-preview.png"))
            report["live_preview"] = f"{frames} new frames presented in a 600ms GUI window"
            from PySide6.QtCore import QPoint
            card = tab.preview_card
            bottom = tab.preview_rate.mapTo(card, tab.preview_rate.rect().bottomLeft()).y()
            assert card.height() > bottom, f"Embedded preview clipped: card={card.height()}, bottom={bottom}"
            picture_bottom = tab.preview.mapTo(card, tab.preview.rect().bottomLeft()).y()
            status_top = tab.preview_rate.mapTo(card, QPoint(0, 0)).y()
            assert picture_bottom < status_top, f"Preview overlaps status: {picture_bottom}, {status_top}"
            assert tab.preview.mapTo(card, QPoint(0, 0)).y() > tab.preview_fps.mapTo(card, tab.preview_fps.rect().bottomLeft()).y()
            report["preview_geometry"] = {"card_height": card.height(), "picture_height": tab.preview.height(),
                                          "picture_bottom": picture_bottom, "status_top": status_top,
                                          "status_bottom": bottom, "all_controls_inside": True}
            tab.toggle_preview()
            tab.open_preview_window()
            tab.preview_window.fps.setValue(30)
            initial = tab.preview_frames_presented
            QTimer.singleShot(400, lambda: inspect_floating_preview(initial))
        except Exception as error:
            report.update(status="failed", error=str(error))
            finish()

    def inspect_floating_preview(initial):
        from PySide6.QtCore import Qt
        tab = runtime.app.main_window.findChildren(DualPCTab)[0]
        try:
            window = tab.preview_window
            frames = tab.preview_frames_presented - initial
            if frames < 2 or window.image.pixmap().isNull():
                raise RuntimeError("Independent preview did not present frames")
            assert tab.preview_target_fps == 30
            assert window.windowFlags() & Qt.WindowStaysOnTopHint
            window.grab().save(str(directory / "gui-independent-preview.png"))
            window.topmost.setChecked(False)
            assert not window.windowFlags() & Qt.WindowStaysOnTopHint
            window.close()
            assert not tab.preview_timer.isActive()
            tab.preview_fps.setValue(60)
            tab.preview_topmost.setChecked(True)
            report["independent_preview"] = f"{frames} frames at target 30 FPS; size, topmost and cleanup passed"
            tab.verticalScrollBar().setValue(0)
            inspect_move()
        except Exception as error:
            report.update(status="failed", error=str(error))
            finish()

    def inspect_move():
        tab = runtime.app.main_window.findChildren(DualPCTab)[0]
        if tab.test_connection_move() is None:
            raise RuntimeError(tab.message.text())
        await_state(lambda: runtime.device_manager.session_snapshot()["last_test"]["status"] != "pending",
                    inspect_test, time.monotonic() + 5)

    def inspect_test():
        result = runtime.device_manager.session_snapshot()["last_test"]
        if result["status"] != "passed" or not result["dry_run"] or result["counts"] != [40, 40]:
            raise RuntimeError(f"Unexpected movement-test result: {result}")
        tab = runtime.app.main_window.findChildren(DualPCTab)[0]
        tab.tick()
        runtime.app.main_window.grab().save(str(directory / "gui-dualpc-test.png"))
        tab.stop_connection()
        await_state(lambda: runtime.device_manager.session_snapshot()["phase"] == "stopped",
                    inspect_stopped, time.monotonic() + 5)

    def inspect_stopped():
        tab = runtime.app.main_window.findChildren(DualPCTab)[0]
        tab.tick()
        if runtime.device_manager.device_connected():
            raise RuntimeError("Devices remained connected after stopping the session")
        runtime.app.main_window.grab().save(str(directory / "gui-dualpc-stopped.png"))
        report.update(status="passed", connection_worker="start, hot apply, dry-run 40/40 movement and stop passed",
                      stopped_released_devices=True)
        finish()

    def finish():
        (directory / "gui-smoke.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
        runtime.app.quit()

    QTimer.singleShot(3500, inspect)
    try:
        runtime.start()
    except SystemExit:
        if report.get("status") != "passed":
            raise RuntimeError("GUI exited before the smoke test passed")
    finally:
        runtime.device_manager.close()
        DualPCTab._probe = original_probe
    print(json.dumps(report, indent=2, ensure_ascii=False))
    if report.get("status") != "passed":
        raise SystemExit(1)
