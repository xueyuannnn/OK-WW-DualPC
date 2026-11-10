"""Export the retained personal UI's exact icons for the Python-free Qt host.

Development tool only. SVG originals and pre-rendered DPI sizes ship with the
C++ sources, so building and running the portable app never invokes Python.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
from PySide6.QtCore import QFile, QIODevice, QRectF, Qt
from PySide6.QtGui import QImage, QPainter
from PySide6.QtSvg import QSvgRenderer
from PySide6.QtWidgets import QApplication

ROOT = Path(__file__).resolve().parents[1]
OLD = ROOT / "dist-native/legacy-desktop/OKWW-Enhanced-DualPC-personal"
RESOURCE = OLD / "runtime/Lib/site-packages/qfluentwidgets/_rc/resource.py"
OUT = ROOT / "native/resources/fluent"
NAMES = ["Play", "Pause", "Connect", "StopWatch", "BookShelf", "Code",
         "Calendar", "People", "Game", "Info", "Sync", "Ringer", "Setting",
         "Question", "Menu", "ChevronDown", "ChevronRight", "Save", "Send",
         "View", "Cancel", "Link", "Search", "Zoom", "Feedback", "Folder",
         "CommandPrompt", "Download", "Share", "Add", "Library", "Return"]
# LIBRARY is the BookShelf enum alias in the original framework.
NAMES.remove("Library")
NAMES += ["Delete", "GitHub", "Robot", "Brush", "Language", "Close"]
SIZES = (10, 12, 16, 20, 24, 32, 40, 48, 60, 64)


def digest(data):
    return hashlib.sha256(data).hexdigest().upper()


def main():
    app = QApplication([])
    spec = importlib.util.spec_from_file_location("retained_fluent_resources", RESOURCE)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    OUT.mkdir(parents=True, exist_ok=True)
    entries, records = [], []
    for theme, color in (("light", "black"), ("dark", "white")):
        for name in NAMES + ["Stop", "ChevronUp"]:
            original_name = "ChevronDown" if name == "ChevronUp" else name
            if name == "Stop":
                source_path = OLD / f"vendor/ok-script/ok/ui/qt/qss/{theme}/stop.svg"
                data = source_path.read_bytes()
                origin = source_path.relative_to(OLD).as_posix()
            else:
                origin = f":/qfluentwidgets/images/icons/{original_name}_{color}.svg"
                source = QFile(origin)
                if not source.open(QIODevice.ReadOnly):
                    raise RuntimeError(f"Original icon is missing: {origin}")
                data = bytes(source.readAll())
                source.close()
            original = OUT / f"source/{theme}/{name}.svg"
            original.parent.mkdir(parents=True, exist_ok=True)
            original.write_bytes(data)
            renderer = QSvgRenderer(data)
            if not renderer.isValid():
                raise RuntimeError(f"Invalid original SVG: {origin}")
            record = dict(name=name, theme=theme, original=origin, sha256=digest(data),
                          rotated_180=name == "ChevronUp")
            records.append(record)
            for size in SIZES:
                target = OUT / f"{theme}/{name}/{size}.png"
                target.parent.mkdir(parents=True, exist_ok=True)
                canvas = QImage(size, size, QImage.Format_ARGB32_Premultiplied)
                canvas.fill(Qt.transparent)
                painter = QPainter(canvas)
                if name == "ChevronUp":
                    painter.translate(size, size)
                    painter.rotate(180)
                renderer.render(painter, QRectF(0, 0, size, size))
                painter.end()
                if not canvas.save(str(target)):
                    raise RuntimeError(f"Cannot save icon: {target}")
                relative = target.relative_to(OUT.parent).as_posix()
                entries.append(f'    <file alias="{theme}/{name}/{size}.png">{relative}</file>')
    qrc = '<RCC>\n  <qresource prefix="/classic-fluent">\n' + '\n'.join(entries) + '\n  </qresource>\n</RCC>\n'
    (OUT.parent / "fluent-icons.qrc").write_text(qrc, encoding="utf-8")
    (OUT / "provenance.json").write_text(json.dumps(dict(
        source="Retained OKWW Enhanced DualPC personal UI / PySide6 Fluent Widgets 1.11.3",
        resource_sha256=digest(RESOURCE.read_bytes()), sizes=SIZES, icons=records,
        python_required_at_runtime=False), indent=2) + '\n', encoding="utf-8")
    license_path = OLD / "runtime/Lib/site-packages/pyside6_fluent_widgets-1.11.3.dist-info/LICENSE"
    (OUT / "LICENSE").write_bytes(license_path.read_bytes())
    print(f"Exported {len(records)} original icon variants / {len(entries)} DPI images.")


if __name__ == "__main__":
    main()
