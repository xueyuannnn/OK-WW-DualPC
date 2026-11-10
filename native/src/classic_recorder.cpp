#include "window.hpp"
#ifdef OKWW_NATIVE_ONLY
#include "runtime/scripts.hpp"
namespace okww {
namespace {
class RecordingWorker {
    Backend &backend_;
    Config config_;
    runtime::ExecutionGate gate_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<QJsonObject> queue_;
    QString error_;

  public:
    RecordingWorker(Backend &backend, Config config) : backend_(backend), config_(std::move(config)) {}
    ~RecordingWorker() { stop(); }
    void start() {
        const auto frame = backend_.latestFrame();
        if (!frame)
            fail("采集画面尚未就绪");
        backend_.arm(true);
        const auto epoch = gate_.start();
        thread_ = std::thread([this, epoch, raw = QSize(frame->width, frame->height)] {
            try {
                runtime::SteadyClock clock;
                runtime::Execution execution(gate_, epoch, clock, [this] {
                    if (!backend_.heartbeat())
                        throw runtime::Cancelled();
                });
                runtime::Input input(
                    config_, raw, backend_.snapshot()["box"].toObject()["absolute"].toBool(),
                    [this](auto event) { return backend_.nativeCommand(event); }, execution);
                for (;;) {
                    execution.checkpoint();
                    QJsonObject event;
                    {
                        std::unique_lock lock(mutex_);
                        if (queue_.empty())
                            condition_.wait_for(lock, std::chrono::milliseconds(15));
                        if (!queue_.empty()) {
                            event = queue_.front();
                            queue_.pop_front();
                        }
                    }
                    if (event.isEmpty())
                        continue;
                    const auto op = event["op"].toString(), key = event["key"].toString();
                    const QPointF point(event["x"].toDouble() * (config_.n("capture.output_width") - 1),
                                        event["y"].toDouble() * (config_.n("capture.output_height") - 1));
                    if (op == "key_down")
                        input.keyDown(key);
                    else if (op == "key_up")
                        input.keyUp(key);
                    else if (op == "move")
                        input.move(point);
                    else if (op == "mouse_down") {
                        input.move(point);
                        input.buttonDown(key);
                    } else if (op == "mouse_up")
                        input.buttonUp(key);
                    else if (op == "wheel")
                        input.scroll(event["steps"].toInt(), point);
                }
            } catch (const runtime::Cancelled &) {
            } catch (const std::exception &error) {
                std::lock_guard lock(mutex_);
                error_ = errorText(error);
            }
        });
    }
    void push(QJsonObject event) {
        std::lock_guard lock(mutex_);
        if (queue_.size() > 200)
            fail("录制输入积压，请停止录制并检查盒子延迟");
        queue_.push_back(std::move(event));
        condition_.notify_one();
    }
    QString error() {
        std::lock_guard lock(mutex_);
        return error_;
    }
    void stop() noexcept {
        gate_.stop();
        condition_.notify_one();
        if (thread_.joinable())
            thread_.join();
        try {
            backend_.arm(false);
        } catch (...) {
        }
    }
};
class RecordingSurface final : public QWidget {
    QImage image_;
    QSet<QString> keys_, buttons_;
    QPointF position_;
    QElapsedTimer moveClock_;
    QRectF display() const {
        if (image_.isNull())
            return {};
        QSizeF size(image_.size());
        size.scale(contentsRect().size(), Qt::KeepAspectRatio);
        return {(width() - size.width()) / 2, (height() - size.height()) / 2, size.width(), size.height()};
    }
    QPointF point(QPointF p) const {
        const auto r = display();
        return {std::clamp((p.x() - r.x()) / r.width(), 0., 1.),
                std::clamp((p.y() - r.y()) / r.height(), 0., 1.)};
    }
    static QString key(QKeyEvent *event) {
        if (event->key() >= Qt::Key_A && event->key() <= Qt::Key_Z)
            return QString(QChar('a' + event->key() - Qt::Key_A));
        if (event->key() >= Qt::Key_0 && event->key() <= Qt::Key_9)
            return QString(QChar('0' + event->key() - Qt::Key_0));
        if (event->key() >= Qt::Key_F1 && event->key() <= Qt::Key_F12)
            return "f" + QString::number(event->key() - Qt::Key_F1 + 1);
        const QMap<int, QString> named{
            {Qt::Key_Control, "ctrl"}, {Qt::Key_Shift, "shift"},   {Qt::Key_Alt, "alt"},
            {Qt::Key_Space, "space"},  {Qt::Key_Escape, "esc"},    {Qt::Key_Return, "enter"},
            {Qt::Key_Enter, "enter"},  {Qt::Key_Tab, "tab"},       {Qt::Key_Backspace, "backspace"},
            {Qt::Key_Left, "left"},    {Qt::Key_Right, "right"},   {Qt::Key_Up, "up"},
            {Qt::Key_Down, "down"},    {Qt::Key_Delete, "delete"}, {Qt::Key_Home, "home"},
            {Qt::Key_End, "end"},      {Qt::Key_PageUp, "pageup"}, {Qt::Key_PageDown, "pagedown"}};
        return named.value(event->key(), event->text().toLower());
    }
    void emitInput(QString op, QString key = {}, QPointF position = {}) {
        if (recording && action)
            action({{"op", op}, {"key", key}, {"x", position.x()}, {"y", position.y()}});
    }

  protected:
    bool event(QEvent *event) override {
        if (recording && event->type() == QEvent::ShortcutOverride) {
            event->accept();
            return true;
        }
        if (recording && (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)) {
            auto e = static_cast<QKeyEvent *>(event);
            if (e->isAutoRepeat())
                return true;
            const auto name = key(e);
            try {
                runtime::Input::keyCode(name);
            } catch (...) {
                return true;
            }
            if (event->type() == QEvent::KeyPress && !keys_.contains(name)) {
                keys_.insert(name);
                emitInput("key_down", name);
            } else if (event->type() == QEvent::KeyRelease && keys_.remove(name))
                emitInput("key_up", name);
            return true;
        }
        return QWidget::event(event);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.fillRect(rect(), QColor("#202020"));
        if (!image_.isNull())
            p.drawImage(display(), image_);
    }
    void mousePressEvent(QMouseEvent *event) override {
        setFocus();
        if (!recording || !display().contains(event->position()))
            return;
        const QString name = event->button() == Qt::LeftButton    ? "left"
                             : event->button() == Qt::RightButton ? "right"
                                                                  : "middle";
        position_ = point(event->position());
        buttons_.insert(name);
        emitInput("mouse_down", name, position_);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        const QString name = event->button() == Qt::LeftButton    ? "left"
                             : event->button() == Qt::RightButton ? "right"
                                                                  : "middle";
        if (buttons_.remove(name))
            emitInput("mouse_up", name, position_);
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (!recording || buttons_.isEmpty() || (moveClock_.isValid() && moveClock_.elapsed() < 33))
            return;
        moveClock_.restart();
        position_ = point(event->position());
        emitInput("move", {}, position_);
    }
    void wheelEvent(QWheelEvent *event) override {
        if (!recording || !display().contains(event->position()))
            return;
        const auto p = point(event->position());
        if (action)
            action({{"op", "wheel"}, {"steps", event->angleDelta().y() / 120}, {"x", p.x()}, {"y", p.y()}});
        event->accept();
    }
    void focusOutEvent(QFocusEvent *event) override {
        releaseHeld();
        QWidget::focusOutEvent(event);
    }

  public:
    bool recording = false;
    std::function<void(QJsonObject)> action;
    RecordingSurface() {
        setFocusPolicy(Qt::StrongFocus);
        setMinimumSize(640, 360);
    }
    void image(QImage image) {
        image_ = std::move(image);
        update();
    }
    void releaseHeld() {
        for (const auto &name : keys_)
            emitInput("key_up", name);
        for (const auto &name : buttons_)
            emitInput("mouse_up", name, position_);
        keys_.clear();
        buttons_.clear();
    }
};
} // namespace
void Window::recordScript() {
    if (automation_ || pendingStart_ || busy_ || scriptOperation_ || !nativeEngine_ ||
        !nativeEngine_->idle()) {
        QMessageBox::information(this, "录制", "请先暂停自动任务，等待按键释放，再开始录制。");
        return;
    }
    if (!backend_.latestFrame() || !backend_.snapshot()["box"].toObject()["connected"].toBool()) {
        QMessageBox::information(this, "录制", "请先在双机设置中连接采集卡和盒子，再打开录制。");
        return;
    }
    QDialog dialog(this);
    dialog.setObjectName("scriptRecorderDialog");
    auto layout = classic::dialogLayout(dialog, "录制脚本",
        "开始录制后，在下方画面操作键鼠，操作会同步发送到游戏机。" + hotkey_ +
        " 可立即暂停输出；失去画面焦点时释放按键。", {1080, 760});
    auto surface = new RecordingSurface;
    surface->setObjectName("recordingPreview");
    layout->addWidget(surface, 1);
    auto status = new QLabel("尚未开始录制");
    status->setObjectName("statusBadge");
    layout->addWidget(status);
    auto bar = new QHBoxLayout;
    auto start = new QPushButton("开始录制"), stop = new QPushButton("停止录制"),
         save = new QPushButton("写入脚本"), cancel = new QPushButton("取消");
    start->setProperty("primary", true);
    save->setProperty("primary", true);
    for (auto button : {start, stop, save, cancel}) {
        button->setAutoDefault(false);
        bar->addWidget(button);
    }
    layout->addLayout(bar);
    stop->setEnabled(false);
    save->setEnabled(false);
    RecordingWorker worker(backend_, config_);
    QElapsedTimer clock;
    QJsonArray events;
    const auto finish = [&] {
        surface->releaseHeld();
        surface->recording = false;
        worker.stop();
        start->setEnabled(false);
        stop->setEnabled(false);
        save->setEnabled(!events.isEmpty());
        status->setText(QString("录制结束，共 %1 个事件；写入后仍需点击保存。").arg(events.size()));
    };
    surface->action = [&](QJsonObject event) {
        try {
            if (events.size() >= 100000)
                fail("录制事件达到上限，请分段录制");
            event["time"] = clock.elapsed() / 1000.;
            worker.push(event);
            events.append(event);
        } catch (const std::exception &error) {
            surface->recording = false;
            worker.stop();
            stop->setEnabled(false);
            status->setText(errorText(error));
        }
    };
    connect(start, &QPushButton::clicked, &dialog, [&] {
        try {
            worker.start();
            clock.start();
            surface->recording = true;
            surface->setFocus();
            start->setEnabled(false);
            stop->setEnabled(true);
            status->setText("正在录制 · 在采集画面内操作");
        } catch (const std::exception &error) {
            status->setText(errorText(error));
        }
    });
    connect(stop, &QPushButton::clicked, &dialog, finish);
    connect(save, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    QTimer preview;
    uint64_t previewSequence = 0;
    QSize previewTarget;
    connect(&preview, &QTimer::timeout, &dialog, [&] {
        const QSize target(int(std::ceil(surface->width() * surface->devicePixelRatioF())),
                           int(std::ceil(surface->height() * surface->devicePixelRatioF())));
        if (target != previewTarget)
            previewSequence = 0;
        previewTarget = target;
        auto image = backend_.preview(target, previewSequence);
        if (!image.isNull())
            surface->image(image);
        if (surface->recording && (!worker.error().isEmpty() || !backend_.snapshot()["armed"].toBool())) {
            const auto error = worker.error();
            finish();
            status->setText(error.isEmpty() ? "已暂停输出，录制已结束。" : error);
        }
    });
    preview.start(33);
    recording_ = true;
    const int result = dialog.exec();
    recording_ = false;
    preview.stop();
    if (surface->recording)
        finish();
    worker.stop();
    if (result != QDialog::Accepted || events.isEmpty())
        return;
    const auto generated = runtime::recordedScript(events);
    if (editingScript_.isEmpty()) {
        scriptAction({{"op", "script_create"},
                      {"name", "RecordedTask_" + QDateTime::currentDateTime().toString("MMddHHmmss")}},
                     [this, generated](auto result) {
                         const auto id = result["selected_script"].toString();
                         scriptChoice_->setCurrentIndex(scriptChoice_->findData(id));
                         showScript(id);
                         scriptEditor_->setPlainText(generated);
                         scriptEditor_->document()->setModified(true);
                     });
    } else if (QMessageBox::question(this, "写入录制结果",
                                     "用录制结果替换当前脚本的 run 方法？初始化配置和其他方法保留。") ==
               QMessageBox::Yes) {
        scriptAction({{"op", "script_record"}, {"script", editingScript_}, {"recording", generated}},
                     [this](auto result) {
                         showScript(editingScript_);
                         scriptMessage_->setText("录制内容已写入并保存。");
                     });
    }
}
} // namespace okww
#endif
