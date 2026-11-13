#include "classic_ui.hpp"
#include "app_info.hpp"
#include <algorithm>
#include <cmath>
#include <QUrlQuery>
namespace okww::classic {
QUrl workshopFeedbackUrl(const QJsonObject &entry) {
    QStringList missing;
    for (const auto &name : entry["unsupported_members"].toArray())
        missing.append(name.toString());
    const QStringList lines{
        "请协助更新以下工坊队伍的适配。", "",
        "软件：" + product::title,
        "软件版本：" + product::version,
        "队伍：" + entry["team_display"].toString(entry["team"].toString()),
        "角色标识：" + entry["team"].toString(),
        "方案：" + entry["name_display"].toString(entry["name"].toString()),
        "方案作者：" + entry["author"].toString(),
        "方案版本：" + entry["version"].toString(),
        "待适配角色：" + (missing.isEmpty() ? QString("请核对该方案所需的角色与接口") : missing.join("、"))};
    QUrl url;
    url.setScheme("mailto");
    url.setPath(product::supportEmail);
    QUrlQuery query;
    query.addQueryItem("subject", "OK-WW双机版：工坊队伍适配请求");
    query.addQueryItem("body", lines.join("\r\n"));
    url.setQuery(query);
    return url;
}

namespace {
class ChineseUi final : public QTranslator {
  public:
    using QTranslator::QTranslator;
    bool isEmpty() const override { return false; }
    QString translate(const char *, const char *source, const char *, int) const override {
        static const QHash<QString, QString> labels{
            {"OK", "确定"}, {"&OK", "确定"}, {"Cancel", "取消"}, {"&Cancel", "取消"},
            {"Save", "保存"}, {"&Save", "保存"}, {"Save All", "全部保存"}, {"Open", "打开"},
            {"&Open", "打开"}, {"Close", "关闭"}, {"&Close", "关闭"}, {"Yes", "是"},
            {"&Yes", "是"}, {"No", "否"}, {"&No", "否"}, {"Apply", "应用"},
            {"Reset", "重置"}, {"Restore Defaults", "恢复默认"}, {"Discard", "不保存"},
            {"Retry", "重试"}, {"Abort", "中止"}, {"Ignore", "忽略"}, {"Help", "帮助"},
            {"&Undo", "撤销"}, {"&Redo", "重做"}, {"Cu&t", "剪切"}, {"&Copy", "复制"},
            {"&Paste", "粘贴"}, {"Delete", "删除"}, {"Select All", "全选"}, {"Select &All", "全选"},
            {"File name:", "文件名："}, {"Files of type:", "文件类型："}, {"Look in:", "位置："},
            {"Back", "返回"}, {"Forward", "前进"}, {"Parent Directory", "上级目录"},
            {"Create New Folder", "新建文件夹"}, {"List View", "列表视图"}, {"Detail View", "详细视图"},
            {"Name", "名称"}, {"Size", "大小"}, {"Type", "类型"}, {"Date Modified", "修改日期"},
            {"Show hidden files", "显示隐藏文件"}, {"&Rename", "重命名"}, {"&Delete", "删除"}};
        return labels.value(QString::fromUtf8(source));
    }
};
} // namespace
void installChineseUi() {
    if (qApp->property("chineseUiInstalled").toBool())
        return;
    QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));
    qApp->installTranslator(new ChineseUi(qApp));
    qApp->setProperty("chineseUiInstalled", true);
}
QVBoxLayout *dialogLayout(QDialog &dialog, const QString &title, const QString &description,
                          QSize preferred) {
    dialog.setWindowTitle(title);
    dialog.setProperty("designedDialog", true);
    dialog.setSizeGripEnabled(true);
    const auto screen = dialog.screen() ? dialog.screen()->availableGeometry().size() : QSize(1280, 800);
    dialog.resize(preferred.boundedTo(screen - QSize(60, 80)));
    auto layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 22, 24, 20);
    layout->setSpacing(16);
    auto heading = new QLabel(title);
    heading->setObjectName("dialogTitle");
    heading->setWordWrap(true);
    layout->addWidget(heading);
    if (!description.isEmpty()) {
        auto note = new QLabel(description);
        note->setObjectName("caption");
        note->setWordWrap(true);
        layout->addWidget(note);
    }
    return layout;
}
void polishForm(QFormLayout *form) {
    form->setContentsMargins(18, 18, 18, 18);
    form->setHorizontalSpacing(22);
    form->setVerticalSpacing(14);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
}
QPixmap characterPortrait(const QString &root, const QString &id) {
    static const QHash<QString, QString> aliases{{"buling", "douling"}, {"lynae", "linnai"},
        {"sigrika", "xigelika"}, {"yangyang: xuanling", "yangyangsp"},
        {"yangyang:_xuanling", "yangyangsp"}, {"luuk herssen", "luhesi"},
        {"xiangli yao", "xiangliyao"}, {"havocrover", "rover"}};
    static QHash<QString, QHash<QString, QString>> files;
    static QHash<QString, QPixmap> images;
    if (!files.contains(root)) {
        auto &names = files[root];
        QDir directory(root + "/native/resources/portraits");
        for (const auto &name : directory.entryList({"*.png"}, QDir::Files))
            names.insert(QFileInfo(name).completeBaseName().toCaseFolded(), directory.filePath(name));
    }
    const auto name = aliases.value(id.trimmed().toCaseFolded(), id.trimmed().toCaseFolded());
    const auto key = root + '/' + name;
    if (!images.contains(key)) {
        const auto path = files[root].value(name);
        images.insert(key, path.isEmpty() ? QPixmap{} : QPixmap(path));
    }
    return images.value(key);
}
void paintPortrait(QPainter &p, const QRectF &rect, const QPixmap &image, const QString &fallback,
                   const QPalette &palette) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const auto base = qApp->palette().color(QPalette::Base);
    p.setBrush(base.lightness() < 128 ? base.lighter(122) : base.darker(104));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(rect, 8, 8);
    if (!image.isNull()) {
        QSizeF size = image.size();
        size.scale(rect.adjusted(2, 2, -2, -2).size(), Qt::KeepAspectRatio);
        QRectF target(QPointF{}, size);
        target.moveCenter(rect.center());
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawPixmap(target, image, image.rect());
    } else {
        p.setPen(palette.color(QPalette::Highlight));
        auto font = p.font();
        font.setPixelSize(int(rect.height() * .4));
        font.setWeight(QFont::DemiBold);
        p.setFont(font);
        p.drawText(rect, Qt::AlignCenter, fallback.left(1).isEmpty() ? "?" : fallback.left(1));
    }
    p.restore();
}
ComboBox::ComboBox(QWidget *parent) : QComboBox(parent) {
    setMaxVisibleItems(8);
    view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    view()->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    view()->setTextElideMode(Qt::ElideRight);
}
void ComboBox::showPopup() {
    if (!count())
        return;
    for (int i = 0; i < count(); ++i)
        if (itemData(i, Qt::ToolTipRole).toString().isEmpty())
            setItemData(i, itemText(i), Qt::ToolTipRole);
    QComboBox::showPopup();
    auto popup = view()->window();
    if (!popup || popup == window() || !popup->isVisible())
        return;
    const auto bounds = screen()->availableGeometry().adjusted(6, 6, -6, -6);
    const auto anchor = mapToGlobal(QPoint(0, height() + 4));
    int rowHeight = 32;
    for (int i = 0; i < count(); ++i)
        rowHeight = std::max(rowHeight, view()->sizeHintForRow(i));
    int popupHeight = std::min(count(), maxVisibleItems()) * rowHeight + 12;
    const int below = bounds.bottom() - anchor.y() + 1;
    if (below >= rowHeight + 12)
        popupHeight = std::min(popupHeight, below);
    else
        popupHeight = std::min(popupHeight, bounds.height());
    const int popupWidth = std::min(bounds.width(), std::max(width(),
        std::min(560, view()->sizeHintForColumn(modelColumn()) + 40)));
    const int x = std::clamp(anchor.x(), bounds.left(), bounds.right() - popupWidth + 1);
    // At the bottom of a screen, keep the list reachable rather than clipping it.
    const int y = std::clamp(anchor.y(), bounds.top(), bounds.bottom() - popupHeight + 1);
    popup->setGeometry(x, y, popupWidth, popupHeight);
    view()->scrollTo(model()->index(currentIndex(), modelColumn(), rootModelIndex()),
                     QAbstractItemView::EnsureVisible);
}
QWidget *sliderEditor(QAbstractSpinBox *number, bool logarithmic) {
    auto integer = qobject_cast<QSpinBox *>(number);
    auto decimal = qobject_cast<QDoubleSpinBox *>(number);
    if (!integer && !decimal)
        return number;
    const double low = integer ? integer->minimum() : decimal->minimum();
    const double high = integer ? integer->maximum() : decimal->maximum();
    if (high <= low || (logarithmic && low <= 0))
        return number;
    auto wrapper = new QWidget;
    wrapper->setObjectName("numericEditor");
    auto layout = new QHBoxLayout(wrapper);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    auto slider = new QSlider(Qt::Horizontal, wrapper);
    slider->setObjectName(number->objectName() + "-slider");
    slider->setAccessibleName(number->accessibleName());
    slider->setToolTip("拖动快速调整，也可在右侧精确输入数值");
    slider->setMinimumWidth(90);
    const int steps = integer ? int(high - low) : 10000;
    slider->setRange(0, steps);
    slider->setPageStep(std::max(1, steps / 20));
    const auto position = [=](double value) {
        const double fraction = logarithmic ? std::log(value / low) / std::log(high / low)
                                            : (value - low) / (high - low);
        return int(std::lround(std::clamp(fraction, 0.0, 1.0) * steps));
    };
    slider->setValue(position(integer ? integer->value() : decimal->value()));
    const auto sync = [=](double value) {
        QSignalBlocker blocker(slider);
        slider->setValue(position(value));
    };
    if (integer)
        QObject::connect(integer, &QSpinBox::valueChanged, wrapper, sync);
    else
        QObject::connect(decimal, &QDoubleSpinBox::valueChanged, wrapper, sync);
    const auto commit = [number] {
        QMetaObject::invokeMethod(number, "editingFinished", Qt::QueuedConnection);
    };
    QObject::connect(slider, &QSlider::valueChanged, wrapper, [=](int value) {
        const double fraction = double(value) / steps;
        const double target = logarithmic ? low * std::pow(high / low, fraction)
                                          : low + fraction * (high - low);
        if (integer)
            integer->setValue(int(std::lround(target)));
        else
            decimal->setValue(target);
        if (!slider->isSliderDown())
            commit();
    });
    QObject::connect(slider, &QSlider::sliderReleased, number, commit);
    number->setMinimumWidth(130);
    number->setMaximumWidth(170);
    layout->addWidget(slider, 1);
    layout->addWidget(number);
    return wrapper;
}
void AspectImageLabel::paintEvent(QPaintEvent *event) {
    const auto image = pixmap();
    if (image.isNull()) {
        QLabel::paintEvent(event);
        return;
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF bounds(contentsRect());
    QSizeF size(image.size());
    size.scale(bounds.size(), Qt::KeepAspectRatio);
    const QRectF target(bounds.x() + (bounds.width() - size.width()) / 2,
                        bounds.y() + (bounds.height() - size.height()) / 2, size.width(), size.height());
    painter.drawPixmap(target, image, QRectF(image.rect()));
}
QIcon fluentIcon(const QString &name, bool dark) {
    static QHash<QString, QIcon> cache;
    const auto key = (dark ? QStringLiteral("dark/") : QStringLiteral("light/")) + name;
    if (!cache.contains(key)) {
        QIcon result;
        for (int size : {10, 12, 16, 20, 24, 32, 40, 48, 60, 64})
            result.addFile(QString(":/classic-fluent/%1/%2.png").arg(key).arg(size), {size, size});
        cache.insert(key, result);
    }
    return cache.value(key);
}
void setFluentIcon(QAbstractButton *button, const QString &name, bool primary) {
    button->setProperty("fluentIcon", name);
    button->setProperty("fluentPrimary", primary);
    button->setIcon(fluentIcon(name, primary
                                         ? qApp->palette().color(QPalette::HighlightedText).lightness() > 128
                                         : button->palette().color(QPalette::Window).lightness() < 128));
    button->setIconSize({16, 16});
}
void refreshFluentIcons(QWidget *root, bool dark) {
    for (auto action : root->findChildren<QAction *>()) {
        const auto name = action->property("fluentIcon").toString();
        if (!name.isEmpty()) action->setIcon(fluentIcon(name, dark));
    }
    for (auto button : root->findChildren<QAbstractButton *>()) {
        const auto name = button->property("fluentIcon").toString();
        if (!name.isEmpty())
            button->setIcon(fluentIcon(
                name, (button->property("fluentPrimary").toBool() || button->objectName() == "primary")
                          ? qApp->palette().color(QPalette::HighlightedText).lightness() > 128
                          : dark));
    }
    for (auto label : root->findChildren<QLabel *>()) {
        const auto name = label->property("fluentIconLabel").toString();
        if (!name.isEmpty())
            label->setPixmap(fluentIcon(name, dark).pixmap(16, 16));
    }
}
void setActionIcon(QAbstractButton *button) {
    static const QHash<QString, QString> names{{"截图", "Zoom"},
                                               {"刷新", "Sync"},
                                               {"刷新设备", "Sync"},
                                               {"开始", "Play"},
                                               {"开始(F9)", "Play"},
                                               {"暂停", "Pause"},
                                               {"暂停(F9)", "Pause"},
                                               {"停止", "Stop"},
                                               {"启动线程 / 应用配置", "Play"},
                                               {"终止线程", "Pause"},
                                               {"保存设置", "Save"},
                                               {"测试移动 ↘ 40 px", "Send"},
                                               {"开启画面预览", "View"},
                                               {"关闭画面预览", "View"},
                                               {"独立预览窗口", "Link"},
                                               {"紧急停止键鼠", "Cancel"},
                                               {"导出日志", "Feedback"},
                                               {"安装目录", "Folder"},
                                               {"截图目录", "Folder"},
                                               {"日志目录", "Folder"},
                                               {"查看日志", "CommandPrompt"},
                                               {"OCR", "Search"},
                                               {"检测设备", "Search"},
                                               {"读取支持的参数", "Info"},
                                               {"检测串口", "Search"},
                                               {"检测 USB 盒子", "Search"},
                                               {"导入原生队伍", "Download"},
                                               {"新建队伍", "Add"},
                                               {"导出选中队伍", "Share"},
                                               {"打开工坊", "BookShelf"},
                                               {"使用导入代码", "Code"},
                                               {"取消使用 / 恢复内置", "Sync"},
                                               {"保存 C++ 草稿", "Save"},
                                               {"重置配置", "Sync"},
                                               {"打开软件目录", "Folder"},
                                               {"配置选中任务的计划", "Calendar"}};
    const auto name = names.value(button->text());
    if (!name.isEmpty())
        setFluentIcon(button, name, button->objectName() == "primary");
}
void setDisclosureButton(QPushButton *button) {
    button->setObjectName("disclosure");
    button->setCheckable(true);
    const auto updateIcon = [button](bool on) {
        setFluentIcon(button, on ? "ChevronDown" : "ChevronRight");
        button->setIconSize({12, 12});
    };
    updateIcon(button->isChecked());
    QObject::connect(button, &QPushButton::toggled, button, updateIcon);
}
QIcon icon(char16_t glyph, QColor color) {
    QPixmap image(40, 40);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont font("Segoe MDL2 Assets");
    font.setPixelSize(32);
    p.setFont(font);
    p.setPen(color);
    p.drawText(image.rect(), Qt::AlignCenter, QString(QChar(glyph)));
    return QIcon(image);
}
QString stylesheet(bool dark, const QColor &accent) {
    const QString background = dark ? "#14161c" : "#f6f7fa", card = dark ? "#1e212a" : "#ffffff",
                  foreground = dark ? "#ebedf4" : "#252b36", muted = dark ? "#9aa4b5" : "#687385",
                  border = dark ? "#303541" : "#e7eaf0", hover = dark ? "#292f3e" : "#edf2fc";
    auto result = QString(R"(
        QMainWindow, QWidget#classicRoot, QDialog { background:%1; color:%3; }
        QWidget { color:%3; font-family:'Segoe UI','Microsoft YaHei UI'; font-size:13px; }
        QWidget#navigationPanel { background:%1; border-right:1px solid %5; }
        QWidget#titleBar { background:%2; border-bottom:1px solid %5; }
        QLabel { background:transparent; }
        QLabel#caption, QLabel[caption="true"], QLabel[subtleStatus="true"] { color:%4; font-size:12px; }
        QLabel#pageTitle { font-size:24px; font-weight:600; margin-bottom:4px; }
        QLabel#dialogTitle { font-size:22px; font-weight:600; }
        QLabel#detailTitle { font-size:17px; font-weight:600; }
        QLabel#cardTitle { font-size:14px; font-weight:600; }
        QLabel#metricCaption { font-size:11px; color:%4; }
        QLabel#metricValue { font-size:13px; font-weight:500; }
        QLabel#taskSymbol { background:%6; border-radius:8px; }
        QLabel#taskVariant { color:#0067c0; background:%6; border-radius:4px; padding:2px 6px; font-size:10px; }
        QLabel#taskMetricValue { color:%3; font-size:15px; font-weight:600; }
        QLabel#applicationStatus { font-size:11px; color:%4; border-top:1px solid %5; background:%2; }
        QLabel#statusBadge, QLabel[statusBadge="true"] { color:%3; background:%6; border-radius:8px; padding:8px 12px; }
        QFrame#classicCard, QFrame[classicCard="true"], QGroupBox { background:%2; border:1px solid %5; border-radius:12px; }
        QFrame[taskCard="true"] { border-radius:10px; }
        QFrame[taskCard="true"][taskState="running"] { border-color:#0067c0; }
        QFrame[taskCard="true"] QWidget#cardBody { border-top:1px solid %5; }
        QGroupBox { margin-top:20px; padding:18px 16px 14px; }
        QGroupBox::title { subcontrol-origin:margin; left:16px; padding:0 4px; }
        QWidget#cardHeader, QWidget#cardBody, QWidget#configRow { background:transparent; }
        QWidget#configRow { min-height:56px; }
        QFrame#configContentFrame { border-radius:12px; }
        QLabel#sectionTitle { font-size:20px; }
        QPushButton { background:%2; border:1px solid %5; border-radius:8px; padding:6px 12px; min-height:18px; font-weight:400; }
        QPushButton:hover, QToolButton:hover { background:%6; }
        QPushButton:pressed { background:%1; }
        QPushButton:disabled { color:%4; background:%1; }
        QPushButton#primary, QPushButton[primary="true"] { background:#0067c0; color:white; border:1px solid #0067c0; font-weight:600; }
        QPushButton#primary:hover, QPushButton[primary="true"]:hover { background:#1975c5; }
        QPushButton#primary:disabled, QPushButton[primary="true"]:disabled { background:%6; border-color:%5; color:%4; }
        QPushButton[danger="true"] { color:#b65337; }
        QPushButton[compactTask="true"] { min-height:0; min-width:0; padding:0 8px; border-radius:6px; font-size:12px; font-weight:500; background:%6; color:#0067c0; border:1px solid %5; }
        QPushButton[compactTask="true"]:hover { border-color:#0067c0; }
        QPushButton[compactTask="true"][danger="true"] { color:#bd4e53; }
        QPushButton[compactTask="true"][taskActionRole="run"] { background:#0067c0; border-color:#0067c0; color:white; font-weight:600; }
        QPushButton[compactTask="true"][taskActionRole="run"]:hover { background:#1975c5; border-color:#1975c5; }
        QPushButton[compactTask="true"][taskActionRole="run"]:disabled { background:%6; color:%4; border-color:%5; }
        QPushButton[compactTask="true"][taskActionRole="pause"] { background:%2; color:%3; border-color:%5; }
        QPushButton[compactTask="true"][taskActionRole="stop"], QPushButton[compactTask="true"][taskActionRole="cancel"] { background:%2; color:#c44d59; border-color:#dfbac0; }
        QToolButton#taskSchedule, QToolButton#taskSettings { background:%2; border:1px solid %5; border-radius:6px; padding:0; font-size:12px; }
        QToolButton#taskSchedule:hover, QToolButton#taskSettings:hover { background:%6; border-color:#0067c0; }
        QLineEdit#taskSearch { border-radius:8px; }
        QLabel#tasksEmpty { color:%4; font-size:14px; padding:32px 0; }
        QPushButton[chip="true"] { min-height:0; padding:5px 12px; border-radius:7px; font-size:12px; }
        QPushButton[chip="true"]:checked { color:#0067c0; border-color:#0067c0; background:%6; }
        QPushButton#disclosure { background:transparent; border:0; text-align:left; padding:5px 8px; }
        QPushButton#disclosure:hover { background:%6; }
        QToolButton { background:transparent; border:0; border-radius:4px; padding:4px; }
        QToolButton#closeButton:hover { background:#c42b1c; color:white; }
        QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QDateEdit, QTimeEdit, QPlainTextEdit, QTextBrowser {
            background:%2; color:%3; border:1px solid %5;
            border-radius:7px; padding:7px 10px; selection-background-color:#0067c0;
        }
        QPlainTextEdit#codeEditor { font-family:'Cascadia Mono','Consolas'; font-size:13px; }
        QTextBrowser[readingPanel="true"] { border:0; border-radius:0; padding:0; background:transparent; }
        QTableWidget#workshopTable { border:0; background:transparent; }
        QTimeEdit#scheduleTime { font-size:21px; font-weight:600; padding:7px 10px; min-height:26px; }
        QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QDateEdit:focus, QTimeEdit:focus, QPlainTextEdit:focus { border:1px solid #0067c0; }
        QComboBox { padding-right:28px; min-height:18px; }
        QComboBox::drop-down { subcontrol-origin:padding; subcontrol-position:top right;
            width:26px; border:0; border-top-right-radius:5px; border-bottom-right-radius:5px; }
        QComboBox::down-arrow { image:url(:/classic-ui/down-%7.png); width:12px; height:12px; }
        QComboBox QAbstractItemView { background:%2; border:1px solid %5; padding:4px; selection-background-color:%6; }
        QComboBox QAbstractItemView::item { min-height:28px; padding:3px 8px; }
        QSlider::groove:horizontal { height:4px; background:%5; border-radius:2px; }
        QSlider::sub-page:horizontal { background:#0067c0; border-radius:2px; }
        QSlider::handle:horizontal { width:14px; margin:-5px 0; border-radius:7px;
            background:#0067c0; border:2px solid %2; }
        QSlider::handle:horizontal:hover { background:#1975c5; }
        QAbstractSpinBox { padding-right:30px; }
        QSpinBox::up-button, QDoubleSpinBox::up-button, QTimeEdit::up-button {
            subcontrol-origin:padding; subcontrol-position:top right; width:26px; border:0; }
        QSpinBox::down-button, QDoubleSpinBox::down-button, QTimeEdit::down-button {
            subcontrol-origin:padding; subcontrol-position:bottom right; width:26px; border:0; }
        QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,
        QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover,
        QTimeEdit::up-button:hover, QTimeEdit::down-button:hover { background:%6; }
        QSpinBox::up-arrow, QDoubleSpinBox::up-arrow, QTimeEdit::up-arrow { image:url(:/classic-ui/up-%7.png); width:12px; height:12px; }
        QSpinBox::down-arrow, QDoubleSpinBox::down-arrow, QTimeEdit::down-arrow { image:url(:/classic-ui/down-%7.png); width:12px; height:12px; }
        QDateEdit::drop-down { subcontrol-origin:padding; subcontrol-position:top right; width:26px; border:0; }
        QDateEdit::down-arrow { image:url(:/classic-ui/down-%7.png); width:12px; height:12px; }
        QListWidget { background:%2; border:1px solid %5; border-radius:12px; padding:6px; outline:0; }
        QListWidget::item { min-height:32px; padding:6px 10px; border-radius:7px; margin:2px 0; }
        QListWidget::item:selected { color:%3; background:%6; border-left:3px solid #0067c0; }
        QListWidget#navigation, QListWidget#bottomNavigation { background:transparent; border:0; padding:4px; }
        QListWidget#navigation::item, QListWidget#bottomNavigation::item { min-height:32px; padding:2px 12px; margin:2px 1px; border-radius:6px; }
        QListWidget#navigation::item:selected, QListWidget#bottomNavigation::item:selected { color:#0067c0; border-left:2px solid #0067c0; }
        QListWidget#teamList { background:transparent; border:0; padding:4px; }
        QListWidget#teamList::item { min-height:32px; padding:2px 4px; margin:2px 0; }
        QScrollArea { background:transparent; border:0; }
        QScrollArea>QWidget>QWidget { background:transparent; }
        QScrollBar:vertical { background:transparent; width:10px; margin:2px; }
        QScrollBar::handle:vertical { background:%5; min-height:32px; border-radius:3px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:none; }
        QScrollBar:horizontal { background:transparent; height:10px; margin:2px; }
        QScrollBar::handle:horizontal { background:#a3adbd; min-width:32px; border-radius:3px; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; }
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background:none; }
        QCheckBox { spacing:7px; background:transparent; }
        QCheckBox::indicator { width:18px; height:18px; border:1px solid #999999; border-radius:4px; background:%2; }
        QCheckBox::indicator:checked { background:#0067c0; border-color:#0067c0; image:url(:/classic-ui/check.png); }
        QCheckBox::indicator:checked:hover { background:#1975c5; }
        QSplitter::handle { background:transparent; width:8px; }
        QTableWidget { background:%2; border:1px solid %5; border-radius:12px; gridline-color:%5; outline:0; selection-background-color:%6; selection-color:%3; }
        QTableWidget::item { padding:8px; border-bottom:1px solid %5; }
        QHeaderView::section { background:%1; color:%4; font-weight:600; border:0; padding:10px 8px; }
        QDialogButtonBox { border-top:1px solid %5; padding-top:14px; }
        QToolTip { background:%2; color:%3; border:1px solid %5; padding:8px; }
        QMenu { background:%2; color:%3; border:1px solid %5; padding:6px; }
        QMenu::item { border-radius:5px; padding:8px 24px; }
        QMenu::item:selected { background:%6; }
        QRadioButton { spacing:8px; min-height:28px; }
        QRadioButton::indicator { width:16px; height:16px; border-radius:9px; border:1px solid %5; background:%2; }
        QRadioButton::indicator:checked { border:5px solid #0067c0; background:%2; }
        QTabWidget::pane { border:1px solid %5; background:%2; border-radius:10px; }
        QTabBar::tab { background:transparent; padding:10px 18px; border-bottom:2px solid transparent; }
        QTabBar::tab:selected { border-bottom-color:#0067c0; }
    )")
                      .arg(background, card, foreground, muted, border, hover, dark ? "dark" : "light");
    result += QString(R"(
        QLabel#remoteFullscreenHint {
            font-size:18px; font-weight:700; color:%1; background:%2;
            border:1px solid %3; border-left:4px solid %4;
            border-radius:8px; padding:12px 14px;
        }
    )").arg(dark ? "#ffd783" : "#805100", dark ? "#352c1c" : "#fff6dc",
           dark ? "#6b5525" : "#f0cf78", dark ? "#f5c65d" : "#d99400");
    result.replace("#0067c0", accent.name());
    result.replace("#1975c5", accent.lighter(110).name());
    if (dark) {
        result.replace("#c44d59", "#ffa2af");
        result.replace("#dfbac0", "#71434a");
    }
    if (qApp->palette().color(QPalette::HighlightedText).lightness() < 128)
        result.replace("color:white", "color:#101010");
    return result;
}
Switch::Switch(QWidget *parent) : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setFixedSize(sizeHint());
}
void Switch::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor text =
        palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::WindowText);
    const bool trigger = property("triggerSwitch").toBool();
    const QRectF track(trigger ? 0 : width() - 42, 5, 40, 20);
    const auto accent = palette().color(QPalette::Highlight);
    p.setPen(isChecked() ? accent : palette().color(QPalette::Mid));
    p.setBrush(isChecked() ? accent : Qt::transparent);
    p.drawRoundedRect(track, 10, 10);
    p.setPen(Qt::NoPen);
    p.setBrush(isChecked() ? Qt::white : text);
    p.drawEllipse(QPointF(track.left() + (isChecked() ? 29 : 11), 15), 6, 6);
    p.setPen(text);
    p.drawText(trigger ? QRect(52, 0, 60, height()) : QRect(0, 0, 32, height()),
               Qt::AlignVCenter | (trigger ? Qt::AlignLeft : Qt::AlignRight),
               trigger ? (isChecked() ? "已启用" : "未启用") : (isChecked() ? "是" : "否"));
    if (hasFocus()) {
        p.setBrush(Qt::NoBrush);
        p.setPen(text);
        p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 4, 4);
    }
}
ExpandCard::ExpandCard(QString title, QString description, QWidget *parent) : QFrame(parent) {
    setObjectName("classicCard");
    setProperty("classicCard", true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    header_ = new QWidget;
    header_->setObjectName("cardHeader");
    header_->setMinimumHeight(description.isEmpty() ? 50 : 70);
    header_->setCursor(Qt::PointingHandCursor);
    auto headerLayout = new QHBoxLayout(header_);
    headerLayout->setContentsMargins(16, 10, 12, 10);
    headerLayout->setSpacing(12);
    textLayout_ = new QVBoxLayout;
    textLayout_->setSpacing(3);
    title_ = new QLabel;
    title_->setWordWrap(true);
    title_->setMinimumWidth(120);
    description_ = new QLabel;
    description_->setObjectName("caption");
    description_->setWordWrap(true);
    textLayout_->addWidget(title_);
    textLayout_->addWidget(description_);
    headerLayout->addLayout(textLayout_, 1);
    actions = new QHBoxLayout;
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(8);
    headerLayout->addLayout(actions);
    chevron_ = new QToolButton;
    chevron_->setFixedSize(30, 30);
    setFluentIcon(chevron_, "ChevronDown");
    chevron_->setIconSize({10, 10});
    chevron_->setAccessibleName("展开设置");
    headerLayout->addWidget(chevron_);
    layout->addWidget(header_);
    body_ = new QWidget;
    body_->setObjectName("cardBody");
    layout->addWidget(body_);
    body_->hide();
    for (auto object : {static_cast<QObject *>(header_), static_cast<QObject *>(title_),
                        static_cast<QObject *>(description_)})
        object->installEventFilter(this);
    connect(chevron_, &QToolButton::clicked, this, [this] { setExpanded(!expanded_); });
    setText(title, description);
}
bool ExpandCard::eventFilter(QObject *object, QEvent *event) {
    if (event->type() == QEvent::MouseButtonRelease &&
        static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
        setExpanded(!expanded_);
        return true;
    }
    return QFrame::eventFilter(object, event);
}
void ExpandCard::setText(QString title, QString description) {
    setProperty("originalTitle", title);
    setAccessibleName(title);
    if (auto variant = findChild<QLabel *>("taskVariant")) {
        const QString text = title.startsWith("⭐") ? "增强" : "";
        variant->setProperty("defaultText", text);
        variant->setText(text);
        variant->setVisible(!text.isEmpty());
    }
    if (property("compactHeader").toBool()) {
        const auto symbol = title.section(' ', 0, 0);
        QStringList codes;
        for (auto code : symbol.toUcs4())
            codes << QString::number(code, 16);
        const auto resource = QString(":/classic-emoji/%1/").arg(codes.join('-'));
        if (QFile::exists(resource + "16.png")) {
            if (!icon_) {
                icon_ = new QLabel;
                icon_->setObjectName("taskSymbol");
                icon_->setFixedSize(32, 32);
                icon_->setAlignment(Qt::AlignCenter);
                qobject_cast<QHBoxLayout *>(header_->layout())->insertWidget(0, icon_);
                header_->layout()->setSpacing(12);
                icon_->installEventFilter(this);
            }
            QIcon image;
            for (int size : {16, 20, 24, 32, 40, 48, 64})
                image.addFile(resource + QString::number(size) + ".png", {size, size});
            icon_->setPixmap(image.pixmap({16, 16}, devicePixelRatioF()));
            icon_->setAccessibleName(symbol);
            title = title.mid(symbol.size()).trimmed();
        } else if (symbol == "👥") {
            setHeaderIcon("People");
            icon_->setObjectName("taskSymbol");
            icon_->setFixedSize(32, 32);
            icon_->setAlignment(Qt::AlignCenter);
            title = title.mid(symbol.size()).trimmed();
        }
    }
    title_->setText(title);
    title_->setToolTip(title);
    description_->setText(property("compactHeader").toBool() ? description.simplified() : description);
    description_->setToolTip(description);
    description_->setVisible(!description.isEmpty());
}
void ExpandCard::setCompactHeader() {
    if (property("compactHeader").toBool())
        return;
    setProperty("compactHeader", true);
    setProperty("taskCard", true);
    header_->setFixedHeight(76);
    header_->layout()->setContentsMargins(16, 12, 16, 12);
    title_->setObjectName("cardTitle");
    title_->setWordWrap(false);
    title_->setMinimumWidth(0);
    title_->setMaximumWidth(QWIDGETSIZE_MAX);
    description_->setWordWrap(false);
    description_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    textLayout_->removeWidget(title_);
    auto titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(8);
    titleRow->addWidget(title_);
    auto variant = new QLabel;
    variant->setObjectName("taskVariant");
    variant->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    titleRow->addWidget(variant);
    titleRow->addStretch();
    textLayout_->insertLayout(0, titleRow);
    textLayout_->setSpacing(4);
    chevron_->setObjectName("taskSettings");
    chevron_->setText("设置");
    chevron_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    chevron_->setFixedSize(64, 32);
    setText(title_->text(), description_->text());
}
void ExpandCard::setHeaderIcon(const QString &name) {
    if (!icon_) {
        icon_ = new QLabel;
        icon_->setFixedSize(16, 16);
        qobject_cast<QHBoxLayout *>(header_->layout())->insertWidget(0, icon_);
    }
    icon_->setProperty("fluentIconLabel", name);
    icon_->setPixmap(fluentIcon(name, palette().color(QPalette::Window).lightness() < 128).pixmap(16, 16));
}
void ExpandCard::setExpanded(bool expanded) {
    if (expanded_ == expanded)
        return;
    expanded_ = expanded;
    body_->setVisible(expanded);
    setFluentIcon(chevron_, expanded ? "ChevronUp" : "ChevronDown");
    chevron_->setIconSize({10, 10});
    chevron_->setAccessibleName(expanded ? "收起设置" : "展开设置");
    if (onExpanded)
        onExpanded(expanded);
}
TitleBar::TitleBar(QMainWindow *window, const QIcon &icon) : QWidget(window), window_(window) {
    setObjectName("titleBar");
    setAttribute(Qt::WA_StyledBackground);
    setFixedHeight(42);
    auto l = new QHBoxLayout(this);
    l->setContentsMargins(12, 0, 0, 0);
    l->setSpacing(9);
    auto logo = new QLabel;
    logo->setPixmap(icon.pixmap(22, 22));
    l->addWidget(logo);
    auto title = new QLabel(product::title);
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    l->addWidget(title);
    l->addStretch();
    auto add = [&](char16_t glyph, QString tooltip, auto action) {
        auto b = new QToolButton;
        b->setProperty("uiGlyph", uint(glyph));
        b->setIcon(classic::icon(glyph));
        b->setToolTip(tooltip);
        b->setFixedSize(46, 36);
        connect(b, &QToolButton::clicked, window, action);
        l->addWidget(b);
        return b;
    };
    add(0xe921, "最小化", [window] { window->showMinimized(); });
    maximize_ = add(0xe922, "最大化 / 还原",
                    [window] { window->isMaximized() ? window->showNormal() : window->showMaximized(); });
    add(0xe8bb, "关闭", [window] { window->close(); })->setObjectName("closeButton");
}
void TitleBar::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton && window_->windowHandle())
        window_->windowHandle()->startSystemMove();
    else
        QWidget::mousePressEvent(e);
}
void TitleBar::mouseDoubleClickEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        window_->isMaximized() ? window_->showNormal() : window_->showMaximized();
    } else
        QWidget::mouseDoubleClickEvent(e);
}
} // namespace okww::classic
