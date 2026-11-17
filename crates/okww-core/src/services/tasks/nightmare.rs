use crate::services::*;
impl Host {
    fn nest_target(&self, tab: f64) -> Result {
        let [w, h, _] = self.call(Op::NestScreen, &[])?;
        if !(0. ..4.).contains(&tab) || h <= 0. {
            return self.fail(300.);
        }
        let count = self.scalar(Op::NestLineCount, &[])? as usize;
        for index in 0..count {
            self.action(Op::NestLineMatches, &[index as f64])?;
            while self.flag(Op::NestMatchHasNext, &[])? {
                let [current, maximum, _] = self.call(Op::NextNestMatch, &[])?;
                if current >= maximum || ![24., 36., 48., 41.].contains(&maximum) {
                    continue;
                }
                let [center, y, height] = self.call(Op::NestLineGeometry, &[index as f64])?;
                let rounded = self.scalar(Op::PixelRound, &[center / h / 0.02])?;
                self.action(Op::BuildNestKey, &[tab, rounded])?;
                if !self.flag(Op::NestUnreachable, &[])? {
                    self.action(Op::StoreNestTarget, &[w * 0.9, y - height * 0.9, tab])?;
                    return Ok(());
                }
            }
        }
        Ok(())
    }
    fn nest_tab(&self, index: f64) -> Result {
        self.action(Op::NestCategory, &[if index < 2. { 0. } else { 1. }])?;
        if index == 1. {
            self.action(Op::ClickRelative, &[0.973, 0.8806, 1.])?;
        }
        if index == 3. {
            self.action(Op::ClickRelative, &[3737. / 3840., 0.54, 1.])?;
        }
        Ok(())
    }
    fn nest_travel(&self) -> Result<bool> {
        self.wait_for(|| self.flag(Op::FindNestTravel, &[0.7]), 1.)?;
        if self.flag(Op::NestTravelExists, &[])? {
            self.action(Op::ClickNestTravel, &[1.])?;
            if self.flag(Op::FindNestConfirm, &[0.6])? {
                self.action(Op::ClickNestConfirm, &[1.])?;
            }
        }
        if self.flag(Op::NestTravelExists, &[])?
            && !self.flag(Op::NestTravelStillVisible, &[0.7])?
            && self.flag(Op::NestWaitTeam, &[120., 0.])?
        {
            return Ok(true);
        }
        self.action(Op::MarkNestUnreachable, &[])?;
        if self.flag(Op::HasInfo, &[])? {
            self.action(Op::NestSkippedInfo, &[])?;
        }
        self.action(Op::Key, &[0., 1.])?;
        Ok(false)
    }
    fn captured_notice(&self) -> Result<bool> {
        self.action(Op::CapturedNoticeArea, &[])?;
        for index in 0..2 {
            if self.flag(Op::CapturedNoticeFeature, &[index as f64, 0.6])? {
                self.action(Op::SetCaptured, &[1.])?;
            }
        }
        self.flag(Op::Captured, &[])
    }
    fn nest_seek_tick(&self, last: f64) -> Result<[f64; 3]> {
        if self.flag(Op::CombatActive, &[])? {
            return Ok([1., last, 0.]);
        }
        let mut last = last;
        if self.scalar(Op::Now, &[])? - last >= 0.5 {
            self.action(Op::MiddleClick, &[0.01])?;
            last = self.scalar(Op::Now, &[])?;
        }
        Ok([0., last, 0.])
    }
    fn nest_wait_combat(&self, timeout: f64) -> Result<bool> {
        let last = Cell::new(-1.);
        self.wait_for_interval(
            || {
                let observed = self.nest_seek_tick(last.get())?;
                last.set(observed[1]);
                Ok(observed[0] != 0.)
            },
            timeout,
            0.02,
        )
    }
    fn nest_walk_echo(&self) -> Result<bool> {
        Ok(self.flag(Op::NestWalkInteract, &[])? && self.flag(Op::NestPick, &[1.])?)
    }
    fn nest_fight(&self) -> Result {
        self.action(Op::ClickNestTarget, &[2.])?;
        if !self.wait_for(|| self.flag(Op::FindNestInterface, &[]), 10.)? {
            return self.fail(301.);
        }
        self.action(Op::Sleep, &[0.5])?;
        let team = self.flag(Op::NestInterfaceTeam, &[])?;
        if team {
            self.action(Op::NavStartChallenge, &[])?;
            self.flag(Op::NestWaitTeam, &[120., 1.])?;
        } else {
            if !self.nest_travel()? {
                return Ok(());
            }
            self.action(Op::Sleep, &[1.])?;
            let mut prompts = 0;
            while self.flag(Op::NestInteractVisible, &[])? {
                prompts += 1;
                if prompts > 12 {
                    return self.fail(302.);
                }
                self.action(Op::Key, &[6., 1.])?;
                self.flag(Op::NestWaitTeam, &[40., 0.])?;
            }
            self.action(Op::Sleep, &[2.])?;
            self.action(Op::NestWalkApproach, &[])?;
        }
        let mut wait = 10.;
        for _wave in 0..30 {
            let need_find = match self.flag(Op::NestCombatRound, &[wait]) {
                Ok(value) => value,
                Err(code) => {
                    if self.scalar(Op::ExceptionKind, &[])? != 3. {
                        return Err(code);
                    }
                    self.action(Op::ClearException, &[])?;
                    let deaths = self.scalar(Op::NestDeaths, &[])? + 1.;
                    self.action(Op::SetNestDeaths, &[deaths])?;
                    if deaths >= 3. {
                        self.action(Op::MarkNestUnreachable, &[])?;
                    }
                    if self.flag(Op::HasInfo, &[])? {
                        self.action(Op::NestRecoveryInfo, &[deaths])?;
                    }
                    return Ok(());
                }
            };
            let early = self.flag(Op::CaptureOnly, &[])?
                && (self.flag(Op::Captured, &[])?
                    || self.wait_for(|| self.captured_notice(), 3.)?);
            if !early {
                self.action(Op::Sleep, &[3.])?;
                let mut dropped = need_find && self.nest_walk_echo()?;
                if need_find && !dropped {
                    dropped = self.flag(Op::NestFindEcho, &[30.])?;
                    if !dropped && !team {
                        self.action(Op::NavEnsureMain, &[30.])?;
                        self.action(Op::OpenBossBook, &[])?;
                        self.nest_tab(self.scalar(Op::NestTargetTab, &[])?)?;
                        self.action(Op::Sleep, &[1.])?;
                        self.action(Op::ClickNestTarget, &[2.])?;
                        if self.wait_for(|| self.flag(Op::FindNestTravelProbe, &[]), 5.)?
                            && self.nest_travel()?
                        {
                            self.action(Op::Sleep, &[2.])?;
                            self.action(Op::NestRunForward, &[2.])?;
                            dropped =
                                self.nest_walk_echo()? || self.flag(Op::NestFindEcho, &[30.])?;
                        }
                    }
                }
                let captured = self.flag(Op::Captured, &[])?;
                self.action(Op::SetCaptured, &[(captured || dropped) as u8 as f64])?;
            }
            if self.flag(Op::CaptureOnly, &[])? || !self.nest_wait_combat(3.)? {
                if team {
                    self.action(Op::LeaveNest, &[])?;
                }
                self.action(Op::Sleep, &[1.])?;
                return Ok(());
            }
            wait = 1.;
        }
        self.fail(303.)
    }
    fn nest_combat_check(&self) -> Result<bool> {
        if self.flag(Op::CaptureOnly, &[])? {
            self.flag(Op::NestPick, &[0.])?;
            if self.captured_notice()? {
                return Ok(false);
            }
        }
        Ok(true)
    }
    fn nest_run(&self, capture: bool) -> Result<bool> {
        self.action(Op::NestTaskScope, &[])?;
        self.action(Op::SetCaptureOnly, &[capture as u8 as f64])?;
        self.action(Op::SetCaptured, &[0.])?;
        self.action(Op::ClearUnreachableNests, &[])?;
        self.action(Op::ClearNestDeaths, &[])?;
        self.action(Op::InstallNestCombatCheck, &[])?;
        let result = (|| {
            if !self.flag(Op::NavInWorld, &[])? && self.flag(Op::CompletedNest, &[])? {
                self.action(Op::LeaveNest, &[])?;
            }
            self.action(Op::RecoverStartingPopup, &[])?;
            self.action(Op::NavEnsureMain, &[30.])?;
            self.action(
                Op::ReadNestConfig,
                &[self.flag(Op::NestSuppliedEmpty, &[])? as u8 as f64],
            )?;
            if self.scalar(Op::NestChoiceCount, &[])? == 0. {
                self.action(Op::DefaultNestChoices, &[])?;
            }
            let mut queue = [0_usize; 4];
            let mut size = 0;
            if self.flag(Op::NestChoice, &[0.])? {
                queue[size] = 0;
                queue[size + 1] = 1;
                size += 2;
            }
            if self.flag(Op::NestChoice, &[1.])? {
                queue[size] = 2;
                queue[size + 1] = 3;
                size += 2;
            }
            let mut front = 0;
            let mut visited = 0;
            while front < size {
                let tab = queue[front] as f64;
                let capture = self.flag(Op::CaptureOnly, &[])?;
                self.action(
                    Op::NestProgress,
                    &[capture as u8 as f64, tab, visited as f64],
                )?;
                self.action(Op::OpenBossBook, &[])?;
                self.nest_tab(tab)?;
                if !self.flag(Op::ReadNextNestTarget, &[tab])? {
                    front += 1;
                    continue;
                }
                let attempts = self.scalar(Op::NestTargetAttempts, &[])? + 1.;
                self.action(Op::SetNestTargetAttempts, &[attempts])?;
                if attempts > 4. {
                    self.action(Op::MarkNestUnreachable, &[])?;
                    continue;
                }
                self.nest_fight()?;
                visited += 1;
                if self.flag(Op::CaptureOnly, &[])? && self.flag(Op::Captured, &[])? {
                    break;
                }
            }
            self.action(Op::NavEnsureMain, &[30.])
        })();
        if result.is_err() {
            // Clearing before cleanup preserves C++'s rule that an exception
            // thrown while restoring the callback replaces the caught one.
            self.action(Op::ClearException, &[])?;
            self.action(Op::RestoreNestCombatCheck, &[])?;
            self.action(Op::RethrowHandled, &[])?;
            return Err(HOST_ERROR);
        }
        self.action(Op::RestoreNestCombatCheck, &[])?;
        self.flag(Op::Captured, &[])
    }
    pub(in crate::services) fn run_nightmare(&self, method: Method, a: &[f64]) -> Result<[f64; 3]> {
        let mut value = 0.;
        match method {
            Method::NextNestTarget => self.nest_target(a[0])?,
            Method::NestTab => self.nest_tab(a[0])?,
            Method::NestTravel => value = self.nest_travel()? as u8 as f64,
            Method::NestCapturedNotice => value = self.captured_notice()? as u8 as f64,
            Method::NestSeekTick => return self.nest_seek_tick(a[0]),
            Method::NestWaitCombat => value = self.nest_wait_combat(a[0])? as u8 as f64,
            Method::NestWalkEcho => value = self.nest_walk_echo()? as u8 as f64,
            Method::NestFight => self.nest_fight()?,
            Method::NestRun => value = self.nest_run(a[0] != 0.)? as u8 as f64,
            Method::NestCombatCheck => value = self.nest_combat_check()? as u8 as f64,
            _ => return Err(INVALID_ACTION),
        }
        Ok([value, 0., 0.])
    }
}
