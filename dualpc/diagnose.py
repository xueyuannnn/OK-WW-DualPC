import json
from pathlib import Path
import time

import cv2

from .capture import CaptureReader


def diagnose(settings, root):
    reader = CaptureReader(settings.capture)
    directory = Path(root) / "validation-dualpc"
    directory.mkdir(exist_ok=True)
    report = {"keyboard_mouse_output": "not opened or used",
              "hardware": settings.capture.device_name or f"Video device #{settings.capture.device_index}",
              "capture": "failed"}
    try:
        reader.start(timeout=8)
        until = time.monotonic() + 3
        while time.monotonic() < until:
            reader.get_frame()
            time.sleep(0.001)
        frame = reader.get_frame()
        if frame is None:
            raise RuntimeError("No fresh frame")
        # imencode + tofile supports a Unicode output path on Windows.
        success, encoded = cv2.imencode(".png", frame)
        if not success:
            raise RuntimeError("Could not encode the capture")
        encoded.tofile(str(directory / "capture.png"))
        report.update(capture="passed", actual=reader.actual, raw_size=reader.raw_size,
                      inference_size=frame.shape[1::-1], age_ms=reader.age_ms, software_statistics=reader.statistics())
    except Exception as error:
        report["error"] = str(error)
    finally:
        reader.close()
        (directory / "hardware-check.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(report, indent=2, ensure_ascii=False))
    if report["capture"] != "passed":
        raise SystemExit(1)
