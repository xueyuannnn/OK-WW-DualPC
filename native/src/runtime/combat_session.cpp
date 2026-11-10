#include "combat_session.hpp"
#include "interaction.hpp"
#include <opencv2/imgcodecs.hpp>
namespace okww::runtime {
namespace {
struct FlagGuard {
    bool &value;
    bool previous;
    FlagGuard(bool &v, bool next = true) : value(v), previous(v) { value = next; }
    ~FlagGuard() { value = previous; }
};
QString skillName(Skill skill) {
    return QStringList{"resonance", "echo", "liberation", "extra_action"}[int(skill)];
}
} // namespace
CombatSession::CombatSession(TaskContext &context, const QString &resources, const QString &directory,
                             std::shared_ptr<TeamStore> teams)
    : context_(context), vision_(context, resources + "/native/resources/characters.json"),
      teams_(std::move(teams)), calibrationPath_(directory + "/_con_full_size.json") {
    std::array<double, 6> areas{};
    if (QFile::exists(calibrationPath_)) {
        auto saved = readJson(calibrationPath_);
        for (int i = 0; i < 6; ++i)
            areas[i] = saved[QString::number(i)].toDouble();
    }
    concerto_ = Concerto(areas);
    enabledRevision_ = context_.state->combatEnableRevision;
}
void CombatSession::pollControls() {
    if (context_.task == "AutoCombatTask" &&
        !context_.profile.value("AutoCombatTask", "_enabled", true).toBool())
        throw CombatEnded("自动战斗已关闭");
    const auto revision = context_.state->combatEnableRevision.load();
    if (revision != enabledRevision_) {
        enabledRevision_ = revision;
        if (context_.task == "AutoCombatTask") {
            macroRearmAt = now();
            pendingMacroReset_ = true;
        }
    }
    if (pendingMacroReset_) {
        if (macroBusy)
            throw CombatEnded("自动战斗已重新启用，重新准备启动连招");
        pendingMacroReset_ = false;
        if (macro_)
            macro_->reset();
    }
    const bool down = context_.physicalFDown && context_.physicalFDown();
    if (down && !physicalFWasDown_ && !macroBusy)
        macroRearmAt = now();
    physicalFWasDown_ = down;
}
Character *CombatSession::current() {
    for (auto &c : party_)
        if (c->state.current)
            return c.get();
    return nullptr;
}
cv::Mat CombatSession::namedImage(const QString &name) {
    const auto box = vision_.box(name).intersected({0, 0, context_.frame.cols, context_.frame.rows});
    if (box.isEmpty())
        return {};
    return context_.frame(cv::Rect(box.x(), box.y(), box.width(), box.height())).clone();
}
void CombatSession::snapshot(const QString &name) {
    const auto path = QFileInfo(calibrationPath_).absolutePath() + "/../logs";
    if (!QDir().mkpath(path)) {
        log("未能创建诊断截图目录");
        return;
    }
    QString safe = name;
    safe.remove(QRegularExpression("[<>:\"/\\\\|?*]"));
    std::vector<uchar> bytes;
    if (!cv::imencode(".png", context_.frame, bytes))
        return;
    QSaveFile file(path + "/" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz_") +
                   safe.left(80) + ".png");
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size()) !=
            static_cast<qint64>(bytes.size()) ||
        !file.commit())
        log("诊断截图保存失败");
}
QVector<Character *> CombatSession::characters() {
    QVector<Character *> out;
    for (auto &c : party_)
        out.append(c.get());
    return out;
}
void CombatSession::publishParty(const QString &codeState) {
    if (!context_.combatInfo) return;
    QJsonArray members;
    const auto hud = vision_.party();
    for (const auto &c : party_)
        members.append(QJsonObject{{"class", c->state.className}, {"name", c->state.name},
            {"index", c->state.index}, {"current", hud.visible && c->state.index == hud.current}});
    const QJsonObject identity{{"members", members}, {"mode", selectedTeam_.startsWith("python:") ? "python"
        : selectedTeam_.isEmpty() ? "builtin" : "native-team"}, {"code_state", codeState}, {"verified", hud.visible}};
    const auto key = QJsonDocument(identity).toJson(QJsonDocument::Compact);
    auto report = identity;
    if (selectedTeam_.startsWith("python:")) report["bridge"] = bridgeInfo_;
    if (report == lastPartyReport_ || (key == lastPartyKey_ && now()-lastPartyReportAt_ < .25)) return;
    lastPartyKey_ = key;
    lastPartyReport_ = report;
    lastPartyReportAt_ = now();
    context_.combatInfo(std::move(report));
}
bool CombatSession::loadParty() {
    auto hud = vision_.party();
    if (!hud.visible)
        return false;
    if (partyScanVersion_ == context_.frameVersion && party_.size() == size_t(hud.size)) {
        for (auto &c : party_) c->state.current = c->state.index == hud.current;
        publishParty();
        return party_.size() >= 2;
    }
    partyScanVersion_ = context_.frameVersion;
    QVector<std::optional<CharacterIdentity>> identities;
    for (int i = 0; i < hud.size; ++i) {
        auto old = i < int(party_.size()) ? &party_[i]->state : nullptr;
        auto id = vision_.character(i, old);
        if (id && !nativeCharacterClasses().contains(id->definition.className))
            fail("当前队伍的 " + id->definition.className + " 尚未迁移；未发送战斗输入。");
        identities.append(id);
    }
    bool changed = party_.size() != size_t(hud.size);
    for (int i = 0; i < identities.size(); ++i)
        if (identities[i] && (i >= int(party_.size()) || !party_[i] ||
                              party_[i]->state.name != identities[i]->definition.canonical))
            changed = true;
    if (changed) {
        macro_.reset();
        selectedTeam_.clear();
    }
    party_.resize(size_t(hud.size));
    partyDefinitions_.resize(hud.size);
    for (int i = 0; i < hud.size; ++i) {
        auto &id = identities[i];
        if (id) {
            auto d = id->definition;
            if (!party_[i] || party_[i]->state.name != d.canonical) {
                CharacterState s;
                s.index = i;
                s.name = d.canonical;
                s.role = d.role;
                s.buffTime = d.buffTime;
                s.ringIndex = d.element;
                s.confidence = id->confidence;
                party_[i] = makeCharacter(d.className, *this, s);
            }
            partyDefinitions_[i] = d;
        } else if (!party_[i]) {
            CharacterState s;
            s.index = i;
            s.name = "unknown";
            party_[i] = makeCharacter("BaseChar", *this, s);
        }
        party_[i]->resetState();
        party_[i]->state.current = i == hud.current;
    }
    const auto selected = teams_ ? teams_->selection(*this) : QString{};
    if (selected != selectedTeam_) {
        macro_.reset();
        selectedTeam_ = selected;
        bridgeInfo_ = {};
        publishParty(selected.startsWith("python:") ? "loading" : "ready");
        if (teams_)
            macro_ = teams_->create(selected, *this);
    }
    if (macro_)
        macro_->reset();
    combatStart = now();
    publishParty();
    return party_.size() >= 2;
}
bool CombatSession::interval(double seconds) {
    if (seconds <= 0)
        return true;
    if (now() - lastInterval_ < seconds) {
        context_.execution.wait(.001);
        context_.refresh();
        return false;
    }
    lastInterval_ = now();
    return true;
}
void CombatSession::tap(QString key, double after, double between, double down) {
    if (!interval(between))
        return;
    context_.input.key(key, down);
    context_.refresh();
    if (after > 0)
        sleep(after);
}
void CombatSession::click(QString button, double between) {
    if (!interval(between))
        return;
    context_.input.click({}, .01, button);
    context_.refresh();
}
QString CombatSession::hotkey(Skill skill) const {
    switch (skill) {
    case Skill::Resonance:
        return context_.hotkey("Resonance Key", "e");
    case Skill::Echo:
        return context_.hotkey("Echo Key", "r");
    case Skill::Liberation:
        return context_.hotkey("Liberation Key", "q");
    default:
        return "4";
    }
}
void CombatSession::watchdog() {
    context_.execution.checkpoint();
    if (combatDeadline_ > 0 && now() > combatDeadline_)
        throw NavigationFailure("本轮战斗超过等待上限，未记录为完成");
    if ((!engaged_ && !combatScope_) || checkingDialog_ || now() - lastDeathCheck_ < 1)
        return;
    lastDeathCheck_ = now();
    FlagGuard guard(checkingDialog_);
    if (vision_.deathDialog()) {
        context_.input.release();
        throw CharacterDead();
    }
}
void CombatSession::nextFrame() {
    context_.execution.wait(.001);
    context_.refresh();
    watchdog();
}
void CombatSession::sleep(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 86400)
        fail("战斗等待时间无效");
    const auto end = now() + seconds;
    do {
        context_.execution.wait(std::min(.4, std::max(0., end - now())));
        context_.refresh();
        watchdog();
        if (!skipCombatCheck && !checking_)
            checkCombat();
    } while (now() < end);
}
bool CombatSession::hasCd(Skill skill, int index) { return cooldown(skill, index) > .2; }
double CombatSession::cooldown(Skill skill, int index) {
    auto c = current();
    if (!c)
        return 0;
    vision_.refreshCooldowns(c->state.index);
    return vision_.cooldown(skill, index < 0 ? c->state.index : index, freezes, now());
}
bool CombatSession::feature(const QString &name, double threshold, const QString &box) {
    MatchOptions options;
    options.threshold = threshold;
    if (!box.isEmpty())
        options.search = vision_.box(box);
    return bool(context_.find(name, options));
}
QString CombatSession::bestInBox(const QString &box, const QStringList &names, double threshold) {
    QString name;
    double score = threshold;
    for (const auto &candidate : names)
        if (auto match = context_.find(candidate, {.threshold = threshold, .search = vision_.box(box)}))
            if (match->confidence >= score) {
                name = candidate;
                score = match->confidence;
            }
    return name;
}
double CombatSession::whiteRatio(QSize reference, QRect area, cv::Scalar lower, bool hc) {
    return colorPercentage(
        context_.frame, Templates::anchored(area, reference, {context_.frame.cols, context_.frame.rows}, hc),
        lower, {255, 255, 255});
}
bool CombatSession::available(Skill skill, int index, bool checkColor, bool checkCd) {
    return (!checkColor || highlighted(skill) > 0) && (!checkCd || !hasCd(skill, index));
}
double CombatSession::highlighted(Skill skill) { return vision_.white("box_" + skillName(skill)); }
cv::Mat CombatSession::imageRegion(QSize reference, QRect area, bool hc) {
    const auto r = Templates::anchored(area, reference, {context_.frame.cols, context_.frame.rows}, hc);
    if (r.x() < 0 || r.y() < 0 || r.right() >= context_.frame.cols || r.bottom() >= context_.frame.rows)
        return {};
    return context_.frame(cv::Rect(r.x(), r.y(), r.width(), r.height()));
}
bool CombatSession::featureInRegion(const QString &name, QSize reference, QRect area, double threshold) {
    return bool(context_.find(
        name,
        {.threshold = threshold,
         .search = Templates::anchored(area, reference, {context_.frame.cols, context_.frame.rows}, true)}));
}
int CombatSession::segmentedGauge(QSize reference, QRect area, ColorRange color, int segments,
                                  int minFrequency, int maxFrequency, double minAmplitude) {
    const auto r = Templates::anchored(area, reference, {context_.frame.cols, context_.frame.rows}, true);
    if (r.x() < 0 || r.y() < 0 || r.right() >= context_.frame.cols || r.bottom() >= context_.frame.rows)
        return 0;
    return segmentedGaugeValue(context_.frame(cv::Rect(r.x(), r.y(), r.width(), r.height())), color, segments,
                               minFrequency, maxFrequency, minAmplitude);
}
double CombatSession::concerto(Character &character) {
    const auto before = concerto_.fullAreas();
    const double cached = character.state.currentCon;
    const double percent = concerto_.detect(context_.frame, character.state);
    character.state.currentCon = cached;
    if (before != concerto_.fullAreas()) {
        QJsonObject saved;
        for (int i = 0; i < 6; ++i)
            saved[QString::number(i)] = concerto_.fullAreas()[i];
        writeJson(calibrationPath_, saved);
    }
    return percent;
}
bool CombatSession::healthBar() {
    if (vision_.healthBar(engaged_))
        return true;
    if (!inTeam())
        return false;
    if (now() - lastHealthCheck_ >= .2) {
        healthCache_ = vision_.shiftedHealthBar();
        lastHealthCheck_ = now();
    }
    return healthCache_;
}
double CombatSession::concertoAreaRatio(Character &character) {
    if (character.state.ringIndex < 0)
        concerto(character);
    const int index = character.state.ringIndex;
    if (index < 0 || index >= 6 || concerto_.fullAreas()[index] <= 0)
        return 0;
    const auto box = Concerto::region({context_.frame.cols, context_.frame.rows});
    if (!QRect(0, 0, context_.frame.cols, context_.frame.rows).contains(box))
        return 0;
    const auto ring =
        countRing(context_.frame(cv::Rect(box.x(), box.y(), box.width(), box.height())),
                  concertoColors()[index], 1500. / 3840 / 2160 * context_.frame.cols * context_.frame.rows);
    return ring.area / concerto_.fullAreas()[index];
}
bool CombatSession::visibleTarget() {
    const auto has = targetName(),
               no = has == "has_target_169" ? QString("no_target_169") : QString("no_target");
    auto first = vision_.box(has);
    const int dx = int(first.width() * .05), dy = int(first.height() * .05);
    first.adjust(-dx, -dy, dx, dy);
    for (const auto &box : {first, vision_.box("box_target_enemy_long"), vision_.box("target_box_long2")}) {
        QString best;
        double score = .6;
        for (const auto &name : {has, no})
            if (auto found = context_.find(name, {.threshold = .6, .search = box});
                found && found->confidence >= score) {
                score = found->confidence;
                best = name;
            }
        if (!best.isEmpty())
            return best == has;
    }
    return false;
}
bool CombatSession::targetEnemy() {
    if (engaged_ && inTeam() && healthBar())
        return true;
    if (targetLocked())
        return true;
    const auto start = now();
    while (now() - start < targetEnemyTimeout) {
        click("middle", .2);
        if (targetLocked())
            return true;
        nextFrame();
    }
    return false;
}
bool CombatSession::targetLocked() {
    const auto c = current();
    const bool shortTarget = c && partyDefinitions_[c->state.index].shortTarget;
    auto result = vision_.targetObservation(shortTarget, true);
    if (!result.extended || targetEscCount_ != 0)
        return result.locked;
    // Recheck the displaced lock-on indicator before the original bear-Echo
    // recovery action. Avoid recursively re-entering combat detection in sleep.
    FlagGuard checking(checking_);
    sleep(1);
    result = vision_.targetObservation(shortTarget, true);
    if (!result.extended)
        return result.locked;
    tap("esc", 2);
    tap("esc", 1.5);
    targetEscCount_ = 1;
    return false;
}
bool CombatSession::checkBreak() {
    if (canBreak_)
        return true;
    if (context_.find("f_break_full", {.threshold = .92}))
        return canBreak_ = true;
    if (!inLiberation && now() - lastBreakCheck_ > 1) {
        lastBreakCheck_ = now();
        if (context_.find("f_break", {.search = gameBox(context_.frame, .2, .2, .75, .8, true, true),
                                      .targetHeight = 720})) {
            if (!isDialogInteract(context_))
                return canBreak_ = true;
        }
    }
    return false;
}
bool CombatSession::macroBreakPrompt() {
    if (visibleBreakPrompt())
        canBreak_ = true;
    return canBreak_;
}
bool CombatSession::visibleBreakPrompt(double threshold) {
    const auto area = Templates::anchored(QRect(384, 216, 1056, 648), {1920, 1080},
                                          {context_.frame.cols, context_.frame.rows}, true, true);
    return bool(context_.find("f_break", {.threshold = threshold, .search = area, .targetHeight = 720})) &&
           !isDialogInteract(context_);
}
bool CombatSession::breakF(bool force) {
    if (force)
        tap("f", .05);
    if (!canBreak_ && !checkBreak())
        return false;
    const auto start = now();
    while (now() - start < .5 || (now() - start < 5 && (canBreak_ || checkBreak()))) {
        tap("f", .1);
        click();
        sleep(.1);
        canBreak_ = false;
    }
    return true;
}
bool CombatSession::combatActive() {
    if (inLiberation)
        return true;
    if (engaged_) {
        if (checkedVersion_ == context_.frameVersion)
            return true;
        checkBreak();
        auto c = current();
        if (c && c->skipCombatCheck())
            return true;
        if (onCombatCheck && !onCombatCheck()) {
            resetCombat();
            return false;
        }
        if (targetLocked()) {
            checkedVersion_ = context_.frameVersion;
            return true;
        }
        if (combatEndCondition && combatEndCondition()) {
            resetCombat();
            return false;
        }
        if (targetEnemy()) {
            checkedVersion_ = context_.frameVersion;
            return true;
        }
        if (context_.monthlyCard())
            return true;
        resetCombat();
        return false;
    }
    if (!loadParty())
        return false;
    const bool target = targetLocked();
    const bool autoTarget =
        !isAutoCombatTask() || context_.profile.value("AutoCombatTask", "Auto Target", true).toBool();
    const bool boss = bool(context_.findAny({"boss_break_shield", "boss_break_lock"}));
    if (!target && !(autoTarget && (healthBar() || boss)))
        return false;
    if (!target && !targetEnemy())
        return false;
    hasLevitator = bool(context_.find("edge_levitator", {.threshold = .65}));
    engaged_ = true;
    context_.state->inCombat = true;
    return true;
}
void CombatSession::checkCombat() {
    context_.execution.checkpoint();
    watchdog();
    if (skipCombatCheck || checking_ || !engaged_)
        return;
    FlagGuard guard(checking_);
    if (!combatActive())
        throw CombatEnded("目标消失，战斗结束");
}
void CombatSession::resetCombat() {
    context_.state->inCombat = false;
    targetEscCount_ = 0;
    engaged_ = inLiberation = hasLevitator = canBreak_ = false;
    checkedVersion_ = UINT64_MAX;
    vision_.reset();
    lastHealthCheck_ = -1;
    healthCache_ = false;
}
void CombatSession::updatePortraits() {
    const QStringList names{"lib_ready_spectro", "lib_ready_electric", "lib_ready_fire",
                            "lib_ready_ice",     "lib_ready_wind",     "lib_ready_havoc"};
    for (auto &c : party_)
        if (!c->state.current && c->state.ringIndex >= 0 && !c->state.liberationReady) {
            auto area = vision_.box(QString("lib_mark_char_%1").arg(c->state.index + 1));
            if (context_.find(names[c->state.ringIndex], {.search = area}))
                c->state.liberationReady = true;
        }
}
void CombatSession::switchNext(Character &previous, bool freeIntro, bool targetLowCon,
                               const SwitchAction &post) {
    updatePortraits();
    bool intro = freeIntro;
    if (!intro) {
        double con = previous.currentCon();
        if (con > .8 && con != 1) {
            sleep(.05);
            nextFrame();
            con = previous.currentCon();
        }
        intro = con == 1;
    }
    auto choose = [&]() -> Character * {
        std::vector<SwitchCandidate> candidates;
        for (auto &c : party_) {
            const auto overridden = macro_ ? macro_->priority(*c, intro) : std::optional<int>{};
            candidates.push_back(
                {&c->state, overridden ? *overridden : c->switchPriority(&previous, intro, targetLowCon)});
        }
        auto decision = chooseSwitch(previous.state, candidates, intro, freezes, now());
        log(QString("%1 → %2：%3")
                .arg(previous.state.index + 1)
                .arg(decision.index + 1)
                .arg(decision.reason));
        return decision.index == previous.state.index ? nullptr : party_[size_t(decision.index)].get();
    };
    auto target = choose();
    if (!target) {
        previous.normalAttacks(.2);
        return;
    }
    applyIntro(previous.state, target->state, intro);
    const auto start = now();
    double lastClick = 0;
    while (now() - start <= 10) {
        if (!target->skipSwitchCombatCheck(intro))
            checkCombat();
        else
            watchdog();
        auto hud = vision_.party();
        if (hud.current == previous.state.index) {
            updatePortraits();
            const bool refreshed = intro || previous.isConFull();
            if (refreshed != intro) {
                intro = refreshed;
                target = choose();
                if (!target) {
                    previous.normalAttacks(.2);
                    return;
                }
            }
            applyIntro(previous.state, target->state, intro);
            if (intro && previous.checkFOnSwitch)
                breakF();
        }
        if (target->waitSwitch()) {
            click();
            sleep(.1);
            continue;
        }
        if (now() - lastClick > .1) {
            lastClick = now();
            tap(QString::number(target->state.index + 1));
            sleep(.001);
            click();
            sleep(.001);
        }
        hud = vision_.party();
        if (!hud.visible) {
            watchdog();
            throw CombatInterrupted("切换角色时队伍界面消失");
        }
        if (hud.current == target->state.index) {
            inLiberation = false;
            if (!intro && previous.checkFOnSwitch)
                breakF();
            previous.switchOut(intro);
            target->state.current = true;
            target->state.lastSwitchIn = now();
            if (intro) {
                freezes.add(now(), now(), target->introDuration(), -100);
                previous.state.lastOutro = now();
            }
            if (post)
                post(*target, intro);
            publishParty();
            return;
        }
        nextFrame();
    }
    throw CombatInterrupted("角色切换超时");
}
void CombatSession::switchHealer() {
    const auto group = context_.task == "FarmEchoTask" || context_.task == "FarmEchoTask2"
                           ? context_.task
                           : QString("AutoCombatTask");
    const auto enabled =
        group == context_.task
            ? context_.setting("Switch to Healer before and after Combat", true)
            : context_.profile.value(group, "Switch to Healer before and after Combat", true);
    if (!enabled.toBool())
        return;
    auto c = current();
    if (!c || c->state.role == Role::Healer)
        return;
    Character *healer = nullptr;
    for (auto &member : party_)
        if (member->state.role == Role::Healer) {
            healer = member.get();
            break;
        }
    if (!healer)
        return;
    FlagGuard skip(skipCombatCheck);
    const auto start = now();
    while (now() - start < 6) {
        auto hud = vision_.party();
        if (hud.visible && hud.current != c->state.index) {
            for (auto &member : party_)
                member->state.current = member->state.index == hud.current;
            return;
        }
        tap(QString::number(healer->state.index + 1));
        sleep(.2);
    }
}
void CombatSession::performCurrent() {
    // A cancelled switch may leave a different character on field. Trust the
    // visible full party HUD before beginning another segment.
    const auto hud = vision_.party();
    if (hud.visible && hud.size == int(party_.size()) && hud.current >= 0 && hud.current < hud.size)
        for (auto &member : party_)
            member->state.current = member->state.index == hud.current;
    auto actor = current();
    if (!actor)
        throw CombatInterrupted("当前角色无法确认");
    actor->state.lastPerform = now();
    if (!macro_ || !macro_->perform(*actor))
        actor->perform();
    publishParty();
}
bool CombatSession::runAutoCombat() {
    if (!inTeam())
        return false;
    const bool earth = bool(context_.find("world_earth_icon", {.threshold = .55, .filter = Filter::White}));
    const bool exit = bool(context_.find("illusive_realm_exit", {.threshold = .7, .filter = Filter::White}));
    const bool world = earth && !exit, realm = exit && !earth;
    openWorld_ = !realm;
    useLiberation = context_.profile.value("AutoCombatTask", "Use Liberation", true).toBool() || !world;
    bool ran = false, switched = false;
    try {
        while (combatActive()) {
            ran = true;
            if (!switched) {
                switchHealer();
                switched = true;
            }
            performCurrent();
            nextFrame();
        }
    } catch (const CharacterDead &) {
        context_.input.release();
        resetCombat();
        throw;
    } catch (const CombatEnded &e) {
        log(errorText(e));
    } catch (...) {
        context_.input.release();
        resetCombat();
        throw;
    }
    context_.input.release();
    try {
        if (ran) {
            // Turning Auto Combat off must not execute any final skill/click.
            if (context_.profile.value("AutoCombatTask", "_enabled", true).toBool()) {
                if (macro_)
                    macro_->combatEnd();
                if (!macro_ || !selectedTeam_.startsWith("python:"))
                    if (auto c = current()) c->combatEnd();
            }
            for (auto &c : party_)
                c->resetState();
            if (context_.profile.value("AutoCombatTask", "_enabled", true).toBool())
                switchHealer();
        }
    } catch (...) {
        context_.input.release();
        resetCombat();
        throw;
    }
    resetCombat();
    return ran;
}
bool CombatSession::active() {
    watchdog();
    return combatActive();
}
void CombatSession::recoverStartingPopup(bool realmEntryAtHealPoint) {
    if (!vision_.deathDialog())
        return;
    resetCombat();
    closeRevivalDialog();
    Navigation navigation(context_);
    const bool realm = navigation.inRealm();
    if (realm)
        navigation.exitRealm(120);
    if (!realm || !realmEntryAtHealPoint)
        navigation.healAtBeacon();
    log("遗留复苏弹窗已关闭并免费回血");
}
bool CombatSession::combatOnce(const CombatOptions &options) {
    if (!std::isfinite(options.waitSeconds) || options.waitSeconds < 0 ||
        !std::isfinite(options.maximumSeconds) || options.maximumSeconds <= 0 ||
        options.maximumSeconds > 7200)
        fail("战斗等待参数无效");
    FlagGuard scope(combatScope_);
    FlagGuard skip(skipCombatCheck, false);
    openWorld_ = false;
    useLiberation = options.useLiberation;
    lastDeathCheck_ = -1;
    const auto recover = [&]() {
        context_.input.release();
        resetCombat();
        combatDeadline_ = 0;
        closeRevivalDialog();
        if (options.recover)
            options.recover();
        else {
            Navigation navigation(context_);
            const bool realm = navigation.inRealm();
            if (realm)
                navigation.exitRealm(120);
            if (!realm || !options.realmEntryAtHealPoint)
                navigation.healAtBeacon();
        }
        resetCombat();
        log("角色已恢复，本轮重新进入后再执行");
        throw CharacterRevived();
    };
    bool found = false;
    try {
        if (vision_.deathDialog())
            throw CharacterDead();
        const auto until = now() + options.waitSeconds;
        do {
            watchdog();
            if (combatActive()) {
                found = true;
                break;
            }
            if (options.target)
                click("middle", .5);
            context_.sleep(.02);
        } while (now() < until);
        if (!found) {
            if (options.required)
                throw NavigationFailure("等待进入战斗超时，未记录为完成");
            resetCombat();
            return false;
        }
        combatDeadline_ = now() + options.maximumSeconds;
        ++combatCount_;
        if (context_.info)
            context_.info("战斗次数", QString::number(combatCount_));
        switchHealer();
        int interruptions = 0;
        const int retryLimit =
            context_.profile.value("Task Recovery", "Resume Interrupted Tasks", true).toBool()
                ? std::clamp(context_.profile.value("Task Recovery", "Recovery Retry Limit", 3).toInt(), 1,
                             10)
                : 0;
        for (;;) {
            try {
                while (combatActive()) {
                    watchdog();
                    performCurrent();
                    nextFrame();
                }
                break;
            } catch (const CharacterDead &) {
                throw;
            } catch (const CombatInterrupted &error) {
                context_.input.release();
                inLiberation = false;
                log(errorText(error));
                if (++interruptions > retryLimit)
                    throw NavigationFailure("本轮连招中断次数达到上限，未记录为完成：" + errorText(error));
                if (!context_.wait([&] { return inTeam() || vision_.deathDialog(); }, 5))
                    throw NavigationFailure("连招中断后未恢复队伍界面，未记录为完成");
                watchdog();
                if (vision_.deathDialog())
                    throw CharacterDead();
                log(QString("继续当前战斗 %1/%2").arg(interruptions).arg(retryLimit));
            } catch (const CombatEnded &error) {
                log(errorText(error));
                break;
            }
        }
        if (vision_.deathDialog())
            throw CharacterDead();
        context_.input.release();
        if (macro_)
            macro_->combatEnd();
        if (!macro_ || !selectedTeam_.startsWith("python:"))
            if (auto actor = current()) actor->combatEnd();
        for (auto &actor : party_)
            actor->resetState();
        switchHealer();
        if (vision_.deathDialog())
            throw CharacterDead();
        combatDeadline_ = 0;
        resetCombat();
        Navigation(context_).waitTeam(10, false);
        return true;
    } catch (const Cancelled &) {
        context_.input.release();
        combatDeadline_ = 0;
        resetCombat();
        throw;
    } catch (const CharacterDead &) {
        recover();
    } catch (...) {
        context_.input.release();
        combatDeadline_ = 0;
        resetCombat();
        if (vision_.deathDialog())
            recover();
        throw;
    }
    return false;
}
bool CombatSession::closeRevivalDialog() {
    if (!vision_.deathDialog())
        return false;
    context_.input.release();
    FlagGuard guard(checkingDialog_);
    FlagGuard skip(skipCombatCheck);
    context_.key("esc", .5);
    if (context_.wait([&] { return !vision_.deathDialog(); }, 2))
        return true;
    context_.clickRelative(.803, .196, .5);
    if (!context_.wait([&] { return !vision_.deathDialog(); }, 2))
        fail("复苏弹窗未关闭，避免误用道具，停止本次恢复");
    return true;
}
} // namespace okww::runtime
