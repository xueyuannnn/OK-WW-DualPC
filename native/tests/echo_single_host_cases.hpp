// Typed SDK bindings shared with production, exercised against frozen C++ policy.
case rust::WorkflowOp::ApplyEchoParameters: {
    parameters();
    break;
}
case rust::WorkflowOp::CheckEchoRealm: {
    checkRealm(a[0]);
    break;
}
case rust::WorkflowOp::Checkpoint: {
    c_.execution.checkpoint();
    break;
}
case rust::WorkflowOp::CircleEchoDrop: {
    out[0] = world_.circleEcho(int(a[0]));
    break;
}
case rust::WorkflowOp::ClickEchoMaterialPoint: {
    c_.click(QPointF(a[0], a[1]));
    break;
}
case rust::WorkflowOp::ClickRelative: {
    c_.clickRelative(a[0], a[1], a[2]);
    break;
}
case rust::WorkflowOp::CreateEchoCombatOptions: {
    combatOptions.emplace();
    break;
}
case rust::WorkflowOp::EchoBypassWait: {
    out[0] = bypassWait_;
    break;
}
case rust::WorkflowOp::EchoCombat: {
    out[0] = combat_.active();
    break;
}
case rust::WorkflowOp::EchoCombatFailure: {
    switch (int(a[0])) {
    case 0:
        fail("刷取次数超出上限");
    case 1:
        fail("未确认周本材料领奖按钮，不重复消耗");
    default:
        fail("单轮刷取失败编号无效");
    }
    break;
}
case rust::WorkflowOp::EchoCombatWait: {
    out[0] = wait_;
    break;
}
case rust::WorkflowOp::EchoEnhanced: {
    out[0] = enhanced_;
    break;
}
case rust::WorkflowOp::EchoHasInteract: {
    out[0] = bool(findInteract(c_));
    break;
}
case rust::WorkflowOp::EchoInWorld: {
    out[0] = navigation_.inWorld();
    break;
}
case rust::WorkflowOp::EchoJustEntered: {
    out[0] = justEntered_;
    break;
}
case rust::WorkflowOp::EchoMaterialClaimPoint: {
    out[0] = materialClaim->box.x();
    out[1] = materialClaim->box.width();
    out[2] = materialClaim->center().y();
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
case rust::WorkflowOp::EchoPickEchoOnly: {
    out[0] = world_.pick(true, true);
    break;
}
case rust::WorkflowOp::EchoPickupMethod: {
    out[0] = QStringList{"Yolo", "Run in Circle"}.indexOf(config_["Echo Pickup Method"].toString("Yolo"));
    break;
}
case rust::WorkflowOp::EchoRealm: {
    out[0] = realm_;
    break;
}
case rust::WorkflowOp::EchoRepeatCount: {
    out[0] = config_["Repeat Farm Count"].toInt();
    break;
}
case rust::WorkflowOp::EchoSingleCountsInfo: {
    c_.info("本副本轮数", QString("%1/%2").arg(int(a[0])).arg(int(a[1])));
    c_.info("声骸数量", QString::number(drops_));
    break;
}
case rust::WorkflowOp::EchoTeleportEnabled: {
    out[0] = teleportEnabled();
    break;
}
case rust::WorkflowOp::EchoTreasure: {
    out[0] = treasure_;
    break;
}
case rust::WorkflowOp::EchoTreasureFound: {
    out[0] = bool(world_.treasure());
    break;
}
case rust::WorkflowOp::EchoTreasureRestart: {
    out[0] = treasureRestart();
    break;
}
case rust::WorkflowOp::EchoUseLiberation: {
    out[0] = config_["Use Liberation"].toBool(true);
    break;
}
case rust::WorkflowOp::EchoWaitTeam: {
    navigation_.waitTeam(a[0]);
    break;
}
case rust::WorkflowOp::EchoWalkResultCombat: {
    out[0] = walkResult_ == "combat";
    break;
}
case rust::WorkflowOp::EchoWeeklyCategory: {
    out[0] = config_["Teleport to Boss"] == "Weekly Challenge";
    break;
}
case rust::WorkflowOp::EnterEchoFromF: {
    enterFromF();
    break;
}
case rust::WorkflowOp::ExecutionWait: {
    c_.execution.wait(a[0]);
    break;
}
case rust::WorkflowOp::FindEchoDrop: {
    out[0] = world_.findEcho(false, a[0] != 0, a[1], a[2]).first;
    break;
}
case rust::WorkflowOp::FindEchoNearestBoss: {
    nearestBoss();
    break;
}
case rust::WorkflowOp::FollowEchoBossDefault: {
    out[0] = world_.followBossMinimap();
    break;
}
case rust::WorkflowOp::HasInfo: {
    out[0] = bool(c_.info);
    break;
}
case rust::WorkflowOp::HuntEchoTreasure: {
    treasureHunt();
    break;
}
case rust::WorkflowOp::IncrementEchoDrops: {
    ++drops_;
    break;
}
case rust::WorkflowOp::IncrementEchoTotalCompleted: {
    ++totalCompleted_;
    break;
}
case rust::WorkflowOp::InvalidCondition: {
    fail("等待条件参数无效");
    break;
}
case rust::WorkflowOp::NavEnsureMain: {
    navigation_.ensureMain(a[0]);
    break;
}
case rust::WorkflowOp::Now: {
    out[0] = c_.execution.now();
    break;
}
case rust::WorkflowOp::PickEchoMaterialTreasure: {
    world_.pick(false);
    break;
}
case rust::WorkflowOp::ReadEchoMaterialClaim: {
    materialClaim = c_.find("gray_confirm_exit_button", {.threshold = .7});
    out[0] = materialClaim.has_value();
    break;
}
case rust::WorkflowOp::ReadEchoRealm: {
    realm_ = navigation_.inRealm();
    break;
}
case rust::WorkflowOp::ReadEchoWeeklyMaterialCount: {
    out[0] = weeklyMaterialCount();
    break;
}
case rust::WorkflowOp::RecognizeEchoBoss: {
    recognizeBoss();
    break;
}
case rust::WorkflowOp::Refresh: {
    c_.refresh();
    break;
}
case rust::WorkflowOp::RestartEchoDomain: {
    restartBossDomain(c_);
    break;
}
case rust::WorkflowOp::RunEchoCombatOnce: {
    combat_.combatOnce(*combatOptions);
    break;
}
case rust::WorkflowOp::RunEchoInteractions: {
    interactions();
    break;
}
case rust::WorkflowOp::ScrollEchoInteractions: {
    scrollInteract();
    break;
}
case rust::WorkflowOp::SeekEchoCombat: {
    out[0] = seekCombat(a[0], a[1] != 0, a[2] != 0);
    break;
}
case rust::WorkflowOp::SetEchoCombatLiberation: {
    combatOptions->useLiberation = a[0] != 0;
    break;
}
case rust::WorkflowOp::SetEchoCombatRecovery: {
    combatOptions->recover = [&] { heal(); };
    break;
}
case rust::WorkflowOp::SetEchoCombatRequired: {
    combatOptions->required = a[0] != 0;
    break;
}
case rust::WorkflowOp::SetEchoCombatWait: {
    combatOptions->waitSeconds = a[0];
    break;
}
case rust::WorkflowOp::Sleep: {
    c_.sleep(a[0]);
    break;
}
case rust::WorkflowOp::StoreEchoEntryAtHeal: {
    entryAtHeal_ = a[0] != 0;
    break;
}
case rust::WorkflowOp::StoreEchoJustEntered: {
    justEntered_ = a[0] != 0;
    break;
}
case rust::WorkflowOp::StoreEchoStartedAt: {
    started_ = a[0];
    break;
}
case rust::WorkflowOp::StoreEchoTreasure: {
    treasure_ = a[0] != 0;
    break;
}
case rust::WorkflowOp::TeleportEcho: {
    teleport();
    break;
}
case rust::WorkflowOp::WaitEchoMaterialReward: {
    awaitWeeklyMaterialReward(c_);
    break;
}
case rust::WorkflowOp::WalkEchoDrop: {
    out[0] = world_.walkToInteract("w", 3, false, 1, "吸收|Absorb", true);
    break;
}
case rust::WorkflowOp::WalkEchoMaterialTreasure: {
    world_.walkToTreasure();
    break;
}
