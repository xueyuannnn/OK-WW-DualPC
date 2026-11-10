#include "nightmare.hpp"
namespace okww::runtime {
namespace {
const QStringList travelFeatures{"fast_travel_custom", "gray_teleport", "remove_custom"};
const QStringList confirms{"confirm_btn_hcenter_vcenter", "confirm_btn_highlight_hcenter_vcenter"};
} // namespace
std::optional<NestTarget> nextNestTarget(const QVector<TextLine> &lines, QSize screen, int tab,
                                         const QSet<QString> &unreachable) {
    const QRegularExpression count("(\\d{1,2})/(\\d{1,2})");
    const QStringList names{"go_nest", "go_nest_scroll", "go_nightmare", "go_nightmare_scroll"};
    if (tab < 0 || tab >= names.size() || screen.height() <= 0)
        fail("巢穴列表参数无效");
    for (const auto &line : lines) {
        auto matches = count.globalMatch(line.text);
        while (matches.hasNext()) {
            const auto match = matches.next();
            if (match.captured(1) != "0" || !QStringList{"24", "36", "48", "41"}.contains(match.captured(2)))
                continue;
            const auto key = QString("%1:%2:%3")
                                 .arg(names[tab], match.captured(2))
                                 .arg(pixelRound(line.center().y() / screen.height() / .02));
            if (!unreachable.contains(key))
                return NestTarget{{screen.width() * .9, line.box.y() - line.box.height() * .9}, key, tab};
        }
    }
    return {};
}
void NightmareFarm::tab(int index) {
    navigation_.bookCategory(index < 2 ? "canxiang" : "mengyan");
    if (index == 1)
        c_.clickRelative(.973, .8806, 1);
    if (index == 3)
        c_.clickRelative(3737. / 3840, .54, 1);
}
bool NightmareFarm::travel(const NestTarget &target) {
    std::optional<Match> button;
    c_.wait(
        [&] {
            button = c_.findAny(travelFeatures, {.threshold = .7});
            return bool(button);
        },
        1);
    if (button) {
        c_.click(*button, 1);
        if (auto confirm = c_.findAny(confirms, {.threshold = .6}))
            c_.click(*confirm, 1);
    }
    if (button && !c_.find(button->name, {.threshold = .7}) && navigation_.waitTeam(120, false))
        return true;
    unreachable_.insert(target.key);
    if (c_.info)
        c_.info("巢穴跳过", "本次无法到达：" + target.key);
    c_.key("esc", 1);
    return false;
}
bool NightmareFarm::capturedNotice() {
    const auto search = gameBox(c_.frame, .078, .488, .094, .514);
    for (const auto &name : {"char_1_text", "char_3_text"})
        if (c_.find(name, {.threshold = .6, .search = search, .filter = Filter::Negative80}))
            captured_ = true;
    return captured_;
}
bool NightmareFarm::waitCombat(double timeout) {
    double last = -1;
    return c_.wait(
        [&] {
            if (combat_.active())
                return true;
            if (c_.execution.now() - last >= .5) {
                c_.input.click({}, .01, "middle");
                last = c_.execution.now();
            }
            return false;
        },
        timeout, .02);
}
bool NightmareFarm::walkEcho() {
    return world_.walkToInteract("w", 5, false, 2.5, "吸收|Absorb", true) && world_.pick();
}
void NightmareFarm::fight(const NestTarget &target) {
    c_.click(target.point, 2);
    std::optional<Match> feature;
    if (!c_.wait(
            [&] {
                feature = c_.findAny({"fast_travel_custom", "gray_teleport", "remove_custom", "team_close"});
                return bool(feature);
            },
            10))
        throw NavigationFailure("巢穴前往后未识别到传送或配队界面");
    c_.sleep(.5);
    const bool team = feature->name == "team_close";
    if (team) {
        navigation_.startChallenge();
        navigation_.waitTeam(120);
    } else {
        if (!travel(target))
            return;
        c_.sleep(1);
        int prompts = 0;
        while (findInteract(c_)) {
            if (++prompts > 12)
                throw NavigationFailure("巢穴入口交互未结束");
            c_.key("f", 1);
            navigation_.waitTeam(40, false);
        }
        c_.sleep(2);
        double last = -1;
        world_.walkUntil(
            [&] {
                if (combat_.active())
                    return true;
                if (c_.execution.now() - last >= .5) {
                    c_.input.click({}, .01, "middle");
                    last = c_.execution.now();
                }
                return false;
            },
            "w", 10);
    }
    double wait = 10;
    // Bounded waves prevent a broken completion screen from farming forever.
    for (int wave = 0; wave < 30; ++wave) {
        bool needFind = false;
        try {
            needFind = combat_.combatOnce({.waitSeconds = wait, .required = false, .target = true});
        } catch (const CharacterRevived &) {
            const int deaths = ++deaths_[target.key];
            if (deaths >= 3)
                unreachable_.insert(target.key);
            if (c_.info)
                c_.info("巢穴恢复", QString("%1；本次死亡 %2 次").arg(target.key).arg(deaths));
            return;
        }
        const bool early = capture_ && (captured_ || c_.wait([&] { return capturedNotice(); }, 3));
        if (!early) {
            c_.sleep(3);
            bool dropped = needFind && walkEcho();
            if (needFind && !dropped) {
                dropped = world_.findEcho(false, true, 30).first;
                if (!dropped && !team) {
                    navigation_.ensureMain();
                    navigation_.openBook("gray_book_boss");
                    tab(target.tab);
                    c_.sleep(1);
                    c_.click(target.point, 2);
                    if (c_.wait([&] { return bool(c_.findAny(travelFeatures)); }, 5) && travel(target)) {
                        c_.sleep(2);
                        world_.walkUntil([] { return false; }, "w", 2, true);
                        dropped = walkEcho() || world_.findEcho(false, true, 30).first;
                    }
                }
            }
            // Unlike the old `else: dropped=True`, a missing fight is not an
            // observed capture. Daily capture mode must actually obtain an echo.
            captured_ |= dropped;
        }
        if (capture_ || !waitCombat(3)) {
            if (team)
                leaveNest(c_);
            c_.sleep(1);
            return;
        }
        wait = 1;
    }
    throw NavigationFailure("巢穴连续战斗超过上限，未记录为完成");
}
bool NightmareFarm::run(bool captureOnly, const QJsonObject &supplied) {
    TaskScope scope(c_, "NightmareNestTask", supplied);
    capture_ = captureOnly;
    captured_ = false;
    unreachable_.clear();
    deaths_.clear();
    const auto oldCheck = combat_.onCombatCheck;
    combat_.onCombatCheck = [&] {
        if (capture_) {
            world_.pick(false);
            if (capturedNotice())
                return false;
        }
        return true;
    };
    try {
        if (!navigation_.inWorld() && completedNest(c_))
            leaveNest(c_);
        combat_.recoverStartingPopup();
        navigation_.ensureMain();
        auto config = supplied.isEmpty() ? c_.profile.group("NightmareNestTask") : supplied;
        auto choices = config["Which to Farm"].toArray();
        if (choices.empty())
            choices = {"Nightmare Purification", "Tacet Discord Nest"};
        QVector<int> queue;
        if (choices.contains("Tacet Discord Nest"))
            queue += {0, 1};
        if (choices.contains("Nightmare Purification"))
            queue += {2, 3};
        QMap<QString, int> attempts;
        while (!queue.empty()) {
            navigation_.openBook("gray_book_boss");
            tab(queue.front());
            const auto target = nextNestTarget(c_.text(.35, .25, 1, .96), {c_.frame.cols, c_.frame.rows},
                                               queue.front(), unreachable_);
            if (!target) {
                queue.removeFirst();
                continue;
            }
            if (++attempts[target->key] > 4) {
                unreachable_.insert(target->key);
                continue;
            }
            fight(*target);
            if (capture_ && captured_)
                break;
        }
        navigation_.ensureMain();
    } catch (...) {
        combat_.onCombatCheck = oldCheck;
        throw;
    }
    combat_.onCombatCheck = oldCheck;
    return captured_;
}
} // namespace okww::runtime
