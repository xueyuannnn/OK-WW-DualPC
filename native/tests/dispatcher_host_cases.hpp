case rust::WorkflowOp::TaskAutoCloseIntro:
out[0] = false; // Legacy parity scenarios do not enable the new intro handler.
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
case rust::WorkflowOp::DispatchMultiAccountDaily:
MultiAccountDaily(c_).run([&] { Daily(c_, combat_, detector_, root_, data_).run(); });
break;
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
