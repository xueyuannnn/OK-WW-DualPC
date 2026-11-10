import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

import pytest
from PySide6.QtCore import QPoint, QPointF, Qt
from PySide6.QtGui import QWheelEvent
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication, QSpinBox, QDoubleSpinBox, QComboBox, QSlider, QScrollArea, QWidget, QVBoxLayout
from qfluentwidgets import SpinBox, CompactSpinBox
from dualpc.qt_controls import install_parameter_wheel_guard


def wheel(app, widget, delta=-120):
    position = QPointF(widget.rect().center())
    event = QWheelEvent(position, QPointF(widget.mapToGlobal(position.toPoint())), QPoint(), QPoint(0, delta),
                        Qt.NoButton, Qt.NoModifier, Qt.NoScrollPhase, False)
    app.sendEvent(widget, event)


@pytest.mark.parametrize("kind", [QSpinBox, QDoubleSpinBox, SpinBox, CompactSpinBox, QSlider])
def test_focused_and_unfocused_numbers_ignore_wheel_but_keep_keyboard(kind):
    app = QApplication.instance() or QApplication([])
    install_parameter_wheel_guard()
    widget = kind()
    widget.setRange(0, 100)
    widget.setValue(40)
    widget.show()
    try:
        for focus in (False, True):
            widget.setFocus() if focus else widget.clearFocus()
            wheel(app, widget)
            if hasattr(widget, "lineEdit"):
                wheel(app, widget.lineEdit())
            assert widget.value() == 40
        QTest.keyClick(widget, Qt.Key_Up)
        assert widget.value() == 41
    finally:
        widget.close()


def test_parameter_wheel_scrolls_parent_page_and_closed_combo_keeps_selection():
    app = QApplication.instance() or QApplication([])
    install_parameter_wheel_guard()
    area = QScrollArea()
    body = QWidget()
    body.setMinimumHeight(1600)
    layout = QVBoxLayout(body)
    spin, combo = QSpinBox(), QComboBox()
    combo.addItems(["30 FPS", "60 FPS", "120 FPS"])
    combo.setCurrentIndex(1)
    layout.addWidget(spin)
    layout.addWidget(combo)
    layout.addStretch(1)
    area.setWidget(body)
    area.resize(500, 300)
    area.show()
    app.processEvents()
    try:
        wheel(app, spin)
        assert area.verticalScrollBar().value() > 0
        wheel(app, combo)
        assert combo.currentIndex() == 1
        combo.showPopup()
        app.processEvents()
        before = combo.view().verticalScrollBar().value()
        wheel(app, combo.view().viewport())
        assert combo.currentIndex() == 1
        assert combo.view().verticalScrollBar().value() >= before
    finally:
        combo.hidePopup()
        area.close()
