{
    const bool material = enhanced_ && config_["Advanced Skill Material Mode"].toBool();
    int total = config_["Repeat Farm Count"].toInt();
    if (material) {
        if (config_["Teleport to Boss"] != "Weekly Challenge")
            return 0;
        total = weeklyMaterialCount();
    }
    if (total <= 0)
        return 0;
    if (total > 10000000)
        fail("刷取次数超出上限");
    realm_ = navigation_.inRealm();
    parameters();
    started_ = c_.execution.now();
    treasure_ = false;
    entryAtHeal_ = false;
    if (teleportEnabled())
        teleport();
    int count = 0, recoveries = 0, travelRetries = 0;
    while (count < total) {
        try {
            checkRealm(60);
            interactions();
            if (justEntered_)
                justEntered_ = false;
            else if (!combat_.active()) {
                if (realm_ && !navigation_.inWorld()) {
                    restartBossDomain(c_);
                    c_.sleep(.1);
                } else {
                    if (treasure_)
                        c_.wait(
                            [&] {
                                return bool(world_.treasure()) || combat_.active() || bool(findInteract(c_));
                            },
                            5);
                    if (!combat_.active()) {
                        if (treasureRestart()) {
                            treasure_ = true;
                            if (teleportEnabled() && realm_)
                                enterFromF();
                            else
                                scrollInteract();
                        } else
                            scrollInteract();
                    }
                }
            }
            if (!realm_ && !treasure_ && !combat_.active()) {
                if (!world_.followBossMinimap()) {
                    nearestBoss();
                    c_.sleep(.5);
                    seekCombat(12, true, true);
                }
                treasureHunt();
            }
            c_.sleep(wait_);
            recognizeBoss();
            CombatOptions options;
            options.waitSeconds = 20;
            options.required = true;
            options.useLiberation = config_["Use Liberation"].toBool(true);
            options.recover = [&] { heal(); };
            combat_.combatOnce(options);
            bool dropped = world_.pick(true, true);
            if (!dropped) {
                const auto method = config_["Echo Pickup Method"].toString("Yolo");
                if (method == "Yolo")
                    dropped = world_.findEcho(false, realm_, realm_ ? 12 : 4, realm_ ? .25 : .65).first;
                else if (method == "Run in Circle")
                    dropped = world_.circleEcho(2);
                else
                    dropped = world_.walkToInteract("w", 3, false, 1, "吸收|Absorb", true) && world_.pick();
            }
            if (dropped)
                ++drops_;
            if (material) {
                bool confirmed = false;
                for (int attempt = 0; attempt < 2 && !confirmed; ++attempt) {
                    world_.walkToTreasure();
                    world_.pick(false);
                    c_.sleep(1);
                    std::optional<Match> claim;
                    if (c_.wait(
                            [&] {
                                claim = c_.find("gray_confirm_exit_button", {.threshold = .7});
                                return bool(claim);
                            },
                            3)) {
                        c_.sleep(.5);
                        c_.click(QPointF(claim->box.x() - claim->box.width(), claim->center().y()));
                        confirmed = true;
                    }
                }
                if (!confirmed)
                    fail("未确认周本材料领奖按钮，不重复消耗");
                awaitWeeklyMaterialReward(c_);
                c_.sleep(4);
                c_.clickRelative(realm_ ? (count + 1 < total ? .63 : .37) : .5, .85, 1);
                navigation_.waitTeam(120);
                // The reward screen's Continue already starts the next round.
                // Do not open the menu and restart that new round again.
                if (realm_ && count + 1 < total)
                    justEntered_ = true;
            }
            ++count;
            ++totalCompleted_;
            if (enhanced_)
                recoveries = 0;
            travelRetries = 0;
            if (c_.info) {
                c_.info("本副本轮数", QString("%1/%2").arg(count).arg(total));
                c_.info("声骸数量", QString::number(drops_));
            }
            if (!bypassWait_)
                c_.wait([&] { return combat_.active(); }, dropped && !treasure_ ? 5 : 1);
        } catch (const Cancelled &) {
            throw;
        } catch (const CharacterRevived &) {
            if (!teleportEnabled() || ++recoveries > (enhanced_ ? 3 : 2))
                throw;
            entryAtHeal_ = true;
            teleport();
        } catch (const NavigationFailure &) {
            if (!teleportEnabled() || walkResult_ != "combat" || ++travelRetries > 3)
                throw;
            teleport();
        }
    }
    if (material)
        navigation_.ensureMain(120);
    return count;
}
