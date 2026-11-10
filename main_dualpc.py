"""Dual-PC entrypoint. main.py remains the upstream local-PC entrypoint."""
import argparse
import json
import os
from pathlib import Path
import sys


def main():
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="backslashreplace")
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="Validate adapter/upstream compatibility without hardware")
    parser.add_argument("--diagnose", action="store_true", help="Capture diagnostics only; does not send keyboard/mouse input")
    parser.add_argument("--smoke-gui", action="store_true", help="Developer UI check with synthetic video and no network output")
    parser.add_argument("--setup", action="store_true", help="Open the dual-PC device settings page on startup")
    parser.add_argument("--settings", default="dualpc.json")
    args, upstream_args = parser.parse_known_args()
    root = Path(__file__).absolute().parent
    os.chdir(root)
    # Pin the bundled framework instead of whichever ok-script happens to be
    # installed globally. Upstream and framework upgrades are independent.
    sys.path.insert(0, str(root / "vendor/ok-script"))
    from dualpc.compat import apply_game_patch, check_contract
    apply_game_patch(root)
    report = check_contract(root)
    if args.check:
        print(json.dumps(report, indent=2, ensure_ascii=False))
        return
    from dualpc.settings import Settings
    settings_path = Path(args.settings).absolute()
    if not settings_path.exists():
        Settings().save(settings_path)
    settings = Settings.load(settings_path)
    if args.smoke_gui:
        sys.argv = [sys.argv[0]]
        from dualpc.smoke import smoke_gui
        from dualpc.session import SessionLock
        with SessionLock(root):
            smoke_gui(root)
        return
    if args.diagnose:
        from dualpc.diagnose import diagnose
        diagnose(settings, root)
        return
    # Let OK-WW parse its own task/exit/headless flags, without adapter flags.
    sys.argv = [sys.argv[0]] + upstream_args
    from dualpc.bootstrap import create_runtime
    from dualpc.session import SessionLock
    with SessionLock(root):
        runtime = create_runtime(settings, root)
        runtime.config["dualpc_settings_path"] = str(settings_path)
        if args.setup:
            from PySide6.QtCore import QTimer
            from dualpc.ui import DualPCTab
            setup_timer = QTimer(runtime.app.app)

            def open_setup():
                window = runtime.app.main_window
                if window is not None:
                    tabs = window.findChildren(DualPCTab)
                    if tabs:
                        window.switchTo(tabs[0])
                        setup_timer.stop()
                        print("Dual-PC settings opened", flush=True)

            setup_timer.timeout.connect(open_setup)
            setup_timer.start(100)
        try:
            runtime.start()
        finally:
            if runtime.device_manager is not None:
                runtime.device_manager.close()


if __name__ == "__main__":
    main()
