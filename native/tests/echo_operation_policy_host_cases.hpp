// Snapshot of typed SDK bindings; the policy oracle is original C++.
case rust::WorkflowOp::BackEchoToBag: {
    backToBag();
    break;
}
case rust::WorkflowOp::Checkpoint: {
    c_.execution.checkpoint();
    break;
}
case rust::WorkflowOp::ClickEchoCentered: {
    centered(a[0], a[1], a[2]);
    break;
}
case rust::WorkflowOp::ClickEchoOperationText: {
    c_.click(*value.text, a[0]);
    break;
}
case rust::WorkflowOp::ClickEchoSelectAll: {
    c_.click(*selectAll, a[0]);
    break;
}
case rust::WorkflowOp::ClickRelative: {
    c_.clickRelative(a[0], a[1], a[2]);
    break;
}
case rust::WorkflowOp::ConfirmEchoMergeDialog: {
    navigation_.confirmDialog(false);
    break;
}
case rust::WorkflowOp::CreateEchoOperationScope: {
    scope.emplace(c_, QStringList{"EnhanceEchoTask", "ChangeEchoTask", "MergeEchoTask"}.at(int(a[0])));
    break;
}
case rust::WorkflowOp::EchoCurrentMainIsTarget: {
    out[0] = fixedMain(lines.at(int(a[0])).text).contains(target);
    break;
}
case rust::WorkflowOp::EchoFailedInfo: {
    c_.info("失败声骸数量", QString::number(int(a[0])));
    break;
}
case rust::WorkflowOp::EchoMergeFullSelection: {
    out[0] = bool(matchingText(c_.text(.670, .660, .895, .958), "100\\s*/\\s*100"));
    break;
}
case rust::WorkflowOp::EchoMergeNotEnoughInfo: {
    c_.info("声骸合成", "未达到 1000 个弃置声骸");
    break;
}
case rust::WorkflowOp::EchoMergePrompt: {
    out[0] = bool(c_.findAny({"cancel_button_hcenter_vcenter", "cancel_button_highlight_hcenter_vcenter"}));
    break;
}
case rust::WorkflowOp::EchoOperationFailure: {
    switch (int(a[0])) {
    case 0:
        fail("必须在背包声骸界面过滤后开始");
    case 1:
        fail("未能打开声骸培养界面");
    case 2:
        fail("声骸处理后未能返回背包");
    case 3:
        fail("无法确认声骸上锁状态");
    case 4:
        fail("无法确认声骸弃置状态");
    case 5:
        fail("声骸上锁失败");
    case 6:
        fail("声骸弃置失败");
    case 7:
        fail("无法创建声骸截图目录");
    case 8:
        fail("声骸截图编码失败");
    case 9:
        fail("声骸截图保存失败");
    case 10:
        fail("强化设置需要开启阶段放入");
    case 11:
        fail("找不到强化并调谐，请开启同步调谐并检查材料");
    case 12:
        fail("强化确认界面未识别，停止继续消耗材料");
    case 13:
        fail("强化结果未关闭，停止继续消耗材料");
    case 14:
        fail("强化声骸仅支持简体或繁体中文游戏");
    case 15:
        fail("强化后词条数量或数值未确认，停止继续消耗材料");
    case 16:
        fail("声骸处理达到单次批量上限");
    case 17:
        fail("修改声骸主属性仅支持简体中文游戏");
    case 18:
        fail("目标主属性无效");
    case 19:
        fail("声骸培养页面未确认");
    case 20:
        fail("找不到当前主属性");
    case 21:
        fail("目标属性和当前属性相同，请修改声骸过滤条件");
    case 22:
        fail("主音属性页面未确认");
    case 23:
        fail("没有找到目标主属性");
    case 24:
        fail("没有找到主属性选择确认按钮");
    case 25:
        fail("没有找到数据重构按钮");
    case 26:
        fail("未能确认重构结果，停止继续消耗材料");
    case 27:
        fail("主属性修改达到单次批量上限");
    case 28:
        throw NavigationFailure("未能打开背包");
    case 29:
        fail("未找到声骸全选按钮");
    default:
        fail("声骸处理错误编号无效");
    }
    break;
}
case rust::WorkflowOp::EchoOperationLanguage: {
    out[0] = QStringList{"zh_CN", "zh_TW"}.indexOf(c_.gameLanguage);
    break;
}
case rust::WorkflowOp::EchoOperationLineCount: {
    out[0] = double(lines.size());
    break;
}
case rust::WorkflowOp::EchoPauseAfterSuccess: {
    out[0] = std::as_const(config)["Pause after Success"].toBool(true);
    break;
}
case rust::WorkflowOp::EchoSuccessInfo: {
    c_.info("成功声骸数量", QString::number(int(a[0])));
    break;
}
case rust::WorkflowOp::EchoTargetAttributeKind: {
    out[0] = QStringList{"攻击",         "暴击伤害",     "暴击",         "生命",
                         "防御",         "共鸣效率",     "冷凝伤害加成", "热熔伤害加成",
                         "导电伤害加成", "气动伤害加成", "衍射伤害加成", "湮灭伤害加成"}
                 .indexOf(target);
    break;
}
case rust::WorkflowOp::EnhancedEchoKeep: {
    out[0] = decision.keep;
    break;
}
case rust::WorkflowOp::EnhancedEchoStatCount: {
    out[0] = double(stats.size());
    break;
}
case rust::WorkflowOp::EnhancedEchoStatNumber: {
    out[0] = number(stats.at(int(a[0])).value);
    break;
}
case rust::WorkflowOp::EnsureEchoOperationMain: {
    navigation_.ensureMain();
    break;
}
case rust::WorkflowOp::EnterEchoCultivate: {
    enterCultivate();
    break;
}
case rust::WorkflowOp::EvaluateEnhancedEcho: {
    decision = evaluateEcho(stats, config);
    break;
}
case rust::WorkflowOp::ExecutionWait: {
    c_.execution.wait(a[0]);
    break;
}
case rust::WorkflowOp::HasInfo: {
    out[0] = bool(c_.info);
    break;
}
case rust::WorkflowOp::HasNotify: {
    out[0] = bool(c_.notify);
    break;
}
case rust::WorkflowOp::InTeam: {
    out[0] = c_.inTeam();
    break;
}
case rust::WorkflowOp::InvalidCondition: {
    fail("等待条件参数无效");
    break;
}
case rust::WorkflowOp::LoadEchoOperationConfig: {
    config = c_.profile.group(c_.task);
    break;
}
case rust::WorkflowOp::MarkEchoItem: {
    mark(a[0] != 0);
    break;
}
case rust::WorkflowOp::NotifyEchoSuccess: {
    c_.notify("符合条件的声骸", "已锁定并暂停任务，请查看强化结果", false);
    break;
}
case rust::WorkflowOp::Now: {
    out[0] = c_.execution.now();
    break;
}
case rust::WorkflowOp::OpenEchoBag: {
    c_.key(c_.hotkey("Bag Key", "b"));
    break;
}
case rust::WorkflowOp::PrepareEchoOperationScreen: {
    screen = QRect(0, 0, c_.frame.cols, c_.frame.rows);
    break;
}
case rust::WorkflowOp::ReadEchoAttributeConfirm: {
    value.text = waitText(screen, "^确认$");
    out[0] = value.text.has_value();
    break;
}
case rust::WorkflowOp::ReadEchoCultivate: {
    value.text = cultivate();
    out[0] = value.text.has_value();
    break;
}
case rust::WorkflowOp::ReadEchoCurrentMain: {
    lines = c_.text(.10, .23, .35, .275);
    break;
}
case rust::WorkflowOp::ReadEchoEnhanceHeading: {
    value.text = waitText(screen, "^声骸强化$");
    out[0] = value.text.has_value();
    break;
}
case rust::WorkflowOp::ReadEchoMainAttributeHeading: {
    value.text = waitText(screen, "^主音属性$");
    out[0] = value.text.has_value();
    break;
}
case rust::WorkflowOp::ReadEchoReconstruct: {
    value.text = waitText(gameBox(c_.frame, .37, .82, .64, .99), "^数据重构$");
    out[0] = value.text.has_value();
    break;
}
case rust::WorkflowOp::ReadEchoReconstructResult: {
    value.text = waitText(screen, "^获得声骸$", 10);
    out[0] = value.text.has_value();
    break;
}
case rust::WorkflowOp::ReadEchoSelectAll: {
    selectAll = c_.find("echo_select_all", {.horizontal = .3});
    out[0] = selectAll.has_value();
    break;
}
case rust::WorkflowOp::ReadEchoTargetAttribute: {
    target = c_.setting("目标属性", "攻击").toString();
    break;
}
case rust::WorkflowOp::ReadEchoTargetOption: {
    value.text = waitText(screen, "^(?:主属性)?" + QRegularExpression::escape(target) + "$");
    out[0] = value.text.has_value();
    break;
}
case rust::WorkflowOp::ReadEchoZeroLevel: {
    out[0] = zeroLevel(a[0] != 0);
    break;
}
case rust::WorkflowOp::ReadEnhancedEchoStats: {
    stats = pairEchoStats(c_.text(.09, .3, .40, .53));
    break;
}
case rust::WorkflowOp::Refresh: {
    c_.refresh();
    break;
}
case rust::WorkflowOp::ScreenshotAcceptedEcho: {
    screenshot("success", int(a[0]));
    break;
}
case rust::WorkflowOp::ScreenshotRejectedEcho: {
    screenshot("failed", int(a[0]), decision.reason);
    break;
}
case rust::WorkflowOp::Sleep: {
    c_.sleep(a[0]);
    break;
}
case rust::WorkflowOp::UpgradeEchoItem: {
    upgrade();
    break;
}
case rust::WorkflowOp::WaitEchoMergeResult: {
    awaitEchoMergeResult(c_);
    break;
}
