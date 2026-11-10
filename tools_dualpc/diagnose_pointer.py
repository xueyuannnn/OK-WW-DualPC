"""Inspect remote pointer movement, or an explicitly requested click or key.

Run with a game menu open. --execute permits real input using saved settings.
The default sequence only moves the pointer. --single skips that sequence;
--click-target or --key sends the requested input. Settings are never saved.
"""
import argparse
from dataclasses import replace
import json
from pathlib import Path
import sys
import time
from types import SimpleNamespace

ROOT = Path(__file__).absolute().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "vendor/ok-script"))
sys.argv[0] = str(ROOT / "main_dualpc.py")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--execute", action="store_true")
    parser.add_argument("--interval", type=float)
    parser.add_argument("--chunk", type=int)
    parser.add_argument("--click-target", nargs=2, type=int, metavar=("X", "Y"))
    parser.add_argument("--down-time", type=float, default=0.01)
    parser.add_argument("--settle", type=float, default=0)
    parser.add_argument("--key", help="Send one key and capture the result instead of the movement sequence")
    parser.add_argument("--key-duration", type=float, default=0.06, help="Hold the requested key for 0–3 seconds")
    parser.add_argument("--single", action="store_true", help="Only perform the requested click")
    parser.add_argument("--output", default="validation-dualpc/pointer-diagnostic")
    args = parser.parse_args()
    if not 0 <= args.key_duration <= 3:
        parser.error("--key-duration must be between 0 and 3 seconds")
    if not args.execute:
        parser.error("Open a menu, then use --execute for the requested movement test")
    import cv2
    from dualpc.capture import CaptureReader
    from dualpc.interaction import KMBoxInteraction
    from dualpc.settings import Settings
    settings = Settings.load(ROOT / "dualpc.json")
    box = replace(settings.kmbox)
    if args.interval is not None:
        box.move_interval = args.interval
    if args.chunk is not None:
        box.move_chunk = args.chunk
    destination = ROOT / args.output
    destination.mkdir(parents=True, exist_ok=True)
    reader = CaptureReader(settings.capture)
    interaction = None
    report = {"protocol": box.protocol, "dry_run": box.dry_run, "steps": [],
              "move_interval": box.move_interval, "move_chunk": box.move_chunk}
    try:
        reader.start(timeout=10)
        capture = SimpleNamespace(reader=reader, connected=reader.connected)
        interaction = KMBoxInteraction(capture, box)
        interaction.client.connect()

        def capture_step(name, action=None):
            if action:
                action()
            time.sleep(max(0.25, args.settle))
            frame = reader.get_frame()
            if frame is None:
                raise RuntimeError("Capture became unavailable during pointer test")
            path = destination / (name + ".png")
            if not cv2.imwrite(str(path), frame):
                raise RuntimeError("Could not save diagnostic frame")
            report["steps"].append({"name": name, "frame": str(path), "sequence": reader.sequence})
            print(name, flush=True)

        capture_step("00-before")
        if args.key:
            capture_step("01-key-result", lambda: interaction.send_key(args.key, down_time=args.key_duration))
        elif not args.single:
            capture_step("01-home-5000", lambda: interaction.client.move(-5000, -5000))
            capture_step("02-single-960-540", lambda: interaction.client.move(960, 540))
            capture_step("03-home-32767", lambda: interaction.client.move(-32767, -32767))
            capture_step("04-single-960-540", lambda: interaction.client.move(960, 540))
            capture_step("05-position-460-788", lambda: interaction.move(460, 788))
            capture_step("06-position-1720-510", lambda: interaction.move(1720, 510))
            capture_step("07-position-960-540", lambda: interaction.move(960, 540))
        if args.click_target:
            def click():
                interaction.move(*args.click_target)
                time.sleep(args.settle)
                interaction.click(move=False, down_time=args.down_time)
            capture_step("08-click-result", click)
            report["click"] = {"target": args.click_target, "down_time": args.down_time,
                               "settle": args.settle}
        report["status"] = "captured"
    except Exception as error:
        report.update(status="failed", error=str(error))
        raise
    finally:
        if interaction is not None:
            interaction.on_destroy()
        reader.close()
        (destination / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")


if __name__ == "__main__":
    main()
