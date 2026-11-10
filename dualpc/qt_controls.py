"""Readable Fluent numeric editors across the application on Qt 6.12."""
from PySide6.QtCore import QTimer, QObject, QEvent
from PySide6.QtGui import QWheelEvent
from PySide6.QtWidgets import (QApplication, QAbstractSpinBox, QComboBox, QSlider,
                               QAbstractScrollArea, QWidget)
from qfluentwidgets import SpinBox, DoubleSpinBox


class ParameterWheelGuard(QObject):
    """Keep wheel gestures scrolling pages instead of changing parameters."""
    def eventFilter(self, watched, event):
        if event.type() != QEvent.Wheel or not isinstance(watched, QWidget):
            return False
        from qfluentwidgets import ComboBox, EditableComboBox
        widget = watched
        while widget is not None:
            # Open popup lists and ordinary text/list views keep native scrolling.
            if isinstance(widget, QAbstractScrollArea):
                return False
            if isinstance(widget, (QAbstractSpinBox, QComboBox, QSlider, ComboBox, EditableComboBox)):
                break
            widget = widget.parentWidget()
        if widget is None:
            return False
        parent = widget.parentWidget()
        while parent is not None and not isinstance(parent, QAbstractScrollArea):
            parent = parent.parentWidget()
        if parent is not None:
            viewport = parent.viewport()
            forwarded = QWheelEvent(viewport.mapFromGlobal(event.globalPosition().toPoint()),
                                    event.globalPosition(), event.pixelDelta(), event.angleDelta(),
                                    event.buttons(), event.modifiers(), event.phase(), event.inverted(),
                                    event.source(), event.pointingDevice())
            QApplication.sendEvent(viewport, forwarded)
        event.accept()
        return True


def install_parameter_wheel_guard():
    app = QApplication.instance()
    if app is not None and not hasattr(app, "_okww_parameter_wheel_guard"):
        guard = ParameterWheelGuard(app)
        app.installEventFilter(guard)
        app._okww_parameter_wheel_guard = guard


def layout_number_editor(widget):
    editor = widget.lineEdit()
    buttons = [getattr(widget, name, None) for name in ("upButton", "downButton", "compactSpinButton")]
    buttons = [button for button in buttons if button is not None and not button.isHidden()]
    right = min((button.x() - 6 for button in buttons if button.x() > 10), default=widget.width() - 12)
    if buttons and right >= widget.width() - 12:
        right = widget.width() - (40 if hasattr(widget, "compactSpinButton") else 80)
    editor.setGeometry(10, 2, max(20, right - 10), max(20, widget.height() - 4))


class NumberFieldLayout:
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        install_parameter_wheel_guard()

    def _layout_number(self):
        layout_number_editor(self)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self._layout_number()

    def showEvent(self, event):
        super().showEvent(event)
        self._layout_number()
        QTimer.singleShot(0, self, self._layout_number)

    def focusInEvent(self, event):
        super().focusInEvent(event)
        self._layout_number()

    def focusOutEvent(self, event):
        super().focusOutEvent(event)
        self._layout_number()


class NumberSpinBox(NumberFieldLayout, SpinBox):
    pass


class NumberDoubleSpinBox(NumberFieldLayout, DoubleSpinBox):
    def textFromValue(self, value):
        text = super().textFromValue(value)
        separator = self.locale().decimalPoint()
        return text.rstrip("0").rstrip(separator) if separator in text else text


def install_number_layout_fix():
    install_parameter_wheel_guard()
    from qfluentwidgets.components.widgets.spin_box import SpinBoxBase
    if getattr(SpinBoxBase, "_okww_editor_fix", False):
        return
    for name in ("resizeEvent", "showEvent", "focusInEvent", "focusOutEvent"):
        original = getattr(SpinBoxBase, name, None)

        def handler(self, event, event_name=name, previous=original):
            if previous is not None:
                previous(self, event)
            else:
                getattr(super(SpinBoxBase, self), event_name)(event)
            layout_number_editor(self)
            if event_name != "resizeEvent":
                QTimer.singleShot(0, self, lambda: layout_number_editor(self))

        setattr(SpinBoxBase, name, handler)
    SpinBoxBase._okww_editor_fix = True
