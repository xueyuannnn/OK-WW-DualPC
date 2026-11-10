#include "garden.hpp"
namespace okww::runtime {
std::optional<QPair<int, int>> Garden::turn() {
    QString text;
    for (const auto &line : c_.text(.025, .085, .23, .185, .75))
        text += line.text + ' ';
    const auto stage = QRegularExpression("(?:[阶階]段|stage)\\s*[:：]?\\s*(\\d)\\s*[/／]\\s*7",
                                          QRegularExpression::CaseInsensitiveOption)
                           .match(text);
    const auto day =
        QRegularExpression("第\\s*(\\d+)\\s*天|day\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption)
            .match(text);
    if (!stage.hasMatch() || !day.hasMatch())
        return {};
    return QPair(stage.captured(1).toInt(),
                 (day.captured(1).isEmpty() ? day.captured(2) : day.captured(1)).toInt());
}
std::pair<std::optional<TextLine>, std::optional<int>> Garden::blessingControls() {
    const auto boxes = c_.text(.035, .70, .23, .815, .75);
    auto button = matchingText(boxes, "^(?:使用祝福|use blessing)$");
    QString text;
    for (const auto &line : boxes)
        text += line.text + ' ';
    const auto uses = QRegularExpression("(?:剩[余餘]次[数數]|remaining\\s+uses?)\\s*[:：]?\\s*(\\d+)",
                                         QRegularExpression::CaseInsensitiveOption)
                          .match(text);
    return {button, uses.hasMatch() ? std::optional(uses.captured(1).toInt()) : std::nullopt};
}
bool Garden::useBlessing(bool open) {
    const auto current = turn();
    if (!open) {
        if (!current || checkedTurn_ == current || attempts_[*current] >= 3)
            return false;
        ++attempts_[*current];
        c_.clickRelative(.18, .785, .3);
        c_.refresh();
    }
    const auto [button, uses] = blessingControls();
    if (button && uses && *uses > 0) {
        c_.click(*button, .3);
        c_.refresh();
        const auto [after, remaining] = blessingControls();
        if (!after || (remaining && *remaining < *uses)) {
            checkedTurn_ = current;
            if (c_.info)
                c_.info("周常技能",
                        current
                            ? QString("第 %1 阶段，第 %2 天已使用").arg(current->first).arg(current->second)
                            : "已使用");
        }
    } else {
        if (uses && *uses == 0) {
            checkedTurn_ = current;
            if (c_.info)
                c_.info("周常技能", "本回合暂无次数，下一回合重新检查");
        }
        c_.clickRelative(.5, .8, .2);
    }
    blessingOpen_ = false;
    return true;
}
bool Garden::maximizeSpeed() {
    if (c_.execution.now() - speedChecked_ < 30)
        return false;
    const QRegularExpression speed("^(?:[x×*]\\s*\\d{1,2}(?:\\.\\d{1,2})?|\\d{1,2}\\.\\d{1,2})$",
                                   QRegularExpression::CaseInsensitiveOption);
    for (const auto &line : c_.text(.625, .025, .715, .09, .75)) {
        const auto text = QString(line.text).trimmed().remove(' ');
        if (text.contains("MAX", Qt::CaseInsensitive) || text == "最大" || text == "最快") {
            speedChecked_ = c_.execution.now();
            speedAttempts_ = 0;
            if (c_.info)
                c_.info("周常倍速", "MAX");
            return false;
        }
        if (speed.match(text).hasMatch()) {
            if (speedAttempts_ >= 6) {
                speedChecked_ = c_.execution.now();
                speedAttempts_ = 0;
                return false;
            }
            c_.click(line, .3);
            ++speedAttempts_;
            return true;
        }
    }
    return false;
}
std::optional<Match> Garden::bestFeature() {
    if (!initialBlessingChecked_) {
        initialBlessingChecked_ = true;
        if (blessingControls().first)
            blessingOpen_ = true;
    }
    if (blessingOpen_) {
        useBlessing(true);
        return {};
    }
    if (auto modern = rewardChoice()) {
        if (modern->name.startsWith("garden_reward_choice_")) {
            c_.sleep(.3);
            c_.refresh();
            return rewardChoice();
        }
        return modern;
    }
    QVector<Match> matches;
    for (const auto &name : c_.templates.names()) {
        if (!name.startsWith("garden_") || name == "garden_get_confirm_gray" ||
            name == "garden_not_interested_confirm")
            continue;
        auto options = MatchOptions{};
        options.limit = 0;
        if (name == "garden_not_interested")
            options.vertical = .4;
        matches += c_.templates.find(c_.frame, name, options);
    }
    std::optional<Match> target;
    for (const auto &priority : {"garden_get_skip", "garden_not_interested_confirm"}) {
        for (const auto &item : matches)
            if (item.name == priority && (!target || item.confidence > target->confidence))
                target = item;
        if (target)
            break;
    }
    if (!target)
        for (const auto &item : matches)
            if (!target || item.confidence > target->confidence)
                target = item;
    if (target && (target->name == "garden_shop_close" || target->name == "garden_shop_buy")) {
        shop();
        return {};
    }
    if (target && target->name == "garden_next_day" && (maximizeSpeed() || useBlessing()))
        return {};
    return target;
}
bool Garden::active() {
    c_.refresh();
    if (c_.findAny({"garden_next_day", "garden_new_stage", "garden_get_skip", "garden_shop_close"},
                   {.threshold = .85}))
        return true;
    if (rewardChoice())
        return true;
    if (blessingControls().first) {
        blessingOpen_ = true;
        return true;
    }
    QString text;
    for (const auto &line : c_.text(0, 0, .3, .2))
        text += line.text + ' ';
    return QRegularExpression("本[周週][游遊][园園]").match(text).hasMatch() &&
           QRegularExpression("[阶階]段\\s*[:：]?\\s*\\d\\s*/\\s*7").match(text).hasMatch();
}
void Garden::loop() {
    checkedTurn_.reset();
    attempts_.clear();
    initialBlessingChecked_ = false;
    speedChecked_ = -1e12;
    const double deadline = c_.execution.now() + 3600;
    double lastAction = c_.execution.now();
    for (;;) {
        // All paths, including repeated reward/skip dialogs, must respect the
        // round deadline even when a click does not advance the screen.
        if (c_.execution.now() > deadline || c_.execution.now() - lastAction > 180)
            throw NavigationFailure("周常当前步骤长时间未推进，保留现场等待恢复");
        c_.sleep(.1);
        const auto target = bestFeature();
        c_.sleep(.2);
        if (target) {
            lastAction = c_.execution.now();
            if (c_.info)
                c_.info("周常步骤", target->name);
            if (target->name == "garden_get_skip") {
                c_.sleep(1);
                if (auto gold = c_.find("garden_get_gold", {.horizontal = .9}))
                    c_.click(*gold, 1);
                else if (auto purple = c_.find("garden_get_purple", {.horizontal = .9}))
                    c_.click(*purple, 1);
                else
                    c_.clickRelative(.5, .2, 1);
                c_.click(Match{"garden_get_confirm_gray",
                               c_.templates.box("garden_get_confirm_gray", {c_.frame.cols, c_.frame.rows}),
                               1},
                         1);
                continue;
            }
            if (target->name == "garden_not_interested") {
                const auto items =
                    c_.templates.find(c_.frame, "garden_not_interested", {.vertical = .4, .limit = 0});
                if (!items.empty())
                    c_.click(items.back(), 1);
                c_.click(
                    Match{"garden_not_interested_confirm",
                          c_.templates.box("garden_not_interested_confirm", {c_.frame.cols, c_.frame.rows}),
                          1},
                    1);
                continue;
            }
            if (target->name == "garden_start_game") {
                c_.clickRelative(965. / 1920, 860. / 1080, 2);
                c_.clickRelative(700. / 1920, 666. / 1080, 2);
                c_.clickRelative(1600. / 1920, 900. / 1080, 2);
            }
            c_.click(*target, 1);
        } else {
            const auto restart = c_.find("a_garden_restart"), back = c_.find("a_garden_back");
            if (restart && back) {
                c_.sleep(2);
                c_.click(*back, 1);
                if (c_.wait([&] { return bool(c_.find("garden_start_game")); }, 5))
                    c_.key("esc", 1);
                return;
            }
            c_.sleep(.2);
        }
    }
}
void Garden::openWeekly() {
    navigation_.openBook("gray_book_quest");
    c_.sleep(1);
    c_.clickRelative(.343, .129, 1);
    c_.clickRelative(.927, .893, 3);
    c_.clickRelative(.927, .893, 2);
}
void Garden::run() {
    TaskScope scope(c_, "GardenTask", config_);
    const auto runLoop = [&] {
        const int retries =
            c_.profile.value("Task Recovery", "Resume Interrupted Tasks", true).toBool()
                ? std::clamp(c_.profile.value("Task Recovery", "Recovery Retry Limit", 3).toInt(), 1, 10)
                : 0;
        for (int attempt = 0;; ++attempt)
            try {
                loop();
                return;
            } catch (const NavigationFailure &) {
                if (attempt >= retries)
                    throw;
                if (c_.info)
                    c_.info("周常恢复", QString("保留当前阶段重试 %1/%2").arg(attempt + 1).arg(retries));
                c_.sleep(.5);
            }
    };
    if (active())
        runLoop();
    navigation_.ensureMain();
    for (int round = 0; round < 50; ++round) {
        openWeekly();
        std::optional<int> progress;
        c_.wait(
            [&] {
                progress = weeklyProgress();
                return bool(progress);
            },
            5);
        if (!progress)
            throw NavigationFailure("无法确认周任务进度，不自动重开乐园");
        if (c_.info)
            c_.info("本周游历值", QString("%1/6000").arg(*progress));
        if (*progress >= 6000)
            return;
        c_.clickRelative(.246, .486, 1);
        runLoop();
    }
    throw NavigationFailure("周常达到单次运行上限，仍未确认本周游历值已满");
}
} // namespace okww::runtime
