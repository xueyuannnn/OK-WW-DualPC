#include "domain.hpp"
namespace okww::runtime {
int domainCost(DomainKind kind) { return kind == DomainKind::Tacet ? 60 : 40; }
int domainCountLimit(int requested, int total, int cost) {
    if (requested < 0 || requested > 100000 || cost <= 0)
        fail("副本次数或体力消耗无效");
    if (total < 0)
        fail("未读到体力数值");
    return std::min(requested, total / cost);
}
void DomainFarm::enter(DomainKind kind, const QJsonObject &config) {
    if (kind == DomainKind::Tacet) {
        navigation_.bookCategory("wuyin");
        navigation_.selectBookEntry(config["Which Tacet Suppression to Farm"].toInt(1), 21, {4, 5, 5, 7});
    } else if (kind == DomainKind::Forgery) {
        navigation_.bookCategory("ningsu");
        navigation_.selectBookEntry(config["Which Forgery Challenge to Farm"].toInt(1), 20, {5, 5, 5, 5});
        c_.clickRelative(.891, .910, 1);
    } else {
        navigation_.bookCategory("moni");
        const auto selection = config["Material Selection"].toString("Shell Credit");
        const int index = selection == "Resonator EXP" ? 0 : selection == "Weapon EXP" ? 1 : 2;
        c_.clickRelative(.973, .8806, 1);
        c_.clickRelative(.898, .533 + index * .14, 1);
        c_.clickRelative(.93, .90, 1);
    }
    navigation_.startChallenge();
    navigation_.waitTeam(kind == DomainKind::Tacet ? 120 : 100);
    c_.sleep(1);
}
void DomainFarm::nextRound(DomainKind kind) {
    if (kind == DomainKind::Tacet) {
        const auto point =
            Templates::anchored({1229, 919, 1, 1}, {1920, 1080}, {c_.frame.cols, c_.frame.rows}, true)
                .topLeft();
        c_.click(point, .2);
        c_.wait([&] { return c_.skipConfirm(); }, 3);
    } else {
        c_.clickRelative(.68, .84, 1);
        std::optional<Match> confirm;
        if (c_.wait(
                [&] {
                    confirm =
                        c_.findAny({"confirm_btn_hcenter_vcenter", "confirm_btn_highlight_hcenter_vcenter"},
                                   {.threshold = .6});
                    return bool(confirm);
                },
                2)) {
            c_.clickRelative(.49, .55, .5);
            c_.click(*confirm, .5);
            if (c_.wait(
                    [&] {
                        confirm = c_.findAny(
                            {"confirm_btn_hcenter_vcenter", "confirm_btn_highlight_hcenter_vcenter"},
                            {.threshold = .6});
                        return bool(confirm);
                    },
                    1))
                c_.click(QPointF(confirm->box.x() - confirm->box.width(), confirm->center().y()), .5);
        }
    }
    navigation_.waitTeam(120);
    c_.sleep(1);
}
void DomainFarm::leave(DomainKind kind) {
    if (kind == DomainKind::Tacet) {
        const auto point =
            Templates::anchored({701, 921, 1, 1}, {1920, 1080}, {c_.frame.cols, c_.frame.rows}, true)
                .topLeft();
        c_.click(point, 2);
    } else
        c_.clickRelative(.42, .84, 2);
    navigation_.ensureMain(120);
}
FarmResult DomainFarm::run(DomainKind kind, bool counted) {
    const auto id = kind == DomainKind::Tacet     ? "TacetTask"
                    : kind == DomainKind::Forgery ? "ForgeryTask"
                                                  : "SimulationTask";
    TaskScope scope(c_, QString(id) + (counted ? "2" : ""));
    combat_.recoverStartingPopup();
    navigation_.ensureMain(180);
    c_.sleep(.5);
    const auto config = c_.profile.group(c_.task);
    int mustUse = 0;
    if (counted) {
        const auto key = kind == DomainKind::Tacet     ? "Tacet Suppression Count"
                         : kind == DomainKind::Forgery ? "Forgery Challenge Count"
                                                       : "Simulation Challenge Count";
        const int requested = config[key].toInt();
        if (requested <= 0)
            return {};
        navigation_.openBook("gray_book_boss");
        const auto stamina = readStamina(c_);
        mustUse = domainCountLimit(requested, stamina.total, domainCost(kind)) * domainCost(kind);
        navigation_.ensureMain();
        if (mustUse == 0)
            return {.noStamina = true};
    }
    return farm(kind, mustUse, counted, config);
}
FarmResult DomainFarm::farm(DomainKind kind, int mustUse, bool exact, const QJsonObject &config) {
    const auto id = kind == DomainKind::Tacet     ? "TacetTask"
                    : kind == DomainKind::Forgery ? "ForgeryTask"
                                                  : "SimulationTask";
    TaskScope scope(c_, QString(id) + (exact ? "2" : ""), config);
    if (mustUse < 0 || mustUse > 24000000)
        fail("刷取体力额度无效");
    FarmResult result;
    if (exact && mustUse == 0)
        return result;
    const int once = domainCost(kind);
    bool entryAtHeal = false;
    while (true) {
        c_.execution.checkpoint();
        navigation_.openBook("gray_book_boss");
        auto stamina = readStamina(c_);
        if (stamina.current < 0 && kind == DomainKind::Tacet) {
            c_.clickRelative(.04, .4, 1);
            stamina = readStamina(c_);
        }
        if (stamina.current < 0)
            fail("进入副本前未读到体力数值");
        if (stamina.total < once || (kind != DomainKind::Tacet &&
                                     (stamina.total < mustUse || (mustUse == 0 && stamina.current < once)))) {
            result.noStamina = true;
            navigation_.ensureMain();
            return result;
        }
        enter(kind, config);
        try {
            int wrongRewards = 0;
            while (true) {
                if (kind != DomainKind::Tacet) {
                    world_.walkToInteract("w", 4, true);
                    world_.pick();
                }
                CombatOptions options;
                options.target = kind == DomainKind::Tacet;
                options.realmEntryAtHealPoint = entryAtHeal;
                combat_.combatOnce(options);
                if (kind != DomainKind::Tacet)
                    c_.sleep(3);
                world_.walkToTreasure();
                world_.pick(false);
                if (kind == DomainKind::Tacet) {
                    c_.sleep(2);
                    if (!c_.find("claim_stamina_sign")) {
                        c_.key("esc", 1);
                        navigation_.confirmDialog(false);
                        navigation_.waitTeam(120);
                        if (++wrongRewards >= 3)
                            throw NavigationFailure("连续未识别到无音区体力领奖界面");
                        continue;
                    }
                }
                const auto spend = claimStamina(c_, once, mustUse, exact);
                result.staminaUsed += spend.used;
                mustUse = std::max(0, mustUse - spend.used);
                if (spend.used > 0 && onStaminaSpent)
                    onStaminaSpent(spend.used);
                if (spend.used > 0)
                    ++result.completed;
                if (c_.info)
                    c_.info("已消耗体力", QString::number(result.staminaUsed));
                c_.sleep(4);
                if (!spend.repeat) {
                    result.noStamina = spend.used == 0;
                    leave(kind);
                    return result;
                }
                nextRound(kind);
            }
        } catch (const CharacterRevived &) {
            if (++result.recoveries >= 3) {
                navigation_.ensureMain(120);
                throw NavigationFailure("副本已连续恢复 3 次，未完成剩余额度");
            }
            entryAtHeal = true;
            if (c_.info)
                c_.info("副本恢复", QString("第 %1 次；重新进入后继续剩余额度").arg(result.recoveries));
            c_.sleep(1);
        }
    }
}
} // namespace okww::runtime
