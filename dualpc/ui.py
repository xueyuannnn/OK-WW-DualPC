"""Device-first dual-PC settings using the app's existing Fluent controls."""
from copy import deepcopy
from collections import deque
import json
from pathlib import Path
import sys
import time

from PySide6.QtCore import QProcess, QSignalBlocker, QTimer, Qt
from PySide6.QtGui import QImage, QPixmap
from PySide6.QtWidgets import QApplication, QGridLayout, QHBoxLayout, QLabel, QLayout, QSizePolicy, QVBoxLayout, QWidget
from qfluentwidgets import (BodyLabel, CaptionLabel, CardWidget, CheckBox, ComboBox, DoubleSpinBox,
                            FluentIcon, LineEdit, PrimaryPushButton, PushButton,
                            SpinBox, StrongBodyLabel, SubtitleLabel, TransparentPushButton)

from ok import og
from ok.ui.qt.widget.CustomTab import CustomTab
from .qt_controls import NumberSpinBox, NumberDoubleSpinBox, install_parameter_wheel_guard
from .preview_window import FloatingPreview, PreviewImage, PreviewRateCombo, load_preview_options, PREVIEW_RATES


PROTOCOL_LABELS = {"kmbox_net": "KMBox Net · 网口"}
BACKEND_LABELS = [("DirectShow（推荐）", "DSHOW"), ("Media Foundation", "MSMF"), ("FFmpeg · DirectShow", "FFMPEG")]
FORMAT_LABELS = {"RGB3": "RGB24", "RGB4": "RGB32", "MJPG": "MJPEG"}


def _fill(combo, items, current=None):
    blocker = QSignalBlocker(combo)
    combo.clear()
    selected = 0
    for index, (label, value) in enumerate(items):
        combo.addItem(label, userData=value)
        if value == current:
            selected = index
    if items:
        combo.setCurrentIndex(selected)
    del blocker


def _caption(text, parent=None):
    label = CaptionLabel(text, parent)
    label.setWordWrap(True)
    return label


class Disclosure(QWidget):
    def __init__(self, title, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(10)
        self.button = TransparentPushButton(FluentIcon.CHEVRON_RIGHT, title, self)
        self.button.setCheckable(True)
        self.button.setSizePolicy(QSizePolicy.Maximum, QSizePolicy.Fixed)
        self.body = QWidget(self)
        self.content = QVBoxLayout(self.body)
        self.content.setContentsMargins(0, 0, 0, 0)
        self.content.setSpacing(12)
        self.body.hide()
        self.button.toggled.connect(self._toggle)
        layout.addWidget(self.button)
        layout.addWidget(self.body)

    def _toggle(self, checked):
        self.body.setVisible(checked)
        self.button.setIcon(FluentIcon.CHEVRON_DOWN_MED if checked else FluentIcon.CHEVRON_RIGHT)


class DualPCTab(CustomTab):
    icon = FluentIcon.CONNECT

    @property
    def name(self):
        return "双机设置"

    @property
    def add_after_default_tabs(self):
        # Keep Capture first, then device setup, followed by the original task tabs.
        return False

    def __init__(self, auto_discover=True):
        super().__init__()
        install_parameter_wheel_guard()
        self.vBoxLayout.setSizeConstraint(QLayout.SetMinimumSize)
        self.saved_settings = deepcopy(og.config["dualpc_settings"])
        self.controls = {}
        self.preview_enabled = False
        self._preview_key = None
        self._preview_times = deque(maxlen=120)
        self.preview_frames_presented = 0
        self.preview_window = None
        self._preview_options_path = Path(og.config["dualpc_settings_path"]).parent / "configs-dualpc/preview.json"
        self._preview_options = load_preview_options(self._preview_options_path)
        self.preview_target_fps = self._preview_options["fps"]
        self._session_notice = None
        self._test_notice = None
        self._modes = []
        self._devices = []
        self._processes = {}
        self._generations = {"devices": 0, "modes": 0}
        self._building = True
        self._build_header()
        self._build_status()
        self._build_capture()
        self._build_controller()
        self._build_connection()
        self._building = False
        self._update_protocol()
        self._update_backend()
        self._watch_changes()
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.tick)
        self.timer.start(250)
        self.preview_timer = QTimer(self)
        self.preview_timer.setTimerType(Qt.PreciseTimer)
        self.preview_timer.setInterval(self._preview_interval())
        self.preview_timer.timeout.connect(self.render_preview)
        QApplication.instance().aboutToQuit.connect(self._close_probes)
        if auto_discover:
            QTimer.singleShot(200, self.list_devices)

    def _card(self, title, subtitle):
        card = CardWidget(self.view)
        card.setBorderRadius(10)
        layout = QVBoxLayout(card)
        layout.setContentsMargins(20, 16, 20, 16)
        layout.setSpacing(14)
        header = QVBoxLayout()
        header.setSpacing(4)
        header.addWidget(StrongBodyLabel(title, card))
        header.addWidget(_caption(subtitle, card))
        layout.addLayout(header)
        self.add_widget(card)
        return card, layout

    def _row(self, label, control):
        row = QWidget()
        layout = QHBoxLayout(row)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(16)
        text = BodyLabel(label, row)
        text.setFixedWidth(108)
        layout.addWidget(text)
        layout.addWidget(control, 1)
        return row

    def _columns(self, fields):
        widget = QWidget()
        grid = QGridLayout(widget)
        grid.setContentsMargins(0, 0, 0, 0)
        grid.setHorizontalSpacing(14)
        grid.setVerticalSpacing(6)
        for column, (label, control) in enumerate(fields):
            grid.addWidget(_caption(label, widget), 0, column)
            grid.addWidget(control, 1, column)
            grid.setColumnStretch(column, 1)
        return widget

    def _value(self, key):
        group, field = key.split(".")
        return getattr(getattr(self.saved_settings, group), field)

    def _text(self, key, placeholder=""):
        widget = LineEdit(self)
        widget.setText(str(self._value(key)))
        widget.setPlaceholderText(placeholder)
        widget.setClearButtonEnabled(True)
        self.controls[key] = widget
        return widget

    def _spin(self, key, low, high, decimals=None):
        widget = NumberDoubleSpinBox(self) if decimals is not None else NumberSpinBox(self)
        widget.setRange(low, high)
        if decimals is not None:
            widget.setDecimals(decimals)
        widget.setValue(self._value(key))
        self.controls[key] = widget
        return widget

    def _select(self, key, items):
        widget = ComboBox(self)
        _fill(widget, items, self._value(key))
        self.controls[key] = widget
        return widget

    def _build_header(self):
        header = QWidget()
        layout = QHBoxLayout(header)
        layout.setContentsMargins(0, 0, 0, 0)
        labels = QVBoxLayout()
        labels.setSpacing(4)
        labels.addWidget(SubtitleLabel("双机设置", header))
        labels.addWidget(_caption("选择设备，启动连接线程，实时查看采集与盒子状态。", header))
        layout.addLayout(labels, 1)
        self.scan_button = PushButton(FluentIcon.SYNC, "刷新设备", header)
        self.scan_button.clicked.connect(self.list_devices)
        self.save_button = PrimaryPushButton(FluentIcon.SAVE, "保存设置", header)
        self.save_button.clicked.connect(self.save)
        layout.addWidget(self.scan_button)
        layout.addWidget(self.save_button)
        self.add_widget(header)
        self.message = _caption("启动线程会应用当前填写的配置；保存设置用于下次启动。")
        self.message.setTextInteractionFlags(Qt.TextSelectableByMouse)
        self.add_widget(self.message)

    def _build_status(self):
        self.status_card = CardWidget(self.view)
        self.status_card.setBorderRadius(10)
        layout = QVBoxLayout(self.status_card)
        layout.setContentsMargins(20, 14, 20, 14)
        layout.setSpacing(8)
        actions = QHBoxLayout()
        self.session_state = StrongBodyLabel("连接线程 · 未启动")
        self.start_thread_button = PrimaryPushButton(FluentIcon.PLAY, "启动线程", self)
        self.start_thread_button.clicked.connect(self.start_connection)
        self.stop_thread_button = PushButton(FluentIcon.PAUSE, "终止线程", self)
        self.stop_thread_button.setEnabled(False)
        self.stop_thread_button.clicked.connect(self.stop_connection)
        actions.addWidget(self.session_state, 1)
        actions.addWidget(self.start_thread_button)
        actions.addWidget(self.stop_thread_button)
        layout.addLayout(actions)
        grid = QGridLayout()
        grid.setHorizontalSpacing(16)
        self.capture_state = StrongBodyLabel("等待连接")
        self.controller_state = StrongBodyLabel("演练模式")
        self.controller_state.setWordWrap(True)
        self.actual_state = StrongBodyLabel("—")
        self.latency_state = StrongBodyLabel("—")
        for column, (label, value) in enumerate((("视频采集", self.capture_state), ("键鼠控制", self.controller_state),
                                                 ("实际画面", self.actual_state), ("软件帧龄", self.latency_state))):
            grid.addWidget(_caption(label), 0, column)
            grid.addWidget(value, 1, column)
            grid.setColumnStretch(column, 1)
        layout.addLayout(grid)
        self.status = _caption("正在读取当前设备状态…")
        layout.addWidget(self.status)
        self.automation_state = _caption("自动任务状态 · 等待就绪")
        layout.addWidget(self.automation_state)
        self.add_widget(self.status_card)

    def _build_capture(self):
        c = self.saved_settings.capture
        self.capture_card, layout = self._card("01  视频采集", "选择接收游戏机 HDMI 画面的采集卡。刷新只读取设备信息。")
        self.video_device = ComboBox(self)
        _fill(self.video_device, [(f"{c.device_name or ('设备 #' + str(c.device_index))} · 等待检测", None)])
        layout.addWidget(self._row("采集设备", self.video_device))
        self.resolution, self.frame_rate, self.pixel_format = ComboBox(self), ComboBox(self), ComboBox(self)
        _fill(self.resolution, [(f"{c.width} × {c.height} · 当前", (c.width, c.height))], (c.width, c.height))
        _fill(self.frame_rate, [(f"{c.fps:g} FPS · 当前", c.fps)], c.fps)
        _fill(self.pixel_format, [("自动 · 驱动协商", "")] + ([(c.fourcc, c.fourcc)] if c.fourcc else []), c.fourcc)
        layout.addWidget(self._columns((("分辨率", self.resolution), ("帧率", self.frame_rate), ("图像格式", self.pixel_format))))
        self.mode_hint = _caption("正在检测可用设备；也可在高级选项中手动配置。")
        layout.addWidget(self.mode_hint)
        self.capture_advanced = Disclosure("高级采集选项")
        advanced = self.capture_advanced.content
        backend = self._select("capture.backend", BACKEND_LABELS)
        advanced.addWidget(self._row("采集方式", backend))
        self.ffmpeg_row = self._row("FFmpeg 路径", self._text("capture.ffmpeg_path", "ffmpeg 或完整可执行文件路径"))
        advanced.addWidget(self.ffmpeg_row)
        self.inference_resolution = ComboBox(self)
        sizes = list(dict.fromkeys([(1920, 1080), (1280, 720), (2560, 1440), (3840, 2160), (c.output_width, c.output_height)]))
        _fill(self.inference_resolution, [(f"{w} × {h}" + ("（推荐）" if w == 1920 else ""), (w, h)) for w, h in sizes],
              (c.output_width, c.output_height))
        advanced.addWidget(self._row("识别分辨率", self.inference_resolution))
        advanced.addWidget(_caption("识别画面保持 16:9。其他比例的输入需在配置文件设置裁剪。"))
        self.manual_capture = CheckBox("手动输入采集参数", self)
        advanced.addWidget(self.manual_capture)
        self.manual_panel = QWidget()
        manual = QVBoxLayout(self.manual_panel)
        manual.setContentsMargins(0, 0, 0, 0)
        manual.setSpacing(10)
        manual.addWidget(self._row("设备名称", self._text("capture.device_name", "完整名称或唯一匹配的名称")))
        manual.addWidget(self._row("设备编号", self._spin("capture.device_index", 0, 255)))
        manual.addWidget(self._columns((("宽度", self._spin("capture.width", 1, 16384)),
                                        ("高度", self._spin("capture.height", 1, 16384)))))
        manual.addWidget(self._columns((("请求帧率", self._spin("capture.fps", 1, 1000, 3)),
                                        ("格式 FourCC", self._text("capture.fourcc", "留空自动，如 YUY2")))))
        manual.addWidget(_caption("手动模式按填写值请求；驱动可能协商为其他模式。"))
        self.manual_panel.hide()
        advanced.addWidget(self.manual_panel)
        self._text("capture.device_path").hide()
        self.controls["capture.fourcc"].setMaxLength(4)
        self.manual_capture.toggled.connect(self._manual_toggled)
        self.video_device.currentIndexChanged.connect(self._video_changed)
        self.pixel_format.currentIndexChanged.connect(self._format_changed)
        self.resolution.currentIndexChanged.connect(self._resolution_changed)
        self.frame_rate.currentIndexChanged.connect(self._rate_changed)
        backend.currentIndexChanged.connect(self._backend_changed)
        layout.addWidget(self.capture_advanced)

    def _build_controller(self):
        b = self.saved_settings.kmbox
        self.controller_card, layout = self._card("02  键鼠控制", "填写 KMBox Net 的 IP、端口和 UUID，连接后可测试鼠标移动。")
        protocol = self._select("kmbox.protocol", [(label, key) for key, label in PROTOCOL_LABELS.items()])
        layout.addWidget(self._row("控制器类型", protocol))
        self.protocol_hint = _caption("")
        layout.addWidget(self.protocol_hint)
        self.network_panel = QWidget()
        network = QVBoxLayout(self.network_panel)
        network.setContentsMargins(0, 0, 0, 0)
        network.setSpacing(10)
        network.addWidget(self._columns((("IP 地址", self._text("kmbox.host", "例如 192.168.2.188")),
                                         ("端口", self._spin("kmbox.port", 0, 65535)),
                                         ("UUID · 8 位十六进制", self._text("kmbox.uuid", "盒子屏幕上的 UUID")))))
        self.controls["kmbox.uuid"].setMaxLength(8)
        self.controls["kmbox.port"].setSpecialValueText("未填写")
        layout.addWidget(self.network_panel)
        self.dry_run = CheckBox("演练模式", self)
        self.dry_run.setChecked(b.dry_run)
        test_row = QHBoxLayout()
        test_row.addWidget(self.dry_run, 1)
        self.test_move_button = PushButton(FluentIcon.SEND, "测试移动 ↘ 40 px", self)
        self.test_move_button.setToolTip("一次向右 40、向下 40 像素的校准目标；只移动，不点击。需先应用当前盒子配置并暂停自动任务。")
        self.test_move_button.clicked.connect(self.test_connection_move)
        self.test_move_button.setEnabled(False)
        test_row.addWidget(self.test_move_button)
        layout.addLayout(test_row)
        layout.addWidget(_caption("演练模式只记录指令，不连接实物。实际测试距离受游戏机鼠标速度、指针加速和下方倍率影响。"))
        self.calibration = Disclosure("鼠标定位与移动倍率")
        self.calibration.content.addWidget(self._columns((("游戏机桌面宽度", self._spin("kmbox.desktop_width", 1, 16384)),
                                                         ("游戏机桌面高度", self._spin("kmbox.desktop_height", 1, 16384)))))
        self.relative_calibration = self._columns((("横向移动倍率", self._spin("kmbox.counts_per_pixel_x", 0.01, 20, 4)),
                                                    ("纵向移动倍率", self._spin("kmbox.counts_per_pixel_y", 0.01, 20, 4))))
        for axis in ("x", "y"):
            self.controls[f"kmbox.counts_per_pixel_{axis}"].setToolTip("每个目标像素发送多少 HID 位移单位。例如倍率 1.5，测试 40 像素时发送 60 counts。实际距离需在游戏机上校准。")
        self.calibration.content.addWidget(self.relative_calibration)
        self.homing_wait_row = self._row("归零等待（秒）", self._spin("kmbox.home_wait", 0, 1, 3))
        self.controls["kmbox.home_wait"].setToolTip("相对鼠标每次菜单定位先归零，再等待游戏处理后移动到目标。默认 0.05 秒；界面切换后定位不稳时可适当增加。")
        self.calibration.content.addWidget(self.homing_wait_row)
        self.calibration_hint = _caption("")
        self.calibration.content.addWidget(self.calibration_hint)
        layout.addWidget(self.calibration)
        protocol.currentIndexChanged.connect(self._update_protocol)

    def _build_connection(self):
        self.preview_card, layout = self._card("03  画面预览与停止", "修改采集卡或盒子参数后，点击上方“应用配置”即可生效，无需重启软件。")
        self.preview_layout = layout
        layout.setSizeConstraint(QLayout.SetMinimumSize)
        layout.setAlignment(Qt.AlignTop)
        self.preview_card.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        actions = QHBoxLayout()
        self.preview_button = PushButton(FluentIcon.VIEW, "开启画面预览", self)
        self.preview_button.clicked.connect(self.toggle_preview)
        self.stop_button = PushButton(FluentIcon.CANCEL, "紧急停止键鼠", self)
        self.stop_button.setToolTip("立即禁止新输入，暂停任务并终止连接线程；重新启动线程后恢复")
        self.stop_button.clicked.connect(self.stop)
        actions.addWidget(self.preview_button)
        self.float_preview_button = PushButton(FluentIcon.LINK, "独立预览窗口", self)
        self.float_preview_button.clicked.connect(self.open_preview_window)
        actions.addWidget(self.float_preview_button)
        actions.addStretch(1)
        actions.addWidget(self.stop_button)
        layout.addLayout(actions)
        self.preview_fps = PreviewRateCombo(self.preview_target_fps, self)
        self.preview_size = ComboBox(self)
        _fill(self.preview_size, [("小 · 高 270", 270), ("中 · 高 405", 405), ("大 · 高 540", 540),
                                  ("特大 · 高 720", 720)], self._preview_options["height"])
        self.preview_topmost = CheckBox("独立窗口始终置顶", self)
        self.preview_topmost.setChecked(self._preview_options["topmost"])
        layout.addWidget(self._columns((("预览帧率（FPS）", self.preview_fps),
                                        ("页面预览大小", self.preview_size), ("窗口显示", self.preview_topmost))))
        layout.addWidget(_caption("预览帧率独立于采集帧率；独立窗口可拖动边缘缩放，切换任务页后仍可查看。"))
        self.preview_fps.valueChanged.connect(self._set_preview_fps)
        self.preview_size.currentIndexChanged.connect(self._preview_size_changed)
        self.preview_topmost.toggled.connect(self._set_preview_topmost)
        self.preview = PreviewImage()
        self.preview.setFixedHeight(self._preview_options["height"])
        self.preview.hide()
        layout.addWidget(self.preview)
        self.preview_rate = _caption("预览等待画面")
        self.preview_rate.hide()
        layout.addWidget(self.preview_rate)

    def _watch_changes(self):
        for widget in self.controls.values():
            if isinstance(widget, LineEdit):
                widget.textChanged.connect(self._dirty)
            elif isinstance(widget, (SpinBox, DoubleSpinBox)):
                widget.valueChanged.connect(self._dirty)
            else:
                widget.currentIndexChanged.connect(self._dirty)
        self.dry_run.toggled.connect(self._dirty)
        self.inference_resolution.currentIndexChanged.connect(self._dirty)

    def _dirty(self, *unused):
        if not self._building:
            self.message.setText("配置有改动 · 点击“启动线程 / 应用配置”立即生效；保存设置用于下次启动。")

    def _update_protocol(self, *unused):
        self.network_panel.setVisible(True)
        self.homing_wait_row.setVisible(True)
        self.protocol_hint.setText("从盒子屏幕填写 IP、端口和 UUID，控制机与盒子需处于可达的局域网。")
        self.calibration_hint.setText(
            "每次菜单定位先归零并等待，再按倍率移动，不沿用上次位置。若游戏持续锁定光标，需先进入可操作鼠标的界面。"
            " 横向 / 纵向分别表示每个目标像素需要的 HID 位移单位；1.5 × 40 像素 = 60 counts。"
            "游戏机建议鼠标速度 6/11、关闭指针加速后校准。")

    def _update_backend(self):
        self.ffmpeg_row.setVisible(self.controls["capture.backend"].currentData() == "FFMPEG")

    def _backend_changed(self, *unused):
        self._update_backend()
        self._generations["modes"] += 1
        self._modes = []
        self.list_devices()

    def _manual_toggled(self, checked):
        self.manual_panel.setVisible(checked)
        for widget in (self.video_device, self.resolution, self.frame_rate, self.pixel_format):
            widget.setEnabled(not checked)
        if checked:
            self._generations["modes"] += 1
            self.controls["capture.device_path"].clear()
        else:
            self._video_changed()
        self._dirty()

    def _probe(self, key, arguments, callback):
        self._generations[key] += 1
        generation = self._generations[key]
        old = self._processes.get(key)
        if old is not None:
            old.kill()
        process = QProcess(self)
        self._processes[key] = process
        process.setWorkingDirectory(str(Path(__file__).absolute().parents[1]))
        timeout = QTimer(process)
        timeout.setSingleShot(True)
        timed_out = [False]
        completed = [False]

        def expire():
            timed_out[0] = True
            process.kill()

        def finish(*unused):
            if completed[0]:
                return
            completed[0] = True
            timeout.stop()
            if generation != self._generations[key]:
                process.deleteLater()
                return
            try:
                raw = bytes(process.readAllStandardOutput()).decode("utf-8")
                result = json.loads(raw) if raw else {"error": "设备读取超时，请重试" if timed_out[0] else "无法读取设备信息"}
            except (UnicodeError, ValueError):
                result = {"error": "驱动未返回有效的设备信息，可重试或使用手动配置"}
            self._processes.pop(key, None)
            process.deleteLater()
            callback(result)

        process.finished.connect(finish)
        process.errorOccurred.connect(lambda error: finish() if error == QProcess.FailedToStart else None)
        timeout.timeout.connect(expire)
        process.start(sys.executable, ["-m", "dualpc.discovery", *arguments])
        timeout.start(12000)

    def _close_probes(self):
        if self.preview_window is not None:
            self.preview_window.close()
        for process in list(self._processes.values()):
            process.kill()
            process.waitForFinished(1000)

    def list_devices(self):
        self._generations["modes"] += 1
        self.scan_button.setEnabled(False)
        self.scan_button.setText("检测中…")
        self._probe("devices", ["devices", self.controls["capture.backend"].currentData()], self._devices_ready)

    def _devices_ready(self, result):
        self.scan_button.setEnabled(True)
        self.scan_button.setText("刷新设备")
        if "error" in result:
            self.mode_hint.setText(result["error"] + "。可展开高级选项手动配置。")
            return
        self._devices = result["video"]
        current_path = self.controls["capture.device_path"].text()
        current_name = self.controls["capture.device_name"].text()
        matches = [d for d in self._devices if d["path"].casefold() == current_path.casefold()] if current_path else []
        if not current_path:
            matches = [d for d in self._devices if d["name"].casefold() == current_name.casefold()]
            if not matches and current_name:
                matches = [d for d in self._devices if current_name.casefold() in d["name"].casefold()]
        chosen = matches[0] if len(matches) == 1 else None
        if not current_name and not current_path:
            chosen = next((d for d in self._devices if d["index"] == self.controls["capture.device_index"].value()), None)
        missing = f"{current_name or '已保存设备'} · 当前未连接" if (current_name or current_path) and chosen is None else "请选择采集设备"
        items = [(missing if self._devices else "未检测到视频设备 · 保留已保存参数", None)]
        items += [(f"{d['name']}  ·  #{d['index']}", d) for d in self._devices]
        _fill(self.video_device, items, chosen)
        if not self.manual_capture.isChecked():
            self._video_changed()
        if result["warnings"]:
            self.message.setText("；".join(result["warnings"]))

    def _video_changed(self, *unused):
        device = self.video_device.currentData()
        self._modes = []
        self._generations["modes"] += 1
        c = self.collect_capture_values()
        _fill(self.resolution, [(f"{c[0]} × {c[1]} · 当前", c[:2])], c[:2])
        _fill(self.frame_rate, [(f"{c[2]:g} FPS · 当前", c[2])], c[2])
        _fill(self.pixel_format, [("自动 · 驱动协商", "")] + ([(c[3], c[3])] if c[3] else []), c[3])
        if not device:
            self.mode_hint.setText("选择实际采集卡后读取可用模式。未连接的已保存设备参数会保留。")
            return
        self.controls["capture.device_index"].setValue(device["index"])
        self.controls["capture.device_name"].setText(device["name"])
        self.controls["capture.device_path"].setText(device["path"])
        self.mode_hint.setText("正在读取驱动提供的分辨率、帧率与图像格式…")
        self._probe("modes", ["modes", json.dumps(device, ensure_ascii=True)], self._modes_ready)

    def _modes_ready(self, result):
        self._modes = result.get("modes", [])
        if not self._modes:
            self.mode_hint.setText(result.get("error", "驱动未公开可用模式") + "。保留当前参数，可在高级选项中手动填写。")
            return
        current = self.controls["capture.fourcc"].text()
        formats = sorted({mode["fourcc"] for mode in self._modes})
        if current and current not in formats:
            formats.append(current)
        _fill(self.pixel_format, [("自动 · 驱动协商", "")] + [(FORMAT_LABELS.get(f, f), f) for f in formats], current)
        self._format_changed()

    def collect_capture_values(self):
        return (self.controls["capture.width"].value(), self.controls["capture.height"].value(),
                self.controls["capture.fps"].value(), self.controls["capture.fourcc"].text())

    def _format_changed(self, index=None):
        preserve = index is None
        fourcc = self.pixel_format.currentData() or ""
        self.controls["capture.fourcc"].setText(fourcc)
        modes = [m for m in self._modes if not fourcc or m["fourcc"] == fourcc]
        current = (self.controls["capture.width"].value(), self.controls["capture.height"].value())
        sizes = sorted({(m["width"], m["height"]) for m in modes}, key=lambda size: size[0] * size[1], reverse=True)
        items = [(f"{w} × {h}" + (" · 16:9" if abs(w / h - 16 / 9) < 0.02 else " · 需裁剪"), (w, h)) for w, h in sizes]
        if current not in sizes:
            if preserve or not sizes:
                items.insert(0, (f"{current[0]} × {current[1]} · 手动值", current))
            else:
                current = (1920, 1080) if (1920, 1080) in sizes else sizes[0]
        _fill(self.resolution, items, current)
        self._resolution_changed(preserve=preserve)

    def _resolution_changed(self, index=None, preserve=None):
        if preserve is None:
            preserve = index is None
        size = self.resolution.currentData()
        if not size:
            return
        self.controls["capture.width"].setValue(size[0])
        self.controls["capture.height"].setValue(size[1])
        fourcc = self.pixel_format.currentData() or ""
        rates = sorted({m["fps"] for m in self._modes if (m["width"], m["height"]) == size
                        and (not fourcc or m["fourcc"] == fourcc)}, reverse=True)
        current = self.controls["capture.fps"].value()
        items = [(f"{fps:g} FPS", fps) for fps in rates]
        if current not in rates:
            if preserve or not rates:
                items.insert(0, (f"{current:g} FPS · 手动值", current))
            else:
                current = min(rates, key=lambda fps: abs(fps - current))
        _fill(self.frame_rate, items, current)
        self._rate_changed()

    def _rate_changed(self, *unused):
        rate = self.frame_rate.currentData()
        if rate is not None:
            self.controls["capture.fps"].setValue(rate)
        if self._modes:
            size = self.resolution.currentData()
            fourcc = self.pixel_format.currentData() or ""
            valid = any((m["width"], m["height"]) == size and m["fps"] == rate
                        and (not fourcc or m["fourcc"] == fourcc) for m in self._modes)
            backend_note = "" if self.controls["capture.backend"].currentData() == "DSHOW" else "此列表来自 DirectShow，当前采集方式需以连接结果为准。"
            self.mode_hint.setText(("已读取驱动模式；实际格式以连接后的状态为准。" if valid else "当前组合未在驱动列表中，可改选已列出的模式或手动配置。") + backend_note)

    def collect_settings(self):
        settings = deepcopy(self.saved_settings)
        for key, control in self.controls.items():
            group, attribute = key.split(".")
            if isinstance(control, LineEdit):
                value = control.text().strip()
            elif isinstance(control, (SpinBox, DoubleSpinBox)):
                value = control.value()
            else:
                value = control.currentData()
            setattr(getattr(settings, group), attribute, value)
        size = self.inference_resolution.currentData()
        settings.capture.output_width, settings.capture.output_height = size
        settings.kmbox.dry_run = self.dry_run.isChecked()
        if not settings.kmbox.dry_run:
            box = settings.kmbox
            if box.protocol == "kmbox_net":
                from ipaddress import IPv4Address
                try:
                    IPv4Address(box.host)
                except ValueError:
                    raise ValueError("请填写有效的 KMBox Net IPv4 地址") from None
                if not 1 <= box.port <= 65535:
                    raise ValueError("请填写盒子屏幕上的端口（1–65535）")
                try:
                    if len(box.uuid) != 8:
                        raise ValueError
                    int(box.uuid, 16)
                except ValueError:
                    raise ValueError("请填写盒子屏幕上的 8 位十六进制 UUID") from None
        return settings

    def save(self):
        try:
            settings = self.collect_settings()
            settings.save(og.config["dualpc_settings_path"])
            self.saved_settings = deepcopy(settings)
            self.message.setText("设置已保存到本地 · 点击“启动线程 / 应用配置”立即生效，下次启动也会读取此配置。")
            return True
        except Exception as error:
            self.message.setText(f"未保存：{error}")
            return False

    def refresh(self):
        return self.start_connection()

    def start_connection(self):
        try:
            settings = self.collect_settings().validate()
            revision = og.device_manager.start_session(settings)
            self.message.setText("正在后台应用当前配置并连接设备… 自动任务将保持暂停。")
            self.tick()
            return revision
        except Exception as error:
            self.message.setText(f"未启动连接线程：{error}")
            return None

    def stop_connection(self):
        try:
            og.device_manager.stop_session()
            self.message.setText("正在终止连接线程、释放键鼠并关闭采集设备…")
            self.tick()
        except Exception as error:
            self.message.setText(f"未能终止连接：{error}")

    def test_connection_move(self):
        try:
            if self.collect_settings().kmbox != og.device_manager.settings.kmbox:
                raise ValueError("盒子参数有改动，请先点击“应用配置”")
            identifier = og.device_manager.test_mouse_move()
            self.message.setText("正在测试一次右移 40、下移 40 像素…")
            return identifier
        except Exception as error:
            self.message.setText(f"无法测试移动：{error}")
            return None

    def stop(self):
        try:
            if hasattr(og.device_manager, "stop_session"):
                og.device_manager.stop_session(emergency=True)
            else:
                og.device_manager.interaction.emergency_stop()
                if og.executor is not None:
                    og.executor.pause()
            self.message.setText("已禁止新的键鼠输入、暂停任务并请求终止线程。重新启动连接线程后可恢复。")
            self.stop_button.setText("键鼠输出已停止")
        except Exception as error:
            self.message.setText(f"停止命令发送失败，请手动松开盒子输入：{error}")

    def toggle_preview(self):
        self.preview_enabled = not self.preview_enabled
        self.preview.setVisible(self.preview_enabled)
        self.preview_rate.setVisible(self.preview_enabled)
        self.preview_button.setText("关闭画面预览" if self.preview_enabled else "开启画面预览")
        self._preview_key = None
        self._preview_times.clear()
        self._sync_preview_timer()
        if not self.preview_enabled:
            self.preview.clear()
        self._refresh_preview_layout()
        QTimer.singleShot(0, self, self._refresh_preview_layout)

    def showEvent(self, event):
        super().showEvent(event)
        self._sync_preview_timer()
        if hasattr(self, "preview_card"):
            QTimer.singleShot(0, self, self._refresh_preview_layout)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        if hasattr(self, "preview_card"):
            QTimer.singleShot(0, self, self._refresh_preview_layout)

    def _refresh_preview_layout(self):
        self.preview_layout.invalidate()
        height = max(self.preview_layout.minimumSize().height(),
                     self.preview_layout.totalHeightForWidth(self.preview_card.width()))
        if self.preview_card.minimumHeight() != height or self.preview_card.maximumHeight() != height:
            self.preview_card.setFixedHeight(height)
            self.preview_card.updateGeometry()
        self.vBoxLayout.invalidate()
        self.view.setMinimumHeight(self.vBoxLayout.minimumSize().height())

    def hideEvent(self, event):
        super().hideEvent(event)
        self._sync_preview_timer()

    def closeEvent(self, event):
        if self.preview_window is not None:
            self.preview_window.close()
        if hasattr(self, "preview_timer"):
            self.preview_timer.stop()
        super().closeEvent(event)

    def _sync_preview_timer(self):
        if not hasattr(self, "preview_timer"):
            return
        floating = self.preview_window is not None and self.preview_window.isVisible() and not self.preview_window.isMinimized()
        if self.preview_enabled and self.isVisible() or floating:
            self.preview_timer.start()
        else:
            self.preview_timer.stop()
            self._preview_times.clear()

    def _save_preview_options(self):
        self._preview_options.update(fps=self.preview_target_fps, height=self.preview_size.currentData(),
                                     topmost=self.preview_topmost.isChecked())
        try:
            self._preview_options_path.parent.mkdir(parents=True, exist_ok=True)
            self._preview_options_path.write_text(json.dumps(self._preview_options, indent=2), encoding="utf-8")
        except OSError as error:
            self.message.setText(f"预览设置暂未保存：{error}")

    def _set_preview_fps(self, value):
        if value not in PREVIEW_RATES:
            return
        self.preview_target_fps = value
        self.preview_timer.setInterval(self._preview_interval())
        for control in (self.preview_fps, self.preview_window.fps if self.preview_window is not None else None):
            if control is not None:
                blocker = QSignalBlocker(control)
                control.setValue(value)
                del blocker
        self._save_preview_options()

    def _preview_interval(self):
        return max(1, round(1000 / self.preview_target_fps)) if self.preview_target_fps else 0

    def _preview_size_changed(self):
        self.preview.setFixedHeight(self.preview_size.currentData())
        self._preview_key = None
        self._save_preview_options()
        self._refresh_preview_layout()
        QTimer.singleShot(0, self, self._refresh_preview_layout)

    def _set_preview_topmost(self, value):
        blocker = QSignalBlocker(self.preview_topmost)
        self.preview_topmost.setChecked(value)
        del blocker
        if self.preview_window is not None:
            self.preview_window.set_topmost(value)
        self._save_preview_options()

    def open_preview_window(self):
        if self.preview_window is None:
            self.preview_window = FloatingPreview(self.preview_target_fps, self.preview_topmost.isChecked())
            self.preview_window.fps_changed.connect(self._set_preview_fps)
            self.preview_window.topmost_changed.connect(self._set_preview_topmost)
            self.preview_window.visibility_changed.connect(self._sync_preview_timer)
        self.preview_window.showNormal()
        self.preview_window.raise_()
        self._preview_key = None
        self._sync_preview_timer()

    def _preview_targets(self):
        targets = []
        if (self.preview_enabled and self.isVisible() and self.preview.isVisible()
                and not self.preview.visibleRegion().isEmpty()):
            targets.append(self.preview)
        if self.preview_window is not None and self.preview_window.isVisible() and not self.preview_window.isMinimized():
            targets.append(self.preview_window.image)
        return targets

    def render_preview(self):
        targets = self._preview_targets()
        if not targets:
            return False
        manager = og.device_manager
        if manager is None:
            if not self.preview_target_fps:
                self.preview_timer.setInterval(50)
            return False
        reader = manager.capture_method.reader
        if not reader.connected():
            if not self.preview_target_fps:
                self.preview_timer.setInterval(50)
            if self._preview_key != "unavailable":
                for target in targets:
                    target.clear()
                    target.setText("暂无可用画面 · 请检查采集设备和 HDMI 输入")
                self._preview_key = "unavailable"
                self._preview_times.clear()
            return False
        bounds = (max(max(1, target.width()) for target in targets),
                  max(max(1, target.height()) for target in targets))
        previous = self._preview_key
        sequence = previous[1] if isinstance(previous, tuple) and previous[0] is reader and previous[2] == bounds else None
        result = reader.preview_frame(*bounds, after_sequence=sequence)
        if result is None:
            if not self.preview_target_fps:
                self.preview_timer.setInterval(1)  # Wait briefly for a new capture frame instead of spinning.
            return False
        sequence, frame = result
        if not isinstance(previous, tuple) or previous[0] is not reader:
            self._preview_times.clear()
        # BGR888 accepts the capture buffer directly: avoid a full-size RGB
        # conversion and scale before allocating the Qt image.
        image = QImage(frame.data, frame.shape[1], frame.shape[0], frame.strides[0], QImage.Format_BGR888).copy()
        pixmap = QPixmap.fromImage(image)
        for target in targets:
            target.setPixmap(pixmap)
        self._preview_key = (reader, sequence, bounds)
        self._preview_times.append(time.perf_counter())
        self.preview_frames_presented += 1
        if not self.preview_target_fps and self.preview_timer.interval():
            self.preview_timer.setInterval(0)
        return True

    def tick(self):
        manager = og.device_manager
        if manager is None:
            return
        reader = manager.capture_method.reader
        snapshot = manager.session_snapshot()
        phase = snapshot["phase"]
        phase_labels = {"idle": "未启动", "starting": "正在连接 / 应用配置", "running": "运行中", "stopping": "正在释放设备", "stopped": "已终止", "error": "连接异常"}
        self.session_state.setText("连接线程 · " + phase_labels.get(phase, phase))
        self.start_thread_button.setText("正在连接…" if phase == "starting" else "正在终止…" if phase == "stopping" else "应用配置" if phase == "running" else "启动线程")
        self.start_thread_button.setEnabled(phase not in {"starting", "stopping"})
        self.stop_thread_button.setEnabled(snapshot["worker_alive"] and phase != "stopping")
        age, connected = reader.age_ms, reader.connected()
        box = manager.interaction.client
        self.capture_state.setText("正在接收帧" if connected else "未连接")
        self.controller_state.setText("演练 · 未连接实物" if manager.settings.kmbox.dry_run else
                                      "KMBox Net 已确认" if box.connected else "KMBox Net 未连接")
        try:
            matches = self.collect_settings().kmbox == manager.settings.kmbox
        except Exception:
            matches = False
        pending_test = snapshot["last_test"] is not None and snapshot["last_test"]["status"] == "pending"
        executor = manager.executor
        if executor is None:
            self.automation_state.setText("自动任务尚未就绪")
        elif executor.paused:
            self.automation_state.setText("自动任务已暂停 · 启动或应用设备配置后，请点击原有“开始”按钮恢复任务。")
        else:
            current = getattr(executor, "current_task", None)
            combat = next((task for task in getattr(executor, "trigger_tasks", [])
                           if task.__class__.__name__ == "AutoCombatTask"), None)
            combat_text = ("自动战斗未开启" if combat is not None and not combat.enabled else
                           "正在自动战斗" if combat is not None and getattr(combat, "_in_combat", False) else
                           "等待识别战斗" if combat is not None else "自动任务运行中")
            if (current in getattr(executor, "onetime_tasks", ()) and
                    current is not None and getattr(current, "running", False)):
                combat_text = self.tr(current.name) + (" · 正在战斗" if getattr(current, "_in_combat", False) else " · 执行中")
            self.automation_state.setText("自动任务运行中 · " + combat_text)
        self.test_move_button.setEnabled(phase == "running" and box.connected and matches and not pending_test and
                                         (executor is None or executor.paused))
        actual = reader.actual
        width, height = actual.get("frame_width"), actual.get("frame_height")
        self.actual_state.setText(f"{width} × {height}" if connected and width and height else "—")
        self.latency_state.setText(f"{age:.1f} ms" if age is not None and connected else "—")
        if connected:
            fps = actual.get("fps")
            # OpenCV DirectShow reports its requested interval here, even when
            # negotiation falls back. Only the separate read counter is measured.
            fps_source = "后端报告" if manager.settings.capture.backend == "MSMF" else "请求"
            fps_text = f" · {fps_source} {fps:g} FPS" if fps else ""
            format_text = f" · {FORMAT_LABELS.get(actual['fourcc'], actual['fourcc'])}" if actual.get("fourcc") else ""
            stats = reader.statistics()
            measured = stats.get("measured_fps")
            measured_text = f" · 读帧 {measured:g} FPS" if measured is not None else ""
            self.status.setText(f"{actual.get('device_name') or manager.settings.capture.device_name} · {actual.get('backend', manager.settings.capture.backend)}{fps_text}{format_text}{measured_text}\n" +
                                "软件帧龄和读帧速率不代表 HDMI 信号的端到端延迟或唯一画面帧率。")
        else:
            self.status.setText("点击“启动线程”应用配置并连接设备。" if phase in {"idle", "stopped"} else manager.error or reader.error or "等待采集画面…")
        if snapshot["controller_error"]:
            self.status.setText(self.status.text() + "\n控制器：" + snapshot["controller_error"])
        elif not manager.settings.kmbox.dry_run and snapshot["last_check"] is not None and phase == "running":
            self.status.setText(self.status.text() + f"\n盒子最近通信：{max(0, time.monotonic() - snapshot['last_check']):.1f} 秒前")
        notice = (snapshot["applied_revision"], phase)
        if phase == "running" and notice != self._session_notice:
            self._session_notice = notice
            self.message.setText("连接线程运行中 · 当前配置已应用，可从原有入口开始自动任务。" + (" " + manager.error if manager.error else ""))
            self.stop_button.setText("紧急停止键鼠")
        elif phase in {"stopped", "error"} and notice != self._session_notice:
            self._session_notice = notice
            self.message.setText("连接线程已终止，设备连接已关闭；修改配置后可重新启动。" if phase == "stopped" else "连接异常：" + snapshot["error"])
        result = snapshot["last_test"]
        if result and result["status"] != "pending" and (result["id"], result["status"]) != self._test_notice:
            self._test_notice = (result["id"], result["status"])
            self.message.setText(result["message"])
        if self.preview_enabled or self.preview_window is not None and self.preview_window.isVisible():
            now = time.perf_counter()
            times = [stamp for stamp in self._preview_times if now - stamp <= 1]
            fps = (len(times) - 1) / (times[-1] - times[0]) if len(times) > 1 else 0
            target_text = f"目标 {self.preview_target_fps} FPS" if self.preview_target_fps else "无限制 · 随采集刷新"
            preview_text = f"预览 {fps:.0f} FPS · {target_text} · 仅显示最新画面"
            self.preview_rate.setText(preview_text)
            if self.preview_window is not None:
                self.preview_window.status.setText(preview_text)
