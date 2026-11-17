{
    TaskScope scope(c_, "MergeEchoTask");
    navigation_.ensureMain();
    c_.key(c_.hotkey("Bag Key", "b"));
    if (!c_.wait([&] { return !c_.inTeam(); }, 5))
        throw NavigationFailure("未能打开背包");
    c_.sleep(3);
    if (!c_.findAny({"cancel_button_hcenter_vcenter", "cancel_button_highlight_hcenter_vcenter"})) {
        if (c_.info)
            c_.info("声骸合成", "未达到 1000 个弃置声骸");
        navigation_.ensureMain();
        return 0;
    }
    navigation_.confirmDialog(false);
    c_.sleep(2);
    c_.clickRelative(.034, .293, 1);
    c_.clickRelative(.580, .911, 4);
    centered(.602, .124, .5);
    centered(.520, .904, 2);
    c_.clickRelative(.041, .918, 1);
    c_.clickRelative(.826, .840, .5);
    c_.clickRelative(.717, .204, .5);
    c_.clickRelative(.041, .918, .5);
    int batches = 0;
    for (; batches < 100; ++batches) {
        c_.clickRelative(.310, .915, .5);
        std::optional<Match> all;
        if (!c_.wait(
                [&] {
                    all = c_.find("echo_select_all", {.horizontal = .3});
                    return bool(all);
                },
                5))
            fail("未找到声骸全选按钮");
        c_.click(*all, 1);
        if (!matchingText(c_.text(.670, .660, .895, .958), "100\\s*/\\s*100"))
            break;
        c_.clickRelative(.782, .910);
        awaitEchoMergeResult(c_);
        c_.sleep(3);
        centered(.496, .972, 1);
    }
    navigation_.ensureMain();
    return batches;
}
