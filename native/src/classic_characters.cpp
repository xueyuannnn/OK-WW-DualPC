#include "window.hpp"
#include "classic_editor.hpp"
#include <QDesktopServices>
namespace okww {
namespace {
class TeamDelegate final : public QStyledItemDelegate {
    QString root_;
  public:
    TeamDelegate(QString root, QObject *parent) : QStyledItemDelegate(parent), root_(std::move(root)) {}
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return {230, 88}; }
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        auto background = option;
        initStyleOption(&background, index);
        background.text.clear();
        auto style = option.widget ? option.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &background, p, option.widget);
        const auto team = index.data(Qt::UserRole).toJsonObject();
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(option.palette.color(QPalette::Text));
        const auto rect = option.rect.adjusted(10, 5, -10, -5);
        auto font = option.font;
        font.setPixelSize(13);
        font.setWeight(QFont::DemiBold);
        p->setFont(font);
        p->drawText(QRect(rect.left(), rect.top(), rect.width(), 26), Qt::AlignVCenter | Qt::AlignLeft,
                    QFontMetrics(font).elidedText(index.data().toString(), Qt::ElideRight, rect.width()));
        const auto members = team["team"].toArray();
        for (int slot = 0; slot < std::min(3, int(members.size())); ++slot)
            classic::paintPortrait(*p, QRectF(rect.left() + slot * 38, rect.top() + 31, 32, 32),
                classic::characterPortrait(root_, members[slot].toString()), "?", option.palette);
        const auto mode = team["source_files"].toArray().isEmpty() ? "未导入" : team["mode"] == "import" ? "工坊" : "内置";
        p->setFont(option.font);
        p->setPen(option.palette.color(QPalette::Highlight));
        p->drawText(rect.adjusted(118, 30, 0, 0), Qt::AlignVCenter | Qt::AlignRight, mode);
        p->restore();
    }
};
QPushButton *action(QString text, QString icon, QObject *owner, std::function<void()> run,
                    bool primary = false) {
    auto button = new QPushButton(text);
    if (primary)
        button->setObjectName("primary");
    classic::setFluentIcon(button, icon, primary);
    QObject::connect(button, &QPushButton::clicked, owner, std::move(run));
    return button;
}
} // namespace
QString Window::characterName(const QString &id) const {
    for (auto value : catalog_["characters"].toArray()) {
        auto character = value.toObject();
        if (character["id"].toString().compare(id, Qt::CaseInsensitive) == 0)
            return character["name"].toString(id);
    }
    return id;
}
bool Window::settleCodeEdits() {
    if (!code_->document()->isModified())
        return true;
    const auto answer = QMessageBox::question(this, "代码尚未保存", "是否先保存当前修改？",
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                              QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save) {
        saveCurrentCode();
        return !code_->document()->isModified();
    }
    code_->document()->setModified(false);
    return true;
}
void Window::saveCurrentCode() {
    const auto team = editingTeam_;
    const auto source = editingSource_.isEmpty() ? editingMember_ : editingSource_;
    if (team.isEmpty() || source.isEmpty() || code_->isReadOnly())
        return;
    const auto text = code_->toPlainText();
    send({{"op", "team_save"}, {"team", team}, {"member", source}, {"code", text}}, [this, team, source,
                                                                                     text](auto catalog) {
        if (editingTeam_ == team && (editingSource_.isEmpty() ? editingMember_ : editingSource_) == source &&
            code_->toPlainText() == text)
            code_->document()->setModified(false);
        setCatalog(catalog);
        log(source.endsWith(".py") ? "Python 工坊代码已保存，下次战斗载入时生效。"
                                   : "源码草稿已保存；编译并导入原生队伍包后生效。");
    });
}
void Window::switchTeamCode(QJsonObject request) {
    if (teamOperation_) return;
    teamOperation_ = true;
    showTeam();
    send(request, [this](auto catalog) {
        teamOperation_ = false;
        setCatalog(catalog);
        log(QString("队伍代码切换完成，用时 %1 ms；已清除旧队伍状态，下次启动重新识别。").arg(catalog["team_switch_ms"].toInt()));
    }, [this](const QString &) {
        teamOperation_ = false;
        showTeam();
    });
}
QWidget *Window::buildClassicCharacters() {
    auto page = new QWidget;
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    auto splitter = new QSplitter;
    splitter->setChildrenCollapsible(false);
    auto left = new QWidget;
    auto teams = new QVBoxLayout(left);
    teams->setContentsMargins(0, 0, 8, 0);
    teams->setSpacing(6);
    teams->addWidget(new QLabel("队伍"));
    teamList_ = new QListWidget;
    teamList_->setObjectName("teamList");
    teamList_->setMinimumWidth(210);
    teamList_->setMaximumWidth(300);
    teamList_->setItemDelegate(new TeamDelegate(root_, teamList_));
    teamList_->setUniformItemSizes(true);
    teams->addWidget(teamList_, 1);
    allTeamModesButton_ = action("切换全部队伍为内置代码", "Sync", this, [this] {
        if (!settleCodeEdits())
            return;
        const auto mode = allTeamModesButton_->property("targetMode").toString();
        if (QMessageBox::question(this, "切换全部队伍代码", allTeamModesButton_->text() + "？",
                                  QMessageBox::Yes | QMessageBox::Cancel) == QMessageBox::Yes)
            switchTeamCode({{"op", "team_all_mode"}, {"mode", mode}});
    });
    teams->addWidget(allTeamModesButton_);
    auto first = new QHBoxLayout;
    first->addWidget(action(
        "创建队伍", "Add", this,
        [this] {
            if (!settleCodeEdits())
                return;
            QDialog dialog(this);
            dialog.setObjectName("createTeamDialog");
            auto content = classic::dialogLayout(dialog, "创建队伍", "按出场顺序选择三名不同的角色。创建后可导入工坊连招或使用内置代码。", {560, 440});
            auto card = new QFrame;
            card->setObjectName("classicCard");
            auto form = new QFormLayout(card);
            classic::polishForm(form);
            content->addWidget(card);
            std::array<QComboBox *, 3> choices;
            int slot = 0;
            for (auto &choice : choices) {
                choice = new classic::ComboBox;
                for (auto value : catalog_["characters"].toArray()) {
                    auto c = value.toObject();
                    choice->addItem(c["name"].toString(), c["id"].toString());
                }
                choice->setCurrentIndex(std::min(slot, choice->count() - 1));
                form->addRow(QString("%1 号位角色").arg(++slot), choice);
            }
            content->addStretch();
            auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            buttons->button(QDialogButtonBox::Ok)->setText("创建队伍");
            buttons->button(QDialogButtonBox::Ok)->setProperty("primary", true);
            content->addWidget(buttons);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            if (dialog.exec() == QDialog::Accepted) {
                QJsonArray team;
                for (auto choice : choices)
                    team.append(choice->currentData().toString());
                send({{"op", "team_create"}, {"team", team}}, [this](auto c) { setCatalog(c); });
            }
        },
        true));
    first->addWidget(action("删除队伍", "Delete", this, [this] {
        auto team = selectedTeam();
        if (team.isEmpty() || !settleCodeEdits())
            return;
        if (QMessageBox::question(this, "删除队伍",
                                  "将此队伍移出列表？配置和草稿会移入个人数据目录的 deleted-teams，可找回。",
                                  QMessageBox::Yes | QMessageBox::Cancel) == QMessageBox::Yes)
            send({{"op", "team_delete"}, {"team", team["team"]}}, [this](auto c) { setCatalog(c); });
    }));
    teams->addLayout(first);
    auto second = new QHBoxLayout;
    auto workshop = action("工坊", "BookShelf", this, [this] {
#ifdef OKWW_NATIVE_ONLY
        openWorkshop();
#else
        QDesktopServices::openUrl(QUrl("https://cfokwwchar.ok-script.com"));
#endif
    });
    workshop->setObjectName("openWorkshop");
    second->addWidget(workshop);
    auto importTeam = action("导入队伍", "Download", this, [this] {
        if (!settleCodeEdits())
            return;
        const auto path = QFileDialog::getOpenFileName(this, "导入 OK-WW / OK-WW-E 工坊队伍", {},
                                                       "Python 工坊队伍 (*.zip);;原生队伍包 (*.okww-team)");
        if (!path.isEmpty())
            send({{"op", "team_import"}, {"path", path}}, [this](auto c) { setCatalog(c); });
    });
    importTeam->setToolTip("直接选择工坊下载的队伍 ZIP，无需解压或转换为 C++。\n"
                           "导入后可切换内置 / 工坊代码，支持的键鼠操作通过所选盒子输出。\n"
                           "新增角色、额外依赖或特殊输入接口可能需要适配。");
    second->addWidget(importTeam);
    teams->addLayout(second);
    auto importHint = new QLabel("兼容 OK-WW / OK-WW-E 工坊 ZIP");
    importHint->setObjectName("caption");
    importHint->setWordWrap(true);
    teams->addWidget(importHint);
    auto third = new QHBoxLayout;
    third->addWidget(action("导出队伍", "Share", this, [this] {
        const auto team = selectedTeam();
        if (team.isEmpty() || !settleCodeEdits())
            return;
        const bool python = team["python"].toBool();
        const auto extension = python ? QString(".zip") : QString(".okww-team");
        auto path = QFileDialog::getSaveFileName(this, "导出队伍", {},
                                                 python ? "工坊队伍 (*.zip)" : "原生队伍包 (*.okww-team)");
        if (path.isEmpty())
            return;
        if (!path.endsWith(extension, Qt::CaseInsensitive))
            path += extension;
        send({{"op", "team_export"}, {"team", team["team"]}, {"path", path}},
             [this](auto) { log("队伍已导出。"); });
    }));
    auto upload = action("上传队伍", "GitHub", this, [] {});
    upload->setEnabled(false);
    upload->setToolTip("可在工坊网站提交导出的 Python ZIP 队伍包。");
    third->addWidget(upload);
    teams->addLayout(third);
    auto right = new QWidget;
    auto editor = new QVBoxLayout(right);
    editor->setContentsMargins(8, 0, 0, 0);
    editor->setSpacing(6);
    auto top = new QHBoxLayout;
    characterPortrait_ = new classic::AspectImageLabel;
    characterPortrait_->setObjectName("characterPortrait");
    characterPortrait_->setFixedSize(52, 52);
    characterPortrait_->setContentsMargins(2, 2, 2, 2);
    characterPortrait_->setAlignment(Qt::AlignCenter);
    top->addWidget(characterPortrait_);
    top->addWidget(new QLabel("角色代码"));
    member_ = new classic::ComboBox;
    member_->setObjectName("characterMember");
    member_->setMinimumWidth(190);
    top->addWidget(member_);
    top->addStretch();
    top->addWidget(action("询问 AI", "Robot", this, [this] {
        QApplication::clipboard()->setText(
            (editingSource_.endsWith(".py")
                 ? QString("请协助检查这段 OKWW Python 工坊角色代码，保留原有连招和双机输入接口。\n\n")
                 : QString("请协助检查这段 OKWW C++ 原生队伍代码，保留原有连招顺序和双机输入接口。\n\n")) +
            code_->toPlainText());
        log("代码和提问模板已复制，可粘贴到你使用的 AI 中。");
    }));
    editor->addLayout(top);
    teamCompatibility_ = new QLabel("双机导入：键鼠指令通过所选盒子输出");
    teamCompatibility_->setWordWrap(true);
    editor->addWidget(teamCompatibility_);
    auto files = new QHBoxLayout;
    files->addWidget(new QLabel("代码文件"));
    codeSource_ = new classic::ComboBox;
    codeSource_->setObjectName("characterSource");
    codeSource_->setMinimumContentsLength(18);
    codeSource_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    files->addWidget(codeSource_, 1);
    codeView_ = new classic::ComboBox;
    codeView_->setObjectName("characterCodeView");
    codeView_->addItem("当前使用的代码", "active");
    files->addWidget(codeView_);
#ifdef OKWW_NATIVE_ONLY
    editor->addLayout(files);
#else
    auto hiddenFiles = new QWidget;
    hiddenFiles->setLayout(files);
    hiddenFiles->hide();
    editor->addWidget(hiddenFiles);
#endif
    code_ = new classic::CodeEditor;
    code_->setObjectName("codeEditor");
    code_->setMinimumHeight(360);
    editor->addWidget(code_, 1);
    auto bottom = new QHBoxLayout;
    teamStatus_ = new QLabel;
    teamStatus_->setObjectName("caption");
    bottom->addWidget(teamStatus_, 1);
    teamModeButton_ = action("切换为内置代码", "Sync", this, [this] {
        if (!settleCodeEdits())
            return;
        const auto team = selectedTeam();
        if (!team.isEmpty())
            switchTeamCode({{"op", "team_mode"},
                  {"team", team["team"]},
                  {"mode", team["mode"] == "import" ? "builtin" : "import"}});
    });
    teamModeButton_->setObjectName("teamCodeMode");
    bottom->addWidget(teamModeButton_);
    saveCodeButton_ = action("保存", "Save", this, [this] { saveCurrentCode(); }, true);
    saveCodeButton_->setToolTip("保存 C++ 源码草稿；编译并导入原生队伍包后生效。");
    bottom->addWidget(saveCodeButton_);
    editor->addLayout(bottom);
    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({260, 760});
    layout->addWidget(splitter);
    connect(teamList_, &QListWidget::currentRowChanged, this, &Window::showTeam);
    connect(member_, &QComboBox::currentIndexChanged, this, &Window::loadCode);
    connect(codeSource_, &QComboBox::activated, this,
            [this] { requestTeamCode(codeSource_->currentData().toString()); });
    connect(codeView_, &QComboBox::activated, this, [this] { loadCode(); });
    return page;
}
void Window::updateCharacterPortrait() {
    if (!characterPortrait_)
        return;
    auto members = selectedTeam()["team"].toArray();
    const auto selected = member_->currentData().toString();
    characterPortrait_->clear();
    if (selected.isEmpty() || !members.contains(selected))
        return;
    static QHash<QString, QPixmap> cache;
    const auto key = root_ + '/' + selected;
    if (!cache.contains(key)) {
        cache.insert(key, QPixmap(root_ + "/native/resources/portraits/" + selected + ".png"));
    }
    const auto portrait = cache.value(key);
    if (portrait.isNull()) {
        characterPortrait_->setText(characterName(selected).left(1));
        characterPortrait_->setToolTip(characterName(selected) + "（暂无完整头像）");
    } else {
        characterPortrait_->setPixmap(portrait);
        characterPortrait_->setToolTip(characterName(selected));
    }
}
void Window::restoreCodeSelection() {
    QSignalBlocker teamBlock(teamList_), memberBlock(member_), sourceBlock(codeSource_), viewBlock(codeView_);
    for (int i = 0; i < teamList_->count(); ++i)
        if (teamList_->item(i)->data(Qt::UserRole).toJsonObject()["team"].toArray() == editingTeam_)
            teamList_->setCurrentRow(i);
    member_->setCurrentIndex(member_->findData(editingMember_));
    codeSource_->setCurrentIndex(codeSource_->findData(editingSource_));
    codeView_->setCurrentIndex(codeView_->findData(editingView_));
    updateCharacterPortrait();
}
void Window::requestTeamCode(const QString &requested) {
#ifdef OKWW_NATIVE_ONLY
    if (loading_ || !engineReady_ || teamOperation_)
        return;
    const auto team = selectedTeam()["team"].toArray();
    const auto member = member_->currentData().toString();
    const auto view = codeView_->currentData().toString();
    if (team.isEmpty() || member.isEmpty()) {
        code_->clear();
        code_->setReadOnly(true);
        saveCodeButton_->setEnabled(false);
        characterPortrait_->clear();
        return;
    }
    if (code_->document()->isModified()) {
        if (team == editingTeam_ && member == editingMember_ && view == editingView_ &&
            requested == editingSource_)
            return;
        if (!settleCodeEdits()) {
            restoreCodeSelection();
            return;
        }
    }
    const int request = ++codeRequest_;
    updateCharacterPortrait();
    code_->clear();
    code_->setReadOnly(true);
    code_->document()->setModified(false);
    saveCodeButton_->setEnabled(false);
    teamCompatibility_->setText("正在读取所选角色的代码…");
    send(
        {{"op", "team_editor"}, {"team", team}, {"member", member}, {"source", requested}, {"view", view}},
        [this, team, member, view, request](auto result) {
            if (request != codeRequest_ || selectedTeam()["team"].toArray() != team ||
                member_->currentData().toString() != member || codeView_->currentData().toString() != view)
                return;
            QSignalBlocker sourceBlock(codeSource_);
            codeSource_->clear();
            for (auto value : result["file_choices"].toArray()) {
                const auto file = value.toObject();
                codeSource_->addItem(file["label"].toString(), file["id"].toString());
            }
            codeSource_->setCurrentIndex(codeSource_->findData(result["source_file"].toString()));
            codeSource_->setToolTip(result["source_file"].toString());
            editingTeam_ = team;
            editingMember_ = member;
            editingSource_ = result["source_file"].toString();
            editingView_ = view;
            editingMode_ = result["active_mode"].toString();
            code_->document()->setProperty("codeLanguage", result["language"].toString());
            code_->setPlainText(result["code"].toString());
            code_->setReadOnly(result["read_only"].toBool(true));
            code_->document()->setModified(false);
            saveCodeButton_->setEnabled(!code_->isReadOnly());
            const QString kind = result["code_kind"] == "builtin"  ? "内置 C++ 连招"
                                 : result["code_kind"] == "python" ? "Python 工坊连招"
                                                                   : "导入队伍连招";
            saveCodeButton_->setToolTip(result["code_kind"] == "python"
                                            ? "保存并校验 Python 工坊代码，下次战斗载入生效。"
                                            : "保存 C++ 源码草稿，重新编译并导入后生效。");
            QString description = characterName(member) + " · " + kind + "。" + result["message"].toString();
            if (result["view_only"].toBool())
                description = "仅查看保留的导入代码；当前仍使用内置连招。" + description;
            if (result["draft"].toBool())
                description += " 当前显示未编译草稿，实际执行仍使用已编译模块。";
            teamCompatibility_->setText(description);
            QTextCursor cursor(
                code_->document()->findBlockByLineNumber(std::max(0, result["focus_line"].toInt(1) - 1)));
            code_->setTextCursor(cursor);
            code_->ensureCursorVisible();
            updateCharacterPortrait();
        },
        [this, request](QString error) {
            if (request != codeRequest_)
                return;
            teamCompatibility_->setText("读取所选代码失败：" + error);
            code_->setPlainText("// " + error);
            code_->document()->setModified(false);
        });
#else
    Q_UNUSED(requested);
#endif
}
} // namespace okww
