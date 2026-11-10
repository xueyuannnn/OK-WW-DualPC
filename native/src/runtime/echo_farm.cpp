#include "echo_farm.hpp"
namespace okww::runtime {
bool weeklyMaterialRewardConfirmed(const QVector<TextLine> &lines) {
    if (restartDialog(lines).kind == RestartKind::Resource)
        return false;
    if (rewardTextConfirmed(lines))
        return true;
    const bool heading = bool(matchingText(
        lines, "^(?:获得奖励|獲得獎勵|获得物品|獲得物品|Rewards? Obtained|Items? Obtained)[.:：]*$"));
    const bool controls =
        bool(matchingText(lines, "^(?:离开|離開|退出|再次挑战|再次挑戰|重新挑战|重新挑戰|继续挑战|繼續挑戰|"
                                 "Leave|Exit|Challenge Again|Continue Challenge)$"));
    return heading && controls;
}
void awaitWeeklyMaterialReward(TaskContext &context) {
    if (!context.wait([&] { return weeklyMaterialRewardConfirmed(context.text(.08, .04, .92, .98, .7)); }, 12,
                      .2))
        fail("未确认周本材料领奖结果，已停止，不会再次点击消耗按钮");
}
QVector<EchoTarget> echoRotationTargets(const QJsonObject &config, int bosses, int weekly) {
    const auto scope = config["Cycle Scope"].toString("All Challenges");
    if (!QStringList{"All Challenges", "Boss Challenge", "Weekly Challenge"}.contains(scope))
        fail("循环范围无效");
    const auto start =
        scope == "All Challenges" ? config["Cycle Start Category"].toString("Boss Challenge") : scope;
    const auto value = config.value("Cycle Start Index");
    const double n = value.isUndefined() ? 1 : value.toDouble(-1);
    if (!QStringList{"Boss Challenge", "Weekly Challenge"}.contains(start) || !std::isfinite(n) ||
        std::floor(n) != n || n < 1 || n > (start == "Boss Challenge" ? bosses : weekly))
        fail("循环起刷序号超出所选分类范围");
    QVector<EchoTarget> targets;
    int offset = 0;
    for (const auto &category : {QString("Boss Challenge"), QString("Weekly Challenge")}) {
        if (scope != "All Challenges" && scope != category)
            continue;
        for (int i = 1; i <= (category == "Boss Challenge" ? bosses : weekly); ++i) {
            if (category == start && i == int(n))
                offset = int(targets.size());
            targets.append({category, i});
        }
    }
    std::rotate(targets.begin(), targets.begin() + offset, targets.end());
    return targets;
}
bool EchoFarm::teleportEnabled() const { return config_["Teleport to Boss"].toString("No") != "No"; }
void EchoFarm::parameters() {
    const auto boss = config_["Boss"].toString("Other");
    wait_ = config_["Combat Wait Time"].toDouble();
    if (wait_ == 0) {
        if (QStringList{"Sentry Construct", "Lioness of Glory", "Fallacy of No Return"}.contains(boss))
            wait_ = 5;
        else if (boss == "Hyvatia")
            wait_ = 7;
    }
    if (boss == "Lady of the Sea")
        realm_ = true;
    bypassWait_ =
        QStringList{"Fenrico", "Fallacy of No Return", "Lady of the Sea", "Nameless Explorer"}.contains(boss);
    treatAsWorld_ = boss == "Nightmare: Hecate";
    combat_.targetEnemyTimeout = realm_ ? 3 : 1.2;
}
void EchoFarm::checkRealm(double seconds) {
    if (!realm_ && !treatAsWorld_ && c_.execution.now() - started_ < seconds) {
        realm_ = navigation_.inRealm();
        combat_.targetEnemyTimeout = realm_ ? 3 : 1.2;
    }
}
void EchoFarm::bossLevel() {
    const auto level = config_["Boss Level"].toString("80");
    if (!QStringList{"50", "60", "70", "80", "90"}.contains(level))
        fail("Boss 等级无效");
    std::optional<TextLine> target;
    if (!c_.wait(
            [&] {
                target = matchingText(c_.text(.030, .113, .323, .660), QRegularExpression::escape(level));
                return bool(target);
            },
            10))
        throw NavigationFailure("未找到所选 Boss 等级");
    c_.sleep(1);
    c_.click(*target);
}
void EchoFarm::enterFromF() {
    if (!c_.wait([&] { return bool(findInteract(c_)); }, 2))
        throw NavigationFailure("进本前未找到 F 提示");
    c_.key("f", 3);
    bossLevel();
    c_.sleep(1);
    c_.clickRelative(.880, .911, 2);
    c_.wait([&] { return c_.skipConfirm(); }, 1);
    c_.clickRelative(.908, .919, 5);
    navigation_.waitTeam(120);
    realm_ = true;
    treatAsWorld_ = false;
    treasure_ = false;
    justEntered_ = true;
    combat_.targetEnemyTimeout = 3;
}
void EchoFarm::teleport() {
    walkResult_.clear();
    navigation_.ensureMain(180);
    const auto category = config_["Teleport to Boss"].toString();
    QString feature;
    int serial = 1, total = 0;
    QVector<int> structure;
    if (category == "Weekly Challenge") {
        feature = "zhange";
        serial = config_["Which Weekly Boss to Teleport"].toInt(1);
        total = 9;
    } else if (category == "Boss Challenge") {
        feature = "qiangdi";
        serial = config_["Which Boss Challenge to Teleport"].toInt(1);
        total = 20;
    } else if (category == "Special Nightmare") {
        feature = "mengyan";
        serial = config_["Which Special Nightmare to Teleport"].toInt(1) + 5;
        total = 15;
        structure = {5, 10};
    } else
        fail("传送 Boss 分类无效");
    navigation_.openBook("gray_book_boss");
    navigation_.bookCategory(feature);
    const bool team = navigation_.selectBookEntry(serial, total, structure);
    if (team) {
        if (category == "Weekly Challenge") {
            bossLevel();
            c_.clickRelative(.880, .911, 2);
            c_.wait([&] { return c_.skipConfirm(); }, 1);
        }
        navigation_.startChallenge();
        justEntered_ = true;
    } else {
        entryAtHeal_ = false;
        navigation_.waitTravel();
    }
    navigation_.waitTeam(120);
    c_.sleep(2);
    if (team)
        walkResult_ = "realm";
    else {
        const auto found = [&] { return combat_.active() || bool(findInteract(c_)); };
        if (!c_.wait(found, 1.5)) {
            c_.input.click({}, .01, "middle");
            c_.sleep(.2);
            double last = -1;
            if (!world_.walkUntil(
                    [&] {
                        if (found())
                            return true;
                        if (c_.execution.now() - last >= .5) {
                            c_.input.click({}, .01, "middle");
                            last = c_.execution.now();
                        }
                        return false;
                    },
                    "w", 20, true))
                throw NavigationFailure("传送后未能接近战斗或副本入口");
        }
        if (combat_.active())
            walkResult_ = "combat";
        else {
            enterFromF();
            walkResult_ = "realm";
        }
    }
    realm_ = walkResult_ == "realm";
    treatAsWorld_ = !realm_;
    treasure_ = false;
    combat_.targetEnemyTimeout = realm_ ? 3 : 1.2;
}
void EchoFarm::nearestBoss() {
    if (!aimBoss_.isEmpty()) {
        navigation_.ensureMain(180);
        navigation_.openBook();
        navigation_.selectMonster(aimBoss_);
        c_.clickRelative(.89, .92, 1);
        c_.clickRelative(.89, .92);
        navigation_.waitTeam(30);
        return;
    }
    c_.key("m", 2);
    if (config_["Boss"].toString() == "Nameless Explorer")
        c_.clickRelative(.06, .50, .5);
    const auto area = gameBox(c_.frame, .3, .3, .7, .7);
    auto marker = c_.find("boss_check_mark", {.search = area});
    if (!marker)
        for (int angle : {0, 90, 180}) {
            if (auto next = c_.find("boss_no_check_mark",
                                    {.threshold = .7, .search = area, .rotation = double(angle)}))
                if (!marker || next->confidence > marker->confidence)
                    marker = next;
        }
    if (!marker)
        throw NavigationFailure("地图中未找到当前 Boss 标记");
    c_.click(*marker);
    navigation_.waitTravel();
    navigation_.waitTeam(30);
}
bool EchoFarm::seekCombat(double timeout, bool move, bool run) {
    double last = -1;
    const auto condition = [&] {
        if (combat_.active())
            return true;
        if (move && world_.treasure())
            return true;
        if (c_.execution.now() - last >= .5) {
            c_.input.click({}, .01, "middle");
            last = c_.execution.now();
        }
        return false;
    };
    return move ? world_.walkUntil(condition, "w", timeout, run) : c_.wait(condition, timeout, .02);
}
void EchoFarm::scrollInteract() {
    c_.sleep(.2);
    const auto end = c_.execution.now() + 5;
    if (treasure_ && !findInteract(c_)) {
        c_.input.scroll(1, QPointF(c_.frame.cols * .5, c_.frame.rows * .5));
        c_.sleep(.2);
    }
    while (findInteract(c_) && !combat_.active() && c_.execution.now() < end) {
        c_.input.scroll(1, QPointF(c_.frame.cols * .5, c_.frame.rows * .5));
        c_.sleep(.2);
        c_.key("f");
        if (world_.dismissClaim())
            treasure_ = true;
    }
}
bool EchoFarm::treasureRestart() {
    if (!world_.treasure())
        return false;
    return world_.walkToBox([&] { return world_.treasure(); }, [&] { return bool(findInteract(c_)); }, 30,
                            .1);
}
void EchoFarm::treasureHunt() {
    if (!combat_.active() && treasureRestart()) {
        treasure_ = true;
        if (teleportEnabled() && realm_)
            enterFromF();
        else
            scrollInteract();
    }
}
void EchoFarm::changeNight() {
    c_.key("esc", 1);
    c_.clickRelative(.71, .96, 2);
    c_.clickRelative(.19, .14, 1);
    for (int i = 0; i < 3; ++i)
        c_.clickRelative(.82, .53, 1);
    c_.clickRelative(.52, .90, 6);
    c_.key("esc", 1);
    c_.key("esc", 1);
    lastNight_ = c_.execution.now();
}
void EchoFarm::interactions() {
    if (combat_.active())
        return;
    const auto boss = config_["Boss"].toString("Other");
    if (boss == "Lorelei" && c_.execution.now() - lastNight_ > 660)
        changeNight();
    if (boss == "Fallacy of No Return") {
        if (!findInteract(c_)) {
            nearestBoss();
            c_.key("d", .5, .25);
            c_.key("w", .5);
        }
        if (world_.walkToInteract("w", 20, true, 0, {}, true, true))
            scrollInteract();
        seekCombat(300);
    } else if (boss == "Fenrico") {
        for (int i = 0; findInteract(c_) && i < 20; ++i) {
            c_.sleep(1);
            if (world_.pick(true, true))
                ++drops_;
        }
        try {
            nearestBoss();
            c_.sleep(2);
        } catch (const NavigationFailure &) {
            navigation_.ensureMain();
        }
        seekCombat(5, true);
        treasureHunt();
        seekCombat(300);
    } else if (boss == "Lady of the Sea") {
        realm_ = true;
        if (!navigation_.inWorld()) {
            navigation_.exitRealm(120);
            c_.sleep(2);
        }
        if (navigation_.inWorld()) {
            world_.pick();
            c_.sleep(2);
            navigation_.waitTeam(120);
            c_.sleep(2);
            seekCombat(10);
        }
    } else if (boss == "Nameless Explorer") {
        if (world_.treasure() || findInteract(c_)) {
            if (treasureRestart())
                treasure_ = true;
            scrollInteract();
        }
        c_.sleep(1);
        if (!combat_.active()) {
            if (!treasure_) {
                nearestBoss();
                seekCombat(12, true, true);
            } else if (!seekCombat(8, true, true))
                world_.followBossMinimap(10);
            treasureHunt();
        }
        seekCombat(10);
    }
}
void EchoFarm::recognizeBoss() {
    if (c_.gameLanguage != "zh_CN")
        return;
    const auto box =
        Templates::anchored({1269, 10, 1264, 130}, {3840, 2160}, {c_.frame.cols, c_.frame.rows}, true);
    const auto lines = c_.text(box);
    for (const auto &entry : QVector<QPair<QString, QString>>{{"伪作的神王", "伪作的神王"},
                                                              {"异构武装", "异构武装|加尔古耶"},
                                                              {"荣耀狮像", "狮像|亚狮诺索"},
                                                              {"罗蕾莱", "罗蕾莱|夜之女皇"}})
        if (matchingText(lines, entry.second)) {
            aimBoss_ = entry.first;
            break;
        }
}
void EchoFarm::heal() {
    if (realm_) {
        if (!teleportEnabled())
            throw CharacterDead();
        navigation_.exitRealm(120);
        if (!entryAtHeal_)
            navigation_.healAtBeacon();
    } else {
        navigation_.healAtBeacon();
        world_.walkUntil([] { return false; }, "s", 1, true);
        nearestBoss();
        c_.sleep(.5);
        seekCombat(12, true, true);
        treasureHunt();
    }
}
int EchoFarm::weeklyMaterialCount() {
    navigation_.ensureMain();
    navigation_.openBook("gray_book_boss");
    navigation_.bookCategory("zhange");
    int remaining = 0;
    for (const auto &line : c_.text(.36, .12, .96, .18)) {
        const auto match = QRegularExpression("(\\d)\\s*/\\s*(\\d)").match(line.text);
        if (match.hasMatch()) {
            remaining = match.captured(1).toInt();
            break;
        }
    }
    const auto stamina = readStamina(c_);
    if (stamina.current < 0)
        fail("未确认周本体力，停止材料刷取");
    navigation_.ensureMain();
    return std::min(remaining, stamina.current / 60);
}
int EchoFarm::single() {
    const bool material = enhanced_ && config_["Advanced Skill Material Mode"].toBool();
    int total = config_["Repeat Farm Count"].toInt();
    if (material) {
        if (config_["Teleport to Boss"] != "Weekly Challenge")
            return 0;
        total = weeklyMaterialCount();
    }
    if (total <= 0)
        return 0;
    if (total > 10000000)
        fail("刷取次数超出上限");
    realm_ = navigation_.inRealm();
    parameters();
    started_ = c_.execution.now();
    treasure_ = false;
    entryAtHeal_ = false;
    if (teleportEnabled())
        teleport();
    int count = 0, recoveries = 0, travelRetries = 0;
    while (count < total) {
        try {
            checkRealm(60);
            interactions();
            if (justEntered_)
                justEntered_ = false;
            else if (!combat_.active()) {
                if (realm_ && !navigation_.inWorld()) {
                    restartBossDomain(c_);
                    c_.sleep(.1);
                } else {
                    if (treasure_)
                        c_.wait(
                            [&] {
                                return bool(world_.treasure()) || combat_.active() || bool(findInteract(c_));
                            },
                            5);
                    if (!combat_.active()) {
                        if (treasureRestart()) {
                            treasure_ = true;
                            if (teleportEnabled() && realm_)
                                enterFromF();
                            else
                                scrollInteract();
                        } else
                            scrollInteract();
                    }
                }
            }
            if (!realm_ && !treasure_ && !combat_.active()) {
                if (!world_.followBossMinimap()) {
                    nearestBoss();
                    c_.sleep(.5);
                    seekCombat(12, true, true);
                }
                treasureHunt();
            }
            c_.sleep(wait_);
            recognizeBoss();
            CombatOptions options;
            options.waitSeconds = 20;
            options.required = true;
            options.useLiberation = config_["Use Liberation"].toBool(true);
            options.recover = [&] { heal(); };
            combat_.combatOnce(options);
            bool dropped = world_.pick(true, true);
            if (!dropped) {
                const auto method = config_["Echo Pickup Method"].toString("Yolo");
                if (method == "Yolo")
                    dropped = world_.findEcho(false, realm_, realm_ ? 12 : 4, realm_ ? .25 : .65).first;
                else if (method == "Run in Circle")
                    dropped = world_.circleEcho(2);
                else
                    dropped = world_.walkToInteract("w", 3, false, 1, "吸收|Absorb", true) && world_.pick();
            }
            if (dropped)
                ++drops_;
            if (material) {
                bool confirmed = false;
                for (int attempt = 0; attempt < 2 && !confirmed; ++attempt) {
                    world_.walkToTreasure();
                    world_.pick(false);
                    c_.sleep(1);
                    std::optional<Match> claim;
                    if (c_.wait(
                            [&] {
                                claim = c_.find("gray_confirm_exit_button", {.threshold = .7});
                                return bool(claim);
                            },
                            3)) {
                        c_.sleep(.5);
                        c_.click(QPointF(claim->box.x() - claim->box.width(), claim->center().y()));
                        confirmed = true;
                    }
                }
                if (!confirmed)
                    fail("未确认周本材料领奖按钮，不重复消耗");
                awaitWeeklyMaterialReward(c_);
                c_.sleep(4);
                c_.clickRelative(realm_ ? (count + 1 < total ? .63 : .37) : .5, .85, 1);
                navigation_.waitTeam(120);
                // The reward screen's Continue already starts the next round.
                // Do not open the menu and restart that new round again.
                if (realm_ && count + 1 < total)
                    justEntered_ = true;
            }
            ++count;
            ++totalCompleted_;
            if (enhanced_)
                recoveries = 0;
            travelRetries = 0;
            if (c_.info) {
                c_.info("本副本轮数", QString("%1/%2").arg(count).arg(total));
                c_.info("声骸数量", QString::number(drops_));
            }
            if (!bypassWait_)
                c_.wait([&] { return combat_.active(); }, dropped && !treasure_ ? 5 : 1);
        } catch (const Cancelled &) {
            throw;
        } catch (const CharacterRevived &) {
            if (!teleportEnabled() || ++recoveries > (enhanced_ ? 3 : 2))
                throw;
            entryAtHeal_ = true;
            teleport();
        } catch (const NavigationFailure &) {
            if (!teleportEnabled() || walkResult_ != "combat" || ++travelRetries > 3)
                throw;
            teleport();
        }
    }
    if (material)
        navigation_.ensureMain(120);
    return count;
}
int EchoFarm::run(bool enhanced, const QJsonObject &supplied) {
    enhanced_ = enhanced;
    config_ = supplied.isEmpty() ? c_.profile.group(enhanced ? "FarmEchoTask2" : "FarmEchoTask") : supplied;
    TaskScope scope(c_, enhanced ? "FarmEchoTask2" : "FarmEchoTask", config_);
    totalCompleted_ = drops_ = 0;
    const bool rotation = config_["Farm Mode"].toString("Single Boss") == "Cycle All Dungeons";
    QVector<EchoTarget> targets;
    int per = 1, total = config_["Repeat Farm Count"].toInt();
    const auto countValue = config_["Repeat Farm Count"];
    if (!countValue.isDouble() || countValue.toDouble() != total || total < 0 || total > 100000)
        fail("刷取次数必须为 0 到 100000 的整数");
    if (total == 0 && !(enhanced_ && config_["Advanced Skill Material Mode"].toBool()))
        return 0;
    if (rotation) {
        if (config_["Advanced Skill Material Mode"].toBool())
            fail("循环刷声骸时请关闭技能进阶材料模式");
        const auto rounds = config_["Rounds per Dungeon"];
        per = rounds.toInt(1);
        if (total < 0 || per < 1 || per > 100000 || (rounds.isDouble() && rounds.toDouble() != per))
            fail("循环刷取次数无效");
        targets = echoRotationTargets(config_);
    }
    const auto oldCheck = combat_.onCombatCheck, oldEnd = combat_.combatEndCondition;
    const auto oldTimeout = combat_.targetEnemyTimeout;
    combat_.onCombatCheck = [&] {
        if (!realm_ && world_.pick())
            ++drops_;
        checkRealm(20);
        return true;
    };
    combat_.combatEndCondition = [&] { return !combat_.hasHealthBar() && !world_.echoes().empty(); };
    try {
        combat_.recoverStartingPopup();
        c_.sleep(.5);
        if (!rotation)
            single();
        else {
            int position = 0;
            while (totalCompleted_ < total) {
                const auto target = targets[position % targets.size()];
                const int rounds = std::min(per, total - totalCompleted_);
                config_["Teleport to Boss"] = target.category;
                config_["Repeat Farm Count"] = rounds;
                config_["Which Weekly Boss to Teleport"] = target.index;
                config_["Which Boss Challenge to Teleport"] = target.index;
                config_["Boss"] = "Other";
                c_.taskOverrides = config_;
                aimBoss_.clear();
                treatAsWorld_ = false;
                justEntered_ = false;
                if (c_.info)
                    c_.info(
                        "循环副本",
                        QString("%1 · %2；本副本 %3 轮").arg(target.category).arg(target.index).arg(rounds));
                if (single() != rounds)
                    throw NavigationFailure("当前副本轮数未完成，不跳到下一副本");
                ++position;
            }
        }
    } catch (...) {
        combat_.onCombatCheck = oldCheck;
        combat_.combatEndCondition = oldEnd;
        combat_.targetEnemyTimeout = oldTimeout;
        throw;
    }
    combat_.onCombatCheck = oldCheck;
    combat_.combatEndCondition = oldEnd;
    combat_.targetEnemyTimeout = oldTimeout;
    return totalCompleted_;
}
} // namespace okww::runtime
