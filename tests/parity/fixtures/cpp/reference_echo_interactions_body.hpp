{
    if (combat_.active())
        return;
    const auto boss = config_["Boss"].toString("Other");
    if (boss == "Lorelei" && c_.execution.now() - lastNight_ > 660)
        changeNight();
    if (boss == "Fallacy of No Return") {
        if (!findInteract(c_)) {
            nearestBoss();
            c_.key("d", .5, .25);
            c_.key("w", .5);
        }
        if (world_.walkToInteract("w", 20, true, 0, {}, true, true))
            scrollInteract();
        seekCombat(300);
    } else if (boss == "Fenrico") {
        for (int i = 0; findInteract(c_) && i < 20; ++i) {
            c_.sleep(1);
            if (world_.pick(true, true))
                ++drops_;
        }
        try {
            nearestBoss();
            c_.sleep(2);
        } catch (const NavigationFailure &) {
            navigation_.ensureMain();
        }
        seekCombat(5, true);
        treasureHunt();
        seekCombat(300);
    } else if (boss == "Lady of the Sea") {
        realm_ = true;
        if (!navigation_.inWorld()) {
            navigation_.exitRealm(120);
            c_.sleep(2);
        }
        if (navigation_.inWorld()) {
            world_.pick();
            c_.sleep(2);
            navigation_.waitTeam(120);
            c_.sleep(2);
            seekCombat(10);
        }
    } else if (boss == "Nameless Explorer") {
        if (world_.treasure() || findInteract(c_)) {
            if (treasureRestart())
                treasure_ = true;
            scrollInteract();
        }
        c_.sleep(1);
        if (!combat_.active()) {
            if (!treasure_) {
                nearestBoss();
                seekCombat(12, true, true);
            } else if (!seekCombat(8, true, true))
                world_.followBossMinimap(10);
            treasureHunt();
        }
        seekCombat(10);
    }
}
