#include "window.hpp"
#ifdef OKWW_NATIVE_ONLY
#include "runtime/scheduler.hpp"
namespace okww {
namespace {
QString nextRun(const QJsonObject &spec) {
    if (!spec["enabled"].toBool())
        return "—";
    auto next = QDateTime::fromString(spec["start"].toString(), Qt::ISODate).toLocalTime();
    const auto now = QDateTime::currentDateTime();
    if (!next.isValid())
        return "—";
    const auto kind = spec["kind"].toString();
    const int interval = std::max(1, spec["interval"].toInt(1));
    if (next < now) {
        if (kind == "Once")
            return "已结束";
        if (kind == "Hours")
            next = next.addSecs((next.secsTo(now) / (interval * 3600) + 1) * interval * 3600);
        else if (kind == "Monthly") {
            auto date = QDate(now.date().year(), now.date().month(), 1);
            for (int i = 0; i < 13; ++i, date = date.addMonths(1)) {
                auto requested = QDate(date.year(), date.month(), spec["monthday"].toInt(1));
                if (!requested.isValid())
                    continue;
                next.setDate(requested);
                if (next > now)
                    break;
            }
        } else if (kind == "Weekly") {
            next.setDate(now.date());
            next = next.addDays((spec["weekday"].toInt(1) - now.date().dayOfWeek() + 7) % 7);
            if (next <= now)
                next = next.addDays(7);
        } else {
            const int days = kind == "Days" ? interval : 1;
            next = next.addDays((next.daysTo(now) / days) * days);
            if (next <= now)
                next = next.addDays(days);
        }
    }
    return next.toString("yyyy-MM-dd HH:mm");
}
} // namespace
QWidget *Window::schedulePage() {
    auto page = new QWidget;
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    auto bar = new QHBoxLayout;
    auto refresh = new QPushButton("刷新");
    classic::setFluentIcon(refresh, "Sync");
    bar->addWidget(refresh);
    connect(refresh, &QPushButton::clicked, this, &Window::refreshScheduleTable);
    auto create = new QPushButton("创建任务");
    create->setObjectName("scheduleCreate");
    classic::setFluentIcon(create, "Add");
    bar->addWidget(create);
    connect(create, &QPushButton::clicked, this, [this] {
        editSchedules();
    });
    bar->addStretch();
    layout->addLayout(bar);
    auto title = new QLabel("当前软件的计划任务");
    layout->addWidget(title);
    scheduleTable_ = new QTableWidget;
    scheduleTable_->setObjectName("scheduleTable");
    scheduleTable_->setColumnCount(6);
    scheduleTable_->setHorizontalHeaderLabels({"任务名称", "状态", "触发方式", "下次运行", "启用", "操作"});
    scheduleTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int col = 1; col < 6; ++col)
        scheduleTable_->horizontalHeader()->setSectionResizeMode(col, QHeaderView::ResizeToContents);
    scheduleTable_->verticalHeader()->setVisible(false);
    scheduleTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    scheduleTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    scheduleTable_->setMinimumHeight(110);
    layout->addWidget(scheduleTable_);
    auto note = new QLabel("仅管理当前个人配置的计划。需保持 Windows "
                           "已登录、游戏已打开、双机设备可用。计划不会抢占正在执行的任务。");
    note->setObjectName("caption");
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch();
    refreshScheduleTable();
    return page;
}
void Window::refreshScheduleTable() {
    if (!scheduleTable_)
        return;
    try {
        runtime::Scheduler scheduler(QCoreApplication::applicationFilePath(), root_, data_);
        const auto plans = scheduler.list();
        scheduleTable_->setRowCount(plans.size());
        const QHash<QString, QString> kinds{{"Daily", "每天"},      {"Weekly", "每周"},
                                            {"Monthly", "每月"},    {"Once", "仅一次"},
                                            {"Days", "自定义天数"}, {"Hours", "自定义小时"}};
        for (int i = 0; i < plans.size(); ++i) {
            const auto spec = plans[i].toObject();
            QString name = spec["label"].toString();
            for (auto v : catalog_["tasks"].toArray())
                if (v.toObject()["id"] == spec["task"])
                    name = v.toObject()["name"].toString();
            const auto next = nextRun(spec);
            const QStringList values{name, !spec["enabled"].toBool() ? "已停用"
                                           : next == "已结束" ? "已结束" : "已启用",
                                     kinds.value(spec["kind"].toString(), spec["kind"].toString()),
                                     next};
            for (int col = 0; col < values.size(); ++col)
                scheduleTable_->setItem(i, col, new QTableWidgetItem(values[col]));
            const auto perform = [this](std::function<void(runtime::Scheduler &)> operation) {
                runAsync(
                    [this, operation] {
                        runtime::Scheduler scheduler(QCoreApplication::applicationFilePath(), root_, data_);
                        operation(scheduler);
                        return QJsonObject{};
                    },
                    [this](auto) { refreshScheduleTable(); });
            };
            auto enabled = new classic::Switch;
            enabled->setChecked(spec["enabled"].toBool());
            connect(enabled, &QAbstractButton::toggled, this, [spec, perform](bool on) {
                perform([spec, on](runtime::Scheduler &s) { s.setEnabled(spec["name"].toString(), on); });
            });
            scheduleTable_->setCellWidget(i, 4, enabled);
            auto remove = new QPushButton("删除");
            classic::setFluentIcon(remove, "Delete");
            connect(remove, &QPushButton::clicked, this, [this, spec, perform] {
                if (QMessageBox::question(this, "删除计划任务", "删除选中的定时计划？",
                                          QMessageBox::Yes | QMessageBox::Cancel) == QMessageBox::Yes)
                    perform([spec](runtime::Scheduler &s) { s.remove(spec["name"].toString()); });
            });
            auto actions = new QWidget;
            auto row = new QHBoxLayout(actions);
            row->setContentsMargins(4, 0, 4, 0);
            auto modify = new QPushButton("修改");
            modify->setObjectName("scheduleModify");
            connect(modify, &QPushButton::clicked, this, [this, spec, name] {
                editSchedules(spec["task"].toString(), name, spec);
            });
            row->addWidget(modify);
            row->addWidget(remove);
            scheduleTable_->setCellWidget(i, 5, actions);
            scheduleTable_->setRowHeight(i, 44);
        }
        scheduleTable_->setFixedHeight(std::clamp(42 + int(plans.size()) * 44, 110, 360));
    } catch (const std::exception &e) {
        log(errorText(e));
    }
}
} // namespace okww
#endif
