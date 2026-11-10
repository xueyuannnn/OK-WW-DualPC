#include "task_registry.hpp"
#include "multi_account.hpp"
namespace okww::runtime {
QVector<TaskDefinition> businessTasks(const std::function<bool(const QString &)> &dispatch) {
    QVector<TaskDefinition> result;
    for (const auto *name : {"AutoCombatTask", "RemoteMouseResetTask", "DailyTask", "DailyTask2",
                             "FarmEchoTask", "FarmEchoTask2", "NightmareNestTask", "TacetTask", "TacetTask2",
                             "ForgeryTask", "ForgeryTask2", "SimulationTask", "SimulationTask2",
                             "MergeEchoTask", "EnhanceEchoTask", "ChangeEchoTask", "GardenTask", "MultiAccountDailyTask"}) {
        const QString id(name);
        result.append({id, id == "AutoCombatTask" || id == "RemoteMouseResetTask", .1,
                       [dispatch, id](TaskContext &) { return dispatch(id); }});
    }
    return result;
}
bool TaskRunner::dispatch(const QString &id) {
    if (id == "RemoteMouseResetTask")
        // Deliberately matches dualpc.task_hooks.RemoteMouseResetTask: local
        // cursor polling is disabled; every remote coordinate click is homed
        // by Input::move according to the selected box's settings.
        return false;
    if (id == "AutoCombatTask") {
        try {
            return combat_.runAutoCombat();
        } catch (const CharacterDead &) {
            const auto enabled = c_.profile.value("Task Recovery", "Resume Interrupted Tasks", true).toBool();
            const int limit =
                std::clamp(c_.profile.value("Task Recovery", "Recovery Retry Limit", 3).toInt(), 1, 10);
            if (c_.execution.now() - recoveredAt_ > 300)
                deathRecoveries_ = 0;
            if (!enabled || ++deathRecoveries_ > limit)
                throw;
            combat_.recoverStartingPopup();
            recoveredAt_ = c_.execution.now();
            if (c_.info)
                c_.info("自动战斗恢复", "已免费回血，继续等待进入战斗");
            return true;
        }
    }
    if (id == "MultiAccountDailyTask")
        MultiAccountDaily(c_).run([&] { Daily(c_, combat_, detector_, root_, data_).run(); });
    else if (id == "DailyTask" || id == "DailyTask2")
        Daily(c_, combat_, detector_, root_, data_).run(id.endsWith('2'));
    else if (id == "FarmEchoTask" || id == "FarmEchoTask2")
        EchoFarm(c_, combat_, detector_).run(id.endsWith('2'));
    else if (id == "NightmareNestTask")
        NightmareFarm(c_, combat_, detector_).run();
    else if (id.startsWith("TacetTask"))
        DomainFarm(c_, combat_).run(DomainKind::Tacet, id.endsWith('2'));
    else if (id.startsWith("ForgeryTask"))
        DomainFarm(c_, combat_).run(DomainKind::Forgery, id.endsWith('2'));
    else if (id.startsWith("SimulationTask"))
        DomainFarm(c_, combat_).run(DomainKind::Simulation, id.endsWith('2'));
    else if (id == "GardenTask")
        Garden(c_, root_).run();
    else if (id == "MergeEchoTask")
        EchoOperations(c_, data_ + "/screenshots").merge();
    else if (id == "EnhanceEchoTask")
        EchoOperations(c_, data_ + "/screenshots").enhance();
    else if (id == "ChangeEchoTask")
        EchoOperations(c_, data_ + "/screenshots").change();
    else
        fail("未注册原生业务任务：" + id);
    return true;
}
} // namespace okww::runtime
