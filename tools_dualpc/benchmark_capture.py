"""Software-only frame handoff benchmark; not an HDMI latency measurement."""
import argparse
import json
from pathlib import Path
import sys
import time

import numpy as np

ROOT = Path(__file__).absolute().parents[1]
sys.path.insert(0, str(ROOT))
from dualpc.capture import CaptureReader
from dualpc.settings import CaptureSettings


class SyntheticCamera:
    def __init__(self, width, height, fps):
        self.frame = np.zeros((height, width, 3), dtype=np.uint8)
        self.period = 1 / fps
        self.due = time.perf_counter()

    def isOpened(self):
        return True

    def set(self, *args):
        return False

    def get(self, *args):
        return 0

    def read(self):
        self.due += self.period
        time.sleep(max(0, self.due - time.perf_counter()))
        return True, self.frame

    def release(self):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=float, default=2)
    args = parser.parse_args()
    results = []
    for width, height, fps in ((1920, 1080, 120), (2560, 1440, 144), (3840, 2160, 60)):
        settings = CaptureSettings(width=width, height=height, fps=fps)
        camera = SyntheticCamera(width, height, fps)
        reader = CaptureReader(settings, lambda *unused: camera)
        try:
            reader.start()
            until = time.perf_counter() + args.seconds
            while time.perf_counter() < until:
                reader.get_frame()
                time.sleep(0.001)
            results.append({"synthetic_source": f"{width}x{height}@{fps}", "output": "1920x1080", **reader.statistics()})
        finally:
            reader.close()
    destination = ROOT / "validation-dualpc/software-latency-benchmark.json"
    destination.parent.mkdir(exist_ok=True)
    destination.write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
