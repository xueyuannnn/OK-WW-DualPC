#include "task_registry.hpp"
#include "multi_account.hpp"
#include "instance_flow.hpp"
#include "task_registry_abi.hpp"
#include "workflow_abi.hpp"
#include "scripts.hpp"
namespace okww::runtime {
QVector<TaskDefinition> businessTasks(const std::function<bool(const QString &)> &dispatch) {
    const QStringList names{"AutoCombatTask",  "RemoteMouseResetTask", "DailyTask",         "DailyTask2",
                            "FarmEchoTask",    "FarmEchoTask2",        "NightmareNestTask", "TacetTask",
                            "TacetTask2",      "ForgeryTask",          "ForgeryTask2",      "SimulationTask",
                            "SimulationTask2", "MergeEchoTask",        "EnhanceEchoTask",   "ChangeEchoTask",
                            "GardenTask",      "MultiAccountDailyTask", "GalGameTask", "RahaiBlocksTask"};
    QString current;
    QVector<TaskDefinition> result;
    rust::workflowRun(
        rust::WorkflowMethod::BusinessTasks, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::BuiltinTaskCount:
                out[0] = double(names.size());
                break;
            case rust::WorkflowOp::LoadBuiltinTask:
                current = names.at(int(a[0]));
                out[0] = businessTaskKind(current);
                break;
            case rust::WorkflowOp::AppendBuiltinTask: {
                const auto id = current;
                result.append({id, bool(a[0]), a[1], [dispatch, id](TaskContext &) { return dispatch(id); }});
                break;
            }
            default:
                fail("Rust 内置任务目录宿主无效");
            }
        });
    return result;
}
bool TaskRunner::dispatch(const QString &id) {
    const std::array args{double(businessTaskKind(id))};
    const auto result = rust::workflowRun(
        rust::WorkflowMethod::DispatchBusinessTask, args,
        [&](rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &error) {
            switch (op) {
            case rust::WorkflowOp::TaskAutoCloseIntro:
                out[0] = c_.profile.value("AutoCombatTask", "Auto Close Abyss Character Intro", true).toBool();
                break;
            case rust::WorkflowOp::TaskPartyVisible:
                out[0] = c_.inTeam();
                break;
            case rust::WorkflowOp::TaskIntroScanAt:
                out[0] = introScanAt_;
                break;
            case rust::WorkflowOp::TaskSetIntroScanAt:
                introScanAt_ = a[0];
                break;
            case rust::WorkflowOp::TaskCombatIntroVisible:
                out[0] = combatIntroVisible(c_);
                break;
            case rust::WorkflowOp::TaskCloseCombatIntro:
                c_.clickRelative(.942, .058, .5);
                break;
            case rust::WorkflowOp::RunAutomaticCombat:
                out[0] = combat_.runAutoCombat();
                break;
            case rust::WorkflowOp::TaskErrorCharacterDead:
                try {
                    if (error)
                        std::rethrow_exception(error);
                    out[0] = false;
                } catch (const CharacterDead &) {
                    out[0] = true;
                } catch (...) {
                    out[0] = false;
                }
                break;
            case rust::WorkflowOp::TaskDeathRecoveryEnabled:
                out[0] = c_.profile.value("Task Recovery", "Resume Interrupted Tasks", true).toBool();
                break;
            case rust::WorkflowOp::TaskDeathRecoveryLimit:
                out[0] = c_.profile.value("Task Recovery", "Recovery Retry Limit", 3).toInt();
                break;
            case rust::WorkflowOp::Now:
                out[0] = c_.execution.now();
                break;
            case rust::WorkflowOp::TaskLastDeathRecovery:
                out[0] = recoveredAt_;
                break;
            case rust::WorkflowOp::TaskSetDeathRecoveries:
                deathRecoveries_ = int(a[0]);
                break;
            case rust::WorkflowOp::TaskDeathRecoveryCount:
                out[0] = deathRecoveries_;
                break;
            case rust::WorkflowOp::TaskRecoverStartingPopup:
                combat_.recoverStartingPopup();
                break;
            case rust::WorkflowOp::TaskStoreRecoveryTime:
                recoveredAt_ = a[0];
                break;
            case rust::WorkflowOp::TaskHasInfo:
                out[0] = bool(c_.info);
                break;
            case rust::WorkflowOp::TaskReportCombatRecovery:
                c_.info("自动战斗恢复", "已免费回血，继续等待进入战斗");
                break;
            case rust::WorkflowOp::TaskEnhancedSuffix:
                out[0] = id.endsWith('2');
                break;
            case rust::WorkflowOp::DispatchMultiAccountDaily: {
                MultiAccountDaily accounts(c_);
                accounts.run([&] {
                    Daily(c_, combat_, detector_, root_, data_).run(false, accountDailyOverrides(c_.profile, accounts.activeAccount()), accounts.activeAccount());
                });
                break;
            }
            case rust::WorkflowOp::DispatchDaily:
                Daily(c_, combat_, detector_, root_, data_).run(bool(a[0]));
                break;
            case rust::WorkflowOp::DispatchEchoFarm:
                EchoFarm(c_, combat_, detector_).run(bool(a[0]));
                break;
            case rust::WorkflowOp::DispatchNightmare:
                NightmareFarm(c_, combat_, detector_).run();
                break;
            case rust::WorkflowOp::DispatchDomain:
                DomainFarm(c_, combat_).run(DomainKind(int(a[0])), bool(a[1]));
                break;
            case rust::WorkflowOp::DispatchGarden:
                Garden(c_, root_).run();
                break;
            case rust::WorkflowOp::DispatchPythonActivity: {
                ScriptRuntime activity(c_, root_, data_, true);
                activity.builtin = [&](const QString &task) { return dispatch(task); };
                activity.run({{"id", "activity:" + id}, {"fields", QJsonArray{}}});
                break;
            }
            case rust::WorkflowOp::DispatchMergeEcho:
                EchoOperations(c_, data_ + "/screenshots").merge();
                break;
            case rust::WorkflowOp::DispatchEnhanceEcho:
                EchoOperations(c_, data_ + "/screenshots").enhance();
                break;
            case rust::WorkflowOp::DispatchChangeEcho:
                EchoOperations(c_, data_ + "/screenshots").change();
                break;
            case rust::WorkflowOp::UnknownBusinessTask:
                fail("未注册原生业务任务：" + id);
                break;
            default:
                fail("Rust 任务分派宿主无效");
            }
        });
    return result[0] != 0;
}
} // namespace okww::runtime
