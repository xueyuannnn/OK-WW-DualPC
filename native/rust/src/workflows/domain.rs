use super::*;
impl Host {
    fn domain_cost(kind: f64) -> i32 {
        if kind == 0. {
            60
        } else {
            40
        }
    }
    fn domain_limit(&self, requested: i32, total: i32, cost: i32) -> Result<i32> {
        if !(0..=100000).contains(&requested) || cost <= 0 {
            self.fail(100.)?;
        }
        if total < 0 {
            self.fail(101.)?;
        }
        Ok(requested.min(total / cost))
    }
    fn domain_enter(&self, kind: f64) -> Result {
        if kind == 0. {
            self.action(Op::NavCategory, &[0.])?;
            let serial = self.scalar(Op::FarmEntrySerial, &[0.])?;
            self.action(Op::NavFarmEntry, &[0., serial])?;
        } else if kind == 1. {
            self.action(Op::NavCategory, &[1.])?;
            let serial = self.scalar(Op::FarmEntrySerial, &[1.])?;
            self.action(Op::NavFarmEntry, &[1., serial])?;
            self.action(Op::ClickRelative, &[0.891, 0.910, 1.])?;
        } else {
            self.action(Op::NavCategory, &[2.])?;
            let index = self.scalar(Op::MaterialKind, &[])?;
            self.action(Op::ClickRelative, &[0.973, 0.8806, 1.])?;
            self.action(Op::ClickRelative, &[0.898, 0.533 + index * 0.14, 1.])?;
            self.action(Op::ClickRelative, &[0.93, 0.90, 1.])?;
        }
        self.action(Op::NavStartChallenge, &[])?;
        self.action(Op::NavWaitTeam, &[if kind == 0. { 120. } else { 100. }])?;
        self.action(Op::Sleep, &[1.])
    }
    fn domain_next(&self, kind: f64) -> Result {
        if kind == 0. {
            self.action(Op::ClickDomainAnchor, &[0., 0.2])?;
            self.wait_for(|| self.flag(Op::SkipConfirm, &[]), 3.)?;
        } else {
            self.action(Op::ClickRelative, &[0.68, 0.84, 1.])?;
            if self.wait_for(|| self.flag(Op::FindFarmConfirm, &[0.6]), 2.)? {
                self.action(Op::ClickRelative, &[0.49, 0.55, 0.5])?;
                self.action(Op::ClickFarmConfirm, &[0., 0.5])?;
                if self.wait_for(|| self.flag(Op::FindFarmConfirm, &[0.6]), 1.)? {
                    self.action(Op::ClickFarmConfirm, &[1., 0.5])?;
                }
            }
        }
        self.action(Op::NavWaitTeam, &[120.])?;
        self.action(Op::Sleep, &[1.])
    }
    fn domain_leave(&self, kind: f64) -> Result {
        if kind == 0. {
            self.action(Op::ClickDomainAnchor, &[1., 2.])?;
        } else {
            self.action(Op::ClickRelative, &[0.42, 0.84, 2.])?;
        }
        self.action(Op::NavEnsureMain, &[120.])
    }
    fn domain_run(&self, kind: f64, counted: bool) -> Result {
        self.action(Op::DomainTaskScope, &[kind, counted as u8 as f64, 0.])?;
        self.action(Op::RecoverStartingPopup, &[])?;
        self.action(Op::NavEnsureMain, &[180.])?;
        self.action(Op::Sleep, &[0.5])?;
        self.action(Op::ReadFarmConfig, &[])?;
        let mut must_use = 0;
        if counted {
            let requested = self.scalar(Op::FarmRequested, &[kind])? as i32;
            if requested <= 0 {
                return Ok(());
            }
            self.action(Op::OpenBossBook, &[])?;
            let stamina = self.call(Op::ReadStamina, &[])?;
            let cost = Self::domain_cost(kind);
            must_use = self.domain_limit(requested, stamina[2] as i32, cost)? * cost;
            self.action(Op::NavEnsureMain, &[30.])?;
            if must_use == 0 {
                self.action(Op::StoreFarmResult, &[0., 0., 0., 1.])?;
                return Ok(());
            }
        }
        self.action(Op::CallFarm, &[kind, must_use as f64, counted as u8 as f64])
    }
    fn domain_farm(&self, kind: f64, mut must_use: i32, exact: bool) -> Result {
        self.action(Op::DomainTaskScope, &[kind, exact as u8 as f64, 1.])?;
        if !(0..=24000000).contains(&must_use) {
            self.fail(102.)?;
        }
        let mut used = 0_i32;
        let mut completed = 0_i32;
        let mut recoveries = 0;
        let target = must_use;
        self.action(Op::FarmProgress, &[kind, 0., target as f64, -1.])?;
        if exact && must_use == 0 {
            return Ok(());
        }
        let once = Self::domain_cost(kind);
        let mut entry_at_heal = false;
        loop {
            self.action(Op::Checkpoint, &[])?;
            self.action(Op::OpenBossBook, &[])?;
            let mut stamina = self.call(Op::ReadStamina, &[])?;
            if stamina[0] < 0. && kind == 0. {
                self.action(Op::ClickRelative, &[0.04, 0.4, 1.])?;
                stamina = self.call(Op::ReadStamina, &[])?;
            }
            if stamina[0] < 0. {
                self.fail(103.)?;
            }
            if stamina[2] < once as f64
                || (kind != 0.
                    && (stamina[2] < must_use as f64
                        || (must_use == 0 && stamina[0] < once as f64)))
            {
                self.action(Op::NavEnsureMain, &[30.])?;
                self.action(
                    Op::StoreFarmResult,
                    &[used as f64, completed as f64, recoveries as f64, 1.],
                )?;
                return Ok(());
            }
            self.domain_enter(kind)?;
            let result = (|| {
                let mut wrong_rewards = 0;
                loop {
                    if kind != 0. {
                        self.action(Op::WalkToFarmInteract, &[])?;
                        self.action(Op::FarmPick, &[1.])?;
                    }
                    self.action(
                        Op::CombatFarmRound,
                        &[(kind == 0.) as u8 as f64, entry_at_heal as u8 as f64],
                    )?;
                    if kind != 0. {
                        self.action(Op::Sleep, &[3.])?;
                    }
                    self.action(Op::WalkToTreasure, &[])?;
                    self.action(Op::FarmPick, &[0.])?;
                    if kind == 0. {
                        self.action(Op::Sleep, &[2.])?;
                        if !self.flag(Op::ClaimSign, &[])? {
                            self.action(Op::Key, &[0., 1.])?;
                            self.action(Op::NavConfirm, &[0.])?;
                            self.action(Op::NavWaitTeam, &[120.])?;
                            wrong_rewards += 1;
                            if wrong_rewards >= 3 {
                                self.fail(104.)?;
                            }
                            continue;
                        }
                    }
                    let spend = self.call(
                        Op::ClaimFarmStamina,
                        &[once as f64, must_use as f64, exact as u8 as f64],
                    )?;
                    let spent = spend[0] as i32;
                    used = used.wrapping_add(spent);
                    must_use = (must_use - spent).max(0);
                    if spent > 0 && self.flag(Op::HasStaminaCallback, &[])? {
                        self.action(Op::StaminaCallback, &[spent as f64])?;
                    }
                    if spent > 0 {
                        completed = completed.wrapping_add(1);
                    }
                    if self.flag(Op::HasInfo, &[])? {
                        self.action(Op::FarmSpentInfo, &[used as f64])?;
                    }
                    self.action(
                        Op::FarmProgress,
                        &[kind, used as f64, target as f64, completed as f64],
                    )?;
                    self.action(Op::Sleep, &[4.])?;
                    if spend[1] == 0. {
                        self.domain_leave(kind)?;
                        self.action(
                            Op::StoreFarmResult,
                            &[
                                used as f64,
                                completed as f64,
                                recoveries as f64,
                                (spent == 0) as u8 as f64,
                            ],
                        )?;
                        return Ok(true);
                    }
                    self.domain_next(kind)?;
                }
            })();
            match result {
                Ok(true) => return Ok(()),
                Err(code) => {
                    if self.scalar(Op::ExceptionKind, &[])? != 3. {
                        return Err(code);
                    }
                    self.action(Op::ClearException, &[])?;
                    recoveries += 1;
                    if recoveries >= 3 {
                        self.action(Op::NavEnsureMain, &[120.])?;
                        return self.fail(105.);
                    }
                    entry_at_heal = true;
                    if self.flag(Op::HasInfo, &[])? {
                        self.action(Op::FarmRecoveryInfo, &[recoveries as f64])?;
                    }
                    self.action(Op::Sleep, &[1.])?;
                }
                Ok(false) => unreachable!(),
            }
        }
    }
    pub(super) fn run_domain(&self, method: Method, a: &[f64]) -> Result<[f64; 3]> {
        match method {
            Method::DomainCost => return Ok([Self::domain_cost(a[0]) as f64, 0., 0.]),
            Method::DomainCountLimit => {
                return Ok([
                    self.domain_limit(a[0] as i32, a[1] as i32, a[2] as i32)? as f64,
                    0.,
                    0.,
                ])
            }
            Method::DomainEnter => self.domain_enter(a[0])?,
            Method::DomainNext => self.domain_next(a[0])?,
            Method::DomainLeave => self.domain_leave(a[0])?,
            Method::DomainRun => self.domain_run(a[0], a[1] != 0.)?,
            Method::DomainFarm => self.domain_farm(a[0], a[1] as i32, a[2] != 0.)?,
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
