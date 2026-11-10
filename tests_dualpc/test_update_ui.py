import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from pathlib import Path
from types import SimpleNamespace

import pytest
from PySide6.QtWidgets import QApplication

from dualpc.bootstrap import make_config
from dualpc.settings import Settings
from dualpc.update_ui import CandidateUpdateCard, install_about_update_hook, json_messages


@pytest.fixture(scope="module")
def qt_app():
    return QApplication.instance() or QApplication([])


def test_update_output_parser_accepts_logs_and_multiple_json_results():
    messages = json_messages('download {unfinished\n{"same_commit": false, "available": "abc"}\n'
                             '{"candidate": "C:/candidate", "status": "static checks passed"}')
    assert len(messages) == 2
    assert messages[-1]["candidate"] == "C:/candidate"


def test_source_about_page_does_not_schedule_the_incompatible_installer(qt_app, monkeypatch):
    from ok.ui.qt.about.AboutTab import AboutTab
    monkeypatch.setattr(AboutTab, "__init__", AboutTab.__init__)
    install_about_update_hook()
    installed = AboutTab.__init__
    install_about_update_hook()
    assert AboutTab.__init__ is installed
    def forbidden(*args, **kwargs):
        raise AssertionError("Native installer update must not run for this adapter")
    root = Path(__file__).absolute().parents[1]
    tab = AboutTab(make_config(Settings(), root), SimpleNamespace(get_version_list=forbidden))
    assert tab.update_card is None
    assert isinstance(tab.candidate_update_card, CandidateUpdateCard)
    assert tab.candidate_update_card.process is None
    tab.check_for_updates()
    tab.close()
    tab.deleteLater()
    qt_app.processEvents()


def test_update_failure_is_visible_and_controls_can_be_retried(qt_app):
    from PySide6.QtCore import QProcess
    card = CandidateUpdateCard()
    card.process = QProcess(card)
    card.output = '{"status":"failed","error":"GitHub update check exceeded 60 seconds"}'
    card.finish(1)
    assert "60 seconds" in card.status.text()
    assert card.check_button.isEnabled() and card.prepare_button.isEnabled()
    assert card.process is None
    card.deleteLater()
    qt_app.processEvents()
