"""Source-aware update controls that preserve the external-device adapter."""
from functools import wraps
import json
from pathlib import Path
import sys

from PySide6.QtCore import QProcess, QProcessEnvironment, Qt
from PySide6.QtWidgets import QHBoxLayout, QVBoxLayout, QWidget
from qfluentwidgets import BodyLabel, PushButton

ROOT = Path(__file__).absolute().parents[1]


def json_messages(output):
    decoder = json.JSONDecoder()
    messages = []
    position = 0
    while (position := output.find("{", position)) >= 0:
        try:
            message, end = decoder.raw_decode(output[position:])
        except ValueError:
            position += 1
            continue
        if isinstance(message, dict):
            messages.append(message)
        position += end
    return messages


class CandidateUpdateCard(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.process = None
        self.output = ""
        self.preparing = False
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        description = BodyLabel("检查增强版上游更新，生成保留双机适配与设置的独立候选目录。候选通过验证后再使用。", self)
        description.setWordWrap(True)
        layout.addWidget(description)
        buttons = QHBoxLayout()
        self.check_button = PushButton("检查上游更新", self)
        self.prepare_button = PushButton("生成更新候选", self)
        buttons.addWidget(self.check_button)
        buttons.addWidget(self.prepare_button)
        buttons.addStretch(1)
        layout.addLayout(buttons)
        self.status = BodyLabel("更新保留当前版本，可随时回退。", self)
        self.status.setWordWrap(True)
        self.status.setTextInteractionFlags(Qt.TextSelectableByMouse)
        layout.addWidget(self.status)
        self.check_button.clicked.connect(lambda: self.begin(False))
        self.prepare_button.clicked.connect(lambda: self.begin(True))

    def begin(self, prepare):
        if self.process is not None:
            return
        self.preparing = prepare
        self.output = ""
        self.check_button.setEnabled(False)
        self.prepare_button.setEnabled(False)
        self.status.setText("正在生成独立更新候选…" if prepare else "正在检查上游更新…")
        process = self.process = QProcess(self)
        environment = QProcessEnvironment.systemEnvironment()
        environment.insert("PYTHONIOENCODING", "utf-8")
        process.setProcessEnvironment(environment)
        process.setWorkingDirectory(str(ROOT))
        process.setProcessChannelMode(QProcess.MergedChannels)
        process.readyReadStandardOutput.connect(self.read_output)
        process.finished.connect(self.finish)
        process.errorOccurred.connect(lambda error: self.finish(-1) if error == QProcess.FailedToStart else None)
        arguments = [str(ROOT / "tools_dualpc/prepare_update.py")]
        if prepare:
            arguments.append("--prepare")
        process.start(sys.executable, arguments)

    def read_output(self):
        if self.process is not None:
            self.output = (self.output + bytes(self.process.readAllStandardOutput()).decode("utf-8", errors="replace"))[-100000:]

    def finish(self, exit_code, *unused):
        if self.process is None:
            return
        self.read_output()
        self.process.deleteLater()
        self.process = None
        self.check_button.setEnabled(True)
        self.prepare_button.setEnabled(True)
        messages = json_messages(self.output)
        if exit_code != 0:
            error = next((m["error"] for m in reversed(messages) if m.get("error")), None)
            self.status.setText("更新检查失败：" + str(error or self.output[-1500:] or "无法启动更新工具"))
        elif self.preparing:
            result = next((m for m in reversed(messages) if m.get("candidate")), None)
            self.status.setText("候选已生成，仍需运行与硬件验证：\n" + result["candidate"] if result
                                else "工具已结束，但未返回可验证的候选目录，请查看 updates-dualpc。")
        else:
            result = next((m for m in reversed(messages) if "same_commit" in m), None)
            self.status.setText(("当前已是相同的上游提交。" if result["same_commit"] else
                                 "发现上游更新：" + str(result["available"])[:12] + "，可生成独立更新候选。")
                                if result else "工具未返回有效版本信息，请重试。")


def install_about_update_hook():
    from ok.ui.qt.about.AboutTab import AboutTab
    original = AboutTab.__init__
    if getattr(original, "_dualpc_candidate_updates", False):
        return

    @wraps(original)
    def init(tab, config, pyappify_module=None, exit_event=None):
        if "dualpc_settings" not in config:
            return original(tab, config, pyappify_module, exit_event)
        if pyappify_module is None:
            import pyappify as pyappify_module
        class SourceRuntime:
            get_version_list = None
            def __getattr__(self, name):
                return getattr(pyappify_module, name)
        original(tab, config, SourceRuntime(), exit_event)
        tab.candidate_update_card = CandidateUpdateCard(tab)
        card = tab.add_card("双机版本更新", tab.candidate_update_card)
        tab.vBoxLayout.removeWidget(card)
        tab.vBoxLayout.insertWidget(1, card)

    init._dualpc_candidate_updates = True
    AboutTab.__init__ = init
