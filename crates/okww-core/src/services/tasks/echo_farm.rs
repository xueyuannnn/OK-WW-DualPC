use crate::services::*;
impl Host {
    fn echo_rotation_targets(&self, bosses: i32, weekly: i32) -> Result {
        let scope = self.scalar(Op::EchoRotationScope, &[])? as i32;
        if !(0..=2).contains(&scope) {
            self.action(Op::EchoFarmFailure, &[0.])?;
        }
        let start = if scope == 0 {
            self.scalar(Op::EchoRotationStartCategory, &[])? as i32
        } else {
            scope - 1
        };
        let number = if self.flag(Op::EchoRotationIndexAbsent, &[])? {
            1.
        } else {
            self.scalar(Op::EchoRotationStartIndex, &[])?
        };
        if !(0..=1).contains(&start)
            || !number.is_finite()
            || number.floor() != number
            || number < 1.
            || number > if start == 0 { bosses } else { weekly } as f64
        {
            self.action(Op::EchoFarmFailure, &[1.])?;
        }
        let mut offset = 0;
        let mut size = 0;
        for category in 0..2 {
            if scope != 0 && scope != category + 1 {
                continue;
            }
            for index in 1..=if category == 0 { bosses } else { weekly } {
                if category == start && index == number as i32 {
                    offset = size;
                }
                self.action(
                    Op::AppendEchoRotationTarget,
                    &[category as f64, index as f64],
                )?;
                size += 1;
            }
        }
        self.action(Op::RotateEchoTargets, &[offset as f64])
    }
    fn weekly_material_reward(&self) -> Result<bool> {
        if self.flag(Op::EchoRewardResourceDialog, &[])? {
            return Ok(false);
        }
        if self.flag(Op::EchoRewardText, &[])? {
            return Ok(true);
        }
        let heading = self.flag(Op::EchoRewardHeading, &[])?;
        // Both OCR scans run in the original, even when the heading is absent.
        let controls = self.flag(Op::EchoRewardControls, &[])?;
        Ok(heading && controls)
    }
    fn echo_parameters(&self) -> Result {
        let boss = self.scalar(Op::EchoConfiguredBoss, &[])? as i32;
        self.action(Op::ReadEchoCombatWait, &[])?;
        if self.scalar(Op::EchoCombatWait, &[])? == 0. {
            if [0, 1, 2].contains(&boss) {
                self.action(Op::StoreEchoCombatWait, &[5.])?;
            } else if boss == 3 {
                self.action(Op::StoreEchoCombatWait, &[7.])?;
            }
        }
        if boss == 4 {
            self.action(Op::StoreEchoRealm, &[1.])?;
        }
        self.action(
            Op::StoreEchoBypassWait,
            &[[5, 2, 4, 6].contains(&boss) as u8 as f64],
        )?;
        self.action(Op::StoreEchoWorldMode, &[(boss == 7) as u8 as f64])?;
        let timeout = if self.flag(Op::EchoRealm, &[])? {
            3.
        } else {
            1.2
        };
        self.action(Op::StoreEchoTargetTimeout, &[timeout])
    }
    fn echo_check_realm(&self, seconds: f64) -> Result {
        if !self.flag(Op::EchoRealm, &[])?
            && !self.flag(Op::EchoWorldMode, &[])?
            && self.scalar(Op::Now, &[])? - self.scalar(Op::EchoStartedAt, &[])? < seconds
        {
            self.action(Op::ReadEchoRealm, &[])?;
            let timeout = if self.flag(Op::EchoRealm, &[])? {
                3.
            } else {
                1.2
            };
            self.action(Op::StoreEchoTargetTimeout, &[timeout])?;
        }
        Ok(())
    }
    fn echo_boss_level(&self) -> Result {
        self.action(Op::ReadEchoBossLevel, &[])?;
        if !self.flag(Op::EchoBossLevelValid, &[])? {
            self.action(Op::EchoFarmFailure, &[2.])?;
        }
        if !self.wait_for(|| self.flag(Op::FindEchoBossLevel, &[]), 10.)? {
            self.action(Op::EchoFarmFailure, &[3.])?;
        }
        self.action(Op::Sleep, &[1.])?;
        self.action(Op::ClickEchoBossLevel, &[])
    }
    fn echo_enter_f(&self) -> Result {
        if !self.wait_for(|| self.flag(Op::EchoHasInteract, &[]), 2.)? {
            self.action(Op::EchoFarmFailure, &[4.])?;
        }
        self.action(Op::EchoKeyF, &[3.])?;
        self.action(Op::SelectEchoBossLevel, &[])?;
        self.action(Op::Sleep, &[1.])?;
        self.action(Op::ClickRelative, &[0.880, 0.911, 2.])?;
        self.wait_for(|| self.flag(Op::EchoSkipConfirm, &[]), 1.)?;
        self.action(Op::ClickRelative, &[0.908, 0.919, 5.])?;
        self.action(Op::EchoWaitTeam, &[120.])?;
        self.action(Op::StoreEchoRealm, &[1.])?;
        self.action(Op::StoreEchoWorldMode, &[0.])?;
        self.action(Op::StoreEchoTreasure, &[0.])?;
        self.action(Op::StoreEchoJustEntered, &[1.])?;
        self.action(Op::StoreEchoTargetTimeout, &[3.])
    }
    fn echo_seek_condition(&self, moving: bool) -> Result<bool> {
        if self.flag(Op::EchoCombat, &[])? {
            return Ok(true);
        }
        if moving && self.flag(Op::EchoTreasureFound, &[])? {
            return Ok(true);
        }
        if self.scalar(Op::Now, &[])? - self.scalar(Op::EchoLastCameraAt, &[])? >= 0.5 {
            self.action(Op::EchoMiddleClick, &[0.01])?;
            let now = self.scalar(Op::Now, &[])?;
            self.action(Op::StoreEchoLastCameraAt, &[now])?;
        }
        Ok(false)
    }
    fn echo_scroll_interact(&self) -> Result {
        self.action(Op::Sleep, &[0.2])?;
        let end = self.scalar(Op::Now, &[])? + 5.;
        if self.flag(Op::EchoTreasure, &[])? && !self.flag(Op::EchoHasInteract, &[])? {
            self.action(Op::ScrollEchoInteract, &[])?;
            self.action(Op::Sleep, &[0.2])?;
        }
        while self.flag(Op::EchoHasInteract, &[])?
            && !self.flag(Op::EchoCombat, &[])?
            && self.scalar(Op::Now, &[])? < end
        {
            self.action(Op::ScrollEchoInteract, &[])?;
            self.action(Op::Sleep, &[0.2])?;
            self.action(Op::EchoKeyFDefault, &[])?;
            if self.flag(Op::EchoDismissClaim, &[])? {
                self.action(Op::StoreEchoTreasure, &[1.])?;
            }
        }
        Ok(())
    }
    fn echo_treasure_hunt(&self) -> Result {
        if !self.flag(Op::EchoCombat, &[])? && self.flag(Op::EchoTreasureRestart, &[])? {
            self.action(Op::StoreEchoTreasure, &[1.])?;
            if self.flag(Op::EchoTeleportEnabled, &[])? && self.flag(Op::EchoRealm, &[])? {
                self.action(Op::EnterEchoFromF, &[])?;
            } else {
                self.action(Op::ScrollEchoInteractions, &[])?;
            }
        }
        Ok(())
    }
    pub(in crate::services) fn run_echo_farm(&self, method: Method, a: &[f64]) -> Result<[f64; 3]> {
        let mut result = 0.;
        match method {
            Method::EchoRotationTargets => self.echo_rotation_targets(a[0] as i32, a[1] as i32)?,
            Method::WeeklyMaterialReward => result = self.weekly_material_reward()? as u8 as f64,
            Method::AwaitWeeklyMaterialReward => {
                if !self.wait_for_interval(|| self.flag(Op::ReadEchoWeeklyReward, &[]), 12., 0.2)? {
                    self.action(Op::EchoFarmFailure, &[5.])?;
                }
            }
            Method::EchoTeleportEnabled => {
                result = self.flag(Op::EchoConfiguredTeleport, &[])? as u8 as f64
            }
            Method::EchoParameters => self.echo_parameters()?,
            Method::EchoCheckRealm => self.echo_check_realm(a[0])?,
            Method::EchoBossLevel => self.echo_boss_level()?,
            Method::EchoEnterF => self.echo_enter_f()?,
            Method::EchoSeekCondition => {
                result = self.echo_seek_condition(a[0] != 0.)? as u8 as f64
            }
            Method::EchoSeekCombat => {
                result = if a[1] != 0. {
                    self.scalar(Op::WalkEchoSeekCombat, &[a[0], a[2]])?
                } else {
                    self.wait_for_interval(|| self.echo_seek_condition(false), a[0], 0.02)? as u8
                        as f64
                };
            }
            Method::EchoScrollInteract => self.echo_scroll_interact()?,
            Method::EchoTreasureRestart => {
                if !self.flag(Op::EchoTreasureFound, &[])? {
                    return Ok([0.; 3]);
                }
                result = self.scalar(Op::WalkEchoTreasureRestart, &[])?;
            }
            Method::EchoTreasureHunt => self.echo_treasure_hunt()?,
            Method::EchoChangeNight => {
                self.action(Op::EchoEscape, &[1.])?;
                self.action(Op::ClickRelative, &[0.71, 0.96, 2.])?;
                self.action(Op::ClickRelative, &[0.19, 0.14, 1.])?;
                for _ in 0..3 {
                    self.action(Op::ClickRelative, &[0.82, 0.53, 1.])?;
                }
                self.action(Op::ClickRelative, &[0.52, 0.90, 6.])?;
                self.action(Op::EchoEscape, &[1.])?;
                self.action(Op::EchoEscape, &[1.])?;
                let now = self.scalar(Op::Now, &[])?;
                self.action(Op::StoreEchoLastNight, &[now])?;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([result, 0., 0.])
    }
}
