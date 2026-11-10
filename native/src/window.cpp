#include "window.hpp"
#include <QDesktopServices>
namespace okww {
void Preview::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.fillRect(rect(), QColor("#111827"));
    if (image_.isNull()) {
        p.setPen(QColor("#94a3b8"));
        p.drawText(rect(), Qt::AlignCenter, "启动连接线程后显示采集画面");
        return;
    }
    QSize size = image_.size();
    size.scale(this->size(), Qt::KeepAspectRatio);
    QRect dst(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
    const QSize pixels(int(std::ceil(size.width() * devicePixelRatioF())),
                       int(std::ceil(size.height() * devicePixelRatioF())));
    if (image_.width() > pixels.width() || image_.height() > pixels.height()) {
        // A smaller second window needs area reduction too. Cache it until the
        // frame or window size changes instead of filtering on every repaint.
        if (reduced_.size() != pixels)
            reduced_ = image_.scaled(pixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        p.drawImage(dst, reduced_);
    } else
        p.drawImage(dst, image_);
}
bool NoWheel::eventFilter(QObject *o, QEvent *e) {
    if (e->type() == QEvent::Wheel && (qobject_cast<QAbstractSpinBox *>(o) || qobject_cast<QComboBox *>(o) || qobject_cast<QSlider *>(o))) {
        e->ignore();
        return true;
    }
    return QObject::eventFilter(o, e);
}
static QWidget *scrollArea(QWidget *child) {
    auto s = new QScrollArea;
    s->setWidgetResizable(true);
    s->setFrameShape(QFrame::NoFrame);
    s->setWidget(child);
    return s;
}
class CurrentPageStack final : public QStackedWidget {
  public:
    QSize sizeHint() const override {
        return currentWidget() ? currentWidget()->sizeHint() : QStackedWidget::sizeHint();
    }
    QSize minimumSizeHint() const override {
        return currentWidget() ? currentWidget()->minimumSizeHint() : QStackedWidget::minimumSizeHint();
    }
};
static QFrame *card(const QString &name, QVBoxLayout *layout) {
    auto b = new QFrame;
    b->setObjectName("classicCard");
    auto outer = new QVBoxLayout(b);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(6);
    outer->addWidget(new QLabel(name));
    const QString subtitle = name.startsWith("01") ? "选择接收游戏机 HDMI 画面的采集卡，一次检测设备及驱动支持的参数。"
                             : name.startsWith("02") ? "选择盒子类型后，只显示该设备需要的连接参数。"
                                                     : "修改后点击右上角“开始”生效；设备调试可单独应用配置。";
    auto hint = new QLabel(subtitle);
    hint->setWordWrap(true);
    hint->setObjectName("caption");
    outer->addWidget(hint);
    outer->addSpacing(6);
    layout->setContentsMargins(2, 0, 2, 0);
    layout->setSpacing(8);
    outer->addLayout(layout);
    return b;
}
static QPushButton *button(const QString &label, std::function<void()> fn, QObject *owner) {
    auto b = new QPushButton(label);
    classic::setActionIcon(b);
    QObject::connect(b, &QPushButton::clicked, owner, std::move(fn));
    return b;
}
Window::Window(QString root, QString data, QString python,
               std::function<void(Backend &, const Config &)> connector,
               std::function<QJsonArray(const Config &)> captureScanner)
    : root_(std::move(root)), data_(std::move(data)), python_(std::move(python)),
      config_(Config::load(data_ + "/dualpc.json")), deviceDraftConfig_(config_), deviceConnector_(std::move(connector)),
      captureScanner_(std::move(captureScanner)) {
    QString draftError;
    if (QFile::exists(data_ + "/native-device-settings.json")) {
        try {
            deviceUiState_ = readJson(data_ + "/native-device-settings.json");
            const auto saved = deviceUiState_["config"].toObject();
            for (auto it = saved.begin(); it != saved.end(); ++it) {
                if (it.value().isObject()) {
                    auto section = deviceDraftConfig_.json[it.key()].toObject();
                    const auto incoming = it.value().toObject();
                    for (auto field = incoming.begin(); field != incoming.end(); ++field)
                        section[field.key()] = field.value();
                    deviceDraftConfig_.json[it.key()] = section;
                } else deviceDraftConfig_.json[it.key()] = it.value();
            }
        } catch (const std::exception &error) { draftError = errorText(error); }
    }
    // User sessions always connect the selected device. Old profiles may still
    // carry the removed practice switch; only explicit offline tests use it.
    config_.set("kmbox.dry_run", false);
    deviceDraftConfig_.set("kmbox.dry_run", false);
    deviceSaveTimer_.setSingleShot(true);
    deviceSaveTimer_.setInterval(350);
    connect(&deviceSaveTimer_, &QTimer::timeout, this, &Window::saveDeviceSettings);
    if (!deviceConnector_)
        deviceConnector_ = [](Backend &backend, const Config &config) { backend.start(config); };
    if (!captureScanner_)
        captureScanner_ = [](const Config &config) { return captureCapabilities(config); };
    startupTimer_.setSingleShot(true);
    startupTimer_.setInterval(30000);
    connect(&startupTimer_, &QTimer::timeout, this, [this] {
        failStartup("设备准备超时，请检查采集卡和盒子连接后重试。");
    });
    closeRetryTimer_.setInterval(100);
    connect(&closeRetryTimer_, &QTimer::timeout, this, [this] {
        if (busy_ || pendingFieldWrites_ || teamOperation_)
            return;
#ifdef OKWW_NATIVE_ONLY
        if (scriptOperation_)
            return;
#endif
        closeRetryTimer_.stop();
        close();
    });
    setWindowTitle(product::title);
    resize(1250, 860);
    setMinimumSize(1000, 680);
    qApp->installEventFilter(new NoWheel(qApp));
    buildUi();
    if (!draftError.isEmpty()) log("设备配置草稿读取失败，已载入上次应用的配置：" + draftError);
    QIcon icon(root_ + "/icons/icon.png");
    if (icon.isNull())
        icon = style()->standardIcon(QStyle::SP_ComputerIcon);
    setWindowIcon(icon);
    tray_ = new QSystemTrayIcon(icon, this);
    tray_->setToolTip(product::title);
    auto trayMenu = new QMenu(this);
    trayMenu->addAction("打开主窗口", this, [this] {
        showNormal();
        raise();
        activateWindow();
    });
    trayMenu->addAction("暂停任务", this, &Window::pause);
    auto exit = trayMenu->addAction("退出", this, &Window::requestExit);
    exit->setObjectName("trayExit");
    tray_->setContextMenu(trayMenu);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](auto reason) {
        if (reason == QSystemTrayIcon::DoubleClick) {
            showNormal();
            raise();
            activateWindow();
        }
    });
    if (QSystemTrayIcon::isSystemTrayAvailable())
        tray_->show();
    connect(&engine_, &QProcess::readyReadStandardOutput, this, &Window::readEngine);
    connect(&engine_, &QProcess::readyReadStandardError, this, [this] {
        errorBuffer_ += engine_.readAllStandardError();
        int n;
        while ((n = int(errorBuffer_.indexOf('\n'))) >= 0) {
            auto line = QString::fromUtf8(errorBuffer_.left(n));
            errorBuffer_.remove(0, n + 1);
            if (line.contains("ERROR") || line.contains("WARNING") || line.contains("Traceback"))
                log(line);
        }
        if (errorBuffer_.size() > 65536)
            errorBuffer_.clear();
    });
    connect(&engine_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        engineReady_ = false;
        automation_ = false;
        try {
            backend_.arm(false);
        } catch (...) {
        }
        callbacks_.clear();
        errorCallbacks_.clear();
        pendingFieldWrites_ = 0;
        engineStatus_->setText("兼容引擎已退出");
        if (restartEngine_ && !busy_ && !closing_) {
            QTimer::singleShot(0, this, &Window::startEngine);
            return;
        }
        if (!closing_)
            log(QString("兼容引擎退出，代码 %1。可点击“重新加载引擎”。").arg(code));
    });
    connect(&engine_, &QProcess::errorOccurred, this,
            [this](auto) { log("兼容引擎启动失败：" + engine_.errorString()); });
    connect(&previewTimer_, &QTimer::timeout, this, &Window::updatePreview);
    previewTimer_.setTimerType(Qt::PreciseTimer);
    if (previewTimer_.interval() <= 0)
        previewTimer_.setInterval(16);
    previewTimer_.start();
    previewRateClock_.start();
    connect(&statusTimer_, &QTimer::timeout, this, &Window::updateStatus);
    statusTimer_.start(500);
    RegisterHotKey(reinterpret_cast<HWND>(winId()), 1, MOD_NOREPEAT, VK_F9);
    QTimer::singleShot(0, this, &Window::startEngine);
#ifdef OKWW_NATIVE_ONLY
    if (qEnvironmentVariable("QT_QPA_PLATFORM") != "offscreen") {
        QTimer::singleShot(3000, this, &Window::updateWorkshop);
        workshopTimer_.setInterval(60000);
        connect(&workshopTimer_, &QTimer::timeout, this, &Window::updateWorkshop);
        workshopTimer_.start();
    }
#endif
}
Window::~Window() {
    closing_ = true;
    deviceAutoSaveReady_ = false;
    deviceSaveTimer_.stop();
    for (auto object : deviceSettings_->findChildren<QObject *>())
        disconnect(object, nullptr, this, nullptr);
    // Focus can change while QWidget destroys its children, after our members are gone.
    for (auto timer : findChildren<QTimer *>())
        if (timer->property("fieldAutoSave").toBool()) {
            timer->stop();
            disconnect(qApp, nullptr, timer->parent(), nullptr);
        }
    previewTimer_.stop();
    statusTimer_.stop();
    UnregisterHotKey(reinterpret_cast<HWND>(winId()), 1);
#ifdef OKWW_NATIVE_ONLY
    nativeEngine_.reset();
#endif
    if (engine_.state() != QProcess::NotRunning) {
        engine_.write("{\"op\":\"shutdown\"}\n");
        if (!engine_.waitForFinished(4000)) {
            engine_.kill();
            engine_.waitForFinished(2000);
        }
    }
    backend_.stop();
    if (floating_)
        delete floating_;
}
bool Window::nativeEvent(const QByteArray &, void *message, qintptr *result) {
    auto m = static_cast<MSG *>(message);
    if (m->message == WM_NCHITTEST && !isMaximized()) {
        RECT r{};
        POINT point{};
        GetWindowRect(reinterpret_cast<HWND>(winId()), &r);
        GetCursorPos(&point);
        const int edge = std::max(5, int(GetDpiForWindow(reinterpret_cast<HWND>(winId())) * 6 / 96));
        const bool left = point.x < r.left + edge, right = point.x >= r.right - edge,
                   top = point.y < r.top + edge, bottom = point.y >= r.bottom - edge;
        if (top || bottom || left || right) {
            *result = top      ? (left    ? HTTOPLEFT
                                  : right ? HTTOPRIGHT
                                          : HTTOP)
                      : bottom ? (left    ? HTBOTTOMLEFT
                                  : right ? HTBOTTOMRIGHT
                                          : HTBOTTOM)
                      : left   ? HTLEFT
                               : HTRIGHT;
            return true;
        }
    }
    if (m->message == WM_HOTKEY && m->wParam == 1) {
#ifdef OKWW_NATIVE_ONLY
        if (recording_) {
            backend_.arm(false);
            *result = 0;
            return true;
        }
#endif
        (automation_ || pendingStart_) ? pause() : startTask();
        *result = 0;
        return true;
    }
    return false;
}
void Window::requestExit() {
    closing_ = true;
    close();
}
void Window::closeEvent(QCloseEvent *e) {
    saveDeviceSettings();
    flushFieldSaves();
    if (pendingStart_) {
        if (closing_)
            stopDevices();
        else
            pause();
    }
    if (busy_) {
        log("正在结束设备操作，完成后将自动退出。");
        closing_ = true;
        closeRetryTimer_.start();
        e->ignore();
        return;
    }
    if (minimizeToTray_ && !closing_ && tray_ && tray_->isVisible()) {
        hide();
        e->ignore();
        return;
    }
    if (pendingFieldWrites_) {
        log("正在保存实时触发配置，完成后将自动退出。");
        closing_ = true;
        closeRetryTimer_.start();
        e->ignore();
        return;
    }
    if (teamOperation_) {
        log("正在切换队伍代码，完成后将自动退出。");
        closing_ = true;
        closeRetryTimer_.start();
        e->ignore();
        return;
    }
#ifdef OKWW_NATIVE_ONLY
    if (scriptOperation_) {
        log("正在保存脚本，完成后将自动退出。");
        closing_ = true;
        closeRetryTimer_.start();
        e->ignore();
        return;
    }
    if (scriptEditor_ && !editingScript_.isEmpty() && scriptEditor_->document()->isModified()) {
        e->ignore();
        const bool explicitExit = closing_;
        // Cancel keeps the application usable; Save resumes once its async write completes.
        closing_ = false;
        withSavedScript([this, explicitExit] {
            closing_ = explicitExit;
            QTimer::singleShot(0, this, &QWidget::close);
        });
        return;
    }
#endif
    closing_ = true;
    closeRetryTimer_.stop();
    pause();
    if (tray_)
        tray_->hide();
    if (floating_)
        floating_->hide();
    e->accept();
    // Closing an already hidden QWidget does not emit lastWindowClosed.
    emit exitAccepted();
}
void Window::log(QString msg) {
    logs_->appendPlainText(QTime::currentTime().toString("HH:mm:ss  ") + msg);
    banner_->setText(msg);
    if (msg.contains("失败") || msg.contains("错误") || msg.startsWith("请") || msg.contains("已保存")) {
        banner_->show();
        QTimer::singleShot(5000, this, [this, msg] {
            if (banner_->text() == msg)
                banner_->hide();
        });
    }
#ifdef OKWW_NATIVE_ONLY
    const auto folder = data_ + "/logs";
    QDir().mkpath(folder);
    const auto path = folder + "/native.log";
    if (QFileInfo(path).size() > 5 * 1024 * 1024) {
        QFile::remove(path + ".2");
        QFile::rename(path + ".1", path + ".2");
        QFile::rename(path, path + ".1");
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append))
        file.write((QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + "  " + msg + '\n').toUtf8());
#endif
}
void Window::buildUi() { buildClassicUi(); }
QWidget *Window::devicePage() {
    auto page = new QWidget;
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    auto pageHeader = new QHBoxLayout;
    auto title = new QLabel("双机设置");
    title->setObjectName("pageTitle");
    pageHeader->addWidget(title);
    pageHeader->addStretch();
    auto refresh = button("检测设备与参数", [this] { scanDevices(); }, this);
    refresh->setObjectName("refreshDevices");
    classic::setFluentIcon(refresh, "Sync");
    deviceSaveStatus_ = new QLabel("参数修改后自动保存");
    deviceSaveStatus_->setObjectName("deviceSaveStatus");
    deviceSaveStatus_->setProperty("caption", true);
    deviceSaveStatus_->setWordWrap(true);
    deviceSaveStatus_->setFixedSize(300, 34);
    deviceSaveStatus_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    pageHeader->addWidget(deviceSaveStatus_);
    start_ = button("开始(F9)", [this] { toggleDevices(); }, this);
    start_->setObjectName("primary");
    start_->setProperty("automationControl", true);
    start_->setMinimumWidth(118);
    start_->setFixedHeight(34);
    pageHeader->addWidget(start_);
    layout->addLayout(pageHeader);
    auto pageLayout = layout;
    deviceSettings_ = new QWidget;
    deviceSettings_->setObjectName("deviceSettings");
    layout = new QVBoxLayout(deviceSettings_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    pageLayout->addWidget(scrollArea(deviceSettings_), 1);
    auto pageNote = new QLabel("右上角“开始”连接设备并运行实时任务；“停止”释放设备。F9 仅暂停或继续自动任务。");
    pageNote->setObjectName("caption");
    layout->addWidget(pageNote);
    auto connection = new QFrame;
    connection->setObjectName("classicCard");
    auto connectionLayout = new QVBoxLayout(connection);
    connectionLayout->setContentsMargins(16, 14, 16, 14);
    auto controls = new QHBoxLayout;
    connectionTitle_ = new QLabel("设备状态 · 未连接");
    connectionTitle_->setObjectName("cardTitle");
    controls->addWidget(connectionTitle_);
    controls->addStretch();
    connectionLayout->addLayout(controls);
    auto diagnostics = new classic::ExpandCard("设备调试（可选）", "仅预览、校准或测试设备时展开，不运行自动任务。");
    diagnostics->setObjectName("deviceDiagnostics");
    diagnostics->setHeaderIcon("Setting");
    diagnostics->setExpanded(false);
    auto debugControls = new QHBoxLayout(diagnostics->body());
    debugControls->setContentsMargins(16, 8, 16, 12);
    auto startThread = button("仅连接预览", [this] { connectDevices(); }, this);
    startConnection_ = startThread;
    startThread->setObjectName("previewConnection");
    debugControls->addWidget(startThread);
    auto stopThread = button(
        "断开设备",
        [this] { stopDevices(); },
        this);
    stopThread->setObjectName("stopThread");
    debugControls->addWidget(stopThread);
    debugControls->addStretch();
    auto moveTest = button(
        "测试移动 ↘ 40 px",
        [this] {
            if (busy_ || pendingStart_ || closing_ || stoppingDevices_ || stopRequested_)
                return;
            boxTestStatus_->setText("正在测试移动…");
            boxTestStatus_->show();
            runAsync(
                [this] {
                    try {
                        return backend_.testMove();
                    } catch (const std::exception &error) {
                        return QJsonObject{{"test_error", errorText(error)}};
                    }
                },
                [this](auto result) {
                    const auto error = result["test_error"].toString();
                    const auto message = !error.isEmpty() ? "测试失败：" + error
                        : result["dry_run"].toBool() ? QString("离线测试完成：已记录向右、向下各 40 px，不向实物发送。")
                        : QString("测试指令已发送：向右、向下各 40 px。任务保持当前运行状态。");
                    boxTestStatus_->setText(message);
                    log(message);
                });
        },
        this);
    moveTest->setObjectName("boxTest");
    moveTest->setEnabled(false);
    moveTest->setToolTip("任务运行中也可测试，保留当前按键和鼠标按住状态；位移可能被脚本的后续移动覆盖。");
    auto statuses = new QHBoxLayout;
    const auto status = [&](QString heading, QLabel *&value) {
        auto column = new QVBoxLayout;
        auto caption = new QLabel(heading);
        caption->setObjectName("metricCaption");
        column->addWidget(caption);
        value = new QLabel("—");
        value->setObjectName("metricValue");
        value->setWordWrap(true);
        column->addWidget(value);
        statuses->addLayout(column, 1);
    };
    status("视频采集", captureConnection_);
    status("键鼠控制", boxConnection_);
    status("实际画面", frameParameters_);
    status("软件帧龄", frameAge_);
    connectionLayout->addLayout(statuses);
    deviceStatus_ = new QLabel("点击右上角“开始”自动连接设备并运行任务。");
    deviceStatus_->setObjectName("caption");
    deviceStatus_->setWordWrap(true);
    connectionLayout->addWidget(deviceStatus_);
    connectionTaskStatus_ =
        new QLabel("副本等任务可在任务页直接开始，设备会自动连接。");
    connectionTaskStatus_->setObjectName("caption");
    connectionTaskStatus_->setWordWrap(true);
    connectionLayout->addWidget(connectionTaskStatus_);
    layout->addWidget(connection);
    layout->addWidget(diagnostics);
    auto cap = new QVBoxLayout;
    auto row = new QHBoxLayout;
    backendChoice_ = new classic::ComboBox;
    backendChoice_->setObjectName("captureBackend");
    backendChoice_->addItem("DirectShow", "DSHOW");
    backendChoice_->addItem("Media Foundation", "MSMF");
    backendChoice_->setCurrentIndex(std::max(0, backendChoice_->findData(deviceDraftConfig_.s("capture.backend"))));
    deviceChoice_ = new classic::ComboBox;
    deviceChoice_->setObjectName("captureDevice");
    deviceChoice_->setProperty("captureBackend", backendChoice_->currentData());
    deviceChoice_->setMinimumWidth(150);
    deviceChoice_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    deviceChoice_->setMinimumContentsLength(16);
    deviceChoice_->addItem(deviceDraftConfig_.s("capture.device_name").isEmpty() ? "请选择采集卡"
                                                                      : deviceDraftConfig_.s("capture.device_name"),
                           QJsonObject{{"name", deviceDraftConfig_.s("capture.device_name")},
                                       {"path", deviceDraftConfig_.s("capture.device_path")},
                                       {"index", deviceDraftConfig_.n("capture.device_index")}}
                               .toVariantMap());
    auto deviceLabel = new QLabel("采集设备");
    deviceLabel->setFixedWidth(116);
    row->addWidget(deviceLabel);
    row->addWidget(deviceChoice_, 1);
    row->addWidget(refresh);
    cap->addLayout(row);
    captureScanStatus_ = new QLabel("点击“检测设备与参数”，一次读取所有发现设备的分辨率、帧率和格式。");
    captureScanStatus_->setObjectName("captureScanStatus");
    captureScanStatus_->setProperty("caption", true);
    captureScanStatus_->setWordWrap(true);
    cap->addWidget(captureScanStatus_);
    resolution_ = new classic::ComboBox;
    resolution_->setObjectName("captureResolution");
    resolution_->setPlaceholderText("暂无可用分辨率");
    resolution_->addItem(QString("%1 × %2").arg(deviceDraftConfig_.n("capture.width")).arg(deviceDraftConfig_.n("capture.height")),
                         QVariantList{deviceDraftConfig_.n("capture.width"), deviceDraftConfig_.n("capture.height")});
    resolution_->setCurrentIndex(0);
    format_ = new classic::ComboBox;
    format_->setObjectName("captureFormat");
    format_->setPlaceholderText("暂无可用格式");
    if (deviceDraftConfig_.s("capture.fourcc").isEmpty())
        format_->addItem("自动 · 驱动协商", "");
    else
        format_->addItem(deviceDraftConfig_.s("capture.fourcc"));
    format_->setCurrentIndex(0);
    fps_ = new classic::ComboBox;
    fps_->setObjectName("captureFps");
    fps_->setPlaceholderText("暂无可用帧率");
    fps_->addItem(QString::number(deviceDraftConfig_.d("capture.fps")), deviceDraftConfig_.d("capture.fps"));
    fps_->setCurrentIndex(0);
    auto parameters = new QHBoxLayout;
    parameters->setSpacing(14);
    for (auto entry : {qMakePair(QString("分辨率"), resolution_), qMakePair(QString("帧率"), fps_),
                       qMakePair(QString("图像格式"), format_)}) {
        auto column = new QVBoxLayout;
        auto caption = new QLabel(entry.first);
        caption->setObjectName("caption");
        column->addWidget(caption);
        column->addWidget(entry.second);
        parameters->addLayout(column, 1);
    }
    cap->addLayout(parameters);
    auto note = new QLabel("列表来自采集卡驱动。运行状态分别显示协商参数和实测收帧率；预览帧率独立设置。");
    note->setWordWrap(true);
    cap->addWidget(note);
    layout->addWidget(card("01  视频采集", cap));
    connect(resolution_, &QComboBox::currentIndexChanged, this, [this] { filterModes(); });
    connect(format_, &QComboBox::currentIndexChanged, this, [this] { filterModes(); });
    connect(deviceChoice_, &QComboBox::currentIndexChanged, this, [this] { selectCaptureDevice(); });
    connect(backendChoice_, &QComboBox::currentIndexChanged, this, [this] {
        if (busy_ || backend_.snapshot()["running"].toBool()) {
            const QSignalBlocker blocker(backendChoice_);
            backendChoice_->setCurrentIndex(backendChoice_->findData(deviceChoice_->property("captureBackend")));
            log("请先停止设备连接，再切换采集后端。");
            return;
        }
        scanDevices();
    });
    auto box = new QVBoxLayout;
    protocol_ = new classic::ComboBox;
    protocol_->setObjectName("boxProtocol");
    protocol_->addItem("KMBox Net · 网络", "kmbox_net");
    protocol_->addItem("MAKCU · 串口", "makcu_serial");
    protocol_->setCurrentIndex(std::max(0, protocol_->findData(deviceDraftConfig_.s("kmbox.protocol"))));
    auto protocolRow = new QHBoxLayout;
    auto protocolLabel = new QLabel("控制器类型");
    protocolLabel->setFixedWidth(116);
    protocolRow->addWidget(protocolLabel);
    protocolRow->addWidget(protocol_, 1);
    box->addLayout(protocolRow);
    boxForms_ = new CurrentPageStack;
    boxForms_->setObjectName("boxConnectionForms");
    boxForms_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto addField = [this](QFormLayout *f, QString label, QString key, bool integer = false, double low = 0,
                           double high = 65535) {
        QWidget *w;
        if (integer) {
            auto s = new QSpinBox;
            s->setRange(int(low), int(high));
            s->setValue(deviceDraftConfig_.n(key));
            w = s;
        } else {
            auto s = new QLineEdit(deviceDraftConfig_.s(key));
            w = s;
        }
        f->addRow(label, w);
        w->setObjectName(key);
        w->setAccessibleName(label);
        settings_[key] = w;
        return w;
    };
    auto addDecimal = [this](QFormLayout *f, const QString &label, const QString &key, double low,
                             double high) {
        auto spin = new QDoubleSpinBox;
        spin->setDecimals(4);
        spin->setRange(low, high);
        spin->setValue(deviceDraftConfig_.d(key));
        settings_[key] = spin;
        spin->setObjectName(key);
        spin->setAccessibleName(label);
        f->addRow(label, classic::sliderEditor(spin));
    };
    auto captureAdvanced = new QWidget;
    auto caf = new QFormLayout(captureAdvanced);
    caf->addRow("采集后端", backendChoice_);
    auto inference = new classic::ComboBox;
    inferenceResolution_ = inference;
    inference->setObjectName("inferenceResolution");
    for (auto size :
         {QSize(1280, 720), QSize(1600, 900), QSize(1920, 1080), QSize(2560, 1440), QSize(3840, 2160)}) {
        inference->addItem(QString("%1 × %2").arg(size.width()).arg(size.height()), size);
        if (size == QSize(deviceDraftConfig_.n("capture.output_width"), deviceDraftConfig_.n("capture.output_height")))
            inference->setCurrentIndex(inference->count() - 1);
    }
    caf->addRow("任务识别分辨率", inference);
    addDecimal(caf, "图像过期时间（秒）", "capture.stale_after", .01, 5);
    manualCapture_ = new QCheckBox("手动输入采集参数");
    manualCapture_->setObjectName("manualCapture");
    caf->addRow(manualCapture_);
    auto manual = new QWidget;
    auto manualForm = new QFormLayout(manual);
    manualForm->setContentsMargins(0, 0, 0, 0);
    addField(manualForm, "设备名称", "capture.device_name");
    addField(manualForm, "设备编号", "capture.device_index", true, 0, 255);
    addField(manualForm, "宽度", "capture.width", true, 1, 16384);
    addField(manualForm, "高度", "capture.height", true, 1, 16384);
    addDecimal(manualForm, "请求帧率", "capture.fps", 1, 1000);
    auto fourcc = qobject_cast<QLineEdit *>(addField(manualForm, "格式 FourCC", "capture.fourcc"));
    fourcc->setMaxLength(4);
    fourcc->setPlaceholderText("留空自动，如 NV12");
    for (const auto &key : {"capture.device_name", "capture.device_index", "capture.width", "capture.height",
                            "capture.fps", "capture.fourcc"})
        settings_[key]->setProperty("manualCaptureField", true);
    auto manualHint = new QLabel("手动模式按填写值请求；实际运行参数以采集卡驱动协商结果为准。");
    manualHint->setWordWrap(true);
    manualHint->setObjectName("caption");
    manualForm->addRow(manualHint);
    caf->addRow(manual);
    manual->hide();
    connect(manualCapture_, &QCheckBox::toggled, this, [this, manual](bool enabled) {
        manual->setVisible(enabled);
        for (auto choice : {deviceChoice_, resolution_, format_, fps_})
            choice->setEnabled(!enabled);
    });
    auto cropToggle = new QCheckBox("裁剪采集画面");
    cropCapture_ = cropToggle;
    cropToggle->setObjectName("captureCrop");
    cropToggle->setChecked(deviceDraftConfig_.get("capture.crop").isArray());
    caf->addRow(cropToggle);
    auto cropFields = new QWidget;
    auto cf = new QFormLayout(cropFields);
    std::array<QSpinBox *, 4> cropSpins;
    auto crop = deviceDraftConfig_.get("capture.crop").toArray();
    if (deviceUiState_["crop_values"].toArray().size() == 4)
        crop = deviceUiState_["crop_values"].toArray();
    for (int i = 0; i < 4; ++i) {
        cropSpins[i] = new QSpinBox;
        cropSpins_[i] = cropSpins[i];
        cropSpins[i]->setObjectName(QString("captureCrop-%1").arg(i));
        cropSpins[i]->setRange(i < 2 ? 0 : 1, 8192);
        cropSpins[i]->setValue(crop.size() == 4 ? crop[i].toInt()
                               : i == 2         ? deviceDraftConfig_.n("capture.width")
                               : i == 3         ? deviceDraftConfig_.n("capture.height")
                                                : 0);
        cf->addRow(QStringList{"左侧起点", "顶部起点", "裁剪宽度", "裁剪高度"}[i], cropSpins[i]);
    }
    cropFields->setVisible(cropToggle->isChecked());
    caf->addRow(cropFields);
    connect(cropToggle, &QCheckBox::toggled, cropFields, &QWidget::setVisible);
    auto expandCapture = new QPushButton("高级采集选项");
    classic::setDisclosureButton(expandCapture);
    cap->addWidget(expandCapture);
    cap->addWidget(captureAdvanced);
    captureAdvanced->hide();
    connect(expandCapture, &QPushButton::toggled, captureAdvanced, &QWidget::setVisible);
    auto net = new QWidget;
    auto networkRow = new QHBoxLayout(net);
    networkRow->setContentsMargins(0, 0, 0, 0);
    networkRow->setSpacing(14);
    for (const auto &pair : {qMakePair(QString("IP 地址"), QString("kmbox.host")),
                             qMakePair(QString("端口"), QString("kmbox.port")),
                             qMakePair(QString("UUID · 8 位十六进制"), QString("kmbox.uuid"))}) {
        auto column = new QWidget;
        auto form = new QFormLayout(column);
        form->setContentsMargins(0, 0, 0, 0);
        form->setRowWrapPolicy(QFormLayout::WrapAllRows);
        addField(form, pair.first, pair.second, pair.second == "kmbox.port", 0, 65535);
        networkRow->addWidget(column, 1);
    }
    qobject_cast<QLineEdit *>(settings_["kmbox.host"])->setPlaceholderText("例如 192.168.2.188");
    qobject_cast<QLineEdit *>(settings_["kmbox.uuid"])->setPlaceholderText("盒子屏幕上的 UUID");
    qobject_cast<QSpinBox *>(settings_["kmbox.port"])->setSpecialValueText("未填写");
    boxForms_->addWidget(net);
    auto ser = new QWidget;
    auto sf = new QFormLayout(ser);
    serial_ = new classic::ComboBox;
    serial_->setObjectName("boxSerialPort");
    serial_->setEditable(true);
    serial_->addItem(deviceDraftConfig_.s("kmbox.serial_port"));
    sf->addRow("串口", serial_);
    auto serialHint = new QLabel("请选择 CH343 串口；波特率需与盒子一致。支持 3.x 鼠标兼容与 V4 路由检测；3.x 不能输出键盘，自动任务需要键鼠均可用。");
    serialHint->setProperty("caption", true);
    serialHint->setWordWrap(true);
    sf->addRow(serialHint);
    addField(sf, "波特率", "kmbox.baudrate", true, 1, 4000000);
    sf->addRow(button(
        "检测串口",
        [this] {
            const auto previous = serial_->currentText();
            const QSignalBlocker blocker(serial_);
            serial_->clear();
            for (const auto &p : Controller::serialDevices())
                serial_->addItem(p.toString());
            if (!previous.isEmpty()) serial_->setEditText(previous);
            queueDeviceSave();
        },
        this));
    boxForms_->addWidget(ser);
    box->addWidget(boxForms_);
    auto switchProtocol = [this] {
        auto p = protocol_->currentData().toString();
        boxForms_->setCurrentIndex(p == "kmbox_net" ? 0 : 1);
        boxForms_->updateGeometry();
    };
    connect(protocol_, &QComboBox::currentIndexChanged, this, switchProtocol);
    switchProtocol();
    auto testBar = new QHBoxLayout;
    testBar->addStretch();
    testBar->addWidget(moveTest);
    box->addLayout(testBar);
    boxTestStatus_ = new QLabel;
    boxTestStatus_->setObjectName("boxTestStatus");
    boxTestStatus_->setProperty("caption", true);
    boxTestStatus_->setWordWrap(true);
    boxTestStatus_->hide();
    box->addWidget(boxTestStatus_);
    auto moveHint = new QLabel("连接盒子后可测试鼠标向右、向下各移动 40 px。实际移动距离受游戏机鼠标速度、指针加速和移动倍率影响。");
    moveHint->setObjectName("boxMoveHint");
    moveHint->setProperty("caption", true);
    moveHint->setWordWrap(true);
    box->addWidget(moveHint);
    auto expand = new QPushButton("鼠标定位与移动倍率");
    classic::setDisclosureButton(expand);
    box->addWidget(expand);
    auto advanced = new QWidget;
    auto af = new QFormLayout(advanced);
    addField(af, "游戏机桌面宽度", "kmbox.desktop_width", true, 1, 16384);
    addField(af, "游戏机桌面高度", "kmbox.desktop_height", true, 1, 16384);
    addField(af, "回到角落的移动量", "kmbox.home_counts", true, 1, 32767);
    addField(af, "每次最大移动量", "kmbox.move_chunk", true, 1, 32767);
    addDecimal(af, "回到角落后等待（秒）", "kmbox.home_wait", 0, 1);
    addDecimal(af, "移动间隔（秒）", "kmbox.move_interval", 0, .1);
    addDecimal(af, "应答超时（秒）", "kmbox.timeout", .01, 5);
    for (auto pair : {std::pair{"横向移动倍率", "kmbox.counts_per_pixel_x"},
                      std::pair{"纵向移动倍率", "kmbox.counts_per_pixel_y"}}) {
        auto n = new QDoubleSpinBox;
        n->setDecimals(4);
        n->setRange(.0001, 100);
        n->setValue(deviceDraftConfig_.d(pair.second));
        settings_[pair.second] = n;
        n->setObjectName(pair.second);
        n->setAccessibleName(pair.first);
        af->addRow(pair.first, classic::sliderEditor(n, true));
    }
    box->addWidget(advanced);
    advanced->setVisible(false);
    connect(expand, &QPushButton::toggled, advanced, &QWidget::setVisible);
    layout->addWidget(card("02  键鼠控制", box));
    layout->addWidget(buildClassicPreview());
    layout->addStretch();
    const auto manualValues = deviceUiState_["manual_values"].toObject();
    for (auto it = settings_.begin(); it != settings_.end(); ++it) {
        auto control = it.value();
        if (control->property("manualCaptureField").toBool() && manualValues.contains(it.key())) {
            const auto value = manualValues[it.key()];
            if (auto field = qobject_cast<QLineEdit *>(control)) field->setText(value.toString());
            else if (auto field = qobject_cast<QSpinBox *>(control)) field->setValue(value.toInt());
            else if (auto field = qobject_cast<QDoubleSpinBox *>(control)) field->setValue(value.toDouble());
        }
        if (auto field = qobject_cast<QLineEdit *>(control))
            connect(field, &QLineEdit::textChanged, this, &Window::queueDeviceSave);
        else if (auto field = qobject_cast<QSpinBox *>(control))
            connect(field, &QSpinBox::valueChanged, this, &Window::queueDeviceSave);
        else if (auto field = qobject_cast<QDoubleSpinBox *>(control))
            connect(field, &QDoubleSpinBox::valueChanged, this, &Window::queueDeviceSave);
        else if (auto field = qobject_cast<QCheckBox *>(control))
            connect(field, &QCheckBox::toggled, this, &Window::queueDeviceSave);
    }
    manualCapture_->setChecked(deviceUiState_["manual_capture"].toBool());
    connect(serial_, &QComboBox::currentTextChanged, this, &Window::queueDeviceSave);
    for (auto choice : {backendChoice_, deviceChoice_, resolution_, format_, fps_, protocol_, inferenceResolution_})
        connect(choice, &QComboBox::currentIndexChanged, this, &Window::queueDeviceSave);
    connect(manualCapture_, &QCheckBox::toggled, this, &Window::queueDeviceSave);
    connect(cropToggle, &QCheckBox::toggled, this, &Window::queueDeviceSave);
    for (auto spin : cropSpins)
        connect(spin, &QSpinBox::valueChanged, this, &Window::queueDeviceSave);
    deviceAutoSaveReady_ = true;
    return page;
}
QWidget *Window::tasksPage(bool trigger) {
    auto page = new QWidget;
    auto row = new QVBoxLayout(page);
    row->setContentsMargins(16, 16, 16, 16);
    row->setSpacing(12);
    auto title = new QLabel(trigger ? "实时触发" : "任务");
    title->setObjectName("pageTitle");
    auto titleRow = new QHBoxLayout;
    titleRow->addWidget(title);
    if (!trigger) {
        auto count = new QLabel;
        count->setObjectName("taskCount");
        count->setProperty("caption", true);
        titleRow->addWidget(count);
        titleRow->addStretch();
        auto search = new QLineEdit;
        search->setObjectName("taskSearch");
        search->setAccessibleName("搜索任务");
        search->setPlaceholderText("搜索任务名称或说明");
        search->setClearButtonEnabled(true);
        search->setFixedWidth(260);
        auto icon = search->addAction(classic::fluentIcon("Search"), QLineEdit::LeadingPosition);
        icon->setProperty("fluentIcon", "Search");
        titleRow->addWidget(search);
        connect(search, &QLineEdit::textChanged, this, [this] { renderTaskCards(false); });
    }
    row->addLayout(titleRow);
    auto guide = new QLabel(trigger ? "开关和参数修改后自动保存；打开需要的功能，再在双机设置点击开始。"
                                    : "点击开始，设备自动连接；设置可调整参数，日历可安排定时。" );
    guide->setObjectName("caption");
    guide->setWordWrap(true);
    row->addWidget(guide);
    row->addWidget(buildTaskInfo(trigger));
    if (!trigger) {
        auto empty = new QLabel("没有找到匹配的任务，换个关键词试试。");
        empty->setObjectName("tasksEmpty");
        empty->setAlignment(Qt::AlignCenter);
        empty->setWordWrap(true);
        empty->hide();
        row->addWidget(empty);
    }
    auto list = new QListWidget(page);
    list->hide();
    auto form = new QWidget;
    auto cards = new QVBoxLayout(form);
    cards->setContentsMargins(0, 0, 0, 0);
    cards->setSpacing(10);
    cards->addStretch();
    row->addWidget(form);
    if (trigger) {
        triggerList_ = list;
        triggerForm_ = form;
    } else {
        taskList_ = list;
        taskForm_ = form;
    }
    return scrollArea(page);
}
QWidget *Window::globalPage() { return buildClassicSettings(); }
QWidget *Window::charactersPage() { return buildClassicCharacters(); }
void Window::runAsync(std::function<QJsonObject()> work, std::function<void(QJsonObject)> done) {
    if (busy_) {
        log("正在处理设备操作，请稍候。");
        return;
    }
    busy_ = true;
    updateStartControl();
    auto watcher = new QFutureWatcher<QJsonObject>(this);
    connect(watcher, &QFutureWatcher<QJsonObject>::finished, this, [this, watcher, done] {
        busy_ = false;
        auto result = watcher->result();
        watcher->deleteLater();
        if (stopRequested_ && !stoppingDevices_) {
            if (result.contains("_error"))
                log(result["_error"].toString());
            stopDevices();
            return;
        }
        if (result.contains("_error")) {
            stoppingDevices_ = false;
            if (pendingStart_)
                failStartup(result["_error"].toString());
            else
                log(result["_error"].toString());
            if (restartEngine_ && engine_.state() == QProcess::NotRunning && !closing_)
                startEngine();
            updateStatus();
            return;
        }
        if (done)
            done(result);
        updateStatus();
        updateStartControl();
        QTimer::singleShot(0, this, &Window::continueStart);
    });
    watcher->setFuture(QtConcurrent::run([work] {
        try {
            return work();
        } catch (const std::exception &e) {
            return QJsonObject{{"_error", errorText(e)}};
        }
    }));
}
void Window::scanDevices() {
    if (busy_)
        return;
    if (backend_.snapshot()["running"].toBool()) {
        log("请先停止设备连接，再检测采集设备与参数。");
        return;
    }
    auto config = deviceDraftConfig_;
    config.set("capture.backend", backendChoice_->currentData().toString());
    const auto previous = QJsonObject::fromVariantMap(deviceChoice_->currentData().toMap());
    if (deviceChoice_->property("captureBackend").toString() != config.s("capture.backend")) {
        const QSignalBlocker blocker(deviceChoice_);
        deviceChoice_->clear();
        selectCaptureDevice();
    }
    deviceChoice_->setProperty("captureBackend", config.s("capture.backend"));
    captureScanStatus_->setText("正在后台检测设备并读取所有设备的支持参数…");
    runAsync(
        [config, scanner = captureScanner_] {
            try {
                return QJsonObject{{"devices", scanner(config)}};
            } catch (const std::exception &error) {
                return QJsonObject{{"scan_error", errorText(error)}};
            }
        },
        [this, previous](auto result) {
            if (result.contains("scan_error")) {
                captureScanStatus_->setText("检测失败：" + result["scan_error"].toString());
                log(captureScanStatus_->text());
                return;
            }
            QSignalBlocker blocker(deviceChoice_);
            deviceChoice_->clear();
            int selected = -1, total = 0, failed = 0;
            for (auto value : result["devices"].toArray()) {
                const auto device = value.toObject();
                deviceChoice_->addItem(device["name"].toString(), device.toVariantMap());
                total += device["modes"].toArray().size();
                if ((!previous["path"].toString().isEmpty() && previous["path"] == device["path"]) ||
                    (previous["path"].toString().isEmpty() && previous["name"] == device["name"] &&
                     previous["index"] == device["index"]))
                    selected = deviceChoice_->count() - 1;
                if (!device["error"].toString().isEmpty()) {
                    ++failed;
                    log(device["name"].toString() + " 参数读取失败：" + device["error"].toString());
                }
            }
            deviceChoice_->setCurrentIndex(selected < 0 && deviceChoice_->count() ? 0 : selected);
            selectCaptureDevice();
            queueDeviceSave();
            log(QString("已检测 %1 个视频设备，读取 %2 组驱动参数。%3")
                    .arg(deviceChoice_->count()).arg(total)
                    .arg(failed ? QString("其中 %1 个设备读取失败，其余设备可正常选择。").arg(failed) : QString{}));
        });
}
void Window::selectCaptureDevice() {
    const auto device = QJsonObject::fromVariantMap(deviceChoice_->currentData().toMap());
    modes_ = device["modes"].toArray();
    // A saved, not-yet-scanned selection keeps its current manual parameters.
    if (!device.isEmpty() && !device.contains("modes"))
        return;
    QSignalBlocker block(resolution_);
    const auto saved = resolution_->currentData();
    resolution_->clear();
    if (modes_.isEmpty()) {
        QSignalBlocker formatBlock(format_), fpsBlock(fps_);
        format_->clear();
        fps_->clear();
        captureScanStatus_->setText(device.isEmpty() ? "未检测到视频设备，请检查连接和驱动后重试。"
            : device["error"].toString().isEmpty() ? "此设备未报告可用参数，可切换设备或使用手动参数。"
                                                  : "此设备参数读取失败：" + device["error"].toString());
        return;
    }
    QSet<QString> seen;
    for (const auto &value : modes_) {
        const auto mode = value.toObject();
        const auto label = QString("%1 × %2").arg(mode["width"].toInt()).arg(mode["height"].toInt());
        if (!seen.contains(label)) {
            seen.insert(label);
            resolution_->addItem(label, QVariantList{mode["width"].toInt(), mode["height"].toInt()});
        }
    }
    resolution_->setCurrentIndex(std::max(0, resolution_->findData(saved)));
    filterModes();
    captureScanStatus_->setText(QString("已检测 %1 个设备 · 当前设备 %2 组参数 · 切换设备无需再次读取")
                                   .arg(deviceChoice_->count()).arg(modes_.size()));
}
void Window::filterModes() {
    if (modes_.isEmpty())
        return;
    QSignalBlocker fb(format_), pb(fps_);
    auto size = resolution_->currentData().toList();
    if (size.size() != 2)
        return;
    auto oldFormat = format_->currentText();
    auto oldFps = fps_->currentData();
    format_->clear();
    for (auto v : modes_) {
        auto m = v.toObject();
        if (m["width"].toInt() == size[0].toInt() && m["height"].toInt() == size[1].toInt() &&
            format_->findText(m["fourcc"].toString()) < 0)
            format_->addItem(m["fourcc"].toString());
    }
    format_->setCurrentIndex(std::max(0, format_->findText(oldFormat)));
    fps_->clear();
    for (auto v : modes_) {
        auto m = v.toObject();
        if (m["width"].toInt() == size[0].toInt() && m["height"].toInt() == size[1].toInt() &&
            m["fourcc"].toString() == format_->currentText() && fps_->findData(m["fps"].toDouble()) < 0)
            fps_->addItem(QString::number(m["fps"].toDouble(), 'g', 8), m["fps"].toDouble());
    }
    fps_->setCurrentIndex(std::max(0, fps_->findData(oldFps)));
}
Config Window::deviceConfiguration(bool requireSelection) const {
    auto current = deviceDraftConfig_;
    current.set("kmbox.dry_run", false);
    auto d = QJsonObject::fromVariantMap(deviceChoice_->currentData().toMap());
    current.set("capture.backend", backendChoice_->currentData().toString());
    current.set("capture.device_name", d["name"].toString());
    current.set("capture.device_path", d["path"].toString());
    current.set("capture.device_index", d["index"].toInt());
    auto size = resolution_->currentData().toList();
    const bool complete = size.size() == 2 && fps_->currentData().isValid() && format_->currentIndex() >= 0;
    if (!manualCapture_->isChecked() && !complete && requireSelection)
        fail("请选择采集设备及分辨率、图像格式和帧率，或使用手动参数");
    if (!manualCapture_->isChecked() && complete) {
        current.set("capture.width", size[0].toInt());
        current.set("capture.height", size[1].toInt());
        current.set("capture.fourcc", format_->currentData().isValid() ? format_->currentData().toString()
                                                                       : format_->currentText());
        current.set("capture.fps", fps_->currentData().toDouble());
    }
    current.set("kmbox.protocol", protocol_->currentData().toString());
    current.set("kmbox.serial_port", serial_->currentText().trimmed());
    if (manualCapture_->isChecked())
        current.set("capture.device_path", "");
    for (auto it = settings_.begin(); it != settings_.end(); ++it) {
        if (it.value()->property("manualCaptureField").toBool() && !manualCapture_->isChecked())
            continue;
        if (auto w = qobject_cast<QLineEdit *>(it.value()))
            current.set(it.key(), w->text().trimmed());
        else if (auto w = qobject_cast<QSpinBox *>(it.value()))
            current.set(it.key(), w->value());
        else if (auto w = qobject_cast<QDoubleSpinBox *>(it.value()))
            current.set(it.key(), w->value());
        else if (auto w = qobject_cast<QCheckBox *>(it.value()))
            current.set(it.key(), w->isChecked());
    }
    const auto inference = inferenceResolution_->currentData().toSize();
    current.set("capture.output_width", inference.width());
    current.set("capture.output_height", inference.height());
    QJsonArray crop;
    for (auto spin : cropSpins_) crop.append(spin->value());
    current.set("capture.crop", cropCapture_->isChecked() ? QJsonValue(crop) : QJsonValue());
    return current;
}
void Window::queueDeviceSave() {
    if (!deviceAutoSaveReady_) return;
    deviceSettingsDirty_ = true;
    deviceSaveStatus_->setText("正在自动保存…");
    deviceSaveTimer_.start();
}
void Window::saveDeviceSettings() {
    if (!deviceAutoSaveReady_ || !deviceSettingsDirty_) return;
    if (busy_) { deviceSaveTimer_.start(); return; }
    deviceSaveTimer_.stop();
    try {
        const auto current = deviceConfiguration(false);
        QJsonObject manual;
        for (auto it = settings_.begin(); it != settings_.end(); ++it) {
            if (!it.value()->property("manualCaptureField").toBool()) continue;
            if (auto field = qobject_cast<QLineEdit *>(it.value())) manual[it.key()] = field->text();
            else if (auto field = qobject_cast<QSpinBox *>(it.value())) manual[it.key()] = field->value();
            else if (auto field = qobject_cast<QDoubleSpinBox *>(it.value())) manual[it.key()] = field->value();
        }
        QJsonArray crop;
        for (auto spin : cropSpins_) crop.append(spin->value());
        QJsonObject saved{{"config", current.json}, {"manual_capture", manualCapture_->isChecked()},
                          {"manual_values", manual}, {"crop_values", crop}};
        // Save even incomplete input as a draft. The running backend and Python
        // workers keep the last validated configuration until the next apply.
        writeJson(data_ + "/native-device-settings.json", saved);
        deviceDraftConfig_ = current;
        deviceUiState_ = saved;
        deviceSettingsDirty_ = false;
        deviceSaveStatus_->setText("参数已自动保存 · 连接时应用");
        deviceSaveStatus_->setToolTip("参数已保留，点击“开始”或“仅应用设备配置”时校验并连接设备。");
        try {
            const auto candidate = deviceConfiguration(true);
            candidate.validateCapture();
            candidate.validateBox();
        } catch (const std::exception &error) {
            deviceSaveStatus_->setText("草稿已自动保存 · 连接参数待完善");
            deviceSaveStatus_->setToolTip(errorText(error));
        }
    } catch (const std::exception &error) {
        deviceSaveStatus_->setText("自动保存失败，请检查配置目录");
        deviceSaveStatus_->setToolTip(errorText(error));
        log("设备参数自动保存失败：" + errorText(error));
    }
}
void Window::collectSettings() {
    if (pendingStart_)
        fail("正在准备任务，请先取消启动，再修改并应用设备配置。");
    saveDeviceSettings();
    const auto current = deviceConfiguration(true);
    current.save(data_ + "/dualpc.json");
    config_ = current;
    deviceDraftConfig_ = current;
}
void Window::connectDevices() {
    if (busy_)
        return;
    pendingStart_.reset();
    startupTimer_.stop();
    try {
        collectSettings();
    } catch (const std::exception &e) {
        log(errorText(e));
        updateStartControl();
        return;
    }
    pauseExecution();
    prepareConnection(config_);
}
void Window::prepareConnection(const Config &config) {
#ifndef OKWW_NATIVE_ONLY
    restartEngine_ = true;
    engineReady_ = false;
    if (engine_.state() != QProcess::NotRunning)
        engine_.write("{\"op\":\"shutdown\"}\n");
#endif
    log("正在后台连接采集卡与盒子…");
    runAsync(
        [this, config] {
            QElapsedTimer elapsed;
            elapsed.start();
            deviceConnector_(backend_, config);
            auto state = backend_.snapshot();
            state["connection_ms"] = elapsed.elapsed();
            return state;
        },
        [this](auto state) {
            log(QString("设备连接完成，用时 %1 ms。").arg(state["connection_ms"].toInteger()));
            if (closing_) return;
            log(pendingStart_ ? "设备已连接，正在准备任务…" : "预览设备已连接，可测试设备；自动任务保持暂停。");
#ifdef OKWW_NATIVE_ONLY
            if (!nativeEngine_)
#else
            if (engine_.state() == QProcess::NotRunning)
#endif
                startEngine();
        });
}
void Window::toggleDevices() {
    if (pendingStart_ || devicesRunning_ || automation_)
        stopDevices();
    else if (!busy_ && !stoppingDevices_ && !stopRequested_)
        startTask();
}
void Window::stopDevices() {
    pendingStart_.reset();
    startupTimer_.stop();
    automation_ = false;
    if (stoppingDevices_)
        return;
    // The connector holds the backend mutex. Queue its cleanup instead of
    // blocking the UI or letting a cancelled connection remain active.
    if (busy_) {
        stopRequested_ = true;
        log("正在停止，当前设备操作结束后会自动断开连接。");
        updateStartControl();
        return;
    }
    stopRequested_ = false;
    stoppingDevices_ = true;
    pauseExecution();
    runAsync(
        [this] {
            backend_.stop();
            return QJsonObject{};
        },
        [this](auto) {
            stoppingDevices_ = false;
            devicesRunning_ = false;
            log("已停止自动任务并断开采集卡与盒子连接。");
            if (taskStatus_)
                taskStatus_->setText("已停止 · 设备已断开");
        });
}
void Window::updateStartControl() {
    if (!start_)
        return;
    start_->setProperty("startupPending", bool(pendingStart_));
    start_->setProperty("deviceBusy", busy_);
    start_->setProperty("devicesRunning", devicesRunning_);
    const bool stopping = stopRequested_ || stoppingDevices_;
    if (auto test = findChild<QPushButton *>("boxTest"))
        test->setEnabled(!busy_ && !pendingStart_ && !stopping && devicesRunning_ &&
                         test->property("boxConnected").toBool());
    if (auto action = findChild<QPushButton *>("refreshDevices"))
        action->setEnabled(!busy_ && !pendingStart_ && !stopping && !devicesRunning_);
    start_->setText(stopping ? "正在停止…" : pendingStart_ ? "取消启动"
                             : devicesRunning_ || automation_ ? "停止" : "开始(" + hotkey_ + ")");
    classic::setFluentIcon(start_, pendingStart_ ? "Cancel" : devicesRunning_ || automation_ ? "Stop" : "Play", true);
    start_->setEnabled(!stopping && (bool(pendingStart_) || !busy_));
    start_->setToolTip(pendingStart_ ? "正在准备设备；再次点击或按快捷键取消，不会在取消后启动任务。"
                         : devicesRunning_ ? "停止自动任务并断开采集卡和盒子；F9 仅暂停或继续自动任务。"
                                           : "自动连接采集卡和盒子，再启动已启用的实时任务。");
    if (pendingStart_)
        connectionTaskStatus_->setText("正在准备设备与任务… 可点击“取消启动”。");
    if (pendingStart_ && taskStatus_)
        taskStatus_->setText("正在准备设备与任务… 可取消");
    if (startConnection_)
        startConnection_->setEnabled(!busy_ && !pendingStart_ && !stopping);
    refreshTaskActions();
}
void Window::failStartup(const QString &error) {
    pendingStart_.reset();
    startupTimer_.stop();
    log("启动失败：" + error);
    updateStartControl();
    emit startupFailed(error);
}
void Window::pauseExecution() {
    automation_ = false;
    taskStopped_ = false;
    if (engineReady_)
        send({{"op", "pause"}});
    try {
        backend_.arm(false);
    } catch (const std::exception &e) {
        log(errorText(e));
    }
    updateStartControl();
}
void Window::stopTask() {
    // A task may be cancelled while its connector owns the backend mutex.
    // Let that connection finish for preview; never queue device teardown.
    const bool connecting = pendingStart_ && busy_;
    pendingStart_.reset();
    startupTimer_.stop();
    if (!connecting)
        pauseExecution();
    else
        automation_ = false;
    taskStopped_ = true;
    updateStartControl();
    refreshTaskInfo();
    log("当前任务已停止，采集卡与盒子保持连接；断开设备请到双机设置点击“停止”。");
}
void Window::pause() {
    if (pendingStart_) {
        stopTask();
        return;
    }
    pauseExecution();
}
void Window::startTask(QString id, bool exitAfter, double limitSeconds) {
    if (teamOperation_) {
        log("正在切换队伍代码，请等待完成后启动。");
        return;
    }
    if (closing_ || pendingStart_ || stoppingDevices_ || stopRequested_)
        return;
    if (busy_) {
        log("设备正在处理操作，请稍候再开始。");
        return;
    }
    try {
        collectSettings();
    } catch (const std::exception &e) {
        failStartup(errorText(e));
        return;
    }
    pauseExecution();
    pendingStart_ = StartRequest{id, exitAfter, limitSeconds, config_};
    startupTimer_.start();
    updateStartControl();
    continueStart();
}
void Window::continueStart() {
    if (!pendingStart_ || busy_ || closing_)
        return;
    const auto state = backend_.snapshot();
    const auto box = state["box"].toObject();
    if (!backend_.configurationMatches(pendingStart_->config) ||
        !state["capture_connected"].toBool() || !box["connected"].toBool()) {
        if (pendingStart_->connected) {
            failStartup(state["error"].toString().isEmpty()
                            ? "采集画面或盒子尚未就绪，请检查设备连接。" : state["error"].toString());
            return;
        }
        pendingStart_->connected = true;
        prepareConnection(pendingStart_->config);
        return;
    }
    if (!box["keyboard"].toBool()) {
        failStartup(box["capability"].toString("盒子没有可用键盘路由，请检查连接后重试。"));
        return;
    }
    if (!engineReady_) {
#ifdef OKWW_NATIVE_ONLY
        if (!nativeEngine_ && !nativeRestartPending_)
            startEngine();
#else
        if (engine_.state() == QProcess::NotRunning)
            startEngine();
#endif
        return;
    }
    const auto request = *pendingStart_;
    if (!request.connected)
        log("复用现有设备连接，直接准备任务。");
    pendingStart_.reset();
    startupTimer_.stop();
#ifndef OKWW_NATIVE_ONLY
    try {
        backend_.arm(true);
    } catch (const std::exception &e) {
        failStartup(errorText(e));
        return;
    }
#endif
    send({{"op", "start"}, {"task", request.task}, {"exit_after", request.exitAfter},
          {"limit_seconds", request.limitSeconds}},
         [this, id = request.task](auto) {
             automation_ = true;
             taskStopped_ = false;
             taskRunClock_.restart();
             taskInfoElapsedSeconds_ = 0;
             combatInfo_ = {};
             updateStartControl();
             emit taskStartAccepted(id);
         },
         [this](const QString &error) { failStartup(error); });
    currentTask_ = request.task;
    updateStartControl();
}
void Window::startEngine() {
#ifdef OKWW_NATIVE_ONLY
    if (closing_)
        return;
    if (nativeEngine_ && !nativeEngine_->idle()) {
        engineReady_ = false;
        engineStatus_->setText("正在等待上一次任务释放…");
        if (!nativeRestartPending_) {
            nativeRestartPending_ = true;
            QTimer::singleShot(50, this, [this] {
                nativeRestartPending_ = false;
                startEngine();
            });
        }
        return;
    }
    try {
        nativeEngine_.reset();
        callbacks_.clear();
        errorCallbacks_.clear();
        const int generation = ++nativeGeneration_;
        nativeEngine_ = std::make_unique<runtime::Engine>(backend_, root_, data_);
        connect(nativeEngine_.get(), &runtime::Engine::message, this, [this, generation](auto message) {
            if (generation == nativeGeneration_)
                handleEngineMessage(message);
        });
        restartEngine_ = false;
        nativeEngine_->load();
    } catch (const std::exception &error) {
        engineReady_ = false;
        log(errorText(error));
    }
#else
    if (engine_.state() != QProcess::NotRunning)
        return;
    restartEngine_ = false;
    outputBuffer_.clear();
    errorBuffer_.clear();
    engineReady_ = false;
    engineStatus_->setText("兼容引擎正在加载…");
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("OKWW_NATIVE_TOKEN", backend_.token());
    env.insert("OKWW_NATIVE_MAPPING", backend_.mappingName());
    env.insert("OKWW_NATIVE_PORT", QString::number(backend_.port()));
    env.insert("PYTHONIOENCODING", "utf-8");
    env.insert("PYTHONUTF8", "1");
    env.remove("QT_PLUGIN_PATH");
    env.remove("QT_QPA_PLATFORM_PLUGIN_PATH");
    engine_.setProcessEnvironment(env);
    engine_.setWorkingDirectory(root_);
    engine_.start(python_, {root_ + "/native/bridge/worker.py", "--root", root_, "--data", data_});
#endif
}
void Window::send(QJsonObject q, std::function<void(QJsonObject)> cb, std::function<void(QString)> failed) {
    if (!engineReady_) {
        log("执行器尚未准备好。");
        if (failed) failed("执行器尚未准备好，配置尚未保存。");
        return;
    }
    int id = nextId_++;
    q["id"] = id;
    if (cb)
        callbacks_[id] = cb;
    if (failed)
        errorCallbacks_[id] = failed;
#ifdef OKWW_NATIVE_ONLY
    nativeEngine_->command(q);
#else
    engine_.write(QJsonDocument(q).toJson(QJsonDocument::Compact) + '\n');
#endif
}
void Window::readEngine() {
    outputBuffer_ += engine_.readAllStandardOutput();
    if (outputBuffer_.size() > 20 * 1024 * 1024) {
        outputBuffer_.clear();
        log("兼容引擎返回数据过大。");
        return;
    }
    int n;
    while ((n = int(outputBuffer_.indexOf('\n'))) >= 0) {
        auto line = outputBuffer_.left(n);
        outputBuffer_.remove(0, n + 1);
        QJsonParseError error;
        auto doc = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
            continue;
        handleEngineMessage(doc.object());
    }
}
void Window::handleEngineMessage(const QJsonObject &o) {
    auto event = o["event"].toString();
    if (event == "ready") {
        engineReady_ = true;
        setCatalog(o["data"].toObject());
#ifdef OKWW_NATIVE_ONLY
        engineStatus_->setText("任务引擎已就绪 · 外部采集与硬件键鼠");
        setWindowTitle(product::title);
        banner_->setText("点击“开始”自动连接设备并启用实时任务；副本等任务可在任务页直接开始。");
#else
        engineStatus_->setText("兼容引擎就绪");
        log("原版任务和角色代码已载入。");
#endif
        updateStartControl();
        QTimer::singleShot(0, this, &Window::continueStart);
    } else if (event == "catalog_changed") {
        setCatalog(o["data"].toObject());
    } else if (event == "log") {
        log(o["text"].toString());
    } else if (event == "task_done") {
        log("任务执行完毕：" + o["task"].toString());
    } else if (event == "notification") {
        if (tray_)
            tray_->showMessage(o["title"].toString(), o["text"].toString(),
                               o["error"].toBool() ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information,
                               10000);
    } else if (event == "quit_requested") {
        log("已按“完成后退出”设置退出软件。双机模式不关闭游戏机上的游戏。");
        closing_ = true;
        QTimer::singleShot(0, this, &QWidget::close);
    } else if (event == "error") {
        if (pendingStart_)
            failStartup(o["error"].toString());
        else
            log(o["error"].toString());
    } else if (event == "combat_info") {
        combatInfo_ = o["data"].toObject();
        refreshTaskInfo();
    } else if (event == "party_invalidated") {
        combatInfo_ = {{"code_state", "pending"}};
        refreshTaskInfo();
    } else if (event == "status") {
        auto s = o["data"].toObject();
        automation_ = !s["paused"].toBool(true);
        currentTask_ = s["current"].toString();
        refreshTaskActions();
        updateStartControl();
        taskStatus_->setText(
            stopRequested_ || stoppingDevices_ ? "正在停止并断开设备…"
            : pendingStart_ ? "正在准备设备与任务… 可取消"
            : automation_
                ? "正在运行：" + (s["current"].toString().isEmpty() ? "实时触发" : s["current"].toString())
                : devicesRunning_ && taskStopped_ ? "当前任务已停止 · 设备保持连接"
                : devicesRunning_ ? "自动任务已暂停 · 设备保持连接" : "已停止 · 设备已断开");
        if (!pendingStart_ && automation_ && s["warming"].toBool())
            taskStatus_->setText("正在准备工坊与识别资源… 可点击暂停");
    } else {
        int id = o["id"].toInt();
        auto cb = callbacks_.take(id);
        auto failed = errorCallbacks_.take(id);
        if (!o["ok"].toBool()) {
            log(o["error"].toString());
            if (failed)
                failed(o["error"].toString());
            try {
                backend_.arm(false);
            } catch (...) {
            }
        } else if (cb)
            cb(o["data"].toObject());
    }
}
void Window::updateStatus() {
    if (busy_)
        return;
    const auto s = backend_.snapshot();
    const auto actual = s["actual"].toObject();
    const auto box = s["box"].toObject();
    const bool running = s["running"].toBool(), captured = s["capture_connected"].toBool();
    devicesRunning_ = running;
    updateStartControl();
    connectionTitle_->setText(running ? "设备状态 · 已连接" : "设备状态 · 未连接");
    startConnection_->setText(running ? "仅应用设备配置" : "仅连接预览");
    if (auto stop = findChild<QPushButton *>("stopThread"))
        stop->setEnabled(running);
    if (auto test = findChild<QPushButton *>("boxTest")) {
        test->setProperty("boxConnected", box["connected"].toBool());
        test->setEnabled(running && box["connected"].toBool() && !pendingStart_ && !stopRequested_ && !stoppingDevices_);
    }
    captureConnection_->setText(
        captured ? QString("已连接 · 实测 %1 FPS").arg(actual["measured_fps"].toDouble(), 0, 'f', 1)
                 : "未连接");
    boxConnection_->setText(s["dry_run"].toBool()       ? "离线测试 · 未连接实物"
                            : box["connected"].toBool() ? "已连接"
                                                        : "未连接");
    if (!s["dry_run"].toBool() && box["transport_samples"].toInt() > 0) {
        boxConnection_->setText(boxConnection_->text() + QString(" · 应答 %1 ms（P95 %2 ms）")
            .arg(box["transport_p50_ms"].toDouble(), 0, 'f', 1)
            .arg(box["transport_p95_ms"].toDouble(), 0, 'f', 1));
        boxConnection_->setToolTip("最近 128 次主机发送至协议应答的通信时间，不包含游戏处理和 USB 报告生效时间。");
    }
    frameParameters_->setText(
        captured ? QString("%1 × %2 · %3\n协商 %4 FPS")
                       .arg(actual["width"].toInt())
                       .arg(actual["height"].toInt())
                       .arg(actual["fourcc"].toString())
                       .arg(actual["negotiated_fps"].toDouble(actual["requested_fps"].toDouble()))
                 : "—");
    frameAge_->setText(
        s["age_ms"].toDouble(-1) < 0 ? "—" : QString("%1 ms").arg(s["age_ms"].toDouble(), 0, 'f', 1));
    deviceStatus_->setText(!s["error"].toString().isEmpty() ? s["error"].toString()
                           : running                        ? "当前配置已应用。"
                                                            : "点击右上角“开始”一键连接并运行，无需单独启动线程。");
    connectionTaskStatus_->setText(
        pendingStart_ ? "正在准备设备与任务… 可点击“取消启动”。"
        : automation_ ? "自动任务正在运行。"
        : running && taskStopped_ ? "当前任务已停止，设备保持连接。可开始新任务，或点击右上角“停止”释放设备。"
        : running ? "自动任务已暂停，设备保持连接。按快捷键继续，或点击右上角“停止”释放设备。"
                  : "已停止，设备已断开。点击右上角“开始”可重新连接并运行任务。");
    if (homeDevices_ && homeDevices_->count())
        homeDevices_->item(0)->setText(
            QString("PC版 %1：%2 / %3 + %4  %5×%6")
                .arg(captured ? "已连接" : "已断开", config_.s("capture.device_name"),
                     config_.s("capture.backend"),
                     s["dry_run"].toBool() ? "离线测试，不输出键鼠" : protocol_->currentText())
                .arg(config_.n("capture.width"))
                .arg(config_.n("capture.height")));
    if (previewRateClock_.isValid() && previewRateClock_.elapsed() >= 1000) {
        actualPreviewFps_ = previewFrames_ * 1000.0 / previewRateClock_.elapsed();
        previewFrames_ = 0;
        previewRateClock_.restart();
    }
    if (previewRate_)
        previewRate_->setText(captured ? QString("预览 %1 FPS · 采集实测 %2 FPS")
                                             .arg(actualPreviewFps_, 0, 'f', 1)
                                             .arg(actual["measured_fps"].toDouble(), 0, 'f', 1)
                                       : "预览等待画面");
    if (!captured) {
        preview_->setImage({});
        if (floating_)
            floatingPreview_->setImage({});
    }
    if (!s["error"].toString().isEmpty()) {
        banner_->setText(s["error"].toString());
        banner_->show();
    }
    refreshTaskInfo();
}
QJsonObject Window::selectedTask(bool trigger) const {
    auto l = trigger ? triggerList_ : taskList_;
    return l->currentItem() ? l->currentItem()->data(Qt::UserRole).toJsonObject() : QJsonObject{};
}
QJsonObject Window::selectedTeam() const {
    return teamList_->currentItem() ? teamList_->currentItem()->data(Qt::UserRole).toJsonObject()
                                    : QJsonObject{};
}
void Window::setCatalog(const QJsonObject &c) {
    catalog_ = c;
#ifdef OKWW_NATIVE_ONLY
    refreshScripts();
#endif
    const auto prefs = c["native_preferences"].toObject();
    minimizeToTray_ = prefs["close_to_tray"].toBool();
    auto hotkey = prefs["hotkey"].toString("F9");
    if (hotkey != hotkey_) {
        UnregisterHotKey(reinterpret_cast<HWND>(winId()), 1);
        hotkey_ = hotkey;
        if (QStringList{"F9", "F10", "F11", "F12"}.contains(hotkey_))
            RegisterHotKey(reinterpret_cast<HWND>(winId()), 1, MOD_NOREPEAT,
                           VK_F9 + hotkey_.mid(1).toInt() - 9);
    }
    loading_ = true;
    auto fill = [&](QListWidget *list, QJsonArray values) {
        auto key = [](const QJsonObject &o) {
            return o.contains("id")
                       ? o["id"].toString()
                       : QString::fromUtf8(QJsonDocument(o["team"].toArray()).toJson(QJsonDocument::Compact));
        };
        auto old =
            list->currentItem() ? key(list->currentItem()->data(Qt::UserRole).toJsonObject()) : QString{};
        QSignalBlocker block(list);
        list->clear();
        for (auto v : values) {
            auto o = v.toObject();
            auto display = o["name"].toString();
            if (list == teamList_) {
                QStringList labels;
                for (auto member : o["team"].toArray())
                    labels << characterName(member.toString());
                display = labels.join("，");
            }
            auto item = new QListWidgetItem(display, list);
            item->setData(Qt::UserRole, o);
            item->setToolTip(display);
            if (key(o) == old)
                list->setCurrentItem(item);
        }
        if (list->currentRow() < 0 && list->count())
            list->setCurrentRow(0);
    };
    QJsonArray tasks, triggers;
    for (auto v : c["tasks"].toArray())
        (v.toObject()["trigger"].toBool() ? triggers : tasks).append(v);
    fill(taskList_, tasks);
    fill(triggerList_, triggers);
    fill(globalList_, c["globals"].toArray());
    fill(teamList_, c["teams"].toArray());
    loading_ = false;
    showTask(false);
    showTask(true);
    showGlobal();
    showTeam();
}
void Window::buildFields(QWidget *target, const QJsonArray &fields,
                         std::function<void(QString, QJsonValue)> changed) {
    const bool automatic = target->property("autoSaveFields").toBool();
    auto layout = qobject_cast<QVBoxLayout *>(target->layout());
    if (!layout)
        layout = new QVBoxLayout(target);
    layout->setContentsMargins(6, 4, 6, 8);
    layout->setSpacing(0);
    while (auto child = layout->takeAt(0)) {
        if (child->widget()) {
            for (auto object : child->widget()->findChildren<QObject *>())
                object->blockSignals(true);
            child->widget()->blockSignals(true);
            child->widget()->hide();
            child->widget()->deleteLater();
        }
        delete child;
    }
    for (auto value : fields) {
        auto f = value.toObject();
        auto key = f["key"].toString(), kind = f["kind"].toString();
        const QJsonValue val = f.value("value");
        auto group = new QWidget;
        auto timer = automatic ? new QTimer(group) : nullptr;
        if (timer) {
            timer->setSingleShot(true);
            timer->setInterval(350);
            timer->setProperty("fieldAutoSave", true);
        }
        const auto latest = std::make_shared<QJsonValue>(val);
        const auto commit = [timer, changed, key, latest] {
            timer->stop();
            changed(key, *latest);
        };
        if (timer) connect(timer, &QTimer::timeout, group, commit);
        const auto edited = [timer, target, latest](QJsonValue value) {
            *latest = value;
            if (auto status = target->findChild<QLabel *>("configSaveStatus")) status->setText("正在保存…");
            timer->start();
        };
        if (timer)
            connect(qApp, &QApplication::focusChanged, group, [group, timer, commit](QWidget *old, QWidget *next) {
                if (timer->isActive() && old && group->isAncestorOf(old) && (!next || !group->isAncestorOf(next)))
                    commit();
            });
        group->setObjectName("configRow");
        auto row = new QHBoxLayout(group);
        row->setContentsMargins(14, 8, 14, 8);
        row->setSpacing(18);
        auto labels = new QWidget;
        auto labelLayout = new QVBoxLayout(labels);
        labelLayout->setContentsMargins(0, 0, 0, 0);
        labelLayout->setSpacing(3);
        auto label = new QLabel(f["label"].toString());
        if (target->property("boldLabels").toBool())
            label->setStyleSheet("font-weight:600;");
        label->setWordWrap(true);
        labelLayout->addWidget(label);
        QWidget *editor = nullptr;
        if (kind == "boolean") {
            auto w = new classic::Switch;
            w->setChecked(val.toBool());
            connect(w, &QAbstractButton::toggled, this, [changed, key](bool b) { changed(key, b); });
            editor = w;
        } else if (kind == "select") {
            auto w = new classic::ComboBox;
            auto opts = f["options"].toArray(), labels = f["option_labels"].toArray();
            for (int i = 0; i < opts.size(); ++i) {
                w->addItem(i < labels.size() ? labels[i].toString() : opts[i].toVariant().toString(),
                           opts[i].toVariant());
                if (opts[i] == val)
                    w->setCurrentIndex(i);
            }
            connect(w, &QComboBox::activated, this,
                    [changed, key, w] { changed(key, QJsonValue::fromVariant(w->currentData())); });
            editor = w;
        } else if (kind == "integer") {
            auto w = new QSpinBox;
            w->setRange(f["minimum"].isDouble() ? f["minimum"].toInt() : -1000000000,
                        f["maximum"].isDouble() ? f["maximum"].toInt() : 1000000000);
            w->setValue(val.toInt());
            if (automatic) connect(w, &QSpinBox::valueChanged, group, edited);
            connect(w, &QSpinBox::editingFinished, group, [changed, key, w, val, timer, commit] {
                if (timer) { if (timer->isActive()) commit(); }
                else if (w->value() != val.toInt())
                    changed(key, w->value());
            });
            w->setObjectName("field-" + key);
            w->setAccessibleName(f["label"].toString());
            editor = f["minimum"].isDouble() && f["maximum"].isDouble()
                         ? classic::sliderEditor(w) : w;
        } else if (kind == "number") {
            auto w = new QDoubleSpinBox;
            w->setDecimals(5);
            w->setRange(f["minimum"].isDouble() ? f["minimum"].toDouble() : -1e9,
                        f["maximum"].isDouble() ? f["maximum"].toDouble() : 1e9);
            w->setValue(val.toDouble());
            if (automatic) connect(w, &QDoubleSpinBox::valueChanged, group, edited);
            connect(w, &QDoubleSpinBox::editingFinished, group, [changed, key, w, val, timer, commit] {
                if (timer) { if (timer->isActive()) commit(); }
                else if (w->value() != val.toDouble())
                    changed(key, w->value());
            });
            w->setObjectName("field-" + key);
            w->setAccessibleName(f["label"].toString());
            editor = f["minimum"].isDouble() && f["maximum"].isDouble()
                         ? classic::sliderEditor(w) : w;
        } else if (kind == "multi_selection" && !f["options"].toArray().isEmpty()) {
            auto w = new QWidget;
            auto grid = new QGridLayout(w);
            grid->setContentsMargins(0, 0, 0, 0);
            grid->setHorizontalSpacing(12);
            grid->setVerticalSpacing(6);
            const auto options = f["options"].toArray(), optionLabels = f["option_labels"].toArray();
            QVector<QCheckBox *> checks;
            for (int i = 0; i < options.size(); ++i) {
                auto check = new QCheckBox(i < optionLabels.size() ? optionLabels[i].toString()
                                                                   : options[i].toVariant().toString());
                check->setChecked(val.toArray().contains(options[i]));
                grid->addWidget(check, i / 2, i % 2);
                checks << check;
            }
            for (auto check : checks)
                connect(check, &QCheckBox::toggled, this, [changed, key, checks, options] {
                    QJsonArray values;
                    for (int i = 0; i < checks.size(); ++i)
                        if (checks[i]->isChecked())
                            values.append(options[i]);
                    changed(key, values);
                });
            editor = w;
        } else if (kind == "list") {
            auto wrap = new QWidget;
            auto vl = new QVBoxLayout(wrap);
            vl->setContentsMargins(0, 0, 0, 0);
            auto list = new QListWidget;
            list->setMaximumHeight(180);
            auto options = f["options"].toArray();
            auto labels = f["option_labels"].toArray();
            auto labelFor = [options, labels](QJsonValue v) {
                int i = -1;
                for (int n = 0; n < options.size(); ++n)
                    if (options[n] == v) {
                        i = n;
                        break;
                    }
                return i >= 0 && i < labels.size() ? labels[i].toString() : v.toVariant().toString();
            };
            for (auto value : val.toArray()) {
                auto item = new QListWidgetItem(labelFor(value), list);
                item->setData(Qt::UserRole, value.toVariant());
            }
            vl->addWidget(list);
            auto bar = new QHBoxLayout;
            auto choices = new classic::ComboBox;
            choices->setEditable(options.isEmpty());
            for (auto option : options)
                choices->addItem(labelFor(option), option.toVariant());
            bar->addWidget(choices, 1);
            auto save = [changed, key, list] {
                QJsonArray values;
                for (int i = 0; i < list->count(); ++i)
                    values.append(QJsonValue::fromVariant(list->item(i)->data(Qt::UserRole)));
                changed(key, values);
            };
            bar->addWidget(button(
                "添加",
                [this, list, choices, options, val, f, save, labelFor] {
                    QJsonValue value = options.isEmpty() ? QJsonValue(choices->currentText())
                                                         : QJsonValue::fromVariant(choices->currentData());
                    if (options.isEmpty() && !val.toArray().isEmpty() && val.toArray().first().isDouble()) {
                        bool ok = false;
                        double n = choices->currentText().toDouble(&ok);
                        if (!ok) {
                            log("请输入数值");
                            return;
                        }
                        value = n;
                    }
                    if (!f["allow_duplication"].toBool())
                        for (int i = 0; i < list->count(); ++i)
                            if (QJsonValue::fromVariant(list->item(i)->data(Qt::UserRole)) == value)
                                return;
                    auto item = new QListWidgetItem(labelFor(value), list);
                    item->setData(Qt::UserRole, value.toVariant());
                    save();
                },
                this));
            vl->addLayout(bar);
            auto order = new QHBoxLayout;
            order->addWidget(button(
                "上移",
                [list, save] {
                    int i = list->currentRow();
                    if (i > 0) {
                        list->insertItem(i - 1, list->takeItem(i));
                        list->setCurrentRow(i - 1);
                        save();
                    }
                },
                this));
            order->addWidget(button(
                "下移",
                [list, save] {
                    int i = list->currentRow();
                    if (i >= 0 && i + 1 < list->count()) {
                        list->insertItem(i + 1, list->takeItem(i));
                        list->setCurrentRow(i + 1);
                        save();
                    }
                },
                this));
            order->addWidget(button(
                "移除",
                [list, save] {
                    if (list->currentRow() >= 0) {
                        delete list->takeItem(list->currentRow());
                        save();
                    }
                },
                this));
            vl->addLayout(order);
            editor = wrap;
        } else if (kind == "json") {
            auto wrap = new QWidget;
            auto l = new QVBoxLayout(wrap);
            l->setContentsMargins(0, 0, 0, 0);
            auto input = new QPlainTextEdit;
            input->setMaximumHeight(180);
            const auto encoded = QJsonDocument(QJsonArray{val}).toJson(QJsonDocument::Compact);
            input->setPlainText(QString::fromUtf8(encoded.mid(1, encoded.size() - 2)));
            l->addWidget(input);
            const auto save = [this, changed, key, input] {
                QJsonParseError error;
                const auto document = QJsonDocument::fromJson('[' + input->toPlainText().toUtf8() + ']', &error);
                if (error.error != QJsonParseError::NoError || !document.isArray() || document.array().size() != 1) {
                    log("参数未保存：请输入有效的 JSON 值。");
                    return;
                }
                changed(key, document.array()[0]);
            };
            if (automatic) {
                connect(input, &QPlainTextEdit::textChanged, group, [target, input, timer, edited] {
                    QJsonParseError error;
                    const auto doc = QJsonDocument::fromJson('[' + input->toPlainText().toUtf8() + ']', &error);
                    if (error.error == QJsonParseError::NoError && doc.isArray() && doc.array().size() == 1) {
                        input->setProperty("autoSaveInvalid", false);
                        edited(doc.array()[0]);
                    } else {
                        input->setProperty("autoSaveInvalid", true);
                        timer->stop();
                        if (auto status = target->findChild<QLabel *>("configSaveStatus"))
                            status->setText("输入不完整，尚未保存；上一次有效配置已保留。");
                    }
                });
            } else l->addWidget(button("保存", save, this));
            editor = wrap;
        } else if (kind == "multiline") {
            auto wrap = new QWidget;
            auto l = new QVBoxLayout(wrap);
            l->setContentsMargins(0, 0, 0, 0);
            auto w = new QPlainTextEdit;
            w->setMaximumHeight(160);
            w->setPlainText(val.isArray() ? QString::fromUtf8(QJsonDocument(val.toArray()).toJson())
                                          : val.toString());
            l->addWidget(w);
            if (automatic) connect(w, &QPlainTextEdit::textChanged, group, [edited, w] { edited(w->toPlainText()); });
            else l->addWidget(button(
                "保存",
                [this, changed, key, w, kind] {
                    if (kind == "list") {
                        auto doc = QJsonDocument::fromJson(w->toPlainText().toUtf8());
                        if (!doc.isArray()) {
                            log("请输入 JSON 列表，例如 [1, 2, 3]");
                            return;
                        }
                        changed(key, doc.array());
                    } else
                        changed(key, w->toPlainText());
                },
                this));
            editor = wrap;
        } else {
            auto w = new QLineEdit(val.toVariant().toString());
            if (f["secret"].toBool())
                w->setEchoMode(QLineEdit::Password);
            if (automatic) connect(w, &QLineEdit::textEdited, group, edited);
            connect(w, &QLineEdit::editingFinished, group, [changed, key, w, val, timer, commit] {
                if (timer) { if (timer->isActive()) commit(); }
                else if (w->text() != val.toVariant().toString())
                    changed(key, w->text());
            });
            editor = w;
        }
        editor->setObjectName("field-" + key);
        editor->setAccessibleName(f["label"].toString());
        if (kind != "boolean") {
            editor->setMinimumWidth(230);
            editor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        }
        row->addWidget(labels, 1);
        row->addWidget(editor, kind == "boolean" ? 0 : 1);
        if (!f["description"].toString().isEmpty()) {
            auto help = new QLabel(f["description"].toString());
            help->setWordWrap(true);
            help->setObjectName("caption");
            labelLayout->addWidget(help);
        }
        layout->addWidget(group);
    }
    if (automatic) {
        auto status = new QLabel("修改后自动保存");
        status->setObjectName("configSaveStatus");
        status->setProperty("caption", true);
        status->setWordWrap(true);
        layout->addWidget(status);
    }
    layout->addStretch();
}
void Window::flushFieldSaves() {
    for (auto timer : findChildren<QTimer *>())
        if (timer->property("fieldAutoSave").toBool() && timer->isActive()) {
            timer->stop();
            QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection);
        }
}
void Window::showTask(bool trigger) {
    if (!loading_)
        renderTaskCards(trigger);
}
void Window::showGlobal() {
    if (!loading_)
        renderGlobalCards();
}
void Window::showTeam() {
    if (loading_)
        return;
    if (code_->document()->isModified() && selectedTeam()["team"].toArray() != editingTeam_ &&
        !settleCodeEdits()) {
        restoreCodeSelection();
        return;
    }
    auto t = selectedTeam();
    auto mode = t["mode"].toString();
    const bool hasModule = !t["source_files"].toArray().isEmpty();
    if (teamModeButton_) {
        teamModeButton_->setText(mode == "import"       ? "切换为内置代码"
                                 : t["python"].toBool() ? "切换为工坊代码"
                                                        : "切换为导入代码");
        teamModeButton_->setEnabled(hasModule && !teamOperation_);
        auto tint = [&](QPushButton *button, const QString &target) {
            QPixmap picture = classic::fluentIcon("Sync").pixmap(32, 32);
            QPainter painter(&picture);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(picture.rect(), target == "import" ? QColor("#1479ca") : QColor("#df303c"));
            painter.end();
            button->setProperty("fluentIcon", QVariant{});
            button->setIcon(QIcon(picture));
        };
        tint(teamModeButton_, mode == "import" ? "builtin" : "import");
        bool allImported = true, any = false;
        for (auto v : catalog_["teams"].toArray()) {
            auto team = v.toObject();
            if (team["source_files"].toArray().isEmpty())
                continue;
            any = true;
            allImported &= team["mode"] == "import";
        }
        auto target = allImported && any ? QString("builtin") : QString("import");
        allTeamModesButton_->setProperty("targetMode", target);
        allTeamModesButton_->setText(target == "builtin" ? "切换全部队伍为内置代码"
                                                         : "切换全部队伍为导入代码");
        allTeamModesButton_->setEnabled(any && !teamOperation_);
        tint(allTeamModesButton_, target);
    }
    teamStatus_->setText(mode == "import"    ? "当前使用：导入连招"
                         : mode == "builtin" ? "当前使用：内置连招（导入副本已保留）"
                                             : "当前使用：内置连招（尚未导入）");
    if (mode == "builtin" && !hasModule)
        teamStatus_->setText("当前使用：内置连招");
    if (t["python"].toBool())
        teamStatus_->setText(mode == "import" ? "当前使用：Python 工坊连招"
                                              : "当前使用：内置 C++ 连招（工坊代码已保留）");
    if (mode == "import" && t["native"].toBool() && !t["available"].toBool())
        teamStatus_->setText(teamStatus_->text() + "\n" + t["error"].toString());
    teamStatus_->setWordWrap(true);
    for (auto control : {static_cast<QWidget *>(teamList_), static_cast<QWidget *>(member_),
                         static_cast<QWidget *>(code_), static_cast<QWidget *>(codeSource_),
                         static_cast<QWidget *>(codeView_)})
        control->setEnabled(!teamOperation_);
    if (teamOperation_) {
        teamStatus_->setText("正在切换队伍代码…");
        saveCodeButton_->setEnabled(false);
        return;
    }
    QSignalBlocker block(member_);
    auto selected = member_->currentData();
    member_->clear();
    auto members = t["team"].toArray();
    for (int i = 0; i < members.size(); ++i)
        member_->addItem(characterName(members[i].toString()), members[i].toString());
    member_->setCurrentIndex(std::max(0, member_->findData(selected)));
    if (codeView_) {
        QSignalBlocker viewBlock(codeView_);
        const auto view =
            t["team"].toArray() == editingTeam_ ? codeView_->currentData().toString() : QString("active");
        codeView_->clear();
        codeView_->addItem("当前使用的代码", "active");
        if (hasModule && mode == "builtin")
            codeView_->addItem("保留的导入代码（仅查看）", "import");
        codeView_->setCurrentIndex(std::max(0, codeView_->findData(view)));
        codeView_->setVisible(hasModule && mode == "builtin");
    }
    updateCharacterPortrait();
    loadCode();
}
void Window::loadCode() {
#ifdef OKWW_NATIVE_ONLY
    const auto same = selectedTeam()["team"].toArray() == editingTeam_ &&
                      member_->currentData().toString() == editingMember_ &&
                      codeView_->currentData().toString() == editingView_ &&
                      selectedTeam()["mode"].toString() == editingMode_;
    requestTeamCode(same ? codeSource_->currentData().toString() : QString{});
#else
    if (loading_ || !engineReady_)
        return;
    auto team = selectedTeam();
    if (team.isEmpty()) {
        code_->clear();
        return;
    }
    auto member = member_->currentData().toString();
    updateCharacterPortrait();
    const auto selected = team["team"].toArray();
    if (code_->document()->isModified()) {
        if (selected == editingTeam_ && member == editingMember_)
            return;
        if (QMessageBox::question(this, "代码尚未保存", "切换角色会丢弃未保存的编辑。是否继续？",
                                  QMessageBox::Discard | QMessageBox::Cancel,
                                  QMessageBox::Cancel) != QMessageBox::Discard) {
            QSignalBlocker guard(teamList_);
            for (int i = 0; i < teamList_->count(); ++i)
                if (teamList_->item(i)->data(Qt::UserRole).toJsonObject()["team"].toArray() == editingTeam_)
                    teamList_->setCurrentRow(i);
            QSignalBlocker block(member_);
            member_->clear();
            auto restored = selectedTeam();
            auto members = restored["team"].toArray(), labels = restored["labels"].toArray();
            if (restored["native"].toBool())
                members = labels = restored["source_files"].toArray();
            for (int i = 0; i < members.size(); ++i)
                member_->addItem(i < labels.size() ? labels[i].toString() : members[i].toString(),
                                 members[i].toString());
            member_->setCurrentIndex(member_->findData(editingMember_));
            return;
        }
    }
    editingTeam_ = selected;
    editingMember_ = member;
    send({{"op", "team_code"}, {"team", team["team"]}, {"member", member}}, [this, member, selected](auto r) {
        if (member_->currentData().toString() == member && selectedTeam()["team"].toArray() == selected) {
            code_->setPlainText(r["code"].toString());
            code_->setReadOnly(r["read_only"].toBool());
            if (saveCodeButton_)
                saveCodeButton_->setEnabled(!r["read_only"].toBool());
            code_->document()->setModified(false);
        }
    });
#endif
}
void Window::smoke(const QString &report, bool integration) {
    auto timer = new QTimer(this);
    timer->setInterval(200);
    auto deadline = std::make_shared<QElapsedTimer>();
    deadline->start();
    connect(timer, &QTimer::timeout, this, [this, timer, deadline, report, integration] {
        if (!engineReady_ && deadline->elapsed() < 30000)
            return;
        timer->stop();
        QJsonObject result{{"engine_ready", engineReady_},
                           {"tasks", catalog_["tasks"].toArray().size()},
                           {"teams", catalog_["teams"].toArray().size()}};
        result["ui_layout"] = "original-personal";
        result["ui_icons"] = "original-fluent-assets";
        result["task_cards"] = taskCards_.size();
        QJsonArray navigation;
        for (int i = 0; i < navigation_->count(); ++i)
            navigation.append(navigation_->item(i)->data(Qt::UserRole + 1).toString());
        for (int i = 0; i < bottomNavigation_->count(); ++i)
            navigation.append(bottomNavigation_->item(i)->data(Qt::UserRole + 1).toString());
        result["navigation"] = navigation;
#ifdef OKWW_NATIVE_ONLY
        bool pythonLoaded = GetModuleHandleW(L"python3.dll") != nullptr;
        for (int minor = 8; minor <= 15; ++minor)
            pythonLoaded |=
                GetModuleHandleW(reinterpret_cast<LPCWSTR>(QString("python3%1.dll").arg(minor).utf16())) !=
                nullptr;
        result["execution_engine"] = "C++ in-process";
        result["python_loaded"] = pythonLoaded;
        bool importsValid = true;
        QJsonArray nativeTeams;
        for (auto value : catalog_["teams"].toArray()) {
            const auto team = value.toObject();
            if (team["mode"] == "import" && !team["available"].toBool())
                importsValid = false;
            nativeTeams.append(QJsonObject{
                {"team", team["team"]}, {"mode", team["mode"]}, {"available", team["available"]}});
        }
        result["import_teams_valid"] = importsValid;
        result["native_teams"] = nativeTeams;
#endif
        try {
            backend_.start(Config::defaults(), true);
            uint64_t seq = 0;
            auto image = backend_.preview(QSize(960, 540), seq);
            preview_->setImage(image);
            result["native_preview"] = !image.isNull();
            backend_.testMove();
            result["dry_run_move"] = true;
            if (integration) {
                result["note"] = "GUI metadata and native preview smoke; see native integration report for "
                                 "input bridge tests";
            }
            grab().save(QFileInfo(report).absolutePath() + "/native-ui.png");
            QJsonObject pageReports;
            const QStringList checkPages{"dualpc",
                                         "tasks",
                                         "triggers",
                                         "characters",
                                         "settings",
                                         "Game Hotkey",
                                         "Monthly Card Config",
                                         "Notification",
                                         "Character Config",
                                         "Task Recovery",
                                         "schedule",
                                         "about"};
            for (int page = 0; page < checkPages.size(); ++page) {
                navigate(checkPages[page]);
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                const auto path =
                    QFileInfo(report).absolutePath() + QString("/native-page-%1.png").arg(page + 1);
                pageReports[checkPages[page]] = grab().save(path);
                auto safeName = checkPages[page];
                safeName.replace(' ', '_');
                grab().save(QFileInfo(report).absolutePath() + "/native-" + safeName + ".png");
            }
            navigate("characters");
            QJsonArray editorRows;
            bool editorValid = true;
            for (int teamIndex = 0; teamIndex < teamList_->count(); ++teamIndex) {
                teamList_->setCurrentRow(teamIndex);
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                const auto team = selectedTeam();
                editorValid &= member_->count() == 3;
                for (int role = 0; role < member_->count(); ++role) {
                    member_->setCurrentIndex(role);
                    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                    const auto member = member_->currentData().toString();
                    bool valid = team["team"].toArray().contains(member) &&
                                 characterPortrait_->toolTip() == characterName(member) &&
                                 !characterPortrait_->pixmap().isNull() && !code_->toPlainText().isEmpty() &&
                                 codeSource_->count() > 0 && !teamCompatibility_->text().contains("失败");
                    if (team["mode"] == "builtin")
                        valid &= code_->isReadOnly();
                    else
                        valid &=
                            team["source_files"].toArray().contains(codeSource_->currentData().toString());
                    editorValid &= valid;
                    editorRows.append(QJsonObject{{"team", team["team"]},
                                                  {"member", member},
                                                  {"source", codeSource_->currentData().toString()},
                                                  {"mode", team["mode"]},
                                                  {"valid", valid}});
                    grab().save(QFileInfo(report).absolutePath() +
                                QString("/native-team-%1-character-%2.png").arg(teamIndex + 1).arg(role + 1));
                }
            }
            result["team_editor_valid"] = editorValid;
            result["team_editor_roles"] = editorRows;
            navigate("tasks");
            if (auto daily = taskCards_.value("DailyTask2")) {
                daily->setExpanded(true);
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                grab().save(QFileInfo(report).absolutePath() + "/native-task-expanded.png");
                daily->setExpanded(false);
            }
            navigate("dualpc");
            if (auto preview = findChild<QPushButton *>("previewToggle")) {
                preview->setChecked(true);
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                grab().save(QFileInfo(report).absolutePath() + "/native-preview-expanded.png");
            }
            result["page_images"] = pageReports;
            resize(1000, 680);
            uiPreferences_["theme"] = "dark";
            applyTheme();
            navigate("tasks");
            if (auto daily = taskCards_.value("DailyTask2"))
                daily->setExpanded(true);
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            result["dark_narrow_image"] =
                grab().save(QFileInfo(report).absolutePath() + "/native-dark-narrow.png");
            writeJson(report, result);
            backend_.stop();
        } catch (const std::exception &e) {
            result["error"] = errorText(e);
            writeJson(report, result);
        }
        closing_ = true;
        QTimer::singleShot(100, this, &QWidget::close);
    });
    timer->start();
}
} // namespace okww
