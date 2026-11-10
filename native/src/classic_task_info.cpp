#include "window.hpp"
namespace okww {
QWidget *Window::buildTaskInfo(bool trigger) {
    const int index = trigger ? 1 : 0;
    auto frame = new QFrame;
    frame->setObjectName("taskRunPanel");
    frame->setProperty("classicCard", true);
    frame->setFixedHeight(108);
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
        const auto mode = python ? QString("Python 工坊") : QString("内置 C++");
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
}
} // namespace okww
