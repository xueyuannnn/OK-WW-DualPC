"""Render original task symbols with the retained UI's Qt font renderer.

Authoring only: the native host embeds these images and needs no Python or font
file. Never modify task names or depend on platform font fallback at runtime.
"""
import json
import os
from pathlib import Path
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
from PySide6.QtCore import Qt, QRectF
from PySide6.QtGui import QFont, QFontDatabase, QImage, QPainter
from PySide6.QtWidgets import QApplication

root = Path(__file__).resolve().parents[1]
app = QApplication([])
QFontDatabase.addApplicationFont("C:/Windows/Fonts/seguiemj.ttf")
catalog = json.loads((root / "native/resources/catalog.json").read_text(encoding="utf-8"))
symbols = sorted({task["name"].split(" ")[0] for task in catalog["tasks"]
                  if not task["name"][0].isalnum()})
out = root / "native/resources/emoji"
out.mkdir(parents=True, exist_ok=True)
entries = []
records = []
for symbol in symbols:
    key = "-".join(f"{ord(char):x}" for char in symbol)
    folder = out / key
    folder.mkdir(exist_ok=True)
    records.append({"symbol": symbol, "key": key})
    for size in (16, 20, 24, 32, 40, 48, 64):
        image = QImage(size, size, QImage.Format_ARGB32_Premultiplied)
        image.fill(Qt.transparent)
        painter = QPainter(image)
        painter.setRenderHint(QPainter.Antialiasing)
        painter.setRenderHint(QPainter.TextAntialiasing)
        font = QFont("Segoe UI Emoji")
        font.setPixelSize(size - 2)
        painter.setFont(font)
        painter.drawText(QRectF(0, 0, size, size), Qt.AlignCenter, symbol)
        painter.end()
        if not image.save(str(folder / f"{size}.png")):
            raise RuntimeError(f"Cannot save {key}")
        entries.append(f'    <file alias="{key}/{size}.png">emoji/{key}/{size}.png</file>')
(root / "native/resources/classic-emoji.qrc").write_text(
    '<RCC>\n  <qresource prefix="/classic-emoji">\n' + "\n".join(entries) + '\n  </qresource>\n</RCC>\n', encoding="utf-8")
(out / "provenance.json").write_text(json.dumps({
    "source": "Original personal UI task-name Unicode symbols",
    "renderer": "Installed Segoe UI Emoji via original PySide6 Qt; font file is not redistributed",
    "symbols": records,
    "sizes": [16, 20, 24, 32, 40, 48, 64]
}, ensure_ascii=False, indent=2), encoding="utf-8")
print(json.dumps({"symbols": len(symbols), "images": len(entries)}))
