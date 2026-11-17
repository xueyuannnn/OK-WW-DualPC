{
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
