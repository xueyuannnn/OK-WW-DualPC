use super::*;
impl Host {
    fn echo_teleport_found(&self) -> Result<bool> {
        Ok(self.flag(Op::EchoCombat, &[])? || self.flag(Op::EchoHasInteract, &[])?)
    }
    fn echo_teleport(&self) -> Result {
        self.action(Op::ClearEchoWalkResult, &[])?;
        self.action(Op::NavEnsureMain, &[180.])?;
        let category = self.scalar(Op::EchoTeleportCategory, &[])? as i32;
        let (feature, serial, total, special) = match category {
            0 => (0., self.scalar(Op::EchoWeeklySerial, &[])?, 9., false),
            1 => (1., self.scalar(Op::EchoBossSerial, &[])?, 20., false),
            2 => (2., self.scalar(Op::EchoSpecialSerial, &[])? + 5., 15., true),
            _ => {
                self.action(Op::EchoTravelFailure, &[0.])?;
                return Err(HOST_ERROR);
            }
        };
        self.action(Op::OpenEchoBossBook, &[])?;
        self.action(Op::SelectEchoBookCategory, &[feature])?;
        let team = self.flag(
            Op::SelectEchoBookEntry,
            &[serial, total, special as u8 as f64],
        )?;
        if team {
            if category == 0 {
                self.action(Op::SelectEchoBossLevel, &[])?;
                self.action(Op::ClickRelative, &[0.880, 0.911, 2.])?;
                self.wait_for(|| self.flag(Op::EchoSkipConfirm, &[]), 1.)?;
            }
            self.action(Op::StartEchoChallenge, &[])?;
            self.action(Op::StoreEchoJustEntered, &[1.])?;
        } else {
            self.action(Op::StoreEchoEntryAtHeal, &[0.])?;
            self.action(Op::WaitEchoTravel, &[])?;
        }
        self.action(Op::EchoWaitTeam, &[120.])?;
        self.action(Op::Sleep, &[2.])?;
        if team {
            self.action(Op::StoreEchoWalkResult, &[0.])?;
        } else {
            if !self.wait_for(|| self.echo_teleport_found(), 1.5)? {
                self.action(Op::EchoMiddleClick, &[0.01])?;
                self.action(Op::Sleep, &[0.2])?;
                self.action(Op::StoreEchoLastCameraAt, &[-1.])?;
                if !self.flag(Op::WalkEchoTeleportEntry, &[])? {
                    self.action(Op::EchoTravelFailure, &[1.])?;
                }
            }
            if self.flag(Op::EchoCombat, &[])? {
                self.action(Op::StoreEchoWalkResult, &[1.])?;
            } else {
                self.action(Op::EnterEchoFromF, &[])?;
                self.action(Op::StoreEchoWalkResult, &[0.])?;
            }
        }
        let realm = self.flag(Op::EchoWalkResultRealm, &[])?;
        self.action(Op::StoreEchoRealm, &[realm as u8 as f64])?;
        self.action(
            Op::StoreEchoWorldMode,
            &[(!self.flag(Op::EchoRealm, &[])?) as u8 as f64],
        )?;
        self.action(Op::StoreEchoTreasure, &[0.])?;
        let timeout = if self.flag(Op::EchoRealm, &[])? {
            3.
        } else {
            1.2
        };
        self.action(Op::StoreEchoTargetTimeout, &[timeout])
    }
    fn echo_nearest(&self) -> Result {
        if !self.flag(Op::EchoAimBossEmpty, &[])? {
            self.action(Op::NavEnsureMain, &[180.])?;
            self.action(Op::OpenEchoDefaultBook, &[])?;
            self.action(Op::SelectEchoAimMonster, &[])?;
            self.action(Op::ClickRelative, &[0.89, 0.92, 1.])?;
            self.action(Op::ClickRelative, &[0.89, 0.92, 0.])?;
            self.action(Op::EchoWaitTeam, &[30.])?;
            return Ok(());
        }
        self.action(Op::OpenEchoMap, &[])?;
        if self.flag(Op::EchoNamelessBoss, &[])? {
            self.action(Op::ClickRelative, &[0.06, 0.50, 0.5])?;
        }
        self.action(Op::ReadEchoMapArea, &[])?;
        self.action(Op::FindEchoCheckedMarker, &[])?;
        if !self.flag(Op::HasEchoMapMarker, &[])? {
            for angle in [0., 90., 180.] {
                self.action(Op::FindEchoUncheckedMarker, &[angle])?;
                if self.flag(Op::HasEchoNextMarker, &[])?
                    && (!self.flag(Op::HasEchoMapMarker, &[])?
                        || self.scalar(Op::EchoNextMarkerConfidence, &[])?
                            > self.scalar(Op::EchoMarkerConfidence, &[])?)
                {
                    self.action(Op::StoreEchoMapMarker, &[])?;
                }
            }
        }
        if !self.flag(Op::HasEchoMapMarker, &[])? {
            self.action(Op::EchoTravelFailure, &[2.])?;
        }
        self.action(Op::ClickEchoMapMarker, &[])?;
        self.action(Op::WaitEchoTravel, &[])?;
        self.action(Op::EchoWaitTeam, &[30.])
    }
    fn echo_recognize_boss(&self) -> Result {
        if !self.flag(Op::EchoChineseGame, &[])? {
            return Ok(());
        }
        self.action(Op::ReadEchoBossNames, &[])?;
        for entry in 0..4 {
            if self.flag(Op::EchoBossNameMatches, &[entry as f64])? {
                self.action(Op::StoreEchoAimBoss, &[entry as f64])?;
                break;
            }
        }
        Ok(())
    }
    fn echo_heal(&self) -> Result {
        if self.flag(Op::EchoRealm, &[])? {
            if !self.flag(Op::EchoTeleportEnabled, &[])? {
                self.action(Op::EchoTravelFailure, &[3.])?;
            }
            self.action(Op::ExitEchoRealm, &[120.])?;
            if !self.flag(Op::EchoEntryAtHeal, &[])? {
                self.action(Op::HealEchoBeacon, &[])?;
            }
        } else {
            self.action(Op::HealEchoBeacon, &[])?;
            self.action(Op::WalkEchoBackFromBeacon, &[])?;
            self.action(Op::FindEchoNearestBoss, &[])?;
            self.action(Op::Sleep, &[0.5])?;
            self.action(Op::SeekEchoCombat, &[12., 1., 1.])?;
            self.action(Op::HuntEchoTreasure, &[])?;
        }
        Ok(())
    }
    fn echo_weekly_count(&self) -> Result<f64> {
        self.action(Op::EnsureEchoMainDefault, &[])?;
        self.action(Op::OpenEchoBossBook, &[])?;
        self.action(Op::SelectEchoBookCategory, &[0.])?;
        self.action(Op::ReadEchoWeeklyCountLines, &[])?;
        let mut remaining = 0;
        for index in 0..self.scalar(Op::EchoWeeklyCountLines, &[])? as usize {
            if self.flag(Op::EchoWeeklyCountMatch, &[index as f64])? {
                remaining = self.scalar(Op::EchoWeeklyCountValue, &[])? as i32;
                break;
            }
        }
        let current = self.scalar(Op::ReadEchoCurrentStamina, &[])? as i32;
        if current < 0 {
            self.action(Op::EchoTravelFailure, &[4.])?;
        }
        self.action(Op::EnsureEchoMainDefault, &[])?;
        Ok(remaining.min(current / 60) as f64)
    }
    pub(super) fn run_echo_travel(&self, method: Method, _a: &[f64]) -> Result<[f64; 3]> {
        let mut result = 0.;
        match method {
            Method::EchoTeleport => self.echo_teleport()?,
            Method::EchoTeleportFound => result = self.echo_teleport_found()? as u8 as f64,
            Method::EchoTeleportSeek => {
                if self.echo_teleport_found()? {
                    return Ok([1., 0., 0.]);
                }
                if self.scalar(Op::Now, &[])? - self.scalar(Op::EchoLastCameraAt, &[])? >= 0.5 {
                    self.action(Op::EchoMiddleClick, &[0.01])?;
                    let now = self.scalar(Op::Now, &[])?;
                    self.action(Op::StoreEchoLastCameraAt, &[now])?;
                }
            }
            Method::EchoNearestBoss => self.echo_nearest()?,
            Method::EchoRecognizeBoss => self.echo_recognize_boss()?,
            Method::EchoHeal => self.echo_heal()?,
            Method::EchoWeeklyCount => result = self.echo_weekly_count()?,
            _ => return Err(INVALID_ACTION),
        }
        Ok([result, 0., 0.])
    }
}
