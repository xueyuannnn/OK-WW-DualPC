"""Resizable preview of the existing capture buffer; no second device connection."""
import json
from pathlib import Path

from PySide6.QtCore import Qt, Signal, QEvent, QSignalBlocker, QSize
from PySide6.QtGui import QPainter, QColor, QPixmap
from PySide6.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QSizePolicy
from qfluentwidgets import BodyLabel, CheckBox, ComboBox

PREVIEW_RATES = (30, 60, 120, 144, 165, 240, 0)


class PreviewRateCombo(ComboBox):
    valueChanged = Signal(int)

    def __init__(self, value=60, parent=None):
        super().__init__(parent)
        for rate in PREVIEW_RATES:
            self.addItem("无限制" if rate == 0 else f"{rate} FPS", userData=rate)
        self.setValue(value)
        self.currentIndexChanged.connect(lambda *_: self.valueChanged.emit(self.value()))

    def value(self):
        return self.currentData()

    def setValue(self, value):
        value = value if value in PREVIEW_RATES else 60
        self.setCurrentIndex(PREVIEW_RATES.index(value))


class PreviewImage(QWidget):
    def __init__(self, text="等待画面", parent=None):
        super().__init__(parent)
        self._source = None
        self._scaled = QPixmap()
        self._text = text
        self.setAccessibleName("采集画面")
        self.setMinimumSize(160, 90)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)

    def sizeHint(self):
        return QSize(640, 360)

    def minimumSizeHint(self):
        return QSize(160, 90)

    def setPixmap(self, pixmap):
        self._source = pixmap
        self._fit()

    def _fit(self):
        if self._source is not None:
            self._scaled = self._source.scaled(self.size(), Qt.KeepAspectRatio, Qt.FastTransformation)
        self.update()

    def pixmap(self):
        return self._scaled

    def text(self):
        return self._text

    def setText(self, text):
        self.clear()
        self._text = text
        self.update()

    def clear(self):
        self._source = None
        self._scaled = QPixmap()
        self._text = ""
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setClipRect(self.rect())
        painter.fillRect(self.rect(), QColor("#101216"))
        if not self._scaled.isNull():
            painter.drawPixmap((self.width() - self._scaled.width()) // 2,
                               (self.height() - self._scaled.height()) // 2, self._scaled)
        else:
            painter.setPen(QColor("#c8ccd4"))
            painter.drawText(self.rect(), Qt.AlignCenter | Qt.TextWordWrap, self._text)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self._fit()


def load_preview_options(path):
    options = {"fps": 60, "height": 405, "topmost": True}
    try:
        saved = json.loads(Path(path).read_text(encoding="utf-8"))
        for key, low, high in (("fps", 0, 240), ("height", 180, 1080)):
            if type(saved.get(key)) is int and low <= saved[key] <= high:
                options[key] = saved[key]
        if options["fps"] not in PREVIEW_RATES:
            options["fps"] = 60
        if type(saved.get("topmost")) is bool:
            options["topmost"] = saved["topmost"]
    except (OSError, ValueError, TypeError, AttributeError):
        pass
    return options


class FloatingPreview(QWidget):
    visibility_changed = Signal()
    fps_changed = Signal(int)
    topmost_changed = Signal(bool)

    def __init__(self, fps=60, topmost=True):
        super().__init__(None, Qt.Window | Qt.WindowMinMaxButtonsHint | Qt.WindowCloseButtonHint)
        self.setWindowTitle("采集画面预览")
        self.setAttribute(Qt.WA_QuitOnClose, False)
        self.setMinimumSize(480, 330)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(2, 2, 2, 2)
        layout.setSpacing(2)
        toolbar = QHBoxLayout()
        toolbar.setSpacing(6)
        toolbar.addWidget(BodyLabel("预览帧率"))
        self.fps = PreviewRateCombo(fps)
        self.fps.setFixedWidth(110)
        toolbar.addWidget(self.fps)
        self.sizes = ComboBox()
        for width, height in ((640, 360), (960, 540), (1280, 720), (1920, 1080)):
            self.sizes.addItem(f"{width} × {height}", userData=(width, height))
        self.sizes.setCurrentIndex(1)
        toolbar.addWidget(self.sizes)
        toolbar.addStretch(1)
        self.topmost = CheckBox("始终置顶")
        self.topmost.setChecked(topmost)
        toolbar.addWidget(self.topmost)
        layout.addLayout(toolbar)
        self.image = PreviewImage()
        layout.addWidget(self.image, 1)
        self.status = BodyLabel("仅显示最新采集画面 · 拖动窗口边缘调整大小")
        layout.addWidget(self.status)
        self.resize(964, 594)
        self.setWindowFlag(Qt.WindowStaysOnTopHint, topmost)
        self.fps.valueChanged.connect(self.fps_changed)
        self.topmost.toggled.connect(self._topmost_changed)
        self.sizes.currentIndexChanged.connect(self._resize_preset)

    def _resize_preset(self):
        width, height = self.sizes.currentData()
        available = self.screen().availableGeometry()
        self.resize(min(width + 4, available.width()), min(height + 54, available.height()))

    def set_topmost(self, enabled):
        blocker = QSignalBlocker(self.topmost)
        self.topmost.setChecked(enabled)
        del blocker
        visible, geometry = self.isVisible(), self.geometry()
        self.setWindowFlag(Qt.WindowStaysOnTopHint, enabled)
        self.setGeometry(geometry)
        if visible:
            self.show()

    def _topmost_changed(self, enabled):
        self.set_topmost(enabled)
        self.topmost_changed.emit(enabled)

    def showEvent(self, event):
        super().showEvent(event)
        self.visibility_changed.emit()

    def hideEvent(self, event):
        super().hideEvent(event)
        self.visibility_changed.emit()

    def changeEvent(self, event):
        super().changeEvent(event)
        if event.type() == QEvent.WindowStateChange:
            self.visibility_changed.emit()
