#include "window.hpp"
#include "runtime/scheduler.hpp"
namespace okww {
namespace {
class ScheduleDialog final : public QDialog {
  public:
    using QDialog::QDialog;
    bool pending = false;
    void reject() override {
        if (!pending)
            QDialog::reject();
    }

  protected:
    void closeEvent(QCloseEvent *event) override {
        if (pending)
            event->ignore();
        else
            QDialog::closeEvent(event);
    }
};
QPushButton *button(const QString &label, std::function<void()> fn, QObject *owner) {
    auto value = new QPushButton(label);
    QObject::connect(value, &QPushButton::clicked, owner, std::move(fn));
    return value;
}
} // namespace
bool Window::scheduledStart(QString task, bool exitAfter, double limitSeconds) {
    if (automation_ || busy_ || pendingStart_ || stopRequested_ || stoppingDevices_ || closing_) {
        log("计划任务未启动：当前任务或设备操作尚未结束。");
        return false;
    }
    bool valid = false;
    const auto tasks = engineReady_ ? catalog_["tasks"].toArray()
                                   : readJson(root_ + "/native/resources/catalog.json")["tasks"].toArray();
    for (auto v : tasks) {
        const auto t = v.toObject();
        if (t["id"] == task && !t["trigger"].toBool() && t["native_supported"].toBool(true))
            valid = true;
    }
    if (!valid || !std::isfinite(limitSeconds) || limitSeconds < 0 || limitSeconds > 604800) {
        log("计划参数无效，未发送游戏操作。");
        return false;
    }
    log("定时任务已到达，正在准备设备与任务：" + task);
    startTask(task, exitAfter, limitSeconds);
    return pendingStart_.has_value() || automation_;
}
void Window::editSchedules(const QString &task, const QString &label, const QJsonObject existing) {
    if (!engineReady_) {
        log("任务引擎正在加载，请稍候再设置计划。");
        return;
    }
    ScheduleDialog dialog(this);
    dialog.setObjectName("scheduleEditor");
    auto layout = classic::dialogLayout(dialog, existing.isEmpty() ? "创建计划任务" : "修改计划任务",
        "安排下一次运行。设备配置和任务参数会沿用当前保存的设置。", {780, 680});
    runtime::Scheduler scheduler(QCoreApplication::applicationFilePath(), root_, data_);
    const auto operate = [&](std::function<void(runtime::Scheduler &)> operation) {
        if (dialog.pending)
            return;
        dialog.pending = true;
        dialog.setEnabled(false);
        auto watcher = new QFutureWatcher<QString>(&dialog);
        connect(watcher, &QFutureWatcher<QString>::finished, &dialog, [&, watcher] {
            const auto error = watcher->result();
            watcher->deleteLater();
            dialog.pending = false;
            dialog.setEnabled(true);
            if (!error.isEmpty()) {
                log("计划未更新：" + error);
                QMessageBox::warning(&dialog, "计划未更新", error);
            } else
                dialog.accept();
        });
        watcher->setFuture(QtConcurrent::run([operation, scheduler]() mutable {
            try {
                operation(scheduler);
                return QString{};
            } catch (const std::exception &e) {
                return errorText(e);
            }
        }));
    };
    auto content = new QWidget;
    auto contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(14);
    contentLayout->setSizeConstraint(QLayout::SetMinimumSize);
    auto timing = new QFrame;
    timing->setObjectName("classicCard");
    timing->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto form = new QFormLayout(timing);
    classic::polishForm(form);
    form->setSizeConstraint(QLayout::SetMinimumSize);
    auto timingTitle = new QLabel("运行安排");
    timingTitle->setObjectName("cardTitle");
    form->addRow(timingTitle);
    contentLayout->addWidget(timing);
    auto area = new QScrollArea;
    area->setWidgetResizable(true);
    area->setWidget(content);
    layout->addWidget(area, 1);
    auto choices = new classic::ComboBox;
    choices->setObjectName("scheduleTask");
    for (auto value : catalog_["tasks"].toArray()) {
        auto entry = value.toObject();
        if (!entry["trigger"].toBool() && entry["native_supported"].toBool())
            choices->addItem(entry["name"].toString(), entry["id"].toString());
    }
    if (!task.isEmpty())
        choices->setCurrentIndex(choices->findData(task));
    choices->setEnabled(existing.isEmpty());
    form->addRow("选择任务", choices);
    auto start = new classic::DateTimeEditor(existing.isEmpty()
                                      ? QDateTime::currentDateTime().addSecs(3600)
                                      : QDateTime::fromString(existing["start"].toString(), Qt::ISODate));
    start->setObjectName("scheduleStart");
    form->addRow("首次执行", start);
    auto kind = new classic::ComboBox;
    kind->setObjectName("scheduleKind");
    for (const auto &p : QVector<QPair<QString, QString>>{{"每天", "Daily"},
                                                          {"每周", "Weekly"},
                                                          {"每月", "Monthly"},
                                                          {"仅一次", "Once"},
                                                          {"每隔若干天", "Days"},
                                                          {"每隔若干小时", "Hours"}})
        kind->addItem(p.first, p.second);
    if (!existing.isEmpty())
        kind->setCurrentIndex(kind->findData(existing["kind"].toString("Daily")));
    form->addRow("重复方式", kind);
    auto weekday = new classic::ComboBox;
    weekday->setObjectName("scheduleWeekday");
    weekday->addItems({"星期一", "星期二", "星期三", "星期四", "星期五", "星期六", "星期日"});
    weekday->setCurrentIndex(existing["weekday"].toInt(start->date().dayOfWeek()) - 1);
    form->addRow("星期", weekday);
    auto monthday = new QSpinBox;
    monthday->setObjectName("scheduleMonthday");
    monthday->setRange(1, 31);
    monthday->setValue(existing["monthday"].toInt(start->date().day()));
    form->addRow("每月日期", monthday);
    auto interval = new QSpinBox;
    interval->setObjectName("scheduleInterval");
    interval->setRange(1, kind->currentData() == "Hours" ? 744 : 365);
    interval->setValue(existing["interval"].toInt(1));
    form->addRow("间隔", interval);
    auto completion = new QFrame;
    completion->setObjectName("classicCard");
    completion->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto completionForm = new QFormLayout(completion);
    classic::polishForm(completionForm);
    completionForm->setSizeConstraint(QLayout::SetMinimumSize);
    auto completionTitle = new QLabel("执行选项");
    completionTitle->setObjectName("cardTitle");
    completionForm->addRow(completionTitle);
    contentLayout->addWidget(completion);
    auto limit = new QSpinBox;
    limit->setObjectName("scheduleLimit");
    limit->setRange(0, 168);
    limit->setSuffix(" 小时");
    limit->setSpecialValueText("不限制");
    limit->setValue(existing["limit_hours"].toInt());
    completionForm->addRow("最长运行", limit);
    auto exit = new QCheckBox("成功完成后退出软件");
    exit->setObjectName("scheduleExit");
    exit->setChecked(existing["exit_after"].toBool(true));
    completionForm->addRow(exit);
    contentLayout->addStretch();
    const auto visibility = [&] {
        auto k = kind->currentData().toString();
        form->setRowVisible(weekday, k == "Weekly");
        form->setRowVisible(monthday, k == "Monthly");
        form->setRowVisible(interval, k == "Days" || k == "Hours");
        interval->setMaximum(k == "Hours" ? 744 : 365);
    };
    connect(kind, &QComboBox::currentIndexChanged, &dialog, visibility);
    visibility();
    auto summary = new QLabel;
    summary->setObjectName("scheduleSummary");
    summary->setProperty("statusBadge", true);
    summary->setWordWrap(true);
    layout->addWidget(summary);
    const auto summarize = [=] {
        const auto date = start->dateTime();
        const auto time = date.toString("HH:mm");
        const auto k = kind->currentData().toString();
        QString text;
        if (k == "Daily") text = "每天 " + time;
        else if (k == "Weekly") text = "每周" + weekday->currentText().mid(2) + ' ' + time;
        else if (k == "Monthly") text = QString("每月 %1 日 %2").arg(monthday->value()).arg(time);
        else if (k == "Days") text = QString("每 %1 天 %2").arg(interval->value()).arg(time);
        else if (k == "Hours") text = QString("每隔 %1 小时").arg(interval->value());
        else text = "仅运行一次 · " + date.toString("M月d日 HH:mm");
        summary->setText("计划预览  ·  " + text + "\n" + date.toString("yyyy年M月d日") + " 起生效；已有任务运行时自动跳过。");
    };
    start->onChanged = summarize;
    connect(kind, &QComboBox::currentIndexChanged, &dialog, [summarize] { summarize(); });
    connect(weekday, &QComboBox::currentIndexChanged, &dialog, [summarize] { summarize(); });
    connect(monthday, &QSpinBox::valueChanged, &dialog, [summarize] { summarize(); });
    connect(interval, &QSpinBox::valueChanged, &dialog, [summarize] { summarize(); });
    summarize();
    auto create = button(
        existing.isEmpty() ? "创建" : "保存修改",
        [&] {
            try {
                if (choices->currentIndex() < 0)
                    fail("请选择有效任务");
                if (start->dateTime() <= QDateTime::currentDateTime() &&
                    (existing.isEmpty() || kind->currentData() == "Once"))
                    fail("首次执行时间需晚于当前时间");
                const QJsonObject spec{{"task", choices->currentData().toString()},
                                       {"label", choices->currentText()},
                                       {"kind", kind->currentData().toString()},
                                       {"start", start->dateTime().toString(Qt::ISODate)},
                                       {"weekday", weekday->currentIndex() + 1},
                                       {"monthday", monthday->value()},
                                       {"interval", interval->value()},
                                       {"limit_hours", limit->value()},
                                       {"exit_after", exit->isChecked()}};
                operate([spec, existing](runtime::Scheduler &scheduler) {
                    if (existing.isEmpty())
                        scheduler.add(spec);
                    else
                        scheduler.update(existing["name"].toString(), spec);
                });
            } catch (const std::exception &e) {
                QMessageBox::warning(&dialog, "计划未创建", errorText(e));
            }
        },
        &dialog);
    create->setObjectName("scheduleSave");
    create->setProperty("primary", true);
    create->setAutoDefault(false);
    auto close = new QDialogButtonBox(QDialogButtonBox::Close);
    close->button(QDialogButtonBox::Close)->setText("取消");
    close->button(QDialogButtonBox::Close)->setAutoDefault(false);
    close->addButton(create, QDialogButtonBox::ActionRole);
    connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(close);
    if (dialog.exec() == QDialog::Accepted)
        log(existing.isEmpty() ? "定时计划已创建。" : "定时计划已修改。");
    refreshScheduleTable();
}
} // namespace okww
