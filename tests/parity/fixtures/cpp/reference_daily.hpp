#pragma once
// Frozen pre-migration daily rules; test-only parity oracle.
#include "runtime/daily.hpp"
#include "runtime/diagnostics.hpp"
namespace okww::runtime::reference_daily {

struct DailyProgress {
    std::optional<int> stamina;
    int points = -1;
    std::optional<bool> echoNeeded;
    bool ready() const { return points >= 100; }
};
DailyProgress reference_dailyProgress(const QVector<TextLine> &objectives, const QVector<TextLine> &points);
DailyProgress reference_observeDailyProgress(TaskContext &context, QJsonObject *observation = nullptr);
DomainKind reference_dailyDomain(const QJsonObject &config);
int reference_dailyStaminaBatch(int progress, int cost, int available, bool preferDouble);
class Daily {
  public:
    TaskContext &c_;
    CombatSession &combat_;
    EchoDetector &detector_;
    Navigation navigation_;
    QString root_, data_;
    QJsonObject config_;
    bool noProgress_ = false;
    int retries_ = 1;
    QStringList failed_;
    QMap<QString,int> dailySpent_;
    int claimedAtPoints_ = -1;
    int extrasDone_ = 0, extrasTotal_ = 0;
    QJsonObject dailyObservation_;
    void status(const QString &step);
    bool stage(const QString &name, const std::function<void()> &run, bool retryNavigation = true);
    DailyProgress open(bool collectObjectives = false);
    [[noreturn]] void observationFailure(const QString &reason);
    void completeStamina();
    void farmCount(DomainKind kind);
    DailyProgress claim();
    void mail();
    void battlePass();
    void validate();

  public:
    Daily(TaskContext &context, CombatSession &combat, EchoDetector &detector, QString root, QString data)
        : c_(context), combat_(combat), detector_(detector), navigation_(context), root_(std::move(root)),
          data_(std::move(data)) {}
    void run(bool enhanced = false);
};

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
DailyProgress reference_dailyProgress(const QVector<TextLine> &objectives, const QVector<TextLine> &points) {
    DailyProgress result;
    const QRegularExpression echoObjective("(吸收|获得|獲得|获取|獲取).{0,20}(声骸|聲骸)|(Absorb|Obtain|Collect).{0,30}Echo",
                                           QRegularExpression::CaseInsensitiveOption);
    for (const auto &line : objectives) {
        if (!echoObjective.match(line.text).hasMatch()) continue;
        bool needed = true;
        for (const auto &candidate : objectives) {
            if (&candidate != &line && std::abs(candidate.center().y()-line.center().y()) >
                std::max(line.box.height(),candidate.box.height())*1.1) continue;
            if (QRegularExpression("已完成|已领取|已領取|Completed|Claimed",QRegularExpression::CaseInsensitiveOption).match(candidate.text).hasMatch()) needed=false;
            const auto match=QRegularExpression("(\\d+)\\s*[/／]\\s*(\\d+)").match(candidate.text);
            if (match.hasMatch() && match.captured(2).toInt()>0 && match.captured(2).toInt()<=20)
                needed = match.captured(1).toInt()<match.captured(2).toInt();
        }
        result.echoNeeded=needed;
        break;
    }
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
    QSet<int> activityValues;
    for (const auto &line : points) {
        // The daily points label is a single number, not an unrelated 100/100 counter.
        const auto text = line.text.trimmed();
        if (!QRegularExpression("^\\d{1,3}$").match(text).hasMatch())
            continue;
        activityValues.insert(text.toInt());
    }
    if (activityValues.size()==1) result.points=*activityValues.cbegin();
    return result;
}
DailyProgress reference_observeDailyProgress(TaskContext &c, QJsonObject *observation) {
    const auto heading = c.text(.09, .08, .65, .18, .75);
    const auto objectives = c.text(.1, .18, .96, .79, .75);
    // The narrow counter crop misses an isolated yellow 0 in PP-OCR. Detect
    // across the reward strip, then accept only the earned counter at its left.
    const auto strip = c.text(.05, .78, .98, .97, .75);
    const auto counter = gameBox(c.frame, .18, .81, .30, .90);
    const auto labelArea = gameBox(c.frame, .17, .88, .34, .96);
    const QString dailyLabel = "^(?:活[跃躍]度|Daily\\s*Activity|Activity(?:\\s*Points?)?)$";
    QVector<TextLine> points, labels;
    QJsonArray headingLines, objectiveLines, stripLines;
    for (const auto &line : heading) headingLines.append(line.json());
    for (const auto &line : objectives) objectiveLines.append(line.json());
    for (const auto &line : strip) {
        stripLines.append(line.json());
        if (counter.contains(line.center().toPoint())) points.append(line);
        if (labelArea.contains(line.center().toPoint())) labels.append(line);
    }
    // Both daily and weekly tab headings are visible on either page. The
    // counter's own label must agree before a number or a claim is trusted.
    const bool confirmed = bool(matchingText(heading, dailyLabel)) &&
                           bool(matchingText(labels, dailyLabel));
    auto result = reference_dailyProgress(objectives, confirmed ? points : QVector<TextLine>{});
    if (observation)
        *observation = {{"page_confirmed", confirmed}, {"points", result.points},
                        {"heading_lines", headingLines}, {"objective_lines", objectiveLines},
                        {"reward_strip_lines", stripLines}};
    return result;
}
DomainKind reference_dailyDomain(const QJsonObject &config) {
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
    c_.workflowStep(name, "running");
    status(name);
    for (int attempt = 0; attempt < retries_; ++attempt) {
        QString error;
        bool navigational = false;
        try {
            run();
            c_.workflowStep(name, "completed");
            return true;
        } catch (const Cancelled &) {
            throw;
        } catch (const ExecutionFailure &) {
            c_.workflowStep(name, "failed", "输入或执行通道异常，已停止后续操作");
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
            c_.workflowStep(name, "failed", error);
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
void Daily::observationFailure(const QString &reason) {
    if (c_.diagnostic) c_.diagnostic(reason, {{"daily_points", dailyObservation_.value("points").toInt(-1)},
                                            {"daily_observation", dailyObservation_}});
    fail(reason);
}
DailyProgress Daily::open(bool collectObjectives) {
    int navigationAttempts = 0;
    QJsonArray observations;
    const auto openPage = [&] {
        ++navigationAttempts;
        if (c_.info) c_.info("每日页面", navigationAttempts == 1 ? "打开每日目标" : "页面未确认，重新打开一次");
        navigation_.openBook("gray_book_quest");
        c_.clickRelative(.17, .12, .7);
    };
    c_.refresh();
    if (c_.inTeam()) openPage();
    const auto read = [&] {
        for (;;) {
            for (int sample = 0; sample < 3; ++sample) {
                auto result = reference_observeDailyProgress(c_, &dailyObservation_);
                observations.append(dailyObservation_);
                if (observations.size() > 6) observations.removeFirst();
                if (result.points >= 0) return result;
                if (sample < 2) c_.sleep(.4);
            }
            // Poll a slow/stale frame first. Reopen only if the page itself is
            // absent, never replay a claim or spending decision on OCR uncertainty.
            if (!dailyObservation_["page_confirmed"].toBool() && navigationAttempts < 2) {
                openPage();
                continue;
            }
            const bool pageConfirmed = dailyObservation_["page_confirmed"].toBool();
            dailyObservation_["recent_observations"] = observations;
            dailyObservation_["navigation_attempts"] = navigationAttempts;
            observationFailure(pageConfirmed
                ? "每日页面已打开，但活跃度连续识别失败，已保存页面与识别记录"
                : "未能进入每日页面，已保存当前画面与识别记录，请检查游戏是否允许打开索拉指南");
        }
    };
    const auto collect = [&](DailyProgress result) {
        if (!collectObjectives) return result;
        for (int count = 0; count < 12; ++count) {
            std::optional<TextLine> target;
            for (const auto &value : dailyObservation_["objective_lines"].toArray()) {
                const auto line = value.toObject();
                const auto box = line["box"].toArray();
                if (box.size()!=4 || !QRegularExpression("^(?:领取|領取|Claim|Collect)$",
                    QRegularExpression::CaseInsensitiveOption).match(line["text"].toString().trimmed()).hasMatch()) continue;
                const QRect rect(box[0].toInt(),box[1].toInt(),box[2].toInt(),box[3].toInt());
                if (gameBox(c_.frame,.8,.18,.96,.79).contains(rect.center())) {
                    target=TextLine{line["text"].toString(),rect,line["confidence"].toDouble()};
                    break;
                }
            }
            if (!target) return result;
            const QJsonValue previous = dailyObservation_.value("objective_lines");
            c_.click(*target, .7);
            auto next=read();
            if (previous==dailyObservation_["objective_lines"]) {
                // Remote video can lag behind input. Observe again, without
                // clicking the old row after its next action becomes "Go".
                c_.sleep(.7);
                next=read();
                if (previous==dailyObservation_["objective_lines"])
                    observationFailure("每日目标领取后页面未更新，已保存现场，未重复点击");
            }
            if (!next.stamina) next.stamina=result.stamina;
            if (!next.echoNeeded) next.echoNeeded=result.echoNeeded;
            result=next;
        }
        observationFailure("每日目标领取次数异常，已保存现场");
    };
    auto result = collect(read());
    QJsonArray pages{dailyObservation_};
    const auto scan = [&](int steps) {
        c_.input.scroll(steps, QPointF(c_.frame.cols * .65, c_.frame.rows * .4));
        c_.sleep(.5);
        const auto next = collect(read());
        pages.append(dailyObservation_);
        if (next.stamina) result.stamina=next.stamina;
        if (next.echoNeeded) result.echoNeeded=next.echoNeeded;
        result.points=next.points;
    };
    // A reused panel retains its scroll position. Read the stamina row at the
    // top explicitly instead of repeatedly clicking the scrollbar's middle.
    if (!result.ready() && !result.stamina) scan(24);
    const bool needEcho = config_.value("Farm Nightmare Nest for Daily Echo").toBool(true);
    if (!result.ready() && needEcho && !result.echoNeeded.has_value()) scan(-24);
    dailyObservation_["objective_pages"] = pages;
    dailyObservation_["stamina"] = result.stamina.value_or(-1);
    dailyObservation_["echo_needed"] = result.echoNeeded ? QJsonValue(*result.echoNeeded) : QJsonValue();
    if (c_.info) {
        c_.info("每日活跃度", result.points>=0 ? QString::number(result.points) : "未识别");
        c_.info("每日体力进度", result.stamina ? QString("%1/180").arg(*result.stamina) : "未识别");
    }
    c_.reportProgress("完成每日目标",result.points,100,"活跃度",
                      result.echoNeeded.value_or(false)?"仍需获取声骸":"检查目标与可领取奖励");
    return result;
}
int reference_dailyStaminaBatch(int progress,int cost,int available,bool preferDouble) {
    if (progress<0 || progress>180 || cost<=0 || available<0) fail("每日体力额度无效");
    return cost*std::min({preferDouble?2:1,(180-progress+cost-1)/cost,available/cost});
}
void Daily::completeStamina() {
    if (noProgress_)
        return;
    std::unique_ptr<CubeBudget> budget;
    try {
        if (config_["Use Stamina Cubes for Daily"].toBool()) budget = std::make_unique<CubeBudget>(data_ + "/configs-dualpc/DailyStaminaUsage.json",
                                              config_["Daily Stamina Cube Limit"].toInt(4));
    } catch (const std::exception &e) {
        noProgress_ = true;
        if (c_.info)
            c_.info("体力魔方记录待核对", errorText(e));
        if (c_.notify)
            c_.notify("体力魔方记录待核对", errorText(e), true);
        return;
    }
    if (c_.info && budget)
        c_.info("已用体力魔方", QString("%1/%2").arg(budget->used()).arg(budget->limit()));
    const auto kind = reference_dailyDomain(config_);
    const int cost = domainCost(kind);
    for (int i = 0; i < 6; ++i) {
        const auto before = open();
        if (before.ready()) return;
        if (before.points<0 || !before.stamina)
            observationFailure("检查每日列表顶部后仍未识别到体力目标，已保存识别记录，未消耗体力");
        if (*before.stamina >= 180) {
            return;
        }
        navigation_.ensureMain();
        navigation_.openBook("gray_book_boss");
        auto stamina = readStamina(c_);
        if (stamina.current < 0 || stamina.reserve < 0 || stamina.total < 0)
            fail("无法确认体力，不使用魔方");
        while (stamina.total < cost) {
            if (!budget || !budget->remaining() || !useOneDailyCube(c_, *budget)) {
                navigation_.ensureMain();
                return;
            }
            stamina = readStamina(c_);
            if (stamina.total < 0)
                fail("补充后无法确认体力，不继续使用魔方");
        }
        navigation_.ensureMain();
        DomainFarm farm(c_,combat_);
        farm.onStaminaSpent=[&](int spent) { dailySpent_[countKey(kind)]+=spent; };
        const int batch=reference_dailyStaminaBatch(*before.stamina,cost,stamina.total,
                                         config_["Reward Claim Mode"].toString("Prefer Double")!="Single");
        farm.farm(kind, batch, true, config_);
        const auto after = open();
        if (after.points>claimedAtPoints_ && after.points>0) claim();
        if (after.ready()) {
            return;
        }
        if (!after.stamina || *after.stamina <= *before.stamina) {
            noProgress_ = true;
            if (c_.info)
                c_.info("每日体力进度", "补做后未确认进度增加，停止进一步消耗");
            if (c_.notify)
                c_.notify("每日体力进度待检查", "补做后未确认进度增加，停止进一步消耗", true);
            return;
        }
    }
}
void Daily::farmCount(DomainKind kind) {
    const auto n = config_[countKey(kind)];
    if (!n.isDouble() || n.toDouble() != std::floor(n.toDouble()) || n.toDouble() < 0 ||
        n.toDouble() > 100000)
        fail("副本次数无效");
    const int requested = std::max(0,n.toInt()-dailySpent_.value(countKey(kind))/domainCost(kind));
    if (!requested)
        return;
    navigation_.ensureMain();
    navigation_.openBook("gray_book_boss");
    int remaining = domainCountLimit(requested, readStamina(c_).total, domainCost(kind)) * domainCost(kind);
    navigation_.ensureMain();
    DomainFarm farm(c_, combat_);
    farm.onStaminaSpent = [&](int used) { remaining = std::max(0, remaining - used); };
    stage(kind==DomainKind::Tacet?"清理体力 · 无音区":kind==DomainKind::Forgery?"清理体力 · 凝素领域":"清理体力 · 模拟领域", [&] {
        if (remaining)
            farm.farm(kind, remaining, true, config_);
    });
}
DailyProgress Daily::claim() {
    const auto before=open(true);
    if (before.points<=0 || before.points<=claimedAtPoints_) {
        return before;
    }
    if (c_.info) c_.info("当前任务","领取每日奖励");
    // Inline reward popovers leave the daily heading/counter visible. They do
    // not contain the full-screen reward text, but consume the next chest click.
    // Dismiss them at a non-interactive gap below the tabs, never at another tier.
    c_.clickRelative(.5, .175, .3);
    // Claim every unlocked tier. A 20-point reward is available before the
    // 100-point chest; always clicking the last chest left early rewards behind.
    for (int tier=1; tier<=std::min(5,before.points/20); ++tier) {
        if (tier*20<=claimedAtPoints_) continue;
        c_.clickRelative(.392+(tier-1)*.1335, .889, .7);
        if (rewardTextConfirmed(c_.text(.2,.12,.8,.96,.7))) {
            c_.clickRelative(.5,.89,.5);
        }
        c_.clickRelative(.5, .175, .35);
        if (!c_.wait([&] { return reference_observeDailyProgress(c_, &dailyObservation_).points >= 0; }, 3, .2))
            observationFailure("每日奖励弹层关闭后未确认每日页面，已保存现场，未点击下一档奖励");
    }
    const auto progress = open();
    claimedAtPoints_=before.points;
    return progress;
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
    dailySpent_.clear();
    claimedAtPoints_=-1;
    extrasDone_=extrasTotal_=0;
    retries_ = enhanced ? std::clamp(config_["Task Retry"].toInt(5), 1, 10) : 1;
    const auto selected = config_[additions].toArray();
    QVector<QPair<QString,std::function<void()>>> extras;
    if (selected.contains(materials)) extras.append({"高级技能材料",[&] { EchoFarm(c_,combat_,detector_).run(true); }});
    if (enhanced) {
        const QList<DomainKind> kinds = config_["Run Tacet or Forgery First"]=="Forgery First" ?
            QList<DomainKind>{DomainKind::Forgery,DomainKind::Tacet,DomainKind::Simulation} :
            QList<DomainKind>{DomainKind::Tacet,DomainKind::Forgery,DomainKind::Simulation};
        for (const auto kind:kinds)
            if (config_[countKey(kind)].toInt()>0)
                extras.append({kind==DomainKind::Tacet?"清理体力 · 无音区":kind==DomainKind::Forgery?"清理体力 · 凝素领域":"清理体力 · 模拟领域",
                               [&,kind] { farmCount(kind); }});
    }
    if (selected.contains(allNests)) extras.append({"清理聚落",[&] { NightmareFarm(c_,combat_,detector_).run(); }});
    extras.append({"邮件",[&] { mail(); }});
    extras.append({"先约电台",[&] { battlePass(); }});
    if (selected.contains(weekly)) extras.append({"周常乐园",[&] { Garden(c_,root_).run(); }});
    if (selected.contains(merge)) extras.append({"弃置声骸合成",[&] { EchoOperations(c_,data_+"/screenshots").merge(); }});
    if (selected.contains(echo)) extras.append({"4C 声骸",[&] { EchoFarm(c_,combat_,detector_).run(); }});
    QJsonArray steps;
    const auto addStep = [&](QString id, double estimate, QString detail = {}) {
        steps.append(QJsonObject{{"id", id}, {"name", id}, {"estimate_seconds", estimate}, {"detail", detail}});
    };
    addStep("进入游戏", 30);
    addStep("检查每日目标与奖励", 20);
    addStep("完成每日目标", 420, "按需补做体力、获取声骸，已完成则跳过");
    addStep("领取每日奖励", 15);
    for (const auto &extra : extras) {
        const double estimate = extra.first == "邮件" ? 15 : extra.first == "先约电台" ? 25
            : extra.first == "周常乐园" ? 1200 : extra.first == "清理聚落"
                ? std::max(1, int(c_.profile.value("NightmareNestTask", "Which to Farm").toArray().size())) * 180.
            : extra.first.startsWith("清理体力") ? 300 : 0;
        addStep(extra.first, estimate);
    }
    c_.beginWorkflow(steps, data_ + "/task-timings.json");
    c_.workflowStep("进入游戏", "running");
    c_.state->loggedIn = false;
    navigation_.ensureMain(180);
    c_.workflowStep("进入游戏", "completed");
    DailyProgress progress;
    if (!stage("检查每日目标与奖励",[&] { progress=claim(); },false))
        fail("每日目标检查未完成，本次未开始刷取或附加任务。\n"+failed_.join('\n'));
    int echoAttempts=0;
    const auto dailyEcho = [&] {
        if (progress.points<0 || !config_["Farm Nightmare Nest for Daily Echo"].toBool(true)) return;
        for (;echoAttempts<3 && !progress.ready() && progress.echoNeeded.value_or(false);) {
            ++echoAttempts;
            bool captured=false;
            if (!stage("获取每日声骸 · 残像聚落",[&] {
                auto config=c_.profile.group("NightmareNestTask");
                config["Which to Farm"]=QJsonArray{"Tacet Discord Nest"};
                captured=NightmareFarm(c_,combat_,detector_).run(true,config);
            })) break;
            stage("每日奖励",[&] { progress=claim(); },false);
            if (!captured) break;
        }
    };
    // A single echo can finish the last twenty points without first spending
    // a full stamina objective. Otherwise domains can also supply that echo.
    const bool dailyAlreadyDone = progress.ready();
    c_.workflowStep("完成每日目标", dailyAlreadyDone ? "skipped" : "running",
                    dailyAlreadyDone ? "每日活跃度已满" : QString{});
    if (progress.points>=80) dailyEcho();
    if (!progress.ready()) stage("补做体力目标", [&] { completeStamina(); }, false);
    stage("检查每日目标",[&] { progress=open(); });
    dailyEcho();
    c_.workflowStep("领取每日奖励", "running");
    const bool rewardClaimed = stage("每日奖励",[&] { progress=claim(); },false);
    c_.workflowStep("领取每日奖励", rewardClaimed && progress.ready() ? "completed" : "failed");
    if (!progress.ready()) {
        c_.workflowStep("完成每日目标", "failed", "每日目标尚未完成");
        const bool echoEnabled = config_.value("Farm Nightmare Nest for Daily Echo").toBool(true);
        QString detail;
        if (!progress.stamina) detail += "\n体力目标未识别。";
        else if (*progress.stamina < 180) detail += QString("\n体力目标：%1/180。").arg(*progress.stamina);
        if (progress.echoNeeded.value_or(false))
            detail += echoEnabled ? "\n声骸目标尚未完成。" : "\n声骸目标尚未完成，自动获取每日声骸已关闭。";
        if (c_.diagnostic) c_.diagnostic("每日目标尚未完成",{{"daily_points",progress.points},
            {"stamina",progress.stamina.value_or(-1)},{"echo_needed",progress.echoNeeded.value_or(false)},
            {"echo_farming_enabled",echoEnabled},{"daily_observation",dailyObservation_}});
        fail(QString("每日活跃度 %1/100，尚有目标未完成，未开始附加聚落和周任务。").arg(progress.points)+detail+
             (failed_.isEmpty()?QString{}:"\n"+failed_.join('\n')));
    }
    if (!dailyAlreadyDone) c_.workflowStep("完成每日目标", "completed");
    // Extra plans never delay the daily claim. Daily runs count toward the
    // configured domain quota instead of spending it a second time.
    validate();
    extrasTotal_=extras.size();
    for (int i=0;i<extras.size();++i) {
        extrasDone_=i;
        c_.reportProgress("每日已领奖 · 附加任务",i,extras.size(),"项",extras[i].first);
        const bool retryNavigation = !QStringList{"高级技能材料","弃置声骸合成","4C 声骸"}.contains(extras[i].first);
        stage(extras[i].first,extras[i].second,retryNavigation);
        c_.reportProgress("每日已领奖 · 附加任务",i+1,extras.size(),"项",extras[i].first);
    }
    extrasDone_=extras.size();
    if (!failed_.empty())
        fail("每日流程已结束，以下项目未完成：\n" + failed_.join('\n'));
    status("每日任务已完成");
}

}
