use super::*;
impl Host {
    pub(super) fn run_echo_combat(&self, method: Method, _a: &[f64]) -> Result<[f64; 3]> {
        match method {
            Method::EchoInteractions => self.echo_interactions()?,
            Method::EchoSingle => return Ok([self.echo_single()? as f64, 0., 0.]),
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
    fn echo_single(&self) -> Result<i32> {
        let material = self.flag(Op::EchoEnhanced, &[])? && self.flag(Op::EchoMaterialMode, &[])?;
        let mut total = self.scalar(Op::EchoRepeatCount, &[])? as i32;
        if material {
            if !self.flag(Op::EchoWeeklyCategory, &[])? {
                return Ok(0);
            }
            total = self.scalar(Op::ReadEchoWeeklyMaterialCount, &[])? as i32;
        }
        if total <= 0 {
            return Ok(0);
        }
        if total > 10_000_000 {
            self.action(Op::EchoCombatFailure, &[0.])?;
        }
        self.action(Op::ReadEchoRealm, &[])?;
        self.action(Op::ApplyEchoParameters, &[])?;
        let now = self.scalar(Op::Now, &[])?;
        self.action(Op::StoreEchoStartedAt, &[now])?;
        self.action(Op::StoreEchoTreasure, &[0.])?;
        self.action(Op::StoreEchoEntryAtHeal, &[0.])?;
        if self.flag(Op::EchoTeleportEnabled, &[])? {
            self.action(Op::TeleportEcho, &[])?;
        }
        let mut count = 0;
        let mut recoveries = 0;
        let mut travel_retries = 0;
        while count < total {
            let round = (|| -> Result {
                self.action(Op::CheckEchoRealm, &[60.])?;
                self.action(Op::RunEchoInteractions, &[])?;
                if self.flag(Op::EchoJustEntered, &[])? {
                    self.action(Op::StoreEchoJustEntered, &[0.])?;
                } else if !self.flag(Op::EchoCombat, &[])? {
                    if self.flag(Op::EchoRealm, &[])? && !self.flag(Op::EchoInWorld, &[])? {
                        self.action(Op::RestartEchoDomain, &[])?;
                        self.action(Op::Sleep, &[0.1])?;
                    } else {
                        if self.flag(Op::EchoTreasure, &[])? {
                            self.wait_for(
                                || {
                                    Ok(self.flag(Op::EchoTreasureFound, &[])?
                                        || self.flag(Op::EchoCombat, &[])?
                                        || self.flag(Op::EchoHasInteract, &[])?)
                                },
                                5.,
                            )?;
                        }
                        if !self.flag(Op::EchoCombat, &[])? {
                            if self.flag(Op::EchoTreasureRestart, &[])? {
                                self.action(Op::StoreEchoTreasure, &[1.])?;
                                if self.flag(Op::EchoTeleportEnabled, &[])?
                                    && self.flag(Op::EchoRealm, &[])?
                                {
                                    self.action(Op::EnterEchoFromF, &[])?;
                                } else {
                                    self.action(Op::ScrollEchoInteractions, &[])?;
                                }
                            } else {
                                self.action(Op::ScrollEchoInteractions, &[])?;
                            }
                        }
                    }
                }
                if !self.flag(Op::EchoRealm, &[])?
                    && !self.flag(Op::EchoTreasure, &[])?
                    && !self.flag(Op::EchoCombat, &[])?
                {
                    if !self.flag(Op::FollowEchoBossDefault, &[])? {
                        self.action(Op::FindEchoNearestBoss, &[])?;
                        self.action(Op::Sleep, &[0.5])?;
                        self.action(Op::SeekEchoCombat, &[12., 1., 1.])?;
                    }
                    self.action(Op::HuntEchoTreasure, &[])?;
                }
                let wait = self.scalar(Op::EchoCombatWait, &[])?;
                self.action(Op::Sleep, &[wait])?;
                self.action(Op::RecognizeEchoBoss, &[])?;
                self.action(Op::CreateEchoCombatOptions, &[])?;
                self.action(Op::SetEchoCombatWait, &[20.])?;
                self.action(Op::SetEchoCombatRequired, &[1.])?;
                let liberation = self.flag(Op::EchoUseLiberation, &[])?;
                self.action(Op::SetEchoCombatLiberation, &[liberation as u8 as f64])?;
                self.action(Op::SetEchoCombatRecovery, &[])?;
                self.action(Op::RunEchoCombatOnce, &[])?;
                let mut dropped = self.flag(Op::EchoPickEchoOnly, &[])?;
                if !dropped {
                    match self.scalar(Op::EchoPickupMethod, &[])? as i32 {
                        0 => {
                            let turn = self.flag(Op::EchoRealm, &[])?;
                            let timeout = if self.flag(Op::EchoRealm, &[])? {
                                12.
                            } else {
                                4.
                            };
                            let threshold = if self.flag(Op::EchoRealm, &[])? {
                                0.25
                            } else {
                                0.65
                            };
                            dropped = self
                                .flag(Op::FindEchoDrop, &[turn as u8 as f64, timeout, threshold])?;
                        }
                        1 => dropped = self.flag(Op::CircleEchoDrop, &[2.])?,
                        _ => {
                            dropped = self.flag(Op::WalkEchoDrop, &[])?
                                && self.flag(Op::EchoPickDefault, &[])?
                        }
                    }
                }
                if dropped {
                    self.action(Op::IncrementEchoDrops, &[])?;
                }
                if material {
                    let mut confirmed = false;
                    for _ in 0..2 {
                        if confirmed {
                            break;
                        }
                        self.action(Op::WalkEchoMaterialTreasure, &[])?;
                        self.action(Op::PickEchoMaterialTreasure, &[])?;
                        self.action(Op::Sleep, &[1.])?;
                        if self.wait_for(|| self.flag(Op::ReadEchoMaterialClaim, &[]), 3.)? {
                            self.action(Op::Sleep, &[0.5])?;
                            let [x, width, y] = self.call(Op::EchoMaterialClaimPoint, &[])?;
                            self.action(Op::ClickEchoMaterialPoint, &[x - width, y])?;
                            confirmed = true;
                        }
                    }
                    if !confirmed {
                        self.action(Op::EchoCombatFailure, &[1.])?;
                    }
                    self.action(Op::WaitEchoMaterialReward, &[])?;
                    self.action(Op::Sleep, &[4.])?;
                    let x = if self.flag(Op::EchoRealm, &[])? {
                        if count + 1 < total {
                            0.63
                        } else {
                            0.37
                        }
                    } else {
                        0.5
                    };
                    self.action(Op::ClickRelative, &[x, 0.85, 1.])?;
                    self.action(Op::EchoWaitTeam, &[120.])?;
                    if self.flag(Op::EchoRealm, &[])? && count + 1 < total {
                        self.action(Op::StoreEchoJustEntered, &[1.])?;
                    }
                }
                count += 1;
                self.action(Op::IncrementEchoTotalCompleted, &[])?;
                if self.flag(Op::EchoEnhanced, &[])? {
                    recoveries = 0;
                }
                travel_retries = 0;
                if self.flag(Op::HasInfo, &[])? {
                    self.action(Op::EchoSingleCountsInfo, &[count as f64, total as f64])?;
                }
                if !self.flag(Op::EchoBypassWait, &[])? {
                    let timeout = if dropped && !self.flag(Op::EchoTreasure, &[])? {
                        5.
                    } else {
                        1.
                    };
                    self.wait_for(|| self.flag(Op::EchoCombat, &[]), timeout)?;
                }
                Ok(())
            })();
            if round.is_ok() {
                continue;
            }
            match self.scalar(Op::ExceptionKind, &[])? as i32 {
                3 => {
                    self.action(Op::ClearException, &[])?;
                    if !self.flag(Op::EchoTeleportEnabled, &[])? {
                        self.action(Op::RethrowHandled, &[])?;
                        return Err(HOST_ERROR);
                    }
                    recoveries += 1;
                    let limit = if self.flag(Op::EchoEnhanced, &[])? {
                        3
                    } else {
                        2
                    };
                    if recoveries > limit {
                        self.action(Op::RethrowHandled, &[])?;
                        return Err(HOST_ERROR);
                    }
                    self.action(Op::StoreEchoEntryAtHeal, &[1.])?;
                    self.action(Op::TeleportEcho, &[])?;
                }
                2 => {
                    self.action(Op::ClearException, &[])?;
                    if !self.flag(Op::EchoTeleportEnabled, &[])?
                        || !self.flag(Op::EchoWalkResultCombat, &[])?
                    {
                        self.action(Op::RethrowHandled, &[])?;
                        return Err(HOST_ERROR);
                    }
                    travel_retries += 1;
                    if travel_retries > 3 {
                        self.action(Op::RethrowHandled, &[])?;
                        return Err(HOST_ERROR);
                    }
                    self.action(Op::TeleportEcho, &[])?;
                }
                _ => return Err(HOST_ERROR),
            }
        }
        if material {
            self.action(Op::NavEnsureMain, &[120.])?;
        }
        Ok(count)
    }
    fn echo_interactions(&self) -> Result {
        if self.flag(Op::EchoCombat, &[])? {
            return Ok(());
        }
        let boss = self.scalar(Op::EchoInteractionBoss, &[])? as i32;
        if boss == 4 && self.scalar(Op::Now, &[])? - self.scalar(Op::EchoLastNight, &[])? > 660. {
            self.action(Op::ChangeEchoNight, &[])?;
        }
        match boss {
            0 => {
                if !self.flag(Op::EchoHasInteract, &[])? {
                    self.action(Op::FindEchoNearestBoss, &[])?;
                    self.action(Op::EchoTapKey, &[3., 0.5, 0.25])?;
                    self.action(Op::EchoTapKey, &[0., 0.5, 0.02])?;
                }
                if self.flag(Op::WalkEchoFallacyInteract, &[])? {
                    self.action(Op::ScrollEchoInteractions, &[])?;
                }
                self.action(Op::SeekEchoCombat, &[300., 0., 0.])?;
            }
            1 => {
                let mut index = 0;
                // The original for-condition probes F before testing the limit.
                while self.flag(Op::EchoHasInteract, &[])? && index < 20 {
                    self.action(Op::Sleep, &[1.])?;
                    if self.flag(Op::EchoPickEchoOnly, &[])? {
                        self.action(Op::IncrementEchoDrops, &[])?;
                    }
                    index += 1;
                }
                let travel = (|| {
                    self.action(Op::FindEchoNearestBoss, &[])?;
                    self.action(Op::Sleep, &[2.])
                })();
                if travel.is_err() {
                    if self.scalar(Op::ExceptionKind, &[])? != 2. {
                        return Err(HOST_ERROR);
                    }
                    self.action(Op::ClearException, &[])?;
                    self.action(Op::EnsureEchoMainDefault, &[])?;
                }
                self.action(Op::SeekEchoCombat, &[5., 1., 0.])?;
                self.action(Op::HuntEchoTreasure, &[])?;
                self.action(Op::SeekEchoCombat, &[300., 0., 0.])?;
            }
            2 => {
                self.action(Op::StoreEchoRealm, &[1.])?;
                if !self.flag(Op::EchoInWorld, &[])? {
                    self.action(Op::ExitEchoRealm, &[120.])?;
                    self.action(Op::Sleep, &[2.])?;
                }
                if self.flag(Op::EchoInWorld, &[])? {
                    self.action(Op::EchoPickDefault, &[])?;
                    self.action(Op::Sleep, &[2.])?;
                    self.action(Op::EchoWaitTeam, &[120.])?;
                    self.action(Op::Sleep, &[2.])?;
                    self.action(Op::SeekEchoCombat, &[10., 0., 0.])?;
                }
            }
            3 => {
                if self.flag(Op::EchoTreasureFound, &[])? || self.flag(Op::EchoHasInteract, &[])? {
                    if self.flag(Op::EchoTreasureRestart, &[])? {
                        self.action(Op::StoreEchoTreasure, &[1.])?;
                    }
                    self.action(Op::ScrollEchoInteractions, &[])?;
                }
                self.action(Op::Sleep, &[1.])?;
                if !self.flag(Op::EchoCombat, &[])? {
                    if !self.flag(Op::EchoTreasure, &[])? {
                        self.action(Op::FindEchoNearestBoss, &[])?;
                        self.action(Op::SeekEchoCombat, &[12., 1., 1.])?;
                    } else if !self.flag(Op::SeekEchoCombat, &[8., 1., 1.])? {
                        self.action(Op::FollowEchoBossMinimap, &[10.])?;
                    }
                    self.action(Op::HuntEchoTreasure, &[])?;
                }
                self.action(Op::SeekEchoCombat, &[10., 0., 0.])?;
            }
            _ => {}
        }
        Ok(())
    }
}
