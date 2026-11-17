{
    TaskScope scope(c_, "EnhanceEchoTask");
    if (c_.gameLanguage != "zh_CN" && c_.gameLanguage != "zh_TW")
        fail("强化声骸仅支持简体或繁体中文游戏");
    const auto config = c_.profile.group(c_.task);
    int success = 0, failed = 0;
    for (int item = 0; item < 3000; ++item) {
        c_.refresh();
        if (!cultivate())
            fail("必须在背包声骸界面过滤后开始");
        c_.clickRelative(.13, .21, .5);
        if (!zeroLevel()) {
            c_.clickRelative(.35, .917, .5);
            c_.clickRelative(.35, .917, .5);
            c_.clickRelative(.13, .21, .5);
            if (!zeroLevel())
                return success;
        }
        enterCultivate();
        for (int count = 1; count <= 5; ++count) {
            upgrade();
            QVector<EchoStat> stats;
            // A failed/partial OCR must not trigger another material-consuming upgrade.
            const bool verified = c_.wait(
                [&] {
                    stats = pairEchoStats(c_.text(.09, .3, .40, .53));
                    return stats.size() == count &&
                           std::all_of(stats.begin(), stats.end(),
                                       [](const auto &s) { return number(s.value) > 0; });
                },
                4, .2);
            if (!verified)
                fail("强化后词条数量或数值未确认，停止继续消耗材料");
            const auto result = evaluateEcho(stats, config);
            if (!result.keep) {
                mark(false);
                screenshot("failed", ++failed, result.reason);
                if (c_.info)
                    c_.info("失败声骸数量", QString::number(failed));
                backToBag();
                break;
            }
            if (count == 5) {
                mark(true);
                screenshot("success", ++success);
                if (c_.info)
                    c_.info("成功声骸数量", QString::number(success));
                if (config["Pause after Success"].toBool(true)) {
                    if (c_.notify)
                        c_.notify("符合条件的声骸", "已锁定并暂停任务，请查看强化结果", false);
                    return success;
                }
                backToBag();
            }
        }
    }
    fail("声骸处理达到单次批量上限");
}
