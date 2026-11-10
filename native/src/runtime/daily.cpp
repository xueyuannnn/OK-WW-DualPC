#include "daily.hpp"
namespace okww::runtime {
namespace {
const QString additions = "Additional Tasks to Run After Daily Task";
const QString weekly = "Check Weekly Garden", allNests = "Auto Farm all Nightmare Nest",
              merge = "Merge Echo If discarded > 1000", echo = "Teleport and Farm 4C Echo",
              materials = "4C Echo and Advanced Skill Material";
QString countKey(DomainKind kind) {
    return kind == DomainKind::Tacet     ? "Tacet Suppression Count"
           : kind == DomainKind::Forgery ? "Forgery Challenge Count"
                                         : "Simulation Challenge Count";
}
} // namespace
DailyProgress dailyProgress(const QVector<TextLine> &objectives, const QVector<TextLine> &points) {
    DailyProgress result;
    for (const auto &line : objectives) {
        const auto match = QRegularExpression("^(\\d+)\\s*[/／]\\s*180$").match(line.text.trimmed());
        if (!match.hasMatch())
            continue;
        bool ok = false;
        const int value = match.captured(1).toInt(&ok);
        if (ok && value <= 180) {
            result.stamina = value;
            break;
        }
    }
    for (const auto &line : points) {
        // The daily points label is a single number, not an unrelated 100/100 counter.
        const auto text = line.text.trimmed();
        if (!QRegularExpression("^\\d{1,3}$").match(text).hasMatch())
            continue;
        const int value = text.toInt();
        if (value <= 180) {
            result.points = value;
            break;
        }
    }
    return result;
}
DomainKind dailyDomain(const QJsonObject &config) {
    const auto preferred = config["Run Tacet or Forgery First"] == "Forgery First"
                               ? "Forgery Challenge"
                               : config["Which to Farm"].toString("Tacet Suppression");
    return preferred == "Forgery Challenge"      ? DomainKind::Forgery
           : preferred == "Simulation Challenge" ? DomainKind::Simulation
                                                 : DomainKind::Tacet;
}
void Daily::status(const QString &step) {
    if (c_.info)
        c_.info("当前任务", step);
}
bool Daily::stage(const QString &name, const std::function<void()> &run, bool retryNavigation) {
    status(name);
    for (int attempt = 0; attempt < retries_; ++attempt) {
        QString error;
        bool navigational = false;
        try {
            run();
            return true;
        } catch (const Cancelled &) {
            throw;
        } catch (const NavigationFailure &e) {
            error = errorText(e);
            navigational = true;
        } catch (const std::exception &e) {
            error = errorText(e);
        }
        if (c_.info)
            c_.info(name, error);
        // Material/result uncertainty is never retried as an entire daily stage.
        if (!navigational || !retryNavigation || attempt + 1 >= retries_) {
            failed_.append(name + "：" + error);
            if (c_.notify)
                c_.notify("每日任务步骤未完成", name + "：" + error, true);
            navigation_.ensureMain(180);
            return false;
        }
        navigation_.ensureMain(180);
    }
    return false;
}
DailyProgress Daily::open() {
    navigation_.openBook("gray_book_quest");
    c_.clickRelative(.17, .12, 1);
    auto objectives = c_.text(.1, .1, .5, .75, .75);
    if (!dailyProgress(objectives, {}).stamina) {
        c_.clickRelative(.974, .6, 1);
        objectives = c_.text(.1, .1, .5, .75, .75);
    }
    const auto result = dailyProgress(objectives, c_.text(.19, .8, .30, .93));
    if (c_.info) {
        c_.info("每日活跃度", QString::number(result.points));
        c_.info("每日体力进度", result.stamina ? QString("%1/180").arg(*result.stamina) : "未识别");
    }
    return result;
}
void Daily::completeStamina() {
    if (!config_["Use Stamina Cubes for Daily"].toBool() || noProgress_)
        return;
    std::unique_ptr<CubeBudget> budget;
    try {
        budget = std::make_unique<CubeBudget>(data_ + "/configs-dualpc/DailyStaminaUsage.json",
                                              config_["Daily Stamina Cube Limit"].toInt(4));
    } catch (const std::exception &e) {
        noProgress_ = true;
        if (c_.info)
            c_.info("体力魔方记录待核对", errorText(e));
        if (c_.notify)
            c_.notify("体力魔方记录待核对", errorText(e), true);
        return;
    }
    if (c_.info)
        c_.info("已用体力魔方", QString("%1/%2").arg(budget->used()).arg(budget->limit()));
    const auto kind = dailyDomain(config_);
    const int cost = domainCost(kind);
    for (int i = 0; i < 6; ++i) {
        const auto before = open();
        if (before.ready() || !before.stamina || *before.stamina >= 180) {
            navigation_.ensureMain();
            return;
        }
        navigation_.ensureMain();
        navigation_.openBook("gray_book_boss");
        auto stamina = readStamina(c_);
        if (stamina.current < 0 || stamina.reserve < 0 || stamina.total < 0)
            fail("无法确认体力，不使用魔方");
        while (stamina.total < cost) {
            if (!budget->remaining() || !useOneDailyCube(c_, *budget)) {
                navigation_.ensureMain();
                return;
            }
            stamina = readStamina(c_);
            if (stamina.total < 0)
                fail("补充后无法确认体力，不继续使用魔方");
        }
        navigation_.ensureMain();
        DomainFarm(c_, combat_).farm(kind, cost, true, config_);
        const auto after = open();
        if (after.ready()) {
            navigation_.ensureMain();
            return;
        }
        if (!after.stamina || *after.stamina <= *before.stamina) {
            noProgress_ = true;
            if (c_.info)
                c_.info("每日体力进度", "补做后未确认进度增加，停止进一步消耗");
            if (c_.notify)
                c_.notify("每日体力进度待检查", "补做后未确认进度增加，停止进一步消耗", true);
            navigation_.ensureMain();
            return;
        }
    }
    navigation_.ensureMain();
}
void Daily::farmCount(DomainKind kind) {
    const auto n = config_[countKey(kind)];
    if (!n.isDouble() || n.toDouble() != std::floor(n.toDouble()) || n.toDouble() < 0 ||
        n.toDouble() > 100000)
        fail("副本次数无效");
    const int requested = n.toInt();
    if (!requested)
        return;
    navigation_.ensureMain();
    navigation_.openBook("gray_book_boss");
    int remaining = domainCountLimit(requested, readStamina(c_).total, domainCost(kind)) * domainCost(kind);
    navigation_.ensureMain();
    DomainFarm farm(c_, combat_);
    farm.onStaminaSpent = [&](int used) { remaining = std::max(0, remaining - used); };
    stage(countKey(kind), [&] {
        if (remaining)
            farm.farm(kind, remaining, true, config_);
    });
}
void Daily::claim() {
    completeStamina();
    navigation_.openBook("gray_book_quest");
    if (!c_.find("boss_proceed", {.search = gameBox(c_.frame, .803, .189, .960, .312)}))
        c_.clickRelative(.885, .250, 2);
    c_.clickRelative(.930, .882, 1);
    navigation_.ensureMain(10);
    const auto progress = open();
    navigation_.ensureMain();
    if (!progress.ready())
        failed_.append("每日活跃度尚未确认达到 100，请检查剩余目标");
}
void Daily::mail() {
    navigation_.ensureMain();
    c_.key("esc", 1.5);
    c_.clickRelative(.64, .95, 1);
    c_.clickRelative(.14, .9, 1);
    navigation_.ensureMain(10);
}
void Daily::battlePass() {
    c_.input.keyDown("alt");
    try {
        c_.sleep(.05);
        c_.clickRelative(.86, .05);
    } catch (...) {
        c_.input.keyUp("alt");
        throw;
    }
    c_.input.keyUp("alt");
    c_.sleep(1);
    if (c_.wait([&] { return bool(matchingText(c_.text(.2, .13, .32, .22), "\\d+")); }, 5)) {
        const auto click = [&] { c_.clickRelative(.68, .91, 2, true); };
        c_.clickRelative(.04, .3, 1);
        click();
        c_.clickRelative(.04, .17, 2);
        click();
        c_.wait(
            [&] {
                if (matchingText(c_.text(.2, .13, .32, .22), "\\d+"))
                    return true;
                click();
                return false;
            },
            5);
    } else if (c_.info)
        c_.info("先约电台", "当前未识别到可领取页面，可能已结束");
    navigation_.ensureMain();
}
void Daily::validate() {
    const auto selected = config_[additions].toArray();
    for (const auto &pair :
         {qMakePair(echo, QString("FarmEchoTask")), qMakePair(materials, QString("FarmEchoTask2"))})
        if (selected.contains(pair.first) && c_.profile.value(pair.second, "Teleport to Boss", "No") == "No")
            fail("每日附加刷声骸任务需要先配置传送目标");
    if (selected.contains(allNests) &&
        c_.profile.value("NightmareNestTask", "Which to Farm").toArray().empty())
        fail("每日附加巢穴任务需要先选择刷取范围");
}
void Daily::run(bool enhanced) {
    TaskScope scope(c_, enhanced ? "DailyTask2" : "DailyTask");
    config_ = c_.profile.group(c_.task);
    noProgress_ = false;
    failed_.clear();
    retries_ = enhanced ? std::clamp(config_["Task Retry"].toInt(5), 1, 10) : 1;
    validate();
    c_.state->loggedIn = false;
    navigation_.ensureMain(180);
    const auto selected = config_[additions].toArray();
    if (enhanced) {
        if (selected.contains(materials))
            stage("高级技能材料", [&] { EchoFarm(c_, combat_, detector_).run(true); }, false);
        if (config_["Run Tacet or Forgery First"] == "Forgery First") {
            farmCount(DomainKind::Forgery);
            farmCount(DomainKind::Tacet);
        } else {
            farmCount(DomainKind::Tacet);
            farmCount(DomainKind::Forgery);
        }
        farmCount(DomainKind::Simulation);
    } else {
        const auto progress = open();
        if (!progress.ready() && progress.stamina.value_or(0) < 180)
            stage(
                "清体力",
                [&] {
                    DomainFarm(c_, combat_)
                        .farm(dailyDomain(config_), 180 - progress.stamina.value_or(0), false, config_);
                },
                false);
    }
    stage("补做体力目标", [&] { completeStamina(); }, false);
    const auto progress = open();
    const bool all = selected.contains(allNests);
    if (all || (config_["Farm Nightmare Nest for Daily Echo"].toBool(true) && !progress.ready() &&
                (enhanced || dailyDomain(config_) != DomainKind::Tacet)))
        stage("梦魇巢穴", [&] { NightmareFarm(c_, combat_, detector_).run(!all); });
    stage("每日奖励", [&] { claim(); }, false);
    stage("邮件", [&] { mail(); });
    stage("先约电台", [&] { battlePass(); });
    if (selected.contains(weekly))
        stage("周常乐园", [&] { Garden(c_, root_).run(); });
    if (selected.contains(merge))
        stage("弃置声骸合成", [&] { EchoOperations(c_, data_ + "/screenshots").merge(); }, false);
    if (selected.contains(echo))
        stage("4C 声骸", [&] { EchoFarm(c_, combat_, detector_).run(); }, false);
    if (!failed_.empty())
        fail("每日流程已结束，以下项目未完成：\n" + failed_.join('\n'));
    status("每日任务已完成");
}
} // namespace okww::runtime
