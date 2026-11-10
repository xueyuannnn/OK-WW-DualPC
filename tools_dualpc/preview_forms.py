"""Render actual task/global configuration cards in an isolated Qt runtime."""
import argparse
from copy import deepcopy
import json
import os
from pathlib import Path
import sys

ROOT = Path(__file__).absolute().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "vendor/ok-script"))
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--width", type=int, default=1050)
    parser.add_argument("--dark", action="store_true")
    parser.add_argument("--only", help="Render one task class or global configuration name")
    args = parser.parse_args()
    sys.argv = [str(ROOT / "main_dualpc.py")]
    from PySide6.QtCore import QTimer
    from PySide6.QtGui import QColor, QFont, QFontDatabase, QPainter, QPixmap
    from PySide6.QtWidgets import QWidget, QVBoxLayout, QAbstractSpinBox
    from qfluentwidgets import Theme, setTheme, FluentIcon
    from dualpc.bootstrap import create_runtime
    from dualpc.settings import Settings
    from dualpc.ui import DualPCTab
    from ok.ui.qt.widget.Tab import Tab
    from ok.ui.qt.tasks.ConfigCard import ConfigCard
    from ok.ui.qt.tasks.ScheduleTaskTab import CreateScheduleTaskDialog
    original_probe = DualPCTab._probe
    DualPCTab._probe = lambda *unused: None
    suffix = ("dark" if args.dark else "light") + f"-{args.width}"
    destination = ROOT / "validation-dualpc/forms-preview" / suffix
    destination.mkdir(parents=True, exist_ok=True)
    runtime_root = ROOT / "validation-dualpc" / ("forms-preview-runtime-" + suffix)
    runtime_root.mkdir(exist_ok=True)
    runtime = create_runtime(Settings(), runtime_root)
    font = Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts/msyh.ttc"
    if font.exists():
        QFontDatabase.addApplicationFont(str(font))
        runtime.app.app.setFont(QFont("Microsoft YaHei", 10))
    setTheme(Theme.DARK if args.dark else Theme.LIGHT)
    report = {"status": "running", "hardware": "not opened", "cards": [], "errors": []}
    sys.excepthook = lambda kind, error, traceback: report["errors"].append(str(error))
    host = QWidget()
    host.setObjectName("formsPreview")
    host.setStyleSheet("#formsPreview { background: " + ("#202020" if args.dark else "#f5f5f5") + "; }")
    layout = QVBoxLayout(host)
    layout.setContentsMargins(0, 0, 0, 0)
    page = Tab()
    layout.addWidget(page)
    host.resize(args.width, 920)
    jobs, baseline = [], []
    active = [None]

    def save_preview(widget, path):
        # Transparent Fluent surfaces need an opaque canvas in exported PNGs.
        source = widget.grab()
        canvas = QPixmap(source.size())
        canvas.setDevicePixelRatio(source.devicePixelRatio())
        canvas.fill(QColor("#202020" if args.dark else "#f5f5f5"))
        painter = QPainter(canvas)
        painter.drawPixmap(0, 0, source)
        painter.end()
        canvas.save(str(path))

    def begin():
        try:
            for task in runtime.task_executor.get_all_tasks():
                jobs.append((task.__class__.__name__, task, task.config, task.name, task.description,
                             task.default_config, task.config_description, task.config_type))
            for name, config, option in runtime.global_config.get_all_visible_configs():
                jobs.append((name, None, config, option.name, option.description,
                             option.default_config, option.config_description, option.config_type))
            if args.only:
                jobs[:] = [job for job in jobs if job[0] == args.only]
            baseline.extend(deepcopy(dict(job[2])) for job in jobs)
            host.show()
            render_next()
        except Exception as error:
            report["errors"].append(str(error))
            finish()

    def render_next(index=0):
        if active[0] is not None:
            page.removeWidget(active[0])
            active[0].hide()
            active[0].deleteLater()
        if index == len(jobs):
            dialog = CreateScheduleTaskDialog(runtime.app.main_window)
            active[0] = dialog
            dialog.show()
            QTimer.singleShot(250, lambda: capture_schedule(dialog))
            return
        name, task, config, title, description, defaults, descriptions, types = jobs[index]
        try:
            card = ConfigCard(task, title, config, description, defaults, descriptions, types, FluentIcon.SETTING)
            active[0] = card
            page.add_widget(card)
            card.setExpand(True)
            QTimer.singleShot(500, lambda: capture_card(index, card, name))
        except Exception as error:
            report["errors"].append(f"{name}: {error}")
            finish()

    def numeric_audit(widget):
        return [{"value": spin.text(), "editor_width": spin.lineEdit().width(),
                 "text_width": spin.fontMetrics().horizontalAdvance(spin.text())}
                for spin in widget.findChildren(QAbstractSpinBox) if not spin.isHidden()]

    def capture_card(index, card, name):
        safe_name = "".join(c if c.isalnum() or c in "-_" else "_" for c in name)
        before = [card.width(), card.height()]
        save_preview(page, destination / (safe_name + "-page.png"))
        save_preview(card, destination / (safe_name + ".png"))
        numbers = numeric_audit(card)
        report["cards"].append({"name": name, "fields": len(card.config_widgets), "numbers": numbers,
                                "size_before_capture": before, "size_after_capture": [card.width(), card.height()],
                                "rows": [{"key": row.key, "height": row.height(), "min": row.minimumHeight(),
                                          "hint": row.sizeHint().height(),
                                          "for_width": row.layout.heightForWidth(row.width())}
                                         for row in card.config_widgets]})
        for number in numbers:
            if number["editor_width"] < number["text_width"] + 4:
                report["errors"].append(f"{name}: clipped numeric value {number['value']}")
        QTimer.singleShot(0, lambda: render_next(index + 1))

    def capture_schedule(dialog):
        save_preview(dialog, destination / "schedule-dialog.png")
        report["schedule_numbers"] = numeric_audit(dialog)
        for number in report["schedule_numbers"]:
            if number["editor_width"] < number["text_width"] + 4:
                report["errors"].append("Schedule dialog: clipped numeric value")
        dialog.reject()
        finish()

    def finish():
        report["config_unchanged"] = baseline == [dict(job[2]) for job in jobs]
        report["field_count"] = sum(card["fields"] for card in report["cards"])
        report["status"] = "passed" if not report["errors"] and report["config_unchanged"] else "failed"
        (destination / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
        host.close()
        runtime.app.quit()

    QTimer.singleShot(3000, begin)
    try:
        runtime.start()
    except SystemExit:
        pass
    finally:
        runtime.device_manager.close()
        DualPCTab._probe = original_probe
    print(json.dumps({key: report[key] for key in ("status", "field_count", "config_unchanged", "errors")}, ensure_ascii=False))
    if report["status"] != "passed":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
