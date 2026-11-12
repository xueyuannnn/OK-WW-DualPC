#include "classic_editor.hpp"
#include "window.hpp"
#ifdef OKWW_NATIVE_ONLY
#include "runtime/scripts.hpp"
namespace okww {
namespace {
struct ActionInput {
    QWidget *widget;
    std::function<QString()> value;
};
ActionInput actionInput(const QString &key, const QString &defaultValue, bool mouse) {
    if (defaultValue == "True" || defaultValue == "False" || (mouse && key == "key")) {
        auto choice = new classic::ComboBox;
        choice->setObjectName("parameterChoice-" + key);
        const auto defaultLabel = defaultValue == "True" ? "是" : defaultValue == "False" ? "否" : "左键";
        choice->addItem("默认（" + QString(defaultLabel) + "）", "");
        if (mouse && key == "key") {
            choice->addItem("左键", "'left'");
            choice->addItem("中键", "'middle'");
            choice->addItem("右键", "'right'");
        } else {
            choice->addItem("是", "True");
            choice->addItem("否", "False");
        }
        return {choice, [choice] { return choice->currentData().toString(); }};
    }
    bool numeric = false;
    const double initial = defaultValue.toDouble(&numeric);
    const QSet<QString> numbers{"x", "y", "from_x", "from_y", "to_x", "to_y", "width", "height",
                               "timeout", "time_out", "duration", "count", "percent", "threshold",
                               "relative_x", "relative_y", "original_screen_width", "original_screen_height"};
    if (numeric || (defaultValue.isEmpty() && numbers.contains(key))) {
        auto wrapper = new QWidget;
        auto row = new QHBoxLayout(wrapper);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(8);
        auto mode = new classic::ComboBox;
        mode->setObjectName("parameterMode-" + key);
        if (!defaultValue.isEmpty())
            mode->addItem("默认", "default");
        mode->addItem("数值", "number");
        mode->addItem("表达式", "expression");
        auto number = new QDoubleSpinBox;
        number->setObjectName("parameterNumber-" + key);
        number->setRange(-1000000000, 1000000000);
        number->setDecimals(4);
        number->setValue(initial);
        number->setSingleStep(key.contains("time") || key.contains("sleep") || key == "duration" ? .05 : 1);
        auto expression = new QLineEdit;
        expression->setObjectName("parameterExpression-" + key);
        expression->setPlaceholderText("输入变量或计算表达式");
        auto fields = new QStackedWidget;
        fields->addWidget(number);
        fields->addWidget(expression);
        const auto update = [mode, number, fields] {
            fields->setCurrentIndex(mode->currentData() == "expression" ? 1 : 0);
            number->setEnabled(mode->currentData() != "default");
        };
        QObject::connect(mode, &QComboBox::currentIndexChanged, wrapper, update);
        update();
        row->addWidget(mode);
        row->addWidget(fields, 1);
        return {wrapper, [mode, number, expression] {
                    return mode->currentData() == "default" ? QString{} : mode->currentData() == "expression"
                        ? expression->text() : QString::number(number->value(), 'g', 14);
                }};
    }
    auto input = new QLineEdit;
    input->setObjectName("parameterText-" + key);
    QString hint = defaultValue;
    if (hint == "None") hint = "自动";
    if (hint == "'default'") hint = "默认引擎";
    input->setPlaceholderText(defaultValue.isEmpty() ? "必填；输入内容或表达式" : "留空使用默认值：" + hint);
    return {input, [input] { return input->text(); }};
}
} // namespace
void Window::scriptAction(QJsonObject command, std::function<void(QJsonObject)> done) {
    if (!engineReady_) {
        scriptMessage_->setText("任务引擎正在初始化，请稍后重试。");
        return;
    }
    if (scriptOperation_) {
        QTimer::singleShot(50, this, [this, command, done] {
            if (!closing_) scriptAction(command, done);
        });
        return;
    }
    scriptOperation_ = true;
    scriptMessage_->setText("正在更新脚本…");
    send(
        command,
        [this, done](auto result) {
            scriptOperation_ = false;
            setCatalog(result);
            if (scriptEditor_->document()->isModified())
                scriptMessage_->setText("设置已更新 · 编辑器仍有未保存的修改");
            if (done)
                done(result);
        },
        [this](const QString &error) {
            scriptOperation_ = false;
            scriptMessage_->setText(error);
            QMessageBox::warning(this, "脚本操作未完成", error);
        });
}
void Window::saveScript(std::function<void()> done) {
    if (editingScript_.isEmpty())
        return;
    const auto id = editingScript_, text = scriptEditor_->toPlainText();
    scriptAction({{"op", "script_save"}, {"script", id}, {"code", text}}, [this, id, text, done](auto) {
        if (editingScript_ == id && scriptEditor_->toPlainText() == text)
            scriptEditor_->document()->setModified(false);
        scriptMessage_->setText("脚本已保存；下次运行载入当前代码。");
        if (done)
            done();
    });
}
void Window::withSavedScript(std::function<void()> action) {
    if (!scriptEditor_ || !scriptEditor_->document()->isModified() || editingScript_.isEmpty()) {
        action();
        return;
    }
    const auto choice = QMessageBox::question(this, "脚本尚未保存", "保存当前脚本的修改？",
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (choice == QMessageBox::Save)
        saveScript(std::move(action));
    else if (choice == QMessageBox::Discard) {
        scriptEditor_->document()->setModified(false);
        showScript(editingScript_);
        action();
    }
}
void Window::showScript(const QString &id) {
    for (auto value : catalog_["scripts"].toArray()) {
        const auto script = value.toObject();
        if (script["id"] != id)
            continue;
        if (editingScript_ != id || scriptEditor_->toPlainText() != script["source"].toString()) {
            scriptEditor_->setPlainText(script["source"].toString());
            scriptEditor_->document()->setModified(false);
        }
        editingScript_ = id;
        scriptEditor_->setReadOnly(false);
        if (scriptWatcher_) {
            if (!scriptWatcher_->files().isEmpty())
                scriptWatcher_->removePaths(scriptWatcher_->files());
            const auto path = data_ + '/' + script["path"].toString();
            if (QFile::exists(path))
                scriptWatcher_->addPath(path);
        }
        scriptMessage_->setText(script["error"].toString().isEmpty()
                                    ? script["path"].toString() + " · Python 脚本，使用当前双机配置"
                                    : script["error"].toString());
        QSignalBlocker repeatBlock(scriptRepeat_), continuousBlock(scriptContinuous_);
        scriptRepeat_->setValue(script["repeat"].toInt(1));
        scriptContinuous_->setChecked(script["continuous"].toBool());
        scriptRepeat_->setEnabled(!script["trigger"].toBool() && !scriptContinuous_->isChecked());
        scriptContinuous_->setEnabled(!script["trigger"].toBool());
        return;
    }
    editingScript_.clear();
    scriptEditor_->clear();
    scriptEditor_->setReadOnly(true);
    scriptEditor_->document()->setModified(false);
    scriptMessage_->setText("创建脚本或导入原版 .okscript 脚本包。角色队伍代码仍在“角色代码”页面管理。");
}
void Window::refreshScripts() {
    if (!scriptChoice_)
        return;
    const bool modified = scriptEditor_->document()->isModified();
    const auto previous = editingScript_;
    QSignalBlocker blocker(scriptChoice_);
    scriptChoice_->clear();
    for (auto value : catalog_["scripts"].toArray()) {
        const auto script = value.toObject();
        scriptChoice_->addItem(script["name"].toString() + " · " + script["path"].toString(),
                               script["id"].toString());
    }
    int index = scriptChoice_->findData(previous);
    if (index < 0 && scriptChoice_->count())
        index = 0;
    scriptChoice_->setCurrentIndex(index);
    if (!modified)
        showScript(scriptChoice_->currentData().toString());
    const auto templates = catalog_["script_templates"].toArray();
    if (scriptSnippets_ && !templates.isEmpty() &&
        scriptSnippets_->property("templates").toJsonArray() != templates) {
        scriptSnippets_->setProperty("templates", templates);
        scriptSnippets_->clear();
        const QMap<QString, QString> groups{{"Mouse", "鼠标"},
                                            {"Key", "键盘"},
                                            {"Control", "流程"},
                                            {"OCR", "文字识别"},
                                            {"Template Matching", "模板识别"},
                                            {"Logging", "日志"},
                                            {"Box", "区域"}};
        for (auto value : templates) {
            const auto info = value.toObject();
            auto item =
                new QListWidgetItem(info["category_label"].toString(groups.value(info["category"].toString(), "其他操作")) +
                                        " · " + info["display_name"].toString(info["name"].toString()),
                                    scriptSnippets_);
            item->setData(Qt::UserRole, info);
            item->setToolTip(info["full_doc_zh"].toString(info["full_doc"].toString()));
        }
    }
}
QWidget *Window::scriptsPage() {
    auto page = new QWidget;
    scriptWatcher_ = new QFileSystemWatcher(page);
    connect(scriptWatcher_, &QFileSystemWatcher::fileChanged, page, [this, page](const QString &path) {
        QTimer::singleShot(100, page, [this, path] {
            if (QFile::exists(path) && !scriptWatcher_->files().contains(path))
                scriptWatcher_->addPath(path);
        });
        if (scriptOperation_)
            return;
        if (automation_ || pendingStart_) {
            scriptMessage_->setText("源文件已在外部修改；暂停后点击刷新载入，当前执行继续使用已加载的代码。");
            return;
        }
        if (scriptEditor_->document()->isModified() &&
            QMessageBox::question(this, "脚本文件已更改",
                                  "源文件已在外部修改，重新载入并放弃编辑器中未保存的修改？") !=
                QMessageBox::Yes)
            return;
        scriptEditor_->document()->setModified(false);
        scriptAction({{"op", "script_catalog"}});
    });
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 16, 20, 16);
    auto title = new QLabel("脚本");
    title->setObjectName("pageTitle");
    layout->addWidget(title);
    auto first = new QHBoxLayout;
    scriptChoice_ = new classic::ComboBox;
    scriptChoice_->setObjectName("scriptChoice");
    scriptChoice_->setMinimumWidth(260);
    first->addWidget(scriptChoice_, 1);
    const auto addButton = [page](QHBoxLayout *bar, QString name, QString text, std::function<void()> run) {
        auto button = new QPushButton(text);
        button->setObjectName(name);
        classic::setActionIcon(button);
        if (name == "scriptRun" || name == "scriptSave")
            button->setProperty("primary", true);
        if (name == "scriptDelete")
            button->setProperty("danger", true);
        bar->addWidget(button);
        QObject::connect(button, &QPushButton::clicked, page, std::move(run));
        return button;
    };
    const auto create = [this](bool copy) {
        if (copy && editingScript_.isEmpty()) {
            scriptMessage_->setText("请先选择要复制的脚本。");
            return;
        }
        withSavedScript([this, copy] {
            bool accepted = false;
            const auto name = QInputDialog::getText(
                this, copy ? "复制脚本" : "新建脚本", "文件名（英文字母、数字、下划线，例如 DailyMacro）",
                QLineEdit::Normal, copy ? "CopiedTask" : "CustomTask", &accepted);
            if (!accepted)
                return;
            QJsonObject request{{"op", "script_create"}, {"name", name}};
            if (copy)
                request["copy_from"] = editingScript_;
            scriptAction(request, [this](auto result) {
                const auto id = result["selected_script"].toString();
                scriptChoice_->setCurrentIndex(scriptChoice_->findData(id));
                showScript(id);
            });
        });
    };
    addButton(first, "scriptCreate", "新建", [create] { create(false); });
    addButton(first, "scriptCopy", "复制", [create] { create(true); });
    addButton(first, "scriptSave", "保存", [this] { saveScript(); });
    addButton(first, "scriptRefresh", "刷新",
              [this] { withSavedScript([this] { scriptAction({{"op", "script_catalog"}}); }); });
    layout->addLayout(first);
    auto second = new QHBoxLayout;
    addButton(second, "scriptImport", "导入脚本包", [this] {
        withSavedScript([this] {
            const auto path =
                QFileDialog::getOpenFileName(this, "导入原版脚本包", {}, "OKScript 脚本包 (*.okscript)");
            if (!path.isEmpty())
                scriptAction({{"op", "script_import"}, {"path", path}});
        });
    });
    addButton(second, "scriptExport", "导出脚本包", [this] {
        withSavedScript([this] {
            if (editingScript_.isEmpty())
                return;
            const auto path = QFileDialog::getSaveFileName(
                this, "导出脚本及同包资源", "custom_scripts.okscript", "OKScript 脚本包 (*.okscript)");
            if (!path.isEmpty())
                scriptAction({{"op", "script_export"}, {"script", editingScript_}, {"path", path}});
        });
    });
    addButton(second, "scriptDelete", "删除", [this] {
        if (!editingScript_.isEmpty() &&
            QMessageBox::question(this, "删除脚本", "删除当前脚本？源文件会移入个人配置的脚本回收目录。") ==
                QMessageBox::Yes) {
            scriptEditor_->document()->setModified(false);
            scriptAction({{"op", "script_delete"}, {"script", editingScript_}});
        }
    });
    addButton(second, "scriptRecord", "录制", [this] { withSavedScript([this] { recordScript(); }); });
    second->addStretch();
    layout->addLayout(second);
    auto runBar = new QHBoxLayout;
    runBar->addWidget(new QLabel("循环次数"));
    scriptRepeat_ = new QSpinBox;
    scriptRepeat_->setObjectName("scriptRepeat");
    scriptRepeat_->setRange(1, 1000000);
    scriptRepeat_->setMinimumWidth(110);
    runBar->addWidget(scriptRepeat_);
    scriptContinuous_ = new QCheckBox("持续循环");
    scriptContinuous_->setObjectName("scriptContinuous");
    runBar->addWidget(scriptContinuous_);
    runBar->addStretch();
    addButton(runBar, "scriptRun", "运行", [this] {
        withSavedScript([this] {
            if (editingScript_.isEmpty())
                return;
            for (auto value : catalog_["scripts"].toArray()) {
                const auto script = value.toObject();
                if (script["id"] == editingScript_ && script["trigger"].toBool()) {
                    send({{"op", "trigger"}, {"task", editingScript_}, {"enabled", true}},
                         [this](auto result) {
                             setCatalog(result);
                             startTask();
                         });
                    return;
                }
            }
            startTask(editingScript_);
        });
    });
    addButton(runBar, "scriptPause", "暂停", [this] { stopTask(); });
    layout->addLayout(runBar);
    auto split = new QSplitter;
    auto snippets = new QListWidget;
    scriptSnippets_ = snippets;
    snippets->setObjectName("scriptSnippets");
    snippets->setMinimumWidth(120);
    snippets->setMaximumWidth(280);
    snippets->setMinimumWidth(230);
    for (const auto &pair : QList<QPair<QString, QString>>{
             {"点击画面", "        self.click_relative(0.5, 0.5, after_sleep=0.3)\n"},
             {"按键", "        self.send_key('f', down_time=0.05)\n"},
             {"长按按键",
              "        self.send_key_down('w')\n        self.sleep(1)\n        self.send_key_up('w')\n"},
             {"等待", "        self.sleep(1)\n"},
             {"等待并点击模板", "        self.wait_click_feature('template_1', time_out=10)\n"},
             {"识别文字", "        texts = self.ocr()\n        self.log_info(str(texts))\n"},
             {"输出日志", "        self.log_info('执行到这里')\n"}}) {
        auto item = new QListWidgetItem(pair.first, snippets);
        item->setData(Qt::UserRole, pair.second);
    }
    scriptEditor_ = new classic::CodeEditor;
    scriptEditor_->setObjectName("scriptEditor");
    scriptEditor_->document()->setProperty("codeLanguage", "python");
    auto palette = new QWidget;
    auto paletteLayout = new QVBoxLayout(palette);
    paletteLayout->setContentsMargins(0, 0, 0, 0);
    auto paletteTitle = new QLabel("动作库");
    paletteTitle->setObjectName("detailTitle");
    paletteLayout->addWidget(paletteTitle);
    auto paletteHint = new QLabel("双击动作，填写参数后插入脚本");
    paletteHint->setObjectName("caption");
    paletteLayout->addWidget(paletteHint);
    auto search = new QLineEdit;
    search->setPlaceholderText("搜索操作模板");
    search->setObjectName("scriptTemplateSearch");
    paletteLayout->addWidget(search);
    paletteLayout->addWidget(snippets, 1);
    split->addWidget(palette);
    split->addWidget(scriptEditor_);
    split->setStretchFactor(1, 1);
    layout->addWidget(split, 1);
    scriptMessage_ = new QLabel("创建或导入脚本后开始编辑。");
    scriptMessage_->setObjectName("scriptMessage");
    scriptMessage_->setWordWrap(true);
    layout->addWidget(scriptMessage_);
    connect(search, &QLineEdit::textChanged, page, [snippets](const QString &text) {
        for (int i = 0; i < snippets->count(); ++i) {
            auto item = snippets->item(i);
            item->setHidden(!item->text().contains(text, Qt::CaseInsensitive) &&
                            !item->toolTip().contains(text, Qt::CaseInsensitive) &&
                            !item->data(Qt::UserRole).toJsonObject()["name"].toString().contains(text, Qt::CaseInsensitive));
        }
    });
    connect(snippets, &QListWidget::itemDoubleClicked, page, [this](QListWidgetItem *item) {
        const auto info = item->data(Qt::UserRole).toJsonObject();
        if (info.isEmpty()) {
            scriptEditor_->textCursor().insertText(item->data(Qt::UserRole).toString());
            scriptEditor_->setFocus();
            return;
        }
        QDialog dialog(this);
        dialog.setObjectName("scriptActionDialog");
        auto layout = classic::dialogLayout(dialog, info["display_name"].toString(info["name"].toString()),
            info["doc_zh"].toString(info["doc"].toString()), {760, 560});
        auto area = new QScrollArea;
        area->setWidgetResizable(true);
        area->setFrameShape(QFrame::NoFrame);
        auto content = new QWidget;
        auto fields = new QVBoxLayout(content);
        fields->setContentsMargins(0, 0, 0, 0);
        auto common = new QFrame;
        common->setObjectName("classicCard");
        auto form = new QFormLayout(common);
        classic::polishForm(form);
        fields->addWidget(common);
        auto advanced = new classic::ExpandCard("高级选项", "调整搜索区域、识别处理和异常行为");
        auto advancedForm = new QFormLayout(advanced->body());
        classic::polishForm(advancedForm);
        fields->addWidget(advanced);
        fields->addStretch();
        QMap<QString, std::function<QString()>> inputs;
        const QSet<QString> usual{"x", "y", "feature", "feature_name", "box", "key", "timeout", "time_out",
                                  "text", "message", "condition", "threshold", "down_time", "after_sleep",
                                  "relative_x", "relative_y", "count", "duration", "from_x", "from_y", "to_x", "to_y"};
        for (auto value : info["params"].toArray()) {
            const auto p = value.toArray();
            const auto key = p[0].toString();
            const auto field = actionInput(key, p[1].toString(), info["category"] == "Mouse");
            const auto label = info["param_labels"].toObject()[key].toString(key);
            field.widget->setAccessibleName(label);
            field.widget->setToolTip(info["param_docs_zh"].toObject()[key].toString());
            inputs[key] = field.value;
            auto target = p[1].toString().isEmpty() || usual.contains(key) ? form : advancedForm;
            target->addRow(label, field.widget);
        }
        advanced->setVisible(advancedForm->rowCount() > 0);
        area->setWidget(content);
        layout->addWidget(area, 1);
        auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Ok)->setText("插入动作");
        buttons->button(QDialogButtonBox::Ok)->setProperty("primary", true);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (!inputs.isEmpty() && dialog.exec() != QDialog::Accepted)
            return;
        QJsonObject values;
        for (auto it = inputs.begin(); it != inputs.end(); ++it)
            values[it.key()] = it.value()();
        scriptAction(
            {{"op", "script_snippet"}, {"name", info["name"]}, {"values", values}}, [this](auto result) {
                scriptEditor_->textCursor().insertText("        " + result["snippet"].toString() + '\n');
                scriptEditor_->setFocus();
            });
    });
    connect(scriptChoice_, &QComboBox::currentIndexChanged, page, [this] {
        const auto selected = scriptChoice_->currentData().toString();
        if (selected == editingScript_)
            return;
        {
            QSignalBlocker blocker(scriptChoice_);
            scriptChoice_->setCurrentIndex(scriptChoice_->findData(editingScript_));
        }
        withSavedScript([this, selected] {
            {
                QSignalBlocker blocker(scriptChoice_);
                scriptChoice_->setCurrentIndex(scriptChoice_->findData(selected));
            }
            showScript(selected);
        });
    });
    connect(scriptRepeat_, &QSpinBox::editingFinished, page, [this] {
        if (!editingScript_.isEmpty())
            scriptAction({{"op", "script_configure"},
                          {"script", editingScript_},
                          {"key", "_repeat"},
                          {"value", scriptRepeat_->value()}});
    });
    connect(scriptContinuous_, &QCheckBox::toggled, page, [this](bool on) {
        scriptRepeat_->setEnabled(!on);
        if (!editingScript_.isEmpty())
            scriptAction({{"op", "script_configure"},
                          {"script", editingScript_},
                          {"key", "_continuous"},
                          {"value", on}});
    });
    auto save = new QShortcut(QKeySequence::Save, scriptEditor_);
    save->setContext(Qt::WidgetWithChildrenShortcut);
    connect(save, &QShortcut::activated, page, [this] { saveScript(); });
    return page;
}
} // namespace okww
#endif
