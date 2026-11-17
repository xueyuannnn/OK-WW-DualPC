#include "domain.hpp"
#include "workflow_abi.hpp"
namespace okww::runtime {
namespace {
[[noreturn]] void domainError(int code) {
    switch (code) {
    case 100:
        fail("副本次数或体力消耗无效");
    case 101:
        fail("未读到体力数值");
    case 102:
        fail("刷取体力额度无效");
    case 103:
        fail("进入副本前未读到体力数值");
    case 104:
        throw NavigationFailure("连续未识别到无音区体力领奖界面");
    case 105:
        throw NavigationFailure("副本已连续恢复 3 次，未完成剩余额度");
    default:
        fail("副本错误编号无效");
    }
}
} // namespace
int domainCost(DomainKind kind) {
    const std::array args{double(int(kind))};
    return int(rust::workflowRun(
        rust::WorkflowMethod::DomainCost, args,
        [](rust::WorkflowOp, const double *, double *) { fail("副本消耗宿主操作无效"); })[0]);
}
int domainCountLimit(int requested, int total, int cost) {
    const std::array args{double(requested), double(total), double(cost)};
    return int(rust::workflowRun(rust::WorkflowMethod::DomainCountLimit, args,
                                 [](rust::WorkflowOp op, const double *a, double *) {
                                     if (op == rust::WorkflowOp::Throw)
                                         domainError(int(a[0]));
                                     else
                                         fail("副本次数宿主操作无效");
                                 })[0]);
}
FarmResult DomainFarm::rustRun(int method, std::initializer_list<double> args, QJsonObject config) {
    FarmResult result;
    std::optional<TaskScope> scope;
    std::optional<Match> confirm;
    rust::workflowRun(
        rust::WorkflowMethod(method), std::span<const double>(args.begin(), args.size()),
        [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::Now: {
                out[0] = c_.execution.now();
                break;
            }
            case rust::WorkflowOp::Checkpoint: {
                c_.execution.checkpoint();
                break;
            }
            case rust::WorkflowOp::ExecutionWait: {
                c_.execution.wait(a[0]);
                break;
            }
            case rust::WorkflowOp::Refresh: {
                c_.refresh();
                break;
            }
            case rust::WorkflowOp::Sleep: {
                c_.sleep(a[0]);
                break;
            }
            case rust::WorkflowOp::InvalidCondition: {
                fail("等待条件参数无效");
                break;
            }
            case rust::WorkflowOp::ClickRelative: {
                c_.clickRelative(a[0], a[1], a[2]);
                break;
            }
            case rust::WorkflowOp::Key: {
                c_.key("esc", a[1]);
                break;
            }
            case rust::WorkflowOp::SkipConfirm: {
                out[0] = c_.skipConfirm();
                break;
            }
            case rust::WorkflowOp::NavCategory: {
                const QStringList names{"wuyin", "ningsu", "moni"};
                navigation_.bookCategory(names.at(int(a[0])));
                break;
            }
            case rust::WorkflowOp::FarmEntrySerial: {
                const QStringList keys{"Which Tacet Suppression to Farm", "Which Forgery Challenge to Farm"};
            out[0] = std::as_const(config)[keys.at(int(a[0]))].toInt(1);
                break;
            }
            case rust::WorkflowOp::NavFarmEntry: {
                const QVector<QVector<int>> structures{{4, 5, 5, 7}, {5, 5, 5, 5}};
                const int totals[]{21, 20};
                navigation_.selectBookEntry(int(a[1]), totals[int(a[0])], structures.at(int(a[0])));
                break;
            }
            case rust::WorkflowOp::MaterialKind: {
            const auto selection = std::as_const(config)["Material Selection"].toString("Shell Credit");
                const int index = QStringList{"Resonator EXP", "Weapon EXP"}.indexOf(selection);
                out[0] = index < 0 ? 2 : index;
                break;
            }
            case rust::WorkflowOp::NavStartChallenge: {
                navigation_.startChallenge();
                break;
            }
            case rust::WorkflowOp::NavWaitTeam: {
                navigation_.waitTeam(a[0]);
                break;
            }
            case rust::WorkflowOp::NavEnsureMain: {
                navigation_.ensureMain(a[0]);
                break;
            }
            case rust::WorkflowOp::NavConfirm: {
                navigation_.confirmDialog(a[0] != 0);
                break;
            }
            case rust::WorkflowOp::ClickDomainAnchor: {
                const QRect anchors[]{{1229, 919, 1, 1}, {701, 921, 1, 1}};
                const auto point = Templates::anchored(anchors[int(a[0])], {1920, 1080},
                                                       {c_.frame.cols, c_.frame.rows}, true)
                                       .topLeft();
                c_.click(point, a[1]);
                break;
            }
            case rust::WorkflowOp::FindFarmConfirm: {
                confirm = c_.findAny({"confirm_btn_hcenter_vcenter", "confirm_btn_highlight_hcenter_vcenter"},
                                     {.threshold = a[0]});
                out[0] = confirm.has_value();
                break;
            }
            case rust::WorkflowOp::ClickFarmConfirm: {
                if (a[0] == 0)
                    c_.click(*confirm, a[1]);
                else
                    c_.click(QPointF(confirm->box.x() - confirm->box.width(), confirm->center().y()), a[1]);
                break;
            }
            case rust::WorkflowOp::DomainTaskScope: {
                const QStringList ids{"TacetTask", "ForgeryTask", "SimulationTask"};
                const int k = int(a[0]);
                const QString id = ids.at(k == 0 ? 0 : k == 1 ? 1 : 2) + (a[1] != 0 ? "2" : "");
                if (a[2] != 0)
                    scope.emplace(c_, id, config);
                else
                    scope.emplace(c_, id);
                break;
            }
            case rust::WorkflowOp::RecoverStartingPopup: {
                combat_.recoverStartingPopup();
                break;
            }
            case rust::WorkflowOp::ReadFarmConfig: {
                config = c_.profile.group(c_.task);
                break;
            }
            case rust::WorkflowOp::FarmRequested: {
                const QStringList keys{"Tacet Suppression Count", "Forgery Challenge Count",
                                       "Simulation Challenge Count"};
                const int k = int(a[0]);
            out[0] = std::as_const(config)[keys.at(k == 0 ? 0 : k == 1 ? 1 : 2)].toInt();
                break;
            }
            case rust::WorkflowOp::OpenBossBook: {
                navigation_.openBook("gray_book_boss");
                break;
            }
            case rust::WorkflowOp::ReadStamina: {
                const auto stamina = readStamina(c_);
                out[0] = stamina.current;
                out[1] = stamina.reserve;
                out[2] = stamina.total;
                break;
            }
            case rust::WorkflowOp::StoreFarmResult: {
                result = {int(a[0]), int(a[1]), int(a[2]), a[3] != 0};
                break;
            }
            case rust::WorkflowOp::CallFarm: {
                result = farm(DomainKind(int(a[0])), int(a[1]), a[2] != 0, config);
                break;
            }
            case rust::WorkflowOp::FarmProgress: {
                const QStringList labels{"刷取无音区", "刷取凝素领域", "刷取模拟领域"};
                const int k = int(a[0]);
                const auto label = labels.at(k == 0 ? 0 : k == 1 ? 1 : 2);
                if (a[3] < 0)
                    c_.reportProgress(label, a[1], a[2], "体力");
                else
                    c_.reportProgress(label, a[1], a[2], "体力", QString("已完成 %1 轮").arg(int(a[3])));
                break;
            }
            case rust::WorkflowOp::WalkToFarmInteract: {
                world_.walkToInteract("w", 4, true);
                break;
            }
            case rust::WorkflowOp::FarmPick: {
                world_.pick(a[0] != 0);
                break;
            }
            case rust::WorkflowOp::CombatFarmRound: {
                CombatOptions options;
                options.target = a[0] != 0;
                options.realmEntryAtHealPoint = a[1] != 0;
                combat_.combatOnce(options);
                break;
            }
            case rust::WorkflowOp::WalkToTreasure: {
                world_.walkToTreasure();
                break;
            }
            case rust::WorkflowOp::ClaimSign: {
                out[0] = bool(c_.find("claim_stamina_sign"));
                break;
            }
            case rust::WorkflowOp::ClaimFarmStamina: {
                const auto spend = claimStamina(c_, int(a[0]), int(a[1]), a[2] != 0);
                out[0] = spend.used;
                out[1] = spend.repeat;
                break;
            }
            case rust::WorkflowOp::HasStaminaCallback: {
                out[0] = bool(onStaminaSpent);
                break;
            }
            case rust::WorkflowOp::StaminaCallback: {
                onStaminaSpent(int(a[0]));
                break;
            }
            case rust::WorkflowOp::HasInfo: {
                out[0] = bool(c_.info);
                break;
            }
            case rust::WorkflowOp::FarmSpentInfo: {
                c_.info("已消耗体力", QString::number(int(a[0])));
                break;
            }
            case rust::WorkflowOp::FarmRecoveryInfo: {
                c_.info("副本恢复", QString("第 %1 次；重新进入后继续剩余额度").arg(int(a[0])));
                break;
            }
            case rust::WorkflowOp::Throw: {
                domainError(int(a[0]));
                break;
            }
            default:
                fail("Rust 副本宿主操作无效");
            }
        });
    return result;
}
void DomainFarm::enter(DomainKind kind, const QJsonObject &config) {
    rustRun(int(rust::WorkflowMethod::DomainEnter), {double(int(kind))}, config);
}
void DomainFarm::nextRound(DomainKind kind) {
    rustRun(int(rust::WorkflowMethod::DomainNext), {double(int(kind))});
}
void DomainFarm::leave(DomainKind kind) {
    rustRun(int(rust::WorkflowMethod::DomainLeave), {double(int(kind))});
}
FarmResult DomainFarm::run(DomainKind kind, bool counted) {
    return rustRun(int(rust::WorkflowMethod::DomainRun), {double(int(kind)), double(counted)});
}
FarmResult DomainFarm::farm(DomainKind kind, int mustUse, bool exact, const QJsonObject &config) {
    return rustRun(int(rust::WorkflowMethod::DomainFarm), {double(int(kind)), double(mustUse), double(exact)},
                   config);
}
} // namespace okww::runtime
