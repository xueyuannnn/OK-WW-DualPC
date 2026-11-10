"""Render the settings page without starting capture, connecting HID, or saving settings."""
import argparse
import os
from pathlib import Path
import sys
import time

ROOT = Path(__file__).absolute().parents[1]
sys.path.insert(0, str(ROOT / "vendor/ok-script"))
sys.path.insert(0, str(ROOT))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--detect", action="store_true", help="Read live device metadata and native modes only")
    parser.add_argument("--dark", action="store_true")
    parser.add_argument("--width", type=int, default=1050)
    parser.add_argument("--height", type=int, default=980)
    args = parser.parse_args()
    from PySide6.QtCore import QTimer
    from PySide6.QtGui import QFont, QFontDatabase
    from PySide6.QtWidgets import QApplication, QVBoxLayout, QWidget
    from qfluentwidgets import Theme, setTheme, setThemeColor
    from ok import og
    from ok.ui.qt import resources  # Register the same styles as the real application.
    from dualpc.settings import Settings
    from dualpc.ui import DualPCTab

    app = QApplication([])
    font = Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts/msyh.ttc"
    if font.exists():
        QFontDatabase.addApplicationFont(str(font))
        app.setFont(QFont("Microsoft YaHei", 10))
    setTheme(Theme.DARK if args.dark else Theme.LIGHT)
    setThemeColor("#0067c0")
    settings = Settings.load(ROOT / "dualpc.json") if (ROOT / "dualpc.json").exists() else Settings()
    og.config = {"dualpc_settings": settings, "dualpc_settings_path": str(ROOT / "dualpc.json")}
    og.device_manager = None
    tab = DualPCTab(auto_discover=args.detect)
    host = QWidget()
    host.setObjectName("previewHost")
    host.setStyleSheet("QWidget#previewHost { background-color: " + ("#202020" if args.dark else "#f5f5f5") + "; }")
    layout = QVBoxLayout(host)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.addWidget(tab)
    tab.status.setText("设置预览 · 尚未启动视频采集或连接键鼠")
    host.resize(args.width, args.height)
    host.show()
    destination = ROOT / "validation-dualpc/ui-preview"
    destination.mkdir(parents=True, exist_ok=True)
    suffix = ("dark" if args.dark else "light") + f"-{args.width}"
    started = time.monotonic()

    def capture():
        if tab._processes and time.monotonic() - started < 16:
            QTimer.singleShot(100, capture)
            return
        for key in ("kmbox_net",):
            combo = tab.controls["kmbox.protocol"]
            combo.setCurrentIndex(next(i for i in range(combo.count()) if combo.itemData(i) == key))
            app.processEvents()
            host.grab().save(str(destination / f"{key}-{suffix}.png"))
        tab.capture_advanced.button.click()
        tab.manual_capture.setChecked(True)
        app.processEvents()
        tab.verticalScrollBar().setValue(0)
        host.grab().save(str(destination / f"advanced-capture-{suffix}.png"))
        print(f"Rendered {destination} ({suffix}); devices={len(tab._devices)}, native_modes={len(tab._modes)}")
        tab._close_probes()
        app.quit()

    QTimer.singleShot(1000, capture)
    app.exec()


if __name__ == "__main__":
    main()
