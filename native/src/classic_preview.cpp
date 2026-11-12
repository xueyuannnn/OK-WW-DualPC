#include "window.hpp"
namespace okww {
void Window::updatePreview() {
    if (busy_)
        return;
    QSize target;
    bool missingImage = false;
    for (auto view : {preview_, floatingPreview_}) {
        if (!view || !view->isVisible() || view->window()->isMinimized())
            continue;
        target = target.expandedTo(view->pixelSize());
        missingImage |= view->imageSize().isEmpty();
    }
    if (target.isEmpty())
        return;
    // Share one conversion sized for the largest visible preview. A resize or
    // a newly opened view must also refresh when the source frame is unchanged.
    if (target != previewTarget_ || missingImage)
        previewSequence_ = 0;
    previewTarget_ = target;
    const auto image = backend_.preview(target, previewSequence_);
    if (!image.isNull()) {
        ++previewFrames_;
        preview_->setImage(image);
        if (floatingPreview_)
            floatingPreview_->setImage(image);
    }
}
void Window::savePreviewPreferences() {
    try {
        QJsonObject preferences;
        if (QFile::exists(data_ + "/native-preview.json"))
            preferences = readJson(data_ + "/native-preview.json");
        preferences["fps"] = previewFps_;
        preferences["height"] = previewHeight_;
        preferences["topmost"] = previewOnTop_;
        writeJson(data_ + "/native-preview.json", preferences);
    } catch (const std::exception &e) {
        log(errorText(e));
    }
}
QWidget *Window::buildClassicPreview() {
    try {
        if (QFile::exists(data_ + "/native-preview.json")) {
            const auto prefs = readJson(data_ + "/native-preview.json");
            previewFps_ = prefs["fps"].toInt(60);
            previewHeight_ = prefs["height"].toInt(405);
            previewOnTop_ = prefs["topmost"].toBool();
        }
    } catch (...) {
    }
    auto card = new QFrame;
    card->setObjectName("classicCard");
    auto layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(8);
    layout->addWidget(new QLabel("03  画面预览与停止"));
    auto hint = new QLabel("修改采集卡或盒子参数后，点击上方“应用配置”即可生效，无需重启软件。");
    hint->setObjectName("caption");
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto actions = new QHBoxLayout;
    auto show = new QPushButton("开启画面预览");
    show->setObjectName("previewToggle");
    show->setCheckable(true);
    classic::setFluentIcon(show, "View");
    actions->addWidget(show);
    auto floating = new QPushButton("独立预览窗口");
    floating->setObjectName("floatingPreviewToggle");
    classic::setFluentIcon(floating, "Link");
    actions->addWidget(floating);
    connect(floating, &QPushButton::clicked, this, [this] {
        if (!floating_) {
            floating_ = new QWidget(nullptr, Qt::Window);
            floating_->setWindowTitle(product::name + " · 画面预览");
            auto content = new QVBoxLayout(floating_);
            content->setContentsMargins(2, 2, 2, 2);
            floatingPreview_ = new Preview;
            floatingPreview_->setObjectName("floatingPreview");
            content->addWidget(floatingPreview_);
            floating_->resize(960, 560);
            floating_->setWindowFlag(Qt::WindowStaysOnTopHint, previewOnTop_);
        }
        floating_->show();
        floating_->raise();
    });
    actions->addStretch();
    auto stop = new QPushButton("紧急停止键鼠");
    classic::setFluentIcon(stop, "Cancel");
    stop->setToolTip("紧急暂停当前任务与全部实时触发并释放按键，采集预览保持连接。");
    connect(stop, &QPushButton::clicked, this, [this] { pause(); });
    actions->addWidget(stop);
    layout->addLayout(actions);
    auto columns = new QHBoxLayout;
    columns->setSpacing(14);
    const auto column = [&](QString text, QWidget *control) {
        auto box = new QVBoxLayout;
        auto label = new QLabel(text);
        label->setObjectName("caption");
        box->addWidget(label);
        box->addWidget(control);
        columns->addLayout(box, 1);
    };
    auto fps = new classic::ComboBox;
    for (int value : {30, 60, 120, 144, 165, 240, 0})
        fps->addItem(value ? QString::number(value) : "无限制", value);
    fps->setCurrentIndex(std::max(0, fps->findData(previewFps_)));
    previewFps_ = fps->currentData().toInt();
    previewTimer_.setInterval(previewFps_ ? std::max(1, int(1000.0 / previewFps_)) : 1);
    column("预览帧率（FPS）", fps);
    auto size = new classic::ComboBox;
    for (auto entry : {qMakePair(QString("小 · 高 270"), 270), qMakePair(QString("中 · 高 405"), 405),
                       qMakePair(QString("大 · 高 540"), 540), qMakePair(QString("特大 · 高 720"), 720)})
        size->addItem(entry.first, entry.second);
    size->setCurrentIndex(std::max(0, size->findData(previewHeight_)));
    previewHeight_ = size->currentData().toInt();
    column("页面预览大小", size);
    auto top = new QCheckBox("独立窗口始终置顶");
    top->setChecked(previewOnTop_);
    column("窗口显示", top);
    layout->addLayout(columns);
    auto explanation = new QLabel("按窗口大小和屏幕缩放显示清晰画面，最高保留采集原始分辨率。预览帧率独立设置，独立窗口可缩放、置顶。");
    explanation->setObjectName("caption");
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    preview_ = new Preview;
    preview_->setObjectName("embeddedPreview");
    preview_->setFixedHeight(previewHeight_);
    layout->addWidget(preview_);
    preview_->hide();
    previewRate_ = new QLabel("预览等待画面");
    previewRate_->setObjectName("caption");
    layout->addWidget(previewRate_);
    previewRate_->hide();
    connect(show, &QPushButton::toggled, this, [this, show](bool on) {
        preview_->setVisible(on);
        previewRate_->setVisible(on);
        show->setText(on ? "关闭画面预览" : "开启画面预览");
    });
    connect(fps, &QComboBox::activated, this, [this, fps] {
        previewFps_ = fps->currentData().toInt();
        previewTimer_.setInterval(previewFps_ ? std::max(1, int(1000.0 / previewFps_)) : 1);
        savePreviewPreferences();
    });
    connect(size, &QComboBox::activated, this, [this, size] {
        previewHeight_ = size->currentData().toInt();
        preview_->setFixedHeight(previewHeight_);
        savePreviewPreferences();
    });
    connect(top, &QCheckBox::toggled, this, [this](bool on) {
        previewOnTop_ = on;
        if (floating_) {
            floating_->setWindowFlag(Qt::WindowStaysOnTopHint, on);
            floating_->show();
        }
        savePreviewPreferences();
    });
    return card;
}
} // namespace okww
