#include "window.hpp"
#include <QDesktopServices>
#include <QSettings>
namespace okww {
void Window::openCloudGame() {
    QStringList candidates;
    QSettings user("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\msedge.exe", QSettings::NativeFormat);
    QSettings machine("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\msedge.exe", QSettings::NativeFormat);
    candidates << user.value(".").toString() << machine.value(".").toString();
    for (const auto &variable : {"ProgramFiles(x86)", "ProgramFiles", "LOCALAPPDATA"})
        candidates << qEnvironmentVariable(variable) + "/Microsoft/Edge/Application/msedge.exe";
    const QStringList arguments{"--app=https://mc.kurogames.com/cloud/#/", "--window-size=1920,1080",
        "--start-fullscreen", "--force-color-profile=srgb", "--disable-direct-composition",
        "--webrtc-ip-handling-policy=default"};
    for (const auto &path : candidates)
        if (QFile::exists(path) && QProcess::startDetached(path, arguments)) return;
    // Other default browsers can open the page; colour/compositor flags require Edge.
    QDesktopServices::openUrl(QUrl("https://mc.kurogames.com/cloud/#/"));
    QMessageBox::information(this, "云·鸣潮", "未找到 Microsoft Edge，已用默认浏览器打开网页。请手动开启 16:9 全屏，并关闭浏览器 HDR。登录后再启动任务。");
}

void Window::editAccountProfiles(QJsonArray profiles, std::function<void(QJsonValue)> changed) {
    auto dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("多账号日常 · 独立配置");
    dialog->resize(960, 720);
    auto layout = new QVBoxLayout(dialog);
    auto hint = new QLabel("账号填写登录框中显示的名称（包括星号）。匹配到的账号使用自己的每日参数，其他账号沿用全局每日参数。开启独立配置后，会先重新登录一次以确认当前账号。更改自动保存。", dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto body = new QHBoxLayout;
    auto left = new QVBoxLayout;
    auto accounts = new QListWidget;
    accounts->setFixedWidth(220);
    left->addWidget(accounts);
    auto controls = new QHBoxLayout;
    auto add = new QPushButton("添加账号"), remove = new QPushButton("移除");
    controls->addWidget(add); controls->addWidget(remove);
    left->addLayout(controls); body->addLayout(left);
    auto area = new QScrollArea;
    area->setWidgetResizable(true); area->setFrameShape(QFrame::NoFrame);
    auto form = new QWidget; area->setWidget(form); body->addWidget(area, 1);
    layout->addLayout(body, 1);
    auto close = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(close, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(close);
    auto entries = std::make_shared<QJsonArray>(profiles);
    QJsonArray schema;
    for (const auto &value : catalog_["tasks"].toArray())
        if (value.toObject()["id"] == "DailyTask") schema = value.toObject()["fields"].toArray();
    for (const auto &entry : *entries) accounts->addItem(entry.toObject()["account"].toString());
    connect(accounts, &QListWidget::currentRowChanged, dialog,
        [this, form, entries, schema, changed](int index) {
            QJsonArray fields;
            if (index >= 0 && index < entries->size()) {
                const auto values = entries->at(index).toObject()["values"].toObject();
                for (const auto &value : schema) {
                    auto field = value.toObject();
                    if (field["key"] == "Exit After Task") continue;
                    if (values.contains(field["key"].toString())) field["value"] = values[field["key"].toString()];
                    fields.append(field);
                }
            }
            buildFields(form, fields, [entries, changed, index](QString key, QJsonValue value) {
                if (index < 0 || index >= entries->size()) return;
                auto entry = entries->at(index).toObject();
                auto values = entry["values"].toObject(); values[key] = value; entry["values"] = values;
                (*entries)[index] = entry; changed(*entries);
            });
        });
    connect(add, &QPushButton::clicked, dialog, [dialog, accounts, entries, changed] {
        bool ok = false;
        const auto name = QInputDialog::getText(dialog, "添加账号", "登录框中显示的完整账号名", QLineEdit::Normal, {}, &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        for (const auto &entry : *entries)
            if (entry.toObject()["account"].toString().compare(name, Qt::CaseInsensitive) == 0) {
                QMessageBox::information(dialog, "账号已存在", "请在左侧选择该账号修改配置。"); return;
            }
        entries->append(QJsonObject{{"account", name}, {"values", QJsonObject{}}});
        accounts->addItem(name); accounts->setCurrentRow(accounts->count() - 1); changed(*entries);
    });
    connect(remove, &QPushButton::clicked, dialog, [accounts, entries, changed] {
        const int index = accounts->currentRow(); if (index < 0) return;
        entries->removeAt(index); delete accounts->takeItem(index); changed(*entries);
    });
    if (accounts->count()) accounts->setCurrentRow(0);
    dialog->show();
}
namespace {
QLabel *caption(QString text) {
    auto label = new QLabel(text);
    label->setObjectName("caption");
    label->setWordWrap(true);
    return label;
}
QFrame *frame() {
    auto value = new QFrame;
    value->setObjectName("classicCard");
    return value;
}
QPushButton *link(QString text, QString icon, const QString &url, QObject *owner) {
    auto button = new QPushButton(text);
    classic::setFluentIcon(button, icon);
    button->setProperty("linkUrl", url);
    QObject::connect(button, &QPushButton::clicked, owner, [url] { QDesktopServices::openUrl(QUrl(url)); });
    return button;
}
QWidget *scrollPage(QWidget *page) {
    auto area = new QScrollArea;
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setWidget(page);
    return area;
}
} // namespace
QWidget *Window::buildClassicSettings() {
    auto page = new QWidget;
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(4);
    auto title = new QLabel("软件设置");
    title->setObjectName("sectionTitle");
    layout->addWidget(title);
    layout->addSpacing(8);
    globalList_ = new QListWidget(page);
    globalList_->hide();
    auto appearance = new classic::ExpandCard("应用主题", "改变应用程序的外观");
    appearance->setHeaderIcon("Brush");
    auto selected = new QLabel;
    appearance->actions->addWidget(selected);
    const QStringList values{"light", "dark", "system"}, labels{"浅色", "深色", "使用系统设置"};
    auto options = new QVBoxLayout(appearance->body());
    options->setContentsMargins(48, 8, 16, 16);
    auto group = new QButtonGroup(appearance);
    for (int i = 0; i < values.size(); ++i) {
        auto radio = new QRadioButton(labels[i]);
        group->addButton(radio, i);
        options->addWidget(radio);
        if (values[i] == uiPreferences_["theme"].toString("system")) {
            radio->setChecked(true);
            selected->setText(labels[i]);
        }
    }
    connect(group, &QButtonGroup::idClicked, this, [this, values, labels, selected](int i) {
        uiPreferences_["theme"] = values.value(i, "system");
        selected->setText(labels.value(i));
        try {
            writeJson(data_ + "/native-ui.json", uiPreferences_);
            applyTheme();
        } catch (const std::exception &e) {
            log(errorText(e));
        }
    });
    layout->addWidget(appearance);
    auto language = frame();
    auto row = new QHBoxLayout(language);
    row->setContentsMargins(16, 12, 16, 12);
    row->setSpacing(16);
    auto icon = new QLabel;
    icon->setFixedSize(16, 16);
    icon->setProperty("fluentIconLabel", "Language");
    row->addWidget(icon);
    auto labelsLayout = new QVBoxLayout;
    labelsLayout->setSpacing(0);
    labelsLayout->addWidget(new QLabel("语言"));
    labelsLayout->addWidget(caption("选择你的语言"));
    row->addLayout(labelsLayout, 1);
    auto languages = new classic::ComboBox;
    languages->addItem("简体中文", "zh_CN");
    languages->addItem("使用系统设置", "Auto");
    languages->setCurrentIndex(std::max(0, languages->findData(uiPreferences_["language"].toString("Auto"))));
    languages->setToolTip("当前原生界面提供简体中文。");
    connect(languages, &QComboBox::activated, this, [this, languages] {
        uiPreferences_["language"] = languages->currentData().toString();
        try {
            writeJson(data_ + "/native-ui.json", uiPreferences_);
        } catch (const std::exception &e) {
            log(errorText(e));
        }
    });
    row->addWidget(languages);
    layout->addWidget(language);
    globalForm_ = new QWidget;
    auto groups = new QVBoxLayout(globalForm_);
    groups->setContentsMargins(0, 0, 0, 0);
    groups->setSpacing(4);
    layout->addWidget(globalForm_);
    layout->addStretch();
    return scrollPage(page);
}
QWidget *Window::buildClassicAbout() {
    auto page = new QWidget;
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    auto version = frame();
    version->setProperty("aboutSection", "project");
    auto header = new QHBoxLayout(version);
    header->setContentsMargins(16, 14, 8, 14);
    auto logo = new QLabel;
    logo->setPixmap(QIcon(root_ + "/icons/icon.png").pixmap(36, 36));
    header->addWidget(logo);
    auto names = new QVBoxLayout;
    names->setSpacing(0);
    names->addWidget(new QLabel(product::title));
    names->addWidget(caption(product::versionLabel()));
    names->addWidget(caption("外部采集 · 硬件键鼠 · 任务自动化"));
    header->addLayout(names, 1);
    const auto documentation = QFile::exists(root_ + "/source/README.md") ? root_ + "/source/README.md"
                                                                        : root_ + "/README.md";
    header->addWidget(link("项目说明", "Info", QUrl::fromLocalFile(documentation).toString(), this));
    auto github = link("GitHub", "GitHub", product::github, this);
    github->setObjectName("projectGithub");
    header->addWidget(github);
    layout->addWidget(version);
    layout->addWidget(buildAppUpdates());

    auto contact = frame();
    contact->setProperty("aboutSection", "contact");
    auto contactLayout = new QHBoxLayout(contact);
    contactLayout->setContentsMargins(16, 14, 16, 14);
    auto contactText = new QVBoxLayout;
    contactText->setSpacing(4);
    contactText->addWidget(new QLabel("联系作者"));
    auto email = new QLabel(product::supportEmail);
    email->setObjectName("authorEmail");
    email->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    contactText->addWidget(email);
    contactText->addWidget(caption("功能反馈与工坊适配建议"));
    contactLayout->addLayout(contactText, 1);
    auto mail = link("发送邮件", "Mail", "mailto:" + product::supportEmail, this);
    mail->setObjectName("contactAuthor");
    contactLayout->addWidget(mail);
    auto copyEmail = new QPushButton("复制邮箱");
    copyEmail->setObjectName("copyAuthorEmail");
    classic::setFluentIcon(copyEmail, "Copy");
    connect(copyEmail, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(product::supportEmail);
        log("联系邮箱已复制。");
    });
    contactLayout->addWidget(copyEmail);
    layout->addWidget(contact);

    layout->addWidget(new QLabel("功能概览"));
    auto features = frame();
    features->setProperty("aboutSection", "features");
    auto featureGrid = new QGridLayout(features);
    featureGrid->setContentsMargins(16, 16, 16, 16);
    featureGrid->setSpacing(12);
    const QVector<QStringList> descriptions{
        {"双机设备", "选择采集卡、DXGI 或 Win32 采集；使用 KMBox Net 或 SendInput 控制键鼠。"},
        {"战斗与任务", "自动战斗、每日任务、4C 声骸循环、噩梦巢穴与周常乐园。"},
        {"角色代码工坊", "导入、编辑和导出 Python 队伍代码，可按队伍切换内置与工坊连招。"},
        {"预览与控制", "独立预览、置顶与帧率选择；一键连接、停止和设备测试。"}};
    for (int i = 0; i < descriptions.size(); ++i) {
        auto item = new QVBoxLayout;
        item->setSpacing(4);
        item->addWidget(new QLabel(descriptions[i][0]));
        item->addWidget(caption(descriptions[i][1]));
        featureGrid->addLayout(item, i / 2, i % 2);
    }
    featureGrid->setColumnStretch(0, 1);
    featureGrid->setColumnStretch(1, 1);
    layout->addWidget(features);
    layout->addWidget(new QLabel("开源与使用说明"));
    auto disclaimer = frame();
    auto disclaimerLayout = new QVBoxLayout(disclaimer);
    disclaimerLayout->setContentsMargins(16, 16, 16, 16);
    auto text = new QLabel;
    text->setWordWrap(true);
    text->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    text->setOpenExternalLinks(true);
    text->setObjectName("aboutNotice");
    QFile html(root_ + "/native/resources/about.html");
    if (html.open(QIODevice::ReadOnly))
        text->setText(QString::fromUtf8(html.readAll()));
    disclaimerLayout->addWidget(text);
    auto documents = new QHBoxLayout;
    documents->addWidget(link("软件许可证", "Info", QUrl::fromLocalFile(root_ + "/LICENSE.txt").toString(), this));
    documents->addWidget(link("第三方说明", "Info", QUrl::fromLocalFile(root_ + "/THIRD-PARTY-DUALPC.md").toString(), this));
    documents->addStretch();
    disclaimerLayout->addLayout(documents);
    layout->addWidget(disclaimer);
    layout->addWidget(new QLabel("开源致谢"));
    auto projects = frame();
    auto grid = new QGridLayout(projects);
    grid->setContentsMargins(16, 16, 16, 16);
    grid->setSpacing(12);
    const QVector<QStringList> links{
        {"OK-WW", "https://github.com/ok-oldking/ok-wuthering-waves", "鸣潮自动化项目"},
        {"OK-WW Enhanced", "https://github.com/zzc-tongji/ok-ww-enhanced", "任务与角色扩展"},
        {"ok-script", "https://github.com/ok-oldking/ok-script", "自动化框架与 Python 工坊基础"}};
    for (int i = 0; i < links.size(); ++i) {
        const auto &entry = links[i];
        auto card = frame();
        auto row = new QHBoxLayout(card);
        row->setContentsMargins(16, 16, 10, 16);
        auto description = new QVBoxLayout;
        description->addWidget(new QLabel(entry[0]));
        description->addWidget(caption(entry[2]));
        row->addLayout(description, 1);
        row->addWidget(link("GitHub", "GitHub", entry[1], this));
        grid->addWidget(card, i / 2, i % 2);
    }
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    layout->addWidget(projects);
    layout->addStretch();
    return scrollPage(page);
}
} // namespace okww
