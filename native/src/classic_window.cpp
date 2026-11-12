#include "window.hpp"
#include <QDesktopServices>
#include <roapi.h>
#include <winrt/Windows.UI.ViewManagement.h>
#ifdef OKWW_NATIVE_ONLY
#include "runtime/scheduler.hpp"
#endif
namespace okww {
namespace {
QPushButton *action(QString title, QObject *owner, std::function<void()> run, bool primary = false) {
    auto b = new QPushButton(title);
    if (primary)
        b->setObjectName("primary");
    classic::setActionIcon(b);
    QObject::connect(b, &QPushButton::clicked, owner, std::move(run));
    return b;
}
QLabel *heading(QString text) {
    auto label = new QLabel(text);
    label->setObjectName("pageTitle");
    return label;
}
QWidget *scrolled(QWidget *content) {
    auto area = new QScrollArea;
    area->setWidgetResizable(true);
    area->setWidget(content);
    area->setFrameShape(QFrame::NoFrame);
    return area;
}
} // namespace
void Window::navigate(const QString &id) {
    if (!pageIndices_.contains(id))
        return;
    pages_->setCurrentIndex(pageIndices_[id]);
#ifdef OKWW_NATIVE_ONLY
    if (id == "schedule")
        refreshScheduleTable();
    if (id == "templates" && refreshTemplates_)
        refreshTemplates_();
    if (id == "scripts" && (catalog_["script_templates"].toArray().isEmpty() ||
        !catalog_["script_templates"].toArray().first().toObject().contains("display_name")) && !scriptOperation_ && engineReady_)
        scriptAction({{"op", "script_catalog"}});
#endif
    for (auto list : {navigation_, bottomNavigation_}) {
        if (!list)
            continue;
        QSignalBlocker blocker(list);
        list->setCurrentRow(-1);
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->data(Qt::UserRole) == id)
                list->setCurrentRow(i);
    }
}
void Window::applyTheme() {
    const auto selected = uiPreferences_["theme"].toString("system");
    QSettings system("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     QSettings::NativeFormat);
    const bool dark =
        selected == "dark" || (selected == "system" && !system.value("AppsUseLightTheme", 1).toBool());
    const QColor accent(dark ? "#8caeff" : "#3269e8");
    QPalette palette;
    palette.setColor(QPalette::Window, dark ? QColor("#14161c") : QColor("#f6f7fa"));
    palette.setColor(QPalette::WindowText, dark ? QColor("#ebedf4") : QColor("#252b36"));
    palette.setColor(QPalette::Text, palette.color(QPalette::WindowText));
    palette.setColor(QPalette::ButtonText, palette.color(QPalette::WindowText));
    palette.setColor(QPalette::Base, dark ? QColor("#1e212a") : Qt::white);
    palette.setColor(QPalette::Button, palette.color(QPalette::Base));
    palette.setColor(QPalette::Highlight, accent);
    palette.setColor(QPalette::HighlightedText, dark && accent.lightness() > 128 ? Qt::black : Qt::white);
    palette.setColor(QPalette::Mid, dark ? QColor("#626c80") : QColor("#a5adba"));
    palette.setColor(QPalette::PlaceholderText, dark ? QColor("#8c97aa") : QColor("#8993a3"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#8e98a8"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#8e98a8"));
    qApp->setPalette(palette);
    setStyleSheet(classic::stylesheet(dark, accent));
    classic::refreshFluentIcons(this, dark);
    for (auto editor : findChildren<QPlainTextEdit *>())
        for (auto highlighter : editor->document()->findChildren<QSyntaxHighlighter *>())
            highlighter->rehighlight();
    for (auto button : findChildren<QToolButton *>())
        if (button->property("uiGlyph").isValid())
            button->setIcon(classic::icon(char16_t(button->property("uiGlyph").toUInt()),
                                          palette.color(QPalette::WindowText)));
    for (auto list : {navigation_, bottomNavigation_})
        if (list)
            for (int i = 0; i < list->count(); ++i) {
                auto item = list->item(i);
                item->setIcon(classic::fluentIcon(item->data(Qt::UserRole + 2).toString(), dark));
            }
}
void Window::buildClassicUi() {
    classic::installChineseUi();
    if (QGuiApplication::platformName() == "offscreen") {
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyhbd.ttc");
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/segmdl2.ttf");
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/seguiemj.ttf");
    }
    qApp->setFont(QFont("Microsoft YaHei UI", 10));
    if (QFile::exists(data_ + "/native-ui.json"))
        try {
            uiPreferences_ = readJson(data_ + "/native-ui.json");
        } catch (...) {
        }
    navigationExpanded_ = uiPreferences_["navigation_expanded"].toBool(true);
    setWindowFlag(Qt::FramelessWindowHint);
    setWindowTitle(product::title);
    auto root = new QWidget;
    root->setObjectName("classicRoot");
    auto outer = new QVBoxLayout(root);
    outer->setContentsMargins(1, 1, 1, 1);
    outer->setSpacing(0);
    QIcon appIcon(root_ + "/icons/icon.png");
    outer->addWidget(new classic::TitleBar(this, appIcon));
    auto row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    auto sidebar = new QWidget;
    sidebar->setObjectName("navigationPanel");
    auto side = new QVBoxLayout(sidebar);
    side->setContentsMargins(6, 8, 6, 10);
    side->setSpacing(3);
    auto hamburger = new QToolButton;
    classic::setFluentIcon(hamburger, "Menu");
    hamburger->setFixedSize(40, 36);
    hamburger->setToolTip("展开 / 收起导航");
    side->addWidget(hamburger);
    navigation_ = new QListWidget;
    navigation_->setObjectName("navigation");
    navigation_->setIconSize({16, 16});
    navigation_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    side->addWidget(navigation_, 1);
    bottomNavigation_ = new QListWidget;
    bottomNavigation_->setObjectName("bottomNavigation");
    bottomNavigation_->setIconSize({16, 16});
    bottomNavigation_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bottomNavigation_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bottomNavigation_->setFixedHeight(128);
    side->addWidget(bottomNavigation_);
    auto version = new QLabel("v" + product::version);
    version->setObjectName("caption");
    version->setAlignment(Qt::AlignCenter);
    side->addWidget(version);
    pages_ = new QStackedWidget;
    pages_->setObjectName("classicPages");
    auto addPage = [&](QString id, QString title, QString icon, QWidget *page, bool bottom = false) {
        page->setObjectName(id);
        pageIndices_[id] = pages_->addWidget(page);
        auto item = new QListWidgetItem(title, bottom ? bottomNavigation_ : navigation_);
        item->setData(Qt::UserRole, id);
        item->setData(Qt::UserRole + 1, title);
        item->setData(Qt::UserRole + 2, icon);
        item->setToolTip(title);
    };
    auto devices = devicePage();
    addPage("dualpc", "双机设置", "Connect", devices);
    addPage("capture", "截图", "Play", capturePage());
    addPage("triggers", "实时触发", "StopWatch", tasksPage(true));
    addPage("tasks", "任务", "BookShelf", tasksPage(false));
    addPage("characters", "角色代码", "Code", charactersPage());
#ifdef OKWW_NATIVE_ONLY
    addPage("scripts", "脚本", "Code", scriptsPage());
    addPage("schedule", "定时任务", "Calendar", schedulePage());
    addPage("templates", "模板", "BookShelf", templatesPage());
#endif
    addPage("Game Hotkey", "游戏快捷键", "Game", globalGroupPage("Game Hotkey", "游戏快捷键"));
    addPage("Character Config", "角色设置", "People", globalGroupPage("Character Config", "角色设置"));
    addPage("Task Recovery", "中断恢复", "Sync", globalGroupPage("Task Recovery", "中断恢复"));
    logs_ = new QPlainTextEdit;
    logs_->setReadOnly(true);
    logs_->setMaximumBlockCount(1200);
    logs_->setObjectName("logs");
    pageIndices_["logs"] = pages_->addWidget(logs_);
    addPage("Notification", "通知", "Ringer", globalGroupPage("Notification", "通知"), true);
    addPage("settings", "设置", "Setting", globalPage(), true);
    addPage("about", "关于", "Question", aboutPage(), true);
    pageIndices_["Monthly Card Config"] = pageIndices_["settings"];
    row->addWidget(sidebar);
    row->addWidget(pages_, 1);
    outer->addLayout(row, 1);
    taskStatus_ = new QLabel("自动任务未启动");
    taskStatus_->setObjectName("applicationStatus");
    taskStatus_->setContentsMargins(16, 4, 12, 6);
    outer->addWidget(taskStatus_);
    const auto resizeNavigation = [this, sidebar, version] {
        sidebar->setFixedWidth(navigationExpanded_ ? 200 : 56);
        version->setVisible(navigationExpanded_);
        for (auto list : {navigation_, bottomNavigation_})
            for (int i = 0; i < list->count(); ++i) {
                auto item = list->item(i);
                item->setText(navigationExpanded_ ? item->data(Qt::UserRole + 1).toString() : QString{});
            }
    };
    resizeNavigation();
    connect(hamburger, &QToolButton::clicked, this, [this, resizeNavigation] {
        navigationExpanded_ = !navigationExpanded_;
        resizeNavigation();
        uiPreferences_["navigation_expanded"] = navigationExpanded_;
        try {
            writeJson(data_ + "/native-ui.json", uiPreferences_);
        } catch (const std::exception &e) {
            log(errorText(e));
        }
    });
    connect(navigation_, &QListWidget::currentRowChanged, this, [this](int i) {
        if (i >= 0)
            navigate(navigation_->item(i)->data(Qt::UserRole).toString());
    });
    connect(bottomNavigation_, &QListWidget::currentRowChanged, this, [this](int i) {
        if (i >= 0)
            navigate(bottomNavigation_->item(i)->data(Qt::UserRole).toString());
    });
    setCentralWidget(root);
    applyTheme();
    navigate("dualpc");
}
QWidget *Window::capturePage() {
    auto page = new QWidget;
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    auto hero = new QFrame;
    hero->setObjectName("classicCard");
    auto top = new QHBoxLayout(hero);
    top->setContentsMargins(16, 14, 16, 14);
    auto icon = new QLabel;
    icon->setPixmap(QIcon(root_ + "/icons/icon.png").pixmap(36, 36));
    top->addWidget(icon);
    auto labels = new QVBoxLayout;
    labels->addWidget(new QLabel(product::title));
    engineStatus_ = new QLabel("任务引擎正在加载…");
    engineStatus_->setObjectName("caption");
    labels->addWidget(engineStatus_);
    auto startupHint = new QLabel("在双机设置右上角“开始／停止”连接或断开设备；副本任务可在任务页直接开始。");
    startupHint->setObjectName("caption");
    startupHint->setWordWrap(true);
    labels->addWidget(startupHint);
    top->addLayout(labels, 1);
    top->addWidget(action("截图", this, [this] {
        try {
            if (busy_)
                fail("设备正在连接或断开，请稍候再截图。");
            auto frame = backend_.latestFrame();
            if (!frame)
                fail("请点击双机设置右上角“开始”连接设备，或展开“设备调试”仅连接预览。");
            const auto folder = data_ + "/screenshots/manual";
            QDir().mkpath(folder);
            const auto path =
                folder + '/' + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") + ".png";
            if (!frameImage(*frame).save(path))
                fail("截图保存失败");
            log("截图已保存：" + path);
        } catch (const std::exception &e) {
            log(errorText(e));
        }
    }));
    top->addWidget(action("刷新", this, [this] { scanDevices(); }));
    layout->addWidget(hero);
    banner_ = new QLabel("点击“开始”自动连接设备并启用实时任务；副本等任务可在任务页直接开始。");
    banner_->setObjectName("caption");
    banner_->setWordWrap(true);
    layout->addWidget(banner_);
    banner_->hide();
    auto columns = new QHBoxLayout;
    columns->setSpacing(12);
    auto add = [&](QString title, QString text, int stretch) {
        auto column = new QVBoxLayout;
        column->setSpacing(8);
        column->addWidget(new QLabel(title));
        auto list = new QListWidget;
        list->setStyleSheet("QListWidget { border:0; background:transparent; }");
        list->addItem(text);
        list->setCurrentRow(0);
        auto box = new QFrame;
        box->setObjectName("classicCard");
        auto contents = new QVBoxLayout(box);
        contents->setContentsMargins(16, 16, 16, 16);
        if (stretch == 2) {
            auto search = new QLineEdit;
            search->setPlaceholderText("搜索窗口标题或程序名称");
            search->setClearButtonEnabled(true);
            search->addAction(classic::fluentIcon("Search"), QLineEdit::TrailingPosition);
            contents->addWidget(search);
            connect(search, &QLineEdit::textChanged, this, [list](const QString &text) {
                for (int i = 0; i < list->count(); ++i)
                    list->item(i)->setHidden(!list->item(i)->text().contains(text, Qt::CaseInsensitive));
            });
        }
        contents->addWidget(list, 1);
        column->addWidget(box, 1);
        columns->addLayout(column, stretch);
        return list;
    };
    homeDevices_ = add("选择窗口", "外部采集 · 等待启动连接线程", 2);
    add("截图方式", "ExternalCapture", 1);
    add("选择交互方式", "HardwareHID", 1);
    layout->addLayout(columns, 1);
    layout->addWidget(new QLabel("开发工具"));
    auto tools = new QFrame;
    tools->setObjectName("classicCard");
    auto bar = new QHBoxLayout(tools);
    bar->setContentsMargins(12, 12, 12, 12);
    bar->addWidget(action("导出日志", this, [this] {
        auto file = QFileDialog::getSaveFileName(this, "导出运行日志", "OKWW-native.log", "日志 (*.log)");
        if (!file.isEmpty()) {
            QSaveFile out(file);
            if (out.open(QIODevice::WriteOnly)) {
                out.write(logs_->toPlainText().toUtf8());
                if (!out.commit())
                    log("导出日志失败");
            }
        }
    }));
    for (const auto &entry :
         {qMakePair(QString("安装目录"), root_), qMakePair(QString("截图目录"), data_ + "/screenshots"),
          qMakePair(QString("日志目录"), data_ + "/logs")})
        bar->addWidget(action(entry.first, this, [path = entry.second] {
            QDir().mkpath(path);
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        }));
    bar->addWidget(action("查看日志", this, [this] { navigate("logs"); }));
#ifdef OKWW_NATIVE_ONLY
    bar->addWidget(action("文字识别", this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, "识别已保存的截图", data_ + "/screenshots",
                                                       "图像 (*.png *.jpg *.jpeg *.bmp)");
        if (path.isEmpty())
            return;
        runAsync(
            [this, path] {
                auto model = root_ + "/assets/ocr/ppocrv5";
                if (!QFile::exists(model + "/det/det.onnx"))
                    model = root_ + "/.venv/Lib/site-packages/onnxocr/models/ppocrv5";
                runtime::Ocr ocr(model);
                QJsonArray lines;
                for (const auto &line : ocr.read(runtime::readImage(path)))
                    lines.append(line.text);
                return QJsonObject{{"lines", lines}};
            },
            [this](auto result) {
                QStringList lines;
                for (auto v : result["lines"].toArray())
                    lines << v.toString();
                QMessageBox::information(this, "OCR 识别结果",
                                         lines.isEmpty() ? "未识别到文字" : lines.join('\n'));
            });
    }));
#endif
    bar->addStretch();
    layout->addWidget(tools);
    return page;
}
QWidget *Window::globalGroupPage(const QString &id, const QString &title) {
    auto content = new QWidget;
    auto l = new QVBoxLayout(content);
    l->setContentsMargins(16, 16, 16, 16);
    l->setSpacing(12);
    l->addWidget(heading(title));
    auto description = new QLabel;
    description->setWordWrap(true);
    description->setObjectName("caption");
    l->addWidget(description);
    globalDescriptions_[id] = description;
    auto fields = new QWidget;
    fields->setLayout(new QVBoxLayout);
    l->addWidget(fields);
    l->addStretch();
    globalPages_[id] = fields;
    return scrolled(content);
}
QWidget *Window::aboutPage() { return buildClassicAbout(); }
void Window::renderTaskCards(bool trigger) {
    auto host = trigger ? triggerForm_ : taskForm_;
    auto layout = qobject_cast<QVBoxLayout *>(host->layout());
    QSet<QString> present;
    for (auto value : catalog_["tasks"].toArray()) {
        auto task = value.toObject();
        if (task["trigger"].toBool() != trigger)
            continue;
        const auto id = task["id"].toString();
        present.insert(id);
        auto card = taskCards_.value(id);
        if (!card) {
            card = new classic::ExpandCard(task["name"].toString(), task["description"].toString());
            card->setCompactHeader();
            card->setObjectName("task-" + id);
            taskCards_[id] = card;
            layout->insertWidget(layout->count() - 1, card);
            if (trigger) {
                auto toggle = new classic::Switch;
                toggle->setProperty("triggerSwitch", true);
                toggle->setFixedWidth(110);
                toggle->setAccessibleName("启用" + task["name"].toString());
                card->actions->addWidget(toggle);
                triggerSwitches_[id] = toggle;
                connect(toggle, &QAbstractButton::toggled, this, [this, id](bool on) {
                    ++pendingFieldWrites_;
                    send({{"op", "trigger"}, {"task", id}, {"enabled", on}}, [this, id, on](auto) {
                        --pendingFieldWrites_;
                        QJsonArray items;
                        for (auto v : catalog_["tasks"].toArray()) {
                            auto object = v.toObject();
                            if (object["id"] == id)
                                object["enabled"] = on;
                            items.append(object);
                        }
                        catalog_["tasks"] = items;
                        if (auto card = taskCards_.value(id))
                            if (auto status = card->body()->findChild<QLabel *>("configSaveStatus"))
                                status->setText("开关状态已自动保存");
                    }, [this, id](const QString &error) {
                        --pendingFieldWrites_;
                        if (auto card = taskCards_.value(id))
                            if (auto status = card->body()->findChild<QLabel *>("configSaveStatus"))
                                status->setText("保存失败：" + error);
                    });
                });
            } else {
                auto pauseButton = action("暂停", this, [this] { stopTask(); });
                pauseButton->setToolTip("暂停当前任务并释放按键，实时触发继续运行；点击开始重新执行此任务。");
                auto stop = action("停止", this, [this] { stopTask(); });
                stop->setToolTip("结束当前任务并释放按键，实时触发继续运行。停止线程请到双机设置。");
                stop->setObjectName("primary");
                auto run = action("开始", this, [this, id] {
                    if (pendingStart_ && pendingStart_->task == id)
                        stopTask();
                    else
                        startTask(id);
                }, true);
                classic::setFluentIcon(pauseButton, "Pause");
                classic::setFluentIcon(stop, "Stop");
                classic::setFluentIcon(run, "Play", true);
                for (auto control : {pauseButton, stop, run}) {
                    control->setObjectName("taskAction");
                    control->setProperty("compactTask", true);
                    control->setFixedSize(76, 32);
                    control->setIconSize({14, 14});
                }
                run->setProperty("taskActionRole", "run");
                pauseButton->setProperty("taskActionRole", "pause");
                stop->setProperty("taskActionRole", "stop");
                card->actions->addWidget(pauseButton);
                card->actions->addWidget(stop);
                card->actions->addWidget(run);
                taskStarts_[id] = run;
                taskStops_[id] = stop;
                taskPauses_[id] = pauseButton;
#ifdef OKWW_NATIVE_ONLY
                auto plan = new QToolButton;
                plan->setObjectName("taskSchedule");
                plan->setFixedSize(32, 32);
                classic::setFluentIcon(plan, "Calendar");
                plan->setToolTip("定时计划");
                plan->setAccessibleName("为此任务设置定时计划");
                connect(plan, &QToolButton::clicked, this,
                        [this, id, task] { editSchedules(id, task["name"].toString()); });
                card->actions->addWidget(plan);
#endif
            }
        }
        card->setText(task["name"].toString(), task["description"].toString());
        card->setProperty("taskSearchText", task["name"].toString() + ' ' + task["description"].toString());
        if (auto toggle = triggerSwitches_.value(id)) {
            QSignalBlocker blocker(toggle);
            toggle->setChecked(task["enabled"].toBool());
        }
        if (auto start = taskStarts_.value(id)) {
            const bool supported = !task.contains("native_supported") || task["native_supported"].toBool();
            start->setProperty("taskSupported", supported);
            start->setEnabled(supported);
        }
        const auto fields = task["fields"].toArray();
        card->body()->setProperty("autoSaveFields", trigger);
        const auto schema = [](QJsonArray fields) {
            QJsonArray result;
            for (auto value : fields) {
                auto field = value.toObject();
                field.remove("value");
                result.append(field);
            }
            return result;
        };
        const bool editing = trigger && qApp->focusWidget() && card->body()->isAncestorOf(qApp->focusWidget());
        const bool keepEditors = editing && card->body()->layout() && !card->property("forceFieldsRefresh").toBool() &&
            schema(card->property("fields").toJsonArray()) == schema(fields);
        if (keepEditors) card->setProperty("fields", fields);
        if (!keepEditors && (card->property("fields").toJsonArray() != fields || !card->body()->layout() ||
                             card->property("forceFieldsRefresh").toBool())) {
            card->setProperty("forceFieldsRefresh", false);
            buildFields(card->body(), fields, [this, id](QString key, QJsonValue v) {
                auto status = taskCards_.value(id)->body()->findChild<QLabel *>("configSaveStatus");
                if (status) status->setText("正在保存…");
                ++pendingFieldWrites_;
                send({{"op", "task_config"}, {"task", id}, {"key", key}, {"value", v}},
                     [this, id](auto c) {
                         --pendingFieldWrites_;
                         setCatalog(c);
                         if (auto card = taskCards_.value(id))
                             if (auto status = card->body()->findChild<QLabel *>("configSaveStatus")) {
                                 bool incomplete = false;
                                 for (auto input : card->body()->findChildren<QPlainTextEdit *>())
                                     incomplete |= input->property("autoSaveInvalid").toBool();
                                 status->setText(incomplete ? "有效配置已保存；部分输入不完整，尚未保存。"
                                     : id.startsWith("script:") && automation_
                                         ? "配置已自动保存，下一次触发时使用新参数。" : "配置已自动保存");
                             }
                     },
                     [this, id](const QString &error) {
                         --pendingFieldWrites_;
                         if (auto card = taskCards_.value(id))
                             if (auto status = card->body()->findChild<QLabel *>("configSaveStatus"))
                                 status->setText("保存失败：" + error);
                     });
            });
            card->setProperty("fields", fields);
            auto content = qobject_cast<QVBoxLayout *>(card->body()->layout());
            auto reset = new QWidget;
            auto row = new QHBoxLayout(reset);
            row->addWidget(new QLabel("操作"));
            row->addStretch();
            row->addWidget(action("重置配置", this, [this, id] {
                taskCards_.value(id)->setProperty("forceFieldsRefresh", true);
                send({{"op", "reset_config"}, {"group", id}}, [this](auto c) { setCatalog(c); });
            }));
            content->insertWidget(std::max(0, content->count() - 1), reset);
        }
    }
    const auto ids = taskCards_.keys();
    for (const auto &id : ids) {
        bool belongs = false;
        for (auto v : catalog_["tasks"].toArray())
            if (v.toObject()["id"] == id && v.toObject()["trigger"].toBool() == trigger)
                belongs = true;
        if (!belongs && !present.contains(id) &&
            ((trigger && triggerSwitches_.contains(id)) || (!trigger && taskStarts_.contains(id)))) {
            auto card = taskCards_.take(id);
            card->deleteLater();
            taskStarts_.remove(id);
            taskStops_.remove(id);
            taskPauses_.remove(id);
            triggerSwitches_.remove(id);
        }
    }
    if (!trigger) {
        auto search = findChild<QLineEdit *>("taskSearch");
        const auto query = search ? search->text().trimmed() : QString{};
        int visible = 0;
        for (const auto &id : present) {
            auto card = taskCards_.value(id);
            const auto text = card->property("taskSearchText").toString();
            const bool matches = query.isEmpty() || text.contains(query, Qt::CaseInsensitive);
            card->setVisible(matches);
            visible += matches;
        }
        if (auto count = findChild<QLabel *>("taskCount"))
            count->setText(query.isEmpty() ? QString("%1 项任务").arg(present.size())
                                          : QString("%1 / %2 项").arg(visible).arg(present.size()));
        if (auto empty = findChild<QLabel *>("tasksEmpty")) empty->setVisible(!visible);
    }
    refreshTaskActions();
#ifdef OKWW_NATIVE_ONLY
    if (!trigger) {
        auto list = pages_->findChild<QListWidget *>("scheduleTasks");
        if (list) {
            auto old = list->currentItem()
                           ? list->currentItem()->data(Qt::UserRole).toJsonObject()["id"].toString()
                           : QString{};
            list->clear();
            for (auto v : catalog_["tasks"].toArray()) {
                auto task = v.toObject();
                if (task["trigger"].toBool())
                    continue;
                auto item = new QListWidgetItem(task["name"].toString(), list);
                item->setData(Qt::UserRole, task);
                if (task["id"] == old)
                    list->setCurrentItem(item);
            }
            if (list->currentRow() < 0)
                list->setCurrentRow(0);
        }
    }
#endif
}
void Window::refreshTaskActions() {
    const bool editable = engineReady_ && !pendingStart_;
    for (auto card : taskCards_)
        card->body()->setEnabled(editable);
    for (auto toggle : triggerSwitches_)
        toggle->setEnabled(editable);
    if (globalForm_)
        globalForm_->setEnabled(editable);
    for (auto host : globalPages_)
        host->setEnabled(editable);
    if (deviceSettings_)
        deviceSettings_->setEnabled(!busy_ && !pendingStart_ && !stopRequested_ && !stoppingDevices_);
    for (const auto &page : {QString("characters")})
        if (pages_ && pageIndices_.contains(page))
            pages_->widget(pageIndices_[page])->setEnabled(!pendingStart_);
    for (auto it = taskStarts_.begin(); it != taskStarts_.end(); ++it) {
        const bool active = automation_ && currentTask_ == it.key();
        const bool preparing = pendingStart_ && pendingStart_->task == it.key();
        it.value()->setVisible(!active);
        const auto supported = it.value()->property("taskSupported");
        it.value()->setEnabled(preparing || (!busy_ && !pendingStart_ &&
                                            (!supported.isValid() || supported.toBool())));
        it.value()->setText(preparing ? "取消启动" : "开始");
        it.value()->setFixedWidth(preparing ? 100 : 76);
        const QString role = preparing ? "cancel" : "run";
        if (it.value()->property("taskActionRole").toString() != role) {
            it.value()->setProperty("taskActionRole", role);
            it.value()->style()->unpolish(it.value());
            it.value()->style()->polish(it.value());
        }
        it.value()->setToolTip(preparing ? "再次点击或按快捷键取消准备，不会在取消后运行任务。"
                                        : "自动连接设备，再运行此任务。");
        classic::setFluentIcon(it.value(), preparing ? "Cancel" : "Play", !preparing);
        it.value()->setIconSize({14, 14});
        taskStops_.value(it.key())->setVisible(active);
        taskPauses_.value(it.key())->setVisible(active);
        auto card = taskCards_.value(it.key());
        const QString state = active ? "running" : preparing ? "preparing" : "idle";
        if (card->property("taskState").toString() != state) {
            card->setProperty("taskState", state);
            card->style()->unpolish(card);
            card->style()->polish(card);
            card->update();
        }
        if (auto variant = card->findChild<QLabel *>("taskVariant")) {
            const auto text = active ? "运行中" : preparing ? "准备中" : variant->property("defaultText").toString();
            variant->setText(text);
            variant->setVisible(!text.isEmpty());
        }
    }
    refreshTaskInfo();
}
void Window::renderGlobalCards() {
    for (auto value : catalog_["globals"].toArray()) {
        auto group = value.toObject();
        const auto id = group["id"].toString();
        auto host =
            (id == "Basic Options" || id == "Monthly Card Config") ? globalForm_ : globalPages_.value(id);
        if (!host)
            continue;
        auto card = globalCards_.value(id);
        if (!card) {
            card = new classic::ExpandCard(group["name"].toString(), group["description"].toString());
            card->setHeaderIcon(id == "Basic Options" ? "Game" : "Info");
            card->body()->setProperty("boldLabels", id != "Basic Options");
            globalCards_[id] = card;
            qobject_cast<QVBoxLayout *>(host->layout())->addWidget(card);
            card->setExpanded(true);
            if (id == "Monthly Card Config")
                card->setExpanded(false);
            if (id != "Basic Options" && id != "Monthly Card Config") {
                card->findChild<QWidget *>("cardHeader")->hide();
                card->setObjectName("configContentFrame");
            }
        }
        if (auto description = globalDescriptions_.value(id)) {
            description->setText(group["description"].toString());
            description->setVisible(!description->text().isEmpty());
        }
        const auto fields = group["fields"].toArray();
        if (card->property("fields").toJsonArray() != fields || !card->body()->layout()) {
            buildFields(card->body(), fields, [this, id](QString key, QJsonValue value) {
                send({{"op", "global_config"}, {"group", id}, {"key", key}, {"value", value}},
                     [this](auto c) { setCatalog(c); });
            });
            card->setProperty("fields", fields);
            auto content = qobject_cast<QVBoxLayout *>(card->body()->layout());
            auto reset = new QWidget;
            auto row = new QHBoxLayout(reset);
            row->addWidget(new QLabel("操作"));
            row->addStretch();
            row->addWidget(action("重置配置", this, [this, id] {
                send({{"op", "reset_config"}, {"group", id}}, [this](auto c) { setCatalog(c); });
            }));
            content->insertWidget(std::max(0, content->count() - 1), reset);
        }
    }
}
} // namespace okww
