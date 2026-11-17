{
    TaskScope scope(c_, "ChangeEchoTask");
    if (c_.gameLanguage != "zh_CN")
        fail("修改声骸主属性仅支持简体中文游戏");
    const auto target = c_.setting("目标属性", "攻击").toString();
    const QStringList allowed{"攻击",         "暴击伤害",     "暴击",         "生命",
                              "防御",         "共鸣效率",     "冷凝伤害加成", "热熔伤害加成",
                              "导电伤害加成", "气动伤害加成", "衍射伤害加成", "湮灭伤害加成"};
    if (!allowed.contains(target))
        fail("目标主属性无效");
    int success = 0;
    for (int i = 0; i < 3000; ++i) {
        c_.refresh();
        if (!cultivate())
            fail("必须在背包声骸界面过滤后开始");
        if (!zeroLevel(true))
            return success;
        enterCultivate();
        const QRect screen(0, 0, c_.frame.cols, c_.frame.rows);
        if (!waitText(screen, "^声骸强化$"))
            fail("声骸培养页面未确认");
        c_.sleep(.5);
        auto current = c_.text(.10, .23, .35, .275);
        if (current.empty())
            fail("找不到当前主属性");
        for (const auto &line : current)
            if (fixedMain(line.text).contains(target))
                fail("目标属性和当前属性相同，请修改声骸过滤条件");
        c_.clickRelative(.04, .41);
        if (!waitText(screen, "^主音属性$"))
            fail("主音属性页面未确认");
        c_.sleep(.8);
        c_.clickRelative(.52, .71);
        const auto option = waitText(screen, "^(?:主属性)?" + QRegularExpression::escape(target) + "$");
        if (!option)
            fail("没有找到目标主属性");
        c_.sleep(.1);
        c_.click(*option, .5);
        const auto confirm = waitText(screen, "^确认$");
        if (!confirm)
            fail("没有找到主属性选择确认按钮");
        c_.click(*confirm, 2);
        const auto reconstruct = waitText(gameBox(c_.frame, .37, .82, .64, .99), "^数据重构$");
        if (!reconstruct)
            fail("没有找到数据重构按钮");
        c_.click(*reconstruct, .5);
        if (!waitText(screen, "^获得声骸$", 10))
            fail("未能确认重构结果，停止继续消耗材料");
        backToBag();
        if (c_.info)
            c_.info("成功声骸数量", QString::number(++success));
        else
            ++success;
    }
    fail("主属性修改达到单次批量上限");
}
