// Typed SDK bindings exercised against original C++ policy.
case rust::WorkflowOp::ApplyEchoRotationTarget: {
    config_["Teleport to Boss"] = target.category;
    config_["Repeat Farm Count"] = int(a[0]);
    config_["Which Weekly Boss to Teleport"] = target.index;
    config_["Which Boss Challenge to Teleport"] = target.index;
    config_["Boss"] = "Other";
    c_.taskOverrides = config_;
    aimBoss_.clear();
    treatAsWorld_ = false;
    justEntered_ = false;
    break;
}
case rust::WorkflowOp::CaptureEchoCallbacks: {
    oldCheck = combat_.onCombatCheck;
    oldEnd = combat_.combatEndCondition;
    oldTimeout = combat_.targetEnemyTimeout;
    break;
}
case rust::WorkflowOp::CheckEchoRealm: {
    checkRealm(a[0]);
    break;
}
case rust::WorkflowOp::CopyEchoSuppliedConfig: {
    config_ = supplied;
    break;
}
case rust::WorkflowOp::CreateEchoTaskScope: {
    scope.emplace(c_, a[0] != 0 ? "FarmEchoTask2" : "FarmEchoTask", config_);
    break;
}
case rust::WorkflowOp::EchoDetectedDrops: {
    out[0] = !world_.echoes().empty();
    break;
}
case rust::WorkflowOp::EchoEnhanced: {
    out[0] = enhanced_;
    break;
}
case rust::WorkflowOp::EchoHasHealthBar: {
    out[0] = combat_.hasHealthBar();
    break;
}
case rust::WorkflowOp::EchoMaterialMode: {
    out[0] = config_["Advanced Skill Material Mode"].toBool();
    break;
}
case rust::WorkflowOp::EchoPickDefault: {
    out[0] = world_.pick();
    break;
}
case rust::WorkflowOp::EchoRealm: {
    out[0] = realm_;
    break;
}
case rust::WorkflowOp::EchoRotationInfo: {
    c_.info("循环副本",
            QString("%1 · %2；本副本 %3 轮").arg(target.category).arg(target.index).arg(int(a[0])));
    break;
}
case rust::WorkflowOp::EchoRotationMode: {
    out[0] = config_["Farm Mode"].toString("Single Boss") == "Cycle All Dungeons";
    break;
}
case rust::WorkflowOp::EchoRotationTargetCount: {
    out[0] = double(targets.size());
    break;
}
case rust::WorkflowOp::EchoRunFailure: {
    switch (int(a[0])) {
    case 0:
        fail("刷取次数必须为 0 到 100000 的整数");
    case 1:
        fail("循环刷声骸时请关闭技能进阶材料模式");
    case 2:
        fail("循环刷取次数无效");
    case 3:
        throw NavigationFailure("当前副本轮数未完成，不跳到下一副本");
    default:
        fail("循环刷取失败编号无效");
    }
    break;
}
case rust::WorkflowOp::EchoSuppliedConfigEmpty: {
    out[0] = supplied.isEmpty();
    break;
}
case rust::WorkflowOp::EchoTotalCompleted: {
    out[0] = totalCompleted_;
    break;
}
case rust::WorkflowOp::HasInfo: {
    out[0] = bool(c_.info);
    break;
}
case rust::WorkflowOp::IncrementEchoDrops: {
    ++drops_;
    break;
}
case rust::WorkflowOp::InstallEchoCombatCheck: {
    combat_.onCombatCheck = [&] {
        return rust::workflowRun(rust::WorkflowMethod::EchoCombatCheck, {}, action)[0] != 0;
    };
    break;
}
case rust::WorkflowOp::InstallEchoCombatEnd: {
    combat_.combatEndCondition = [&] {
        return rust::workflowRun(rust::WorkflowMethod::EchoCombatEnd, {}, action)[0] != 0;
    };
    break;
}
case rust::WorkflowOp::LoadEchoProfileConfig: {
    config_ = c_.profile.group(a[0] != 0 ? "FarmEchoTask2" : "FarmEchoTask");
    break;
}
case rust::WorkflowOp::ReadEchoRotationRounds: {
    const auto rounds = config_["Rounds per Dungeon"];
    out[0] = rounds.toInt(1);
    out[1] = rounds.isDouble();
    out[2] = rounds.toDouble();
    break;
}
case rust::WorkflowOp::ReadEchoRotationTargets: {
    targets = echoRotationTargets(config_);
    break;
}
case rust::WorkflowOp::ReadEchoRunCount: {
    const int total = config_["Repeat Farm Count"].toInt();
    const auto value = config_["Repeat Farm Count"];
    out[0] = total;
    out[1] = value.isDouble();
    out[2] = value.toDouble();
    break;
}
case rust::WorkflowOp::RecoverEchoStartingPopup: {
    combat_.recoverStartingPopup();
    break;
}
case rust::WorkflowOp::ResetEchoRunCounters: {
    totalCompleted_ = drops_ = 0;
    break;
}
case rust::WorkflowOp::RestoreEchoCombatCheck: {
    combat_.onCombatCheck = oldCheck;
    break;
}
case rust::WorkflowOp::RestoreEchoCombatEnd: {
    combat_.combatEndCondition = oldEnd;
    break;
}
case rust::WorkflowOp::RestoreEchoTargetTimeout: {
    combat_.targetEnemyTimeout = oldTimeout;
    break;
}
case rust::WorkflowOp::RunEchoSingle: {
    out[0] = single();
    break;
}
case rust::WorkflowOp::SelectEchoRotationTarget: {
    target = targets.at(int(a[0]));
    break;
}
case rust::WorkflowOp::Sleep: {
    c_.sleep(a[0]);
    break;
}
case rust::WorkflowOp::StoreEchoEnhanced: {
    enhanced_ = a[0] != 0;
    break;
}
