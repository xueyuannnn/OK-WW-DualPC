QVector<TaskDefinition> reference_businessTasks(const std::function<bool(const QString &)> &dispatch) {
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
