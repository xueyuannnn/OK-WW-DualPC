import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
from copy import deepcopy
from types import SimpleNamespace
import sys

import pytest
from PySide6.QtCore import Qt, QTimer
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication, QVBoxLayout, QWidget, QBoxLayout
from qfluentwidgets import SpinBox, DoubleSpinBox, CompactSpinBox

from ok import og
from ok.util.config import Config
from dualpc.config_ui import install_config_ui, ValueEditor, GraphicalConfigRow


@pytest.fixture
def form_env(monkeypatch, tmp_path):
    app = QApplication.instance() or QApplication([])
    monkeypatch.setattr(og, "app", SimpleNamespace(tr=str))
    monkeypatch.setattr(og, "config", {})
    monkeypatch.setattr(Config, "config_folder", str(tmp_path))
    errors = []
    monkeypatch.setattr(sys, "excepthook", lambda kind, error, traceback: errors.append(error))
    install_config_ui()
    widgets = []
    yield app, widgets
    for widget in widgets:
        widget.close()
        widget.deleteLater()
    app.processEvents()
    assert not errors, f"Unhandled Qt callbacks: {errors}"


def row_for(config, key, spec=None):
    from ok.ui.qt.tasks.ConfigItemFactory import config_widget
    return config_widget({key: spec} if spec else {}, {}, config, key, config[key], None)


def test_numeric_editors_across_pages_keep_current_values_visible(form_env):
    app, widgets = form_env
    host = QWidget()
    widgets.append(host)
    layout = QVBoxLayout(host)
    editors = [SpinBox(), DoubleSpinBox(), CompactSpinBox()]
    for editor in editors:
        editor.setRange(0, 999999)
        editor.setValue(12345)
        editor.setFixedWidth(220)
        layout.addWidget(editor)
    host.show()
    app.processEvents()
    for editor in editors:
        assert editor.lineEdit().width() >= editor.fontMetrics().horizontalAdvance(editor.text()) + 4
    editors[0].lineEdit().setFocus()
    QTest.keyClick(editors[0].lineEdit(), Qt.Key_A, Qt.ControlModifier)
    QTest.keyClicks(editors[0].lineEdit(), "23456")
    QTest.keyClick(editors[0].lineEdit(), Qt.Key_Return)
    assert editors[0].value() == 23456


def test_opening_and_refreshing_forms_preserves_numbers_and_config_file(form_env):
    app, widgets = form_env
    config = Config("Numbers", {"Count": 12345678, "Rate": 12345.12345678})
    from pathlib import Path
    path = Path(config.config_file)
    before = path.read_bytes()
    rows = [row_for(config, key) for key in config]
    widgets.extend(rows)
    assert rows[0].spin_box.value() == 12345678
    assert rows[1].spin_box.value() == pytest.approx(12345.12345678, abs=1e-8)
    for row in rows:
        row.update_value()
    assert path.read_bytes() == before


def test_decimal_display_keeps_precision_without_trailing_zeroes(form_env):
    app, widgets = form_env
    editor = ValueEditor(13.8)
    widgets.append(editor)
    assert editor.control.text() == "13.8"
    editor.control.setValue(0.12345678)
    assert editor.value == pytest.approx(0.12345678, abs=1e-8)
    assert editor.control.text() == "0.12345678"


def test_saved_unknown_option_stays_visible_and_conditional_controls_survive_reset(form_env):
    app, widgets = form_env
    config = Config("Choices", {"Mode": "A", "Count": 5, "Rate": 1.5})
    spec = {"Mode": {"options": ["A", "B"], "sub_configs": {"A": ["Count"], "B": ["Rate"]}}}
    from ok.ui.qt.tasks.ConfigCard import ConfigCard
    from qfluentwidgets import FluentIcon
    card = ConfigCard(None, "Choices", config, "", config.default, {}, spec, FluentIcon.SETTING)
    widgets.append(card)
    combo = card.config_widget_by_key["Mode"].combo_box
    combo.setCurrentIndex(1)
    assert config["Mode"] == "B"
    assert card.config_widget_by_key["Count"].isHidden()
    config["Mode"] = "A"
    card.update_config()
    assert card.config_widget_by_key["Mode"].combo_box is combo
    combo.setCurrentIndex(1)
    assert card.config_widget_by_key["Count"].isHidden()
    assert not card.config_widget_by_key["Rate"].isHidden()
    config["Mode"] = "Saved Legacy Choice"
    card.update_config()
    assert combo.currentData() == "Saved Legacy Choice" and "Saved Legacy" in combo.currentText()


def test_graphical_list_add_remove_reorder_and_preserve_old_selection(form_env):
    app, widgets = form_env
    config = Config("Lists", {"Items": ["Legacy", "A"]})
    row = row_for(config, "Items", {"options_available": ["A", "B"]})
    widgets.append(row)
    editor = row.editor
    assert config["Items"] == ["Legacy", "A"]
    editor.list_input.setCurrentIndex(1)
    editor.add_button.click()
    assert config["Items"] == ["Legacy", "A", "B"]
    editor.up_button.click()
    assert config["Items"] == ["Legacy", "B", "A"]
    editor.remove_button.click()
    assert config["Items"] == ["Legacy", "A"]
    editor.list_input.setCurrentIndex(0)
    editor.add_button.click()
    assert config["Items"] == ["Legacy", "A"]


@pytest.mark.parametrize("values,new", [([1, 2], 3), ([0.5], 0.75), ([True], False)])
def test_list_add_uses_typed_numeric_and_boolean_controls(form_env, values, new):
    app, widgets = form_env
    editor = ValueEditor(values)
    widgets.append(editor)
    if isinstance(new, bool):
        editor.list_input.setChecked(new)
    else:
        editor.list_input.setValue(new)
    editor.add_button.click()
    assert editor.value == values + [new]
    assert type(editor.value[-1]) is type(new)


def test_nested_mapping_keeps_value_types(form_env):
    app, widgets = form_env
    editor = ValueEditor({"Enabled": True, "Count": 5, "Ratio": 0.125})
    widgets.append(editor)
    editor.children_by_key["Count"].control.setValue(8)
    assert editor.value == {"Enabled": True, "Count": 8, "Ratio": 0.125}
    editor.children_by_key["Enabled"].control.setChecked(False)
    assert editor.value["Enabled"] is False


@pytest.mark.parametrize("save", [True, False])
def test_list_edit_dialog_only_commits_after_save(form_env, save):
    from qfluentwidgets import MessageBoxBase
    app, widgets = form_env
    host = QWidget()
    host.resize(900, 650)
    widgets.append(host)
    layout = QVBoxLayout(host)
    editor = ValueEditor([{"Enabled": True, "Count": 5}])
    layout.addWidget(editor)
    host.show()
    app.processEvents()
    editor.list_widget.setCurrentRow(0)

    def complete_dialog():
        dialog = next(widget for widget in host.findChildren(MessageBoxBase) if widget.isVisible())
        nested = next(widget for widget in dialog.findChildren(ValueEditor) if widget.children_by_key)
        nested.children_by_key["Count"].control.setValue(9)
        assert editor.value[0]["Count"] == 5
        (dialog.yesButton if save else dialog.cancelButton).click()

    QTimer.singleShot(100, complete_dialog)
    editor.edit_list_item()
    assert editor.value == [{"Enabled": True, "Count": 9 if save else 5}]
    assert isinstance(editor.value[0]["Count"], int)


def test_long_titles_descriptions_and_values_fit_narrow_rows(form_env):
    app, widgets = form_env
    config = Config("Wrap", {"很长的配置标题" * 5: "当前值和长文本内容" * 15})
    key = next(iter(config))
    row = GraphicalConfigRow({key: "完整参数说明" * 15}, config, key)
    widgets.append(row)
    row.resize(580, 400)
    row.show()
    app.processEvents()
    assert row.layout.direction() == QBoxLayout.TopToBottom
    assert row.editor.width() <= row.width()
    assert row.editor.control.toPlainText() == config[key]
    assert row.title.wordWrap() and row.contentLabel.wordWrap()


def test_expanded_card_height_matches_visible_rows(form_env):
    from qfluentwidgets import FluentIcon
    from ok.ui.qt.tasks.ConfigCard import ConfigCard
    app, widgets = form_env
    config = Config("Card height", {"A long numeric parameter title": 12345,
                                    "Another long parameter title": "A"})
    descriptions = {key: "A detailed parameter explanation that can wrap naturally. " * 3 for key in config}
    host = QWidget()
    widgets.append(host)
    host.resize(1000, 800)
    layout = QVBoxLayout(host)
    card = ConfigCard(None, "Settings", config, "", config.default, descriptions, {}, FluentIcon.SETTING)
    layout.addWidget(card)
    layout.addStretch(1)
    host.show()
    card.setExpand(True)
    QTest.qWait(250)
    app.processEvents()
    for row in card.config_widgets:
        assert row.sizeHint().height() <= row.height()
    visible_height = sum(card.viewLayout.itemAt(i).widget().height()
                         for i in range(card.viewLayout.count()))
    assert visible_height <= card.height() <= visible_height + 100


def test_game_hotkeys_offer_a_dropdown_and_keep_saved_binding(form_env):
    app, widgets = form_env
    config = Config("Game Hotkey", {"Echo Key": "q"})
    row = row_for(config, "Echo Key")
    widgets.append(row)
    assert row.hotkey and row.combo_box.currentText() == "q"
    index = next(i for i in range(row.combo_box.count()) if row.combo_box.itemText(i) == "e")
    row.combo_box.setCurrentIndex(index)
    assert config["Echo Key"] == "e"
    combo = row.combo_box
    config["Echo Key"] = "tab"
    row.update_value()
    assert row.combo_box is combo and combo.currentText() == "tab"


def test_echo_rotation_controls_switch_without_duplicate_fields(form_env):
    from types import SimpleNamespace
    from qfluentwidgets import FluentIcon
    from src.task.FarmEchoTask2 import FarmEchoTask2
    from ok.ui.qt.tasks.ConfigCard import ConfigCard
    from dualpc.echo_rotation import CYCLE, SINGLE, WEEKLY
    app, widgets = form_env
    task = FarmEchoTask2(SimpleNamespace(scene=None, text_fix={}, global_config=SimpleNamespace(get_config=lambda name: {})), None)
    config = Config("Rotation form", task.default_config)
    card = ConfigCard(None, "Rotation", config, "", config.default, task.config_description, task.config_type, FluentIcon.SETTING)
    widgets.append(card)
    assert len(card.config_keys) == len(set(card.config_keys))
    assert card.config_widget_by_key["Cycle Scope"].isHidden()
    assert card.config_widget_by_key["Cycle Start Index"].isHidden()
    assert not card.config_widget_by_key["Teleport to Boss"].isHidden()
    mode = card.config_widget_by_key["Farm Mode"].combo_box
    mode.setCurrentIndex(next(i for i in range(mode.count()) if mode.itemData(i) == CYCLE))
    assert not card.config_widget_by_key["Rounds per Dungeon"].isHidden()
    assert not card.config_widget_by_key["Cycle Start Category"].isHidden()
    assert not card.config_widget_by_key["Cycle Start Index"].isHidden()
    scope = card.config_widget_by_key["Cycle Scope"].combo_box
    scope.setCurrentIndex(next(i for i in range(scope.count()) if scope.itemData(i) == WEEKLY))
    assert card.config_widget_by_key["Cycle Start Category"].isHidden()
    assert not card.config_widget_by_key["Cycle Start Index"].isHidden()
    card.config_widget_by_key["Cycle Start Index"].editor.control.setValue(3)
    assert config["Cycle Start Index"] == 3
    assert not card.config_widget_by_key["Boss Level"].isHidden()
    assert card.config_widget_by_key["Teleport to Boss"].isHidden()
    assert card.config_widget_by_key["Boss"].isHidden()
    mode.setCurrentIndex(next(i for i in range(mode.count()) if mode.itemData(i) == SINGLE))
    assert card.config_widget_by_key["Cycle Scope"].isHidden()
    assert card.config_widget_by_key["Cycle Start Index"].isHidden()


