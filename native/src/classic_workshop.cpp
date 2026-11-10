#include "window.hpp"
#ifdef OKWW_NATIVE_ONLY
#include "runtime/workshop.hpp"
#include <array>
#include <QDesktopServices>
#include <QUrlQuery>
namespace okww {
namespace {
class PartyPortrait final : public QWidget {
    QPixmap image_;
    QString name_;
  public:
    explicit PartyPortrait(QWidget *parent = nullptr) : QWidget(parent) { setFixedSize(72, 96); }
    void setMember(const QString &root, const QString &id, const QString &name) {
        image_ = classic::characterPortrait(root, id);
        name_ = name;
        setAccessibleName(name);
        setToolTip(image_.isNull() ? name + " · 暂无头像" : name);
        update();
    }
  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        classic::paintPortrait(painter, QRectF(0, 0, 72, 72), image_, name_, palette());
        auto font = painter.font(); font.setPixelSize(12); painter.setFont(font);
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(QRect(0, 78, 72, 18), Qt::AlignCenter,
            QFontMetrics(font).elidedText(name_, Qt::ElideRight, 72));
    }
};
class WorkshopDelegate final : public QStyledItemDelegate {
    QString root_;
  public:
    WorkshopDelegate(QString root, QObject *parent) : QStyledItemDelegate(parent), root_(std::move(root)) {}
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return {530, 106}; }
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        const auto entry = index.data(Qt::UserRole + 1).toJsonObject();
        const auto roles = entry["team"].toString().split(',', Qt::SkipEmptyParts);
        const auto names = entry["team_display"].toString().split(" / ");
        const bool dark = qApp->palette().color(QPalette::Window).lightness() < 128;
        const QColor accent(dark ? "#8caeff" : "#3269e8");
        p->save(); p->setRenderHint(QPainter::Antialiasing);
        const QRect rect = option.rect.adjusted(3, 4, -3, -4);
        auto fill = qApp->palette().color(QPalette::Base);
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        if (selected || option.state.testFlag(QStyle::State_MouseOver)) {
            const double amount = selected ? .06 : .025;
            fill.setRgbF(fill.redF()*(1-amount)+accent.redF()*amount,
                         fill.greenF()*(1-amount)+accent.greenF()*amount,
                         fill.blueF()*(1-amount)+accent.blueF()*amount);
        }
        QColor border = selected ? accent : option.palette.color(QPalette::Mid);
        border.setAlpha(selected ? 160 : 30);
        p->setPen(QPen(border, 1)); p->setBrush(fill); p->drawRoundedRect(rect, 10, 10);
        for (int i = 0; i < std::min(3, int(roles.size())); ++i) {
            const QRectF imageRect(rect.left() + 14 + i * 52, rect.top() + 24, 46, 46);
            const auto name = i < names.size() ? names[i] : roles[i].trimmed();
            classic::paintPortrait(*p, imageRect, classic::characterPortrait(root_, roles[i]), name, option.palette);
        }
        const int left = rect.left() + 184, width = std::max(0, rect.right() - left - 16);
        const auto description = entry["description_display"].toString(entry["description"].toString());
        auto title = description.section('\n', 0, 0).trimmed();
        if (title.isEmpty() || title.startsWith("作者暂未")) title = entry["name_display"].toString();
        auto headline = option.font; headline.setPixelSize(13); headline.setWeight(QFont::DemiBold);
        p->setFont(headline); p->setPen(option.palette.color(QPalette::Text));
        const auto party = entry["team_display"].toString().replace(" / ", " · ");
        p->drawText(QRect(left, rect.top()+13, width, 22), Qt::AlignVCenter,
                    QFontMetrics(headline).elidedText(party, Qt::ElideRight, width));
        auto body = option.font; body.setPixelSize(12); p->setFont(body);
        QColor muted = option.palette.color(QPalette::Text); muted.setAlpha(170); p->setPen(muted);
        p->drawText(QRect(left, rect.top()+38, width, 19), Qt::AlignVCenter,
                    QFontMetrics(body).elidedText(title, Qt::ElideRight, width));
        const bool compatible = entry["compatible"].toBool(true);
        const QString state = !compatible ? "待适配" : entry["downloaded"].toBool() ? "已缓存" : "待下载";
        const auto statusColor = compatible ? accent : QColor(dark ? "#dfb16d" : "#8e642b");
        QRect pill(rect.right()-72, rect.top()+67, 58, 20);
        QColor tint = statusColor; tint.setAlpha(20);
        p->setPen(Qt::NoPen); p->setBrush(tint); p->drawRoundedRect(pill, 6, 6);
        p->setPen(statusColor); p->drawText(pill, Qt::AlignCenter, state);
        const auto meta = entry["author"].toString() + "  ·  v" + entry["version"].toString();
        p->setPen(muted);
        p->drawText(QRect(left, rect.top()+67, std::max(0,width-70), 20), Qt::AlignVCenter,
                    QFontMetrics(body).elidedText(meta, Qt::ElideRight, std::max(0,width-70)));
        p->restore();
    }
};
} // namespace
void Window::workshopRequest(QJsonObject request, std::function<void(QJsonObject)> done) {
    const bool readOnly = request["op"] == "library";
    if (workshopBusy_ && !readOnly) {
        done({{"_error", "工坊正在同步，请稍候再试。"}});
        return;
    }
    if (!readOnly)
        workshopBusy_ = true;
    auto watcher = new QFutureWatcher<QJsonObject>(this);
    connect(watcher, &QFutureWatcher<QJsonObject>::finished, this, [this, watcher, done, readOnly] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (!readOnly)
            workshopBusy_ = false;
        done(result);
    });
    watcher->setFuture(QtConcurrent::run([root = root_, data = data_, request] {
        try {
            return runtime::workshopManage(root, data, request);
        } catch (const std::exception &error) {
            return QJsonObject{{"_error", errorText(error)}};
        }
    }));
}
void Window::updateWorkshop() {
    if (closing_ || workshopBusy_)
        return;
    try {
        const auto settingsPath = data_ + "/workshop-library/settings.json";
        const auto settings = QFile::exists(settingsPath) ? readJson(settingsPath) : QJsonObject{};
        if (!settings["auto_update"].toBool(true))
            return;
        const auto indexPath = data_ + "/workshop-library/index.json";
        const auto index = QFile::exists(indexPath) ? readJson(indexPath) : QJsonObject{};
        const auto hours = std::clamp(settings["interval_hours"].toInt(24), 1, 720);
        if (QDateTime::currentSecsSinceEpoch() - index["attempted_at"].toDouble() < hours * 3600.)
            return;
    } catch (const std::exception &error) {
        log("角色工坊设置读取失败：" + errorText(error));
        return;
    }
    workshopRequest({{"op", "library_refresh"}, {"due", true}}, [this](auto result) {
        const auto error = result["_error"].toString(result["error"].toString());
        if (!error.isEmpty())
            log("角色工坊：" + error);
        else if (result["auto_update"].toBool())
            log(QString("角色工坊：本地 %1 / %2 份代码，可在“角色代码 → 工坊”选择导入。")
                    .arg(result["downloaded"].toInt())
                    .arg(result["total"].toInt()));
    });
}
void Window::openWorkshop() {
    if (!settleCodeEdits())
        return;
    QDialog dialog(this);
    dialog.setObjectName("workshopDialog");
    auto layout = classic::dialogLayout(dialog, "角色代码工坊",
        "选择队伍  →  阅读方案与适用条件  →  导入使用", {1220, 820});
    QPointer<QDialog> guard(&dialog);
    auto settings = new classic::ExpandCard("联网与更新设置", "自动同步新的方案，已导入队伍保持当前版本");
    settings->setHeaderIcon("Sync");
    auto settingsLayout = new QVBoxLayout(settings->body());
    settingsLayout->setContentsMargins(18, 4, 18, 18);
    auto source = new QLineEdit;
    source->setObjectName("workshopSource");
    source->setPlaceholderText("原工坊 GitHub 仓库或 teams.json 地址");
    auto form = new QFormLayout;
    form->addRow("工坊源", source);
    settingsLayout->addLayout(form);
    auto options = new QHBoxLayout;
    auto automatic = new QCheckBox("联网自动同步");
    automatic->setObjectName("workshopAutomatic");
    auto save = new QPushButton("保存设置");
    save->setObjectName("workshopSave");
    auto refresh = new QPushButton("检查并同步");
    auto download = new QPushButton("下载全部 / 重试");
    options->addWidget(automatic);
    auto interval = new QSpinBox;
    interval->setObjectName("workshopInterval");
    interval->setRange(1, 720);
    interval->setValue(24);
    interval->setSuffix(" 小时");
    interval->setToolTip("自动检查并下载工坊更新的间隔，1 至 720 小时；也可随时手动同步。");
    options->addWidget(new QLabel("周期"));
    options->addWidget(classic::sliderEditor(interval));
    options->addStretch();
    settings->actions->addWidget(refresh);
    settingsLayout->addLayout(options);
    auto settingActions = new QHBoxLayout;
    settingActions->addStretch();
    settingActions->addWidget(download);
    settingActions->addWidget(save);
    settingsLayout->addLayout(settingActions);
    layout->addWidget(settings);
    auto searchBar = new QHBoxLayout;
    auto search = new QLineEdit;
    search->setObjectName("workshopSearch");
    search->setPlaceholderText("搜索角色、队伍或作者…");
    search->setClearButtonEnabled(true);
    auto searchIcon = search->addAction(classic::fluentIcon("Search", palette().color(QPalette::Window).lightness() < 128), QLineEdit::LeadingPosition);
    searchIcon->setProperty("fluentIcon", "Search");
    searchBar->addWidget(search, 1);
    auto filter = new classic::ComboBox;
    filter->setObjectName("workshopFilter");
    filter->addItems({"全部方案", "已缓存方案", "当前可导入", "角色待适配"});
    searchBar->addWidget(filter);
    auto count = new QLabel;
    count->setObjectName("caption");
    searchBar->addWidget(count);
    layout->addLayout(searchBar);
    auto split = new QSplitter;
    split->setChildrenCollapsible(false);
    auto table = new QTableWidget(0, 5);
    table->setObjectName("workshopTable");
    table->setHorizontalHeaderLabels({"队伍 / 方案", "作者", "版本", "状态", "说明"});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->hide();
    for (int column : {1, 2, 3})
        table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    for (int column = 1; column < 5; ++column) table->setColumnHidden(column, true);
    table->setShowGrid(false);
    table->setWordWrap(false);
    table->verticalHeader()->setDefaultSectionSize(106);
    table->setItemDelegateForColumn(0, new WorkshopDelegate(root_, table));
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setMouseTracking(true);
    table->setMinimumWidth(480);
    split->addWidget(table);
    auto detailCard = new QFrame;
    detailCard->setObjectName("classicCard");
    detailCard->setMinimumWidth(280);
    auto detailLayout = new QVBoxLayout(detailCard);
    detailLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    detailLayout->setContentsMargins(20, 20, 20, 18);
    detailLayout->setSpacing(16);
    auto stage = new QLabel("方案预览");
    stage->setObjectName("caption");
    detailLayout->addWidget(stage);
    auto portraitsRow = new QHBoxLayout;
    portraitsRow->setSpacing(12);
    std::array<PartyPortrait *, 3> portraits;
    for (int slot = 0; slot < 3; ++slot) {
        portraits[slot] = new PartyPortrait;
        portraits[slot]->setObjectName("workshopPortrait-" + QString::number(slot));
        portraitsRow->addWidget(portraits[slot]);
    }
    portraitsRow->addStretch();
    auto detailTitle = new QLabel("先选择左侧方案");
    detailTitle->setObjectName("detailTitle");
    detailTitle->setWordWrap(true);
    detailTitle->setMaximumHeight(84);
    detailLayout->addWidget(detailTitle);
    detailLayout->addLayout(portraitsRow);
    auto detailMeta = new QLabel;
    detailMeta->setObjectName("workshopMeta");
    detailMeta->setProperty("caption", true);
    detailMeta->setWordWrap(true);
    detailLayout->addWidget(detailMeta);
    auto compatibility = new QWidget;
    compatibility->setObjectName("workshopCompatibility");
    auto compatibilityLayout = new QVBoxLayout(compatibility);
    compatibilityLayout->setContentsMargins(0, 0, 0, 0);
    compatibilityLayout->setSpacing(8);
    auto compatibilityHint = new QLabel("此方案所需的角色尚未适配，可提醒软件作者更新。\n反馈邮箱：" + product::supportEmail);
    compatibilityHint->setObjectName("workshopCompatibilityHint");
    compatibilityHint->setWordWrap(true);
    compatibilityHint->setProperty("caption", true);
    compatibilityLayout->addWidget(compatibilityHint);
    auto feedbackActions = new QHBoxLayout;
    auto feedback = new QPushButton("提醒作者更新");
    feedback->setObjectName("workshopFeedback");
    feedback->setProperty("primary", true);
    feedback->setToolTip("打开邮件客户端，预填邮箱和当前方案信息；查看并确认后发送。");
    classic::setFluentIcon(feedback, "Send", true);
    auto copyFeedback = new QPushButton("复制反馈信息");
    copyFeedback->setObjectName("workshopCopyFeedback");
    classic::setFluentIcon(copyFeedback, "Copy");
    feedbackActions->addWidget(feedback);
    feedbackActions->addWidget(copyFeedback);
    feedbackActions->addStretch();
    compatibilityLayout->addLayout(feedbackActions);
    compatibility->hide();
    detailLayout->addWidget(compatibility);
    auto detail = new QTextBrowser;
    detail->setObjectName("workshopDetails");
    detail->setOpenExternalLinks(false);
    detail->setProperty("readingPanel", true);
    detail->setPlaceholderText("选择左侧方案，查看适用场景、角色要求和完整说明。");
    detail->setMinimumHeight(140);
    detailLayout->addWidget(detail, 1);
    auto previewScroll = new QScrollArea;
    previewScroll->setObjectName("workshopPreviewScroll");
    previewScroll->setFrameShape(QFrame::NoFrame);
    previewScroll->setWidgetResizable(true);
    previewScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    previewScroll->setMinimumWidth(300);
    previewScroll->setWidget(detailCard);
    split->addWidget(previewScroll);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    layout->addWidget(split, 1);
    auto hint = new QLabel("正在读取本地代码库…");
    hint->setObjectName("workshopStatus");
    hint->setProperty("subtleStatus", true);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto bottom = new QHBoxLayout;
    auto importButton = new QPushButton("导入选中版本");
    importButton->setObjectName("workshopImport");
    importButton->setProperty("primary", true);
    classic::setFluentIcon(importButton, "Download", true);
    auto close = new QPushButton("关闭");
    auto notice = new QLabel("已缓存方案可离线导入 · 原始署名与代码保留");
    notice->setObjectName("caption");
    bottom->addWidget(notice, 1);
    bottom->addWidget(importButton);
    bottom->addWidget(close);
    layout->addLayout(bottom);
    auto current = std::make_shared<QJsonObject>();
    auto feedbackEntry = std::make_shared<QJsonObject>();
    const auto describe = [root = root_, current, table, detail, detailTitle, detailMeta, portraits, importButton,
                           compatibility, feedbackEntry] {
        *feedbackEntry = QJsonObject{};
        compatibility->hide();
        const auto selected = table->item(table->currentRow(), 0);
        if (!selected) {
            detail->clear();
            detailTitle->setText("没有符合条件的方案");
            detailMeta->setText("换个角色名或调整筛选条件试试。");
            for (auto portrait : portraits) portrait->hide();
            importButton->setEnabled(false);
            return;
        }
        for (auto value : (*current)["entries"].toArray()) {
            const auto entry = value.toObject();
            if (entry["id"] != selected->data(Qt::UserRole).toString())
                continue;
            const auto text = [](QString value) { return value.toHtmlEscaped().replace('\n', "<br>"); };
            const auto status = entry["compatible"].toBool(true) ? "可导入" : "角色待适配";
            const auto roles = entry["team"].toString().split(',', Qt::SkipEmptyParts);
            const auto names = entry["team_display"].toString().split(" / ");
            for (int slot = 0; slot < 3; ++slot) {
                portraits[slot]->setVisible(slot < roles.size());
                if (slot < roles.size()) portraits[slot]->setMember(root, roles[slot], slot < names.size() ? names[slot] : roles[slot]);
            }
            auto description = entry["description_display"].toString(entry["description"].toString());
            auto title = description.section('\n', 0, 0).trimmed();
            if (title.isEmpty() || title.startsWith("作者暂未")) title = entry["name_display"].toString(entry["name"].toString());
            detailTitle->setText(entry["team_display"].toString().replace(" / ", " · "));
            detailTitle->setToolTip(entry["team_display"].toString());
            detailMeta->setText(QString("作者：%1  ·  v%2  ·  %3").arg(entry["author"].toString(), entry["version"].toString(), status));
            detail->setHtml(QString("<p style='line-height:155%;margin:0'>%1</p>").arg(text(description)));
            importButton->setEnabled(entry["compatible"].toBool(true));
            if (!entry["compatible"].toBool(true)) {
                *feedbackEntry = entry;
                compatibility->show();
            }
            break;
        }
    };
    auto render = [guard, current, table, search, filter, count, describe] {
        if (!guard)
            return;
        const auto selected = table->item(table->currentRow(), 0);
        const auto previous = selected ? selected->data(Qt::UserRole).toString() : QString{};
        QSignalBlocker blocker(table);
        table->setRowCount(0);
        int selectedRow = -1;
        for (auto value : (*current)["entries"].toArray()) {
            const auto entry = value.toObject();
            const auto description = entry["description_display"].toString(entry["description"].toString());
            const QStringList fields{entry["team_display"].toString(entry["team"].toString()) + "\n" + description.section('\n', 0, 0),
                                     entry["author"].toString(), entry["version"].toString(),
                                     !entry["compatible"].toBool(true) ? "待适配" : entry["downloaded"].toBool() ? "已缓存" : "待下载",
                                     description};
            if (!(fields.join(' ') + entry["team"].toString() + entry["name"].toString()).contains(search->text(), Qt::CaseInsensitive))
                continue;
            if ((filter->currentIndex() == 1 && !entry["downloaded"].toBool()) ||
                (filter->currentIndex() == 2 && !entry["compatible"].toBool(true)) ||
                (filter->currentIndex() == 3 && entry["compatible"].toBool(true)))
                continue;
            const int row = table->rowCount();
            table->insertRow(row);
            if (entry["id"] == previous)
                selectedRow = row;
            for (int col = 0; col < fields.size(); ++col) {
                auto item = new QTableWidgetItem(fields[col]);
                item->setToolTip(fields[col] + '\n' + entry["error"].toString());
                item->setData(Qt::UserRole, entry["id"]);
                item->setData(Qt::UserRole + 1, entry);
                table->setItem(row, col, item);
            }
        }
        if (table->rowCount())
            table->selectRow(selectedRow < 0 ? 0 : selectedRow);
        count->setText(QString("%1 份方案").arg(table->rowCount()));
        describe();
    };
    auto show = [guard, current, render, source, automatic, interval, hint](QJsonObject result) {
        if (!guard)
            return;
        if (result.contains("_error")) {
            hint->setText(result["_error"].toString());
            return;
        }
        *current = result;
        source->setText(result["source"].toString());
        automatic->setChecked(result["auto_update"].toBool());
        interval->setValue(result["interval_hours"].toInt(24));
        const auto rawTime = result["last_checked"].toString();
        auto checked = QDateTime::fromString(rawTime, Qt::ISODate);
        if (!checked.isValid()) checked = QDateTime::fromString(rawTime, "yyyy-MM-dd HH:mm:ss");
        const auto checkedText = checked.isValid() ? checked.toLocalTime().toString("M月d日 HH:mm") : "尚未成功检查";
        hint->setText(QString("已缓存 %1 / %2 份  ·  上次检查：%3")
                          .arg(result["downloaded"].toInt())
                          .arg(result["total"].toInt())
                          .arg(checkedText) + (result["error"].toString().isEmpty() ? "" : "\n" + result["error"].toString()));
        render();
    };
    connect(search, &QLineEdit::textChanged, &dialog, [render] { render(); });
    connect(filter, &QComboBox::currentIndexChanged, &dialog, [render] { render(); });
    connect(table, &QTableWidget::itemSelectionChanged, &dialog, describe);
    auto copyRequest = [feedbackEntry, hint] {
        if (feedbackEntry->isEmpty())
            return;
        const auto url = classic::workshopFeedbackUrl(*feedbackEntry);
        const QUrlQuery query(url);
        QApplication::clipboard()->setText("收件人：" + product::supportEmail + "\n主题：" +
            query.queryItemValue("subject", QUrl::FullyDecoded) + "\n\n" + query.queryItemValue("body", QUrl::FullyDecoded));
        hint->setText("反馈信息已复制，可发送至 " + product::supportEmail + "。");
    };
    connect(copyFeedback, &QPushButton::clicked, &dialog, copyRequest);
    connect(feedback, &QPushButton::clicked, &dialog, [feedbackEntry, hint, copyRequest] {
        if (feedbackEntry->isEmpty())
            return;
        if (QDesktopServices::openUrl(classic::workshopFeedbackUrl(*feedbackEntry)))
            hint->setText("已请求打开邮件客户端，请查看并确认反馈内容后发送。");
        else {
            copyRequest();
            hint->setText("未能打开邮件客户端，反馈信息已复制，可手动发送至 " + product::supportEmail + "。");
        }
    });
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(save, &QPushButton::clicked, &dialog, [this, source, automatic, interval, show] {
        workshopRequest({{"op", "library_settings"},
                         {"source", source->text()},
                         {"auto_update", automatic->isChecked()},
                         {"interval_hours", interval->value()}},
                        show);
    });
    connect(refresh, &QPushButton::clicked, &dialog, [this, guard, source, automatic, interval, hint, show] {
        hint->setText("正在同步原工坊索引和全部代码包，界面可以继续操作…");
        workshopRequest({{"op", "library_settings"},
                         {"source", source->text()},
                         {"auto_update", automatic->isChecked()},
                         {"interval_hours", interval->value()}},
                        [this, guard, show](auto result) {
                            if (!guard)
                                return;
                            if (result.contains("_error"))
                                show(result);
                            else
                                workshopRequest({{"op", "library_refresh"}}, show);
                        });
    });
    connect(download, &QPushButton::clicked, &dialog, [this, hint, show] {
        hint->setText("正在下载代码包，完成后可离线选择…");
        workshopRequest({{"op", "library_download"}}, show);
    });
    connect(importButton, &QPushButton::clicked, &dialog, [this, guard, table, hint] {
        if (automation_ || pendingStart_ || !nativeEngine_ || !nativeEngine_->idle()) {
            hint->setText("请先暂停任务，再导入工坊版本。");
            return;
        }
        const auto item = table->item(table->currentRow(), 0);
        if (!item)
            return;
        workshopRequest(
            {{"op", "library_select"}, {"entry", item->data(Qt::UserRole).toString()}},
            [this, guard, hint](auto result) {
                if (!guard)
                    return;
                if (result.contains("_error")) {
                    hint->setText(result["_error"].toString());
                    return;
                }
                const auto m = result["manifest"].toObject();
                const auto text =
                    QString(
                        "%1\n作者：%2 · 版本：%3\n\n导入并启用此工坊代码？相同队伍原有代码会保存一份备份。")
                        .arg(result["team_display"].toString(m["team"].toString()), m["author"].toString(), m["version"].toString());
                if (QMessageBox::question(guard, "导入工坊队伍", text) != QMessageBox::Yes)
                    return;
                send(
                    {{"op", "team_import"}, {"path", result["path"]}},
                    [this, guard, hint](auto catalog) {
                        setCatalog(catalog);
                        if (guard)
                            hint->setText("队伍已导入，当前使用此版本的 Python 工坊代码。");
                    },
                    [guard, hint](auto error) {
                        if (guard)
                            hint->setText(error);
                    });
            });
    });
    workshopRequest({{"op", "library"}}, show);
    dialog.exec();
}
} // namespace okww
#endif
