#include "window.hpp"
namespace okww {
QWidget *Window::buildExecutionProgress() {
    auto widget = new QWidget;
    widget->setObjectName("executionProgressPanel");
    auto layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(5);
    auto row = new QHBoxLayout;
    auto stage = new QLabel("运行进度 · 等待开始");
    stage->setObjectName("executionStage");
    stage->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    row->addWidget(stage,1);
    auto count = new QLabel;
    count->setObjectName("executionCount");
    count->setAlignment(Qt::AlignLeft);
    count->setStyleSheet("font-weight: 600; color: #1677d2;");
    layout->addLayout(row);
    layout->addWidget(count);
    auto bar = new QProgressBar;
    bar->setObjectName("executionProgress");
    bar->setRange(0,1000);
    bar->setValue(0);
    bar->setTextVisible(false);
    bar->setFixedHeight(6);
    layout->addWidget(bar);
    auto estimate = new QLabel("完整流程将在任务开始后列出；预计耗时会随本机运行记录调整。");
    estimate->setObjectName("executionEstimate");
    estimate->setProperty("caption", true);
    layout->addWidget(estimate);
    auto steps = new QTreeWidget;
    steps->setObjectName("executionFlow");
    steps->setHeaderLabels({"流程项目", "状态", "预计耗时", "已用时间"});
    steps->setRootIsDecorated(false);
    steps->setUniformRowHeights(true);
    steps->setIndentation(0);
    steps->setSelectionMode(QAbstractItemView::NoSelection);
    steps->setFocusPolicy(Qt::NoFocus);
    steps->setFixedHeight(156);
    steps->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < 4; ++column) steps->header()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    new QTreeWidgetItem(steps, {"等待开始任务", "待机", "—", "—"});
    layout->addWidget(steps);
    taskProgressPanels_.append(widget);
    return widget;
}
QWidget *Window::buildTaskInfo(bool trigger) {
    const int index = trigger ? 1 : 0;
    auto frame = new QFrame;
    frame->setObjectName("taskRunPanel");
    frame->setProperty("classicCard", true);
    frame->setFixedHeight(356);
    auto layout = new QVBoxLayout(frame);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(12);
    auto bar = new QHBoxLayout;
    auto title = new QLabel;
    title->setObjectName("taskInfoTitle");
    title->setProperty("caption", true);
    title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    bar->addWidget(title, 1);
    auto party = new QLabel("当前队伍：等待识别");
    party->setObjectName("taskPartySummary");
    party->setProperty("caption", true);
    party->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    party->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    bar->addWidget(party, 2);
    layout->addLayout(bar);
    auto metrics = new QHBoxLayout;
    metrics->setSpacing(28);
    const QStringList names{"已运行", "视频采集", "键鼠控制"};
    for (int column = 0; column < names.size(); ++column) {
        auto metric = new QVBoxLayout;
        metric->setSpacing(5);
        auto caption = new QLabel(names[column]);
        caption->setObjectName("metricCaption");
        auto value = new QLabel("—");
        value->setObjectName("taskMetricValue");
        value->setAccessibleName(names[column]);
        metric->addWidget(caption);
        metric->addWidget(value);
        metrics->addLayout(metric, 1);
        taskInfoValues_[index][column] = value;
    }
    layout->addLayout(metrics);
    layout->addWidget(buildExecutionProgress());
    taskInfoPanels_[index] = frame;
    return frame;
}
void Window::refreshTaskInfo() {
    const auto snapshot = backend_.snapshot();
    if (automation_ && taskRunClock_.isValid())
        taskInfoElapsedSeconds_ = taskRunClock_.elapsed() / 1000;
    const auto taskId = pendingStart_ ? pendingStart_->task : currentTask_;
    QString name = "实时触发";
    for (auto value : catalog_["tasks"].toArray())
        if (value.toObject()["id"] == taskId)
            name = value.toObject()["name"].toString();
    if (auto card = taskCards_.value(taskId))
        if (auto title = card->findChild<QLabel *>("cardTitle")) name = title->text();
    const auto state = stopRequested_ || stoppingDevices_ ? QString("正在停止 · 等待设备断开")
        : pendingStart_ ? "正在准备 · " + name
        : !executionError_.isEmpty() ? QString("异常已暂停 · 可重试任务")
        : automation_ ? "正在运行 · " + name
        : devicesRunning_ && taskStopped_ ? QString("已停止 · 设备保持连接")
        : devicesRunning_ && taskRunClock_.isValid() ? QString("已暂停 · 设备保持连接")
        : devicesRunning_ ? QString("待机 · 设备已连接")
        : taskRunClock_.isValid() ? QString("已停止 · 可重新开始任务")
        : QString("待机 · 等待开始任务");
    for (int index = 0; index < 2; ++index) {
        if (!taskInfoPanels_[index]) continue;
        auto title = taskInfoPanels_[index]->findChild<QLabel *>("taskInfoTitle");
        title->setText(state);
        title->setToolTip(state);
        QStringList members;
        for (auto value : combatInfo_["members"].toArray()) {
            const auto member = value.toObject();
            auto name = member["class"].toString();
            name = name.isEmpty() || name == "BaseChar" ? QString("未识别") : characterName(name);
            if (member["current"].toBool()) name += "（当前）";
            members.append(name);
        }
        const bool python = combatInfo_["mode"] == "python";
        const auto mode = python ? QString("Python 工坊") : QString("内置 Rust");
        const auto summary = members.isEmpty() ? combatInfo_["code_state"] == "pending"
                ? QString("队伍配置已更新 · 下次启动重新识别") : QString("当前队伍：等待识别")
            : (automation_ && combatInfo_["verified"].toBool(true) ? QString("当前队伍：") : QString("上次识别：")) + members.join(" / ") + " · " + mode +
              (combatInfo_["code_state"] == "loading" ? "（加载中）" : "");
        auto party = taskInfoPanels_[index]->findChild<QLabel *>("taskPartySummary");
        party->setProperty("fullTeamText", summary);
        party->setText(party->fontMetrics().elidedText(summary, Qt::ElideRight, std::max(0, party->width())));
        auto tooltip = summary;
        if (python && !combatInfo_["bridge"].toObject().isEmpty()) {
            const auto metrics = combatInfo_["bridge"].toObject();
            tooltip += QString("\n工坊启动 %1 ms · 空请求往返中位数 %2 ms\n最近图像复制 %3 ms · 图像请求 %4 / 实际复制 %5"
                               "\n这些为软件内部指标，不包含采集卡、盒子与游戏的端到端延迟。")
                .arg(metrics["startup_ms"].toDouble(), 0, 'f', 1).arg(metrics["ping_ms"].toDouble(), 0, 'f', 2)
                .arg(metrics["last_copy_ms"].toDouble(), 0, 'f', 3)
                .arg(metrics["frame_requests"].toDouble(), 0, 'f', 0).arg(metrics["frame_copies"].toDouble(), 0, 'f', 0);
            tooltip += QString("\n空请求 P95 %1 ms（%2 次）\n取帧至输入请求：中位数 %3 / P95 %4 ms（%5 次，含连招等待）")
                .arg(metrics["ping_p95_ms"].toDouble(), 0, 'f', 2).arg(metrics["ping_samples"].toInt())
                .arg(metrics["frame_input_p50_ms"].toDouble(), 0, 'f', 2)
                .arg(metrics["frame_input_p95_ms"].toDouble(), 0, 'f', 2).arg(metrics["frame_input_samples"].toInt());
        }
        party->setToolTip(tooltip);
        const qint64 seconds = taskInfoElapsedSeconds_;
        taskInfoValues_[index][0]->setText(QString("%1:%2:%3")
            .arg(seconds / 3600, 2, 10, QChar('0')).arg(seconds / 60 % 60, 2, 10, QChar('0'))
            .arg(seconds % 60, 2, 10, QChar('0')));
        taskInfoValues_[index][1]->setText(snapshot["capture_connected"].toBool() ? "已连接" : "未连接");
        taskInfoValues_[index][2]->setText(snapshot["dry_run"].toBool() ? "离线测试"
            : snapshot["box"].toObject()["connected"].toBool() ? "已连接" : "未连接");
    }
    for (auto panel : taskProgressPanels_) {
        if (!panel) continue;
        auto stage = panel->findChild<QLabel *>("executionStage");
        auto count = panel->findChild<QLabel *>("executionCount");
        auto bar = panel->findChild<QProgressBar *>("executionProgress");
        auto title = taskProgress_["stage"].toString();
        if (!executionError_.isEmpty()) title = "异常已暂停 · " + title;
        if (title.isEmpty()) title = automation_ ? "执行中" : "等待开始";
        const auto detail = taskProgress_["detail"].toString();
        const auto full = "运行进度 · " + title + (detail.isEmpty() ? "" : " · " + detail);
        stage->setText(stage->fontMetrics().elidedText(full,Qt::ElideRight,std::max(0,stage->width())));
        stage->setToolTip(full);
        const auto current = taskProgress_["current"].toDouble(-1), total = taskProgress_["total"].toDouble(-1);
        const bool known = std::isfinite(current) && std::isfinite(total) && current >= 0 && total > 0;
        const auto unit = taskProgress_["unit"].toString();
        const auto flow = taskProgress_["flow"].toArray();
        auto tree = panel->findChild<QTreeWidget *>("executionFlow");
        auto estimate = panel->findChild<QLabel *>("executionEstimate");
        const auto duration = [](double seconds) {
            if (!std::isfinite(seconds) || seconds < 0) return QString("待估算");
            if (seconds < 60) return QString("%1 秒").arg(std::max(1, int(std::ceil(seconds))));
            if (seconds < 3600) return QString("%1 分钟").arg(int(std::ceil(seconds / 60)));
            return QString("%1 小时 %2 分").arg(int(seconds / 3600)).arg(int(seconds / 60) % 60);
        };
        if (!flow.isEmpty()) {
            if (known) stage->setText(stage->fontMetrics().elidedText(full + QString(" · %1/%2 %3")
                .arg(current,0,'g',8).arg(total,0,'g',8).arg(unit),Qt::ElideRight,std::max(0,stage->width())));
            while (tree->topLevelItemCount() > flow.size()) delete tree->takeTopLevelItem(tree->topLevelItemCount()-1);
            int completed = 0, skipped = 0, failed = 0;
            double remaining = 0;
            bool unknown = false, overrun = false;
            const double drift = automation_ && !taskProgress_["finished"].toBool()
                ? std::max(0., (QDateTime::currentMSecsSinceEpoch() - taskProgress_["flow_observed_at"].toDouble()) / 1000.) : 0;
            static const QMap<QString, QString> labels{{"pending","待执行"},{"running","执行中"},
                {"completed","已完成"},{"skipped","已跳过"},{"failed","未完成"}};
            for (int i = 0; i < flow.size(); ++i) {
                const auto step = flow[i].toObject();
                const auto state = step["state"].toString();
                completed += state == "completed";
                skipped += state == "skipped";
                failed += state == "failed";
                const auto expected = step["estimate_seconds"].toDouble();
                const auto elapsed = step["elapsed_seconds"].toDouble() + (state == "running" ? drift : 0);
                if (state == "pending" || state == "running" || state == "failed") {
                    unknown |= expected <= 0 || state == "failed";
                    overrun |= state == "running" && expected > 0 && elapsed >= expected;
                    remaining += std::max(0., expected - (state == "running" ? elapsed : 0));
                }
                auto item = i < tree->topLevelItemCount() ? tree->topLevelItem(i) : new QTreeWidgetItem(tree);
                item->setText(0, QString("%1. %2").arg(i+1).arg(step["name"].toString()));
                item->setToolTip(0, step["detail"].toString());
                item->setText(1, state == "running" && !automation_ ? "已暂停" : labels.value(state, "待执行"));
                item->setText(2, state == "skipped" ? "—" : expected > 0 ? "约 " + duration(expected) : "待估算");
                item->setToolTip(2, step["estimate_source"] == "history" ? "根据本机已完成项目的最近运行记录估算" : "参考耗时；战斗、加载及重试可能延长时间");
                item->setText(3, state == "pending" || state == "skipped" ? "—" : duration(elapsed));
                const auto color = state == "running" ? QColor("#1677d2") : state == "failed" ? QColor("#bc3b37") : palette().text().color();
                for (int column = 0; column < 4; ++column) item->setForeground(column, color);
            }
            const int settled = completed + skipped, left = flow.size() - settled;
            const int percent = settled * 100 / flow.size();
            count->setText(QString("已完成 %1 · 剩余 %2 / 共 %3 项 · %4%")
                           .arg(completed).arg(left).arg(flow.size()).arg(percent));
            if (skipped) count->setText(count->text() + QString(" · 跳过 %1").arg(skipped));
            if (failed) count->setText(count->text() + QString(" · 未完成 %1").arg(failed));
            count->setToolTip("总进度按完成或无需执行的项目统计；失败项目不会计为完成。");
            bar->setRange(0, 1000);
            bar->setValue(settled * 1000 / flow.size());
            estimate->setText(left == 0 ? "全流程已结束" : !executionError_.isEmpty() ? "异常已暂停 · 剩余时间将在重试后重新估算"
                : unknown ? "剩余时间待估算 · 部分项目尚无运行记录"
                : overrun ? "当前项目已超过预计耗时，剩余时间待更新"
                : QString("预计还需约 %1 · 按参考值或本机历史记录估算").arg(duration(remaining)));
            continue;
        }
        while (tree->topLevelItemCount() > 1) delete tree->takeTopLevelItem(tree->topLevelItemCount()-1);
        auto item = tree->topLevelItemCount() ? tree->topLevelItem(0) : new QTreeWidgetItem(tree);
        item->setText(0,title);
        item->setText(1,automation_ ? "执行中" : "待机");
        item->setText(2,"待估算");
        item->setText(3,"—");
        estimate->setText("未提供完整流程的脚本显示当前步骤；持续运行的任务不显示完成时间。");
        count->setText(known ? QString("%1 / %2 %3").arg(current,0,'g',8).arg(total,0,'g',8).arg(unit)
            : automation_ ? "正在执行" : taskProgress_.isEmpty() ? "" : "已暂停 / 停止");
        bar->setRange(0,automation_ && !known ? 0 : 1000);
        if (known) bar->setValue(int(std::clamp(current/total,0.,1.)*1000));
        else if (!automation_) bar->setValue(0);
    }
}
} // namespace okww
