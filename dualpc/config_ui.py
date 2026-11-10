"""Graphical, responsive configuration for task and global settings pages."""
from copy import deepcopy
from pathlib import Path
import importlib
import math

from PySide6.QtCore import QSignalBlocker, QTimer, Qt, Signal
from PySide6.QtWidgets import QBoxLayout, QHBoxLayout, QVBoxLayout, QWidget, QSizePolicy, QListWidgetItem
from qfluentwidgets import (BodyLabel, CaptionLabel, ComboBox, EditableComboBox, LineEdit, ListWidget,
                            PushButton, TextEdit, FluentIcon, CheckBox, MessageBoxBase)

from ok import og
from ok.ui.qt.tasks.ConfigLabelAndWidget import ConfigLabelAndWidget
from .qt_controls import NumberSpinBox, NumberDoubleSpinBox, install_number_layout_fix


def translated(value):
    return og.app.tr(str(value)) if og.app is not None else str(value)


class ValueEditor(QWidget):
    changed = Signal(object)

    def __init__(self, value, spec=None, parent=None):
        super().__init__(parent)
        self.spec = spec or {}
        self.value = deepcopy(value)
        self.control = None
        self.children_by_key = {}
        self.layout = QVBoxLayout(self)
        self.layout.setContentsMargins(0, 0, 0, 0)
        self.layout.setSpacing(6)
        self._build(value)

    def commit(self, value):
        self.value = deepcopy(value)
        if self.control is not None:
            self.control.setToolTip(str(value))
        self.changed.emit(deepcopy(value))

    def set_value(self, value):
        self.value = deepcopy(value)
        if isinstance(value, list):
            self._fill_list()
        elif isinstance(value, dict):
            for key, editor in self.children_by_key.items():
                if key in value:
                    editor.set_value(value[key])
        elif self.control is not None:
            blocker = QSignalBlocker(self.control)
            if isinstance(self.control, ComboBox):
                index = next((i for i in range(self.control.count()) if self.control.itemData(i) == value), -1)
                if index < 0:
                    self.control.addItem(translated(value), userData=value)
                    index = self.control.count() - 1
                self.control.setCurrentIndex(index)
            elif isinstance(self.control, CheckBox):
                self.control.setChecked(value)
            elif isinstance(self.control, (NumberSpinBox, NumberDoubleSpinBox)):
                self.control.setRange(min(self.control.minimum(), value), max(self.control.maximum(), value))
                self.control.setValue(value)
            elif isinstance(self.control, TextEdit):
                self.control.setPlainText(value)
            else:
                self.control.setText(str(value) if value is not None else "")
            self.control.setToolTip(str(value))
            del blocker

    def _build(self, value):
        options = self.spec.get("options")
        if options is not None and not isinstance(value, (list, dict)):
            self.control = ComboBox()
            choices = list(options)
            if value not in choices:
                choices.insert(0, value)
            for option in choices:
                self.control.addItem(translated(option), userData=option)
            self.control.setCurrentIndex(choices.index(value))
            self.control.currentTextChanged.connect(lambda text: self.commit(self.control.currentData()))
        elif isinstance(value, bool):
            self.control = CheckBox("开启")
            self.control.setChecked(value)
            self.control.toggled.connect(self.commit)
        elif isinstance(value, int) and -2147483648 <= value <= 2147483647:
            self.control = NumberSpinBox()
            low = self.spec.get("min", -999999 if value < 0 else 0)
            high = self.spec.get("max", 99999999)
            self.control.setRange(min(low, value), max(high, value))
            self.control.setValue(value)
            self.control.valueChanged.connect(self.commit)
        elif isinstance(value, float) and math.isfinite(value):
            self.control = NumberDoubleSpinBox()
            self.control.setDecimals(int(self.spec.get("decimals", 8)))
            self.control.setRange(min(self.spec.get("min", -99999999), value),
                                  max(self.spec.get("max", 99999999), value))
            self.control.setSingleStep(float(self.spec.get("step", 0.01)))
            self.control.setValue(value)
            self.control.valueChanged.connect(self.commit)
        elif isinstance(value, dict):
            for key, item in value.items():
                self.layout.addWidget(BodyLabel(translated(key)))
                editor = ValueEditor(item)
                self.children_by_key[key] = editor
                editor.changed.connect(lambda updated, field=key: self._update_mapping(field, updated))
                self.layout.addWidget(editor)
            if not value:
                self.layout.addWidget(CaptionLabel("当前无配置项"))
            return
        elif isinstance(value, list):
            self._build_list(value)
            return
        elif isinstance(value, str) and ("\n" in value or self.spec.get("type") == "text_edit" or len(value) > 72):
            self.control = TextEdit()
            self.control.setPlainText(value)
            lines = max(2, min(8, len(value.splitlines()) + len(value) // 65))
            self.control.setMinimumHeight(30 + lines * 22)
            self.control.textChanged.connect(lambda: self.commit(self.control.toPlainText()))
        else:
            self.control = LineEdit()
            self.control.setPlaceholderText(translated(self.spec.get("placeholder", "")))
            self.control.setText(str(value) if value is not None else "")
            self.control.setCursorPosition(0)
            if isinstance(value, int):
                self.control.editingFinished.connect(self._commit_large_integer)
            else:
                self.control.textChanged.connect(self.commit)
        self.control.setMinimumWidth(180)
        self.control.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed if not isinstance(self.control, TextEdit)
                                   else QSizePolicy.Preferred)
        self.control.setToolTip(str(value))
        self.layout.addWidget(self.control)

    def _commit_large_integer(self):
        try:
            self.commit(int(self.control.text()))
            self.control.setToolTip(self.control.text())
        except ValueError:
            self.control.setText(str(self.value))

    def _update_mapping(self, key, value):
        updated = deepcopy(self.value)
        updated[key] = value
        self.commit(updated)

    def _build_list(self, values):
        self.list_widget = ListWidget()
        self.list_widget.setWordWrap(True)
        self.list_widget.setMinimumHeight(70)
        self.list_widget.setMaximumHeight(190)
        self.list_widget.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
        self.layout.addWidget(self.list_widget)
        self.list_count = CaptionLabel()
        self.layout.addWidget(self.list_count)
        bar = QHBoxLayout()
        options = self.spec.get("options_available", self.spec.get("options"))
        if options is not None:
            self.list_input = ComboBox()
        elif values and all(isinstance(item, bool) for item in values):
            self.list_input = CheckBox("开启")
        elif values and all(type(item) is int and -2147483648 <= item <= 2147483647 for item in values):
            self.list_input = NumberSpinBox()
            self.list_input.setRange(-2147483648, 2147483647)
        elif values and all(type(item) in (int, float) for item in values):
            self.list_input = NumberDoubleSpinBox()
            self.list_input.setDecimals(8)
            self.list_input.setRange(-99999999, 99999999)
        else:
            self.list_input = LineEdit()
        if options is not None:
            for item in options:
                self.list_input.addItem(translated(item), userData=item)
        elif isinstance(self.list_input, LineEdit):
            self.list_input.setPlaceholderText("输入新项目后添加")
        bar.addWidget(self.list_input, 1)
        self.add_button = PushButton(FluentIcon.ADD, "添加")
        self.add_button.clicked.connect(self.add_list_item)
        bar.addWidget(self.add_button)
        self.layout.addLayout(bar)
        actions = QHBoxLayout()
        self.remove_button = PushButton("移除选中")
        self.remove_button.clicked.connect(self.remove_list_item)
        self.up_button, self.down_button = PushButton("上移"), PushButton("下移")
        self.up_button.clicked.connect(lambda: self.move_list_item(-1))
        self.down_button.clicked.connect(lambda: self.move_list_item(1))
        actions.addWidget(self.remove_button)
        self.edit_button = PushButton("编辑选中")
        self.edit_button.clicked.connect(self.edit_list_item)
        actions.addWidget(self.edit_button)
        actions.addWidget(self.up_button)
        actions.addWidget(self.down_button)
        actions.addStretch(1)
        self.layout.addLayout(actions)
        self.list_widget.currentRowChanged.connect(self._select_list_item)
        self._fill_list()

    def _fill_list(self, selected=-1):
        self.list_widget.clear()
        for value in self.value:
            text = translated(value) if not isinstance(value, (list, dict)) else ("配置组" if isinstance(value, dict) else "列表")
            item = QListWidgetItem(text)
            item.setToolTip(str(value))
            self.list_widget.addItem(item)
        self.list_widget.setMaximumHeight(min(190, max(70, len(self.value) * 40 + 8)))
        self.list_count.setText(f"当前 {len(self.value)} 项 · 顺序与配置一致")
        self.list_widget.setCurrentRow(selected)
        self._select_list_item(selected)

    def _select_list_item(self, index):
        self.remove_button.setEnabled(index >= 0)
        self.edit_button.setEnabled(index >= 0)
        self.up_button.setEnabled(index > 0)
        self.down_button.setEnabled(0 <= index < len(self.value) - 1)

    def edit_list_item(self):
        index = self.list_widget.currentRow()
        if index < 0:
            return
        value = self._edit_value(self.value[index])
        if value is None:
            return
        if not self.spec.get("allow_duplication", False) and value in self.value[:index] + self.value[index + 1:]:
            self.list_count.setText("此项目已在列表中")
            return
        updated = deepcopy(self.value)
        updated[index] = value
        self.commit(updated)
        self._fill_list(index)

    def _edit_value(self, value):
        dialog = MessageBoxBase(self.window())
        dialog.viewLayout.addWidget(BodyLabel("编辑列表项目"))
        options = self.spec.get("options_available", self.spec.get("options"))
        editor = ValueEditor(value, {"options": options} if options is not None else {})
        dialog.viewLayout.addWidget(editor)
        dialog.yesButton.setText("保存")
        dialog.cancelButton.setText("取消")
        dialog.widget.setMinimumWidth(480)
        if dialog.exec():
            return editor.value
        return None

    def add_list_item(self):
        if isinstance(self.list_input, ComboBox):
            value = self.list_input.currentData()
            if self.list_input.currentIndex() < 0:
                return
        elif isinstance(self.list_input, (NumberSpinBox, NumberDoubleSpinBox)):
            value = self.list_input.value()
        elif isinstance(self.list_input, CheckBox):
            value = self.list_input.isChecked()
        elif self.value and isinstance(self.value[0], (dict, list)):
            value = self._edit_value(self.value[0])
            if value is None:
                return
        else:
            value = self.list_input.text().strip()
            if not value:
                return
            if self.value and all(isinstance(item, (int, float)) and not isinstance(item, bool) for item in self.value):
                try:
                    value = float(value) if any(isinstance(item, float) for item in self.value) else int(value)
                except ValueError:
                    self.list_count.setText("请输入有效数字")
                    return
        if not self.spec.get("allow_duplication", False) and value in self.value:
            self.list_count.setText("此项目已在列表中")
            return
        self.commit(self.value + [value])
        self._fill_list(len(self.value) - 1)
        if isinstance(self.list_input, LineEdit):
            self.list_input.clear()

    def remove_list_item(self):
        index = self.list_widget.currentRow()
        if index < 0:
            return
        updated = deepcopy(self.value)
        updated.pop(index)
        self.commit(updated)
        self._fill_list(min(index, len(updated) - 1))

    def move_list_item(self, delta):
        index = self.list_widget.currentRow()
        target = index + delta
        if not (0 <= index < len(self.value) and 0 <= target < len(self.value)):
            return
        updated = deepcopy(self.value)
        updated[index], updated[target] = updated[target], updated[index]
        self.commit(updated)
        self._fill_list(target)


class GraphicalConfigRow(ConfigLabelAndWidget):
    def __init__(self, descriptions, config, key, spec=None, hotkey=False):
        super().__init__(descriptions, config, key)
        self.spec, self.hotkey = spec or {}, hotkey
        self.editor = None
        self.update_value()

    def update_value(self):
        value = self.config.get(self.key)
        if self.editor is not None:
            if self.hotkey:
                blocker = QSignalBlocker(self.combo_box)
                self.combo_box.setText(str(value))
                del blocker
            else:
                self.editor.set_value(value)
            return
        if self.hotkey:
            self.editor = QWidget()
            layout = QVBoxLayout(self.editor)
            layout.setContentsMargins(0, 0, 0, 0)
            self.combo_box = EditableComboBox()
            choices = [*list("abcdefghijklmnopqrstuvwxyz"), *list("0123456789"),
                       *[f"f{i}" for i in range(1, 13)], "space", "tab", "esc", "enter", "lshift", "lctrl", "alt"]
            if self.key == "Start/Stop":
                choices += ["None", "ctrl+f9", "ctrl+alt+f9", "ctrl+shift+f9"]
            if value not in choices:
                choices.insert(0, value)
            self.combo_box.addItems(choices)
            self.combo_box.setText(str(value))
            self.combo_box.currentTextChanged.connect(lambda text: self.update_config(text))
            layout.addWidget(self.combo_box)
        else:
            self.editor = ValueEditor(value, self.spec)
            self.editor.changed.connect(self.update_config)
            if isinstance(self.editor.control, ComboBox):
                self.combo_box = self.editor.control  # Preserve conditional field wiring.
            elif isinstance(self.editor.control, (NumberSpinBox, NumberDoubleSpinBox)):
                self.spin_box = self.editor.control
        self.editor.setMinimumWidth(220)
        self.editor.setMaximumWidth(560)
        self.add_widget(self.editor, stretch=1)


def _row_height(row):
    return max(56, row.layout.minimumSize().height(), row.layout.heightForWidth(max(200, row.width())))


def _refresh_row(row):
    if not hasattr(row, "title_layout"):
        return
    stacked = row.width() < 700
    row.layout.setDirection(QBoxLayout.TopToBottom if stacked else QBoxLayout.LeftToRight)
    row.layout.setStretch(0, 1 if not stacked else 0)
    row.layout.setStretch(1, 0)
    height = _row_height(row)
    if row.minimumHeight() != height:
        row.setMinimumHeight(height)
        parent = row.parentWidget()
        while parent is not None:
            if hasattr(parent, "_adjustViewSize"):
                QTimer.singleShot(0, parent, parent._adjustViewSize)
                break
            parent = parent.parentWidget()


def install_config_ui():
    install_number_layout_fix()
    factory = importlib.import_module("ok.ui.qt.tasks.ConfigItemFactory")
    if getattr(factory, "_okww_graphical_config", False):
        return
    from ok.ui.qt.tasks.LabelAndWidget import LabelAndWidget
    original_init, original_resize = LabelAndWidget.__init__, LabelAndWidget.resizeEvent
    original_size_hint = LabelAndWidget.sizeHint

    def row_init(self, *args, **kwargs):
        original_init(self, *args, **kwargs)
        self.title.setWordWrap(True)
        self.title.setMinimumWidth(0)
        self.title.setSizePolicy(QSizePolicy.Ignored, QSizePolicy.Preferred)
        if hasattr(self, "contentLabel"):
            self.contentLabel.setMinimumWidth(0)
            self.contentLabel.setSizePolicy(QSizePolicy.Ignored, QSizePolicy.Preferred)
        QTimer.singleShot(0, self, lambda: _refresh_row(self))

    def row_resize(self, event):
        original_resize(self, event)
        _refresh_row(self)

    def row_size_hint(self):
        hint = original_size_hint(self)
        if hasattr(self, "title_layout"):
            # ExpandSettingCard sizes itself from these hints. A wrapping label
            # with an ignored width otherwise contributes a one-character-wide
            # height and leaves a large empty area beneath the visible rows.
            hint.setHeight(_row_height(self))
        return hint

    LabelAndWidget.__init__, LabelAndWidget.resizeEvent = row_init, row_resize
    LabelAndWidget.sizeHint = row_size_hint
    original_factory = factory.config_widget

    def config_widget(config_type, descriptions, config, key, value, task):
        spec = (config_type or {}).get(key) or {}
        spec = spec if isinstance(spec, dict) else {}
        current = config.get(key)
        default = config.get_default(key)
        resolved = factory._resolve_type(spec, default)
        config_name = Path(getattr(config, "config_file", "")).stem
        hotkey = config_name == "Game Hotkey" or config_name == "Basic Options" and key == "Start/Stop"
        if hotkey and isinstance(current, str):
            return GraphicalConfigRow(descriptions, config, key, spec, hotkey=True)
        if resolved in {"global", "button", "multi_selection", "file_selector"} or isinstance(current, bool):
            row = original_factory(config_type, descriptions, config, key, value, task)
            if hasattr(row, "value_label"):
                row.value_label.setWordWrap(True)
                row.value_label.setMinimumWidth(0)
                row.value_label.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
            return row
        if isinstance(current, (int, float, str, list, dict)):
            return GraphicalConfigRow(descriptions, config, key, spec)
        return original_factory(config_type, descriptions, config, key, value, task)

    factory.config_widget = config_widget
    importlib.import_module("ok.ui.qt.tasks.ConfigCard").config_widget = config_widget
    factory._okww_graphical_config = True
