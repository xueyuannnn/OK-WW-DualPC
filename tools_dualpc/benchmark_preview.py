"""Compare Qt preview presentation cadence using synthetic video, no hardware."""
import json
import os
from pathlib import Path
import sys
import time
from types import SimpleNamespace

ROOT = Path(__file__).absolute().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "vendor/ok-script"))
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtCore import QEventLoop, QTimer
from PySide6.QtWidgets import QApplication
from ok import og
from dualpc.capture import CaptureReader
from dualpc.settings import Settings
from dualpc.ui import DualPCTab
from tools_dualpc.benchmark_capture import SyntheticCamera


def main():
    app = QApplication.instance() or QApplication([])
    settings = Settings()
    settings.capture.fps = 120
    og.config = {"dualpc_settings": settings, "dualpc_settings_path": str(ROOT / "dualpc.example.json")}
    camera = SyntheticCamera(1920, 1080, 120)
    reader = CaptureReader(settings.capture, lambda *unused: camera)
    og.device_manager = SimpleNamespace(capture_method=SimpleNamespace(reader=reader))
    tab = DualPCTab(auto_discover=False)
    tab.timer.stop()
    results = []
    try:
        reader.start()
        tab.resize(1000, 900)
        tab.show()
        tab.toggle_preview()
        app.processEvents()
        tab.verticalScrollBar().setValue(tab.verticalScrollBar().maximum())
        app.processEvents()
        for name, interval in (("previous_250ms_cadence", 250), ("independent_17ms_preview", 17)):
            tab.preview_timer.stop()
            tab.preview_timer.setInterval(interval)
            tab._preview_times.clear()
            before = tab.preview_frames_presented
            started = time.perf_counter()
            tab.preview_timer.start()
            loop = QEventLoop()
            QTimer.singleShot(3000, loop.quit)
            loop.exec()
            tab.preview_timer.stop()
            elapsed = time.perf_counter() - started
            frames = tab.preview_frames_presented - before
            results.append({"mode": name, "seconds": round(elapsed, 3), "presented_frames": frames,
                            "preview_fps": round(frames / elapsed, 2),
                            "source_read_fps": reader.statistics()["measured_fps"]})
        report = {"scope": "synthetic 1920x1080 at 120 FPS; Qt offscreen; no physical capture or box",
                  "results": results}
        destination = ROOT / "validation-dualpc/preview-benchmark.json"
        destination.parent.mkdir(exist_ok=True)
        destination.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(json.dumps(report, indent=2))
    finally:
        tab.preview_timer.stop()
        tab._close_probes()
        tab.close()
        reader.close()


if __name__ == "__main__":
    main()
