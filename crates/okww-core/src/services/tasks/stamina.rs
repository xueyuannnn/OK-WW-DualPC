use crate::services::*;
impl Host {
    fn claim_stamina(&self, once: f64, remaining: f64, exact: bool) -> Result<[f64; 3]> {
        self.action(Op::Sleep, &[1.])?;
        let before = self.call(Op::ReadBeforeStamina, &[])?;
        let twice = !self.flag(Op::SingleRewardMode, &[])?;
        let plan = self.call(
            Op::PlanStaminaClaim,
            &[once, remaining, exact as u8 as f64, twice as u8 as f64],
        )?;
        if plan[0] == 0. {
            return Ok(plan);
        }
        if !self.flag(Op::FindRewardButton, &[(plan[0] == once) as u8 as f64])?
            || !self.flag(Op::ClaimSign, &[])?
        {
            self.fail(415.)?;
        }
        if self.flag(Op::HasInfo, &[])? {
            self.action(Op::RewardClaimInfo, &[plan[0] / once, plan[0]])?;
        }
        self.action(Op::ClickRewardButton, &[2.])?;
        let refill = self.observe(|| self.flag(Op::GemStaminaDialog, &[]), 2., 0.5)?;
        if refill {
            if before[0] >= plan[0] || before[1] < plan[0] - before[0] {
                self.fail(409.)?;
            }
            self.action(Op::PrepareReservePoint, &[])?;
            self.action(Op::ClickReservePoint, &[1.])?;
            self.action(Op::ClickReservePoint, &[1.])?;
            self.action(Op::Key, &[0., 1.])?;
            if !self.flag(Op::FindRetryReward, &[])? || !self.flag(Op::ClaimSign, &[])? {
                self.fail(410.)?;
            }
            self.action(Op::ClickRetryReward, &[1.])?;
        }
        if !self.wait_for(
            || Ok(!self.flag(Op::ClaimSign, &[])? && !self.flag(Op::GemStaminaDialog, &[])?),
            8.,
        )? {
            self.fail(411.)?;
        }
        Ok(plan)
    }
    fn cube_dialog(&self) -> Result<bool> {
        self.flag(Op::CubeDialog, &[])
    }
    fn open_cube(&self) -> Result<bool> {
        if self.cube_dialog()? {
            return Ok(true);
        }
        let count = self.scalar(Op::ReadCubeOpenCounters, &[])? as usize;
        if count == 0 {
            return Ok(false);
        }
        let mut selected = 0;
        for index in 1..count {
            if self.scalar(Op::CounterMaximum, &[selected as f64])?
                < self.scalar(Op::CounterMaximum, &[index as f64])?
            {
                selected = index;
            }
        }
        let [x, width, _] = self.call(Op::CubeOpenCounterBox, &[selected as f64])?;
        let [w, h, _] = self.call(Op::ImageSize, &[])?;
        let x = self.scalar(Op::PixelRound, &[x + width + 0.027 * w])?;
        let y = self.scalar(Op::PixelRound, &[0.06 * h])?;
        self.action(Op::ClickCubeCounterPoint, &[x, y, 0.4])?;
        self.action(Op::Refresh, &[])?;
        self.cube_dialog()
    }
    fn cube_stock(&self) -> Result<Option<i32>> {
        self.action(Op::ClickRelative, &[0.497, 0.444, 0.7])?;
        let value = Cell::new(None);
        self.wait_for(
            || {
                self.action(Op::Refresh, &[])?;
                let count = self.scalar(Op::ReadCubeStockDescription, &[])? as usize;
                for index in 0..count {
                    self.action(Op::AppendCubeStockDescription, &[index as f64])?;
                }
                if !self.flag(Op::CubeStockDescriptionValid, &[])? {
                    return Ok(false);
                }
                let count = self.scalar(Op::ReadCubeStockCounts, &[])? as usize;
                let mut valid = 0;
                let mut selected = 0;
                for index in 0..count {
                    if !self.flag(Op::CubeStockCountMatches, &[index as f64])? {
                        continue;
                    }
                    let [number, ok, _] = self.call(Op::CubeStockCountValue, &[index as f64])?;
                    if ok != 0. {
                        valid += 1;
                        selected = number as i32;
                    }
                }
                if valid != 1 {
                    return Ok(false);
                }
                value.set(Some(selected));
                Ok(true)
            },
            3.,
        )?;
        Ok(value.get())
    }
    fn cancel_cube(&self) -> Result {
        if self.flag(Op::FindCubeCancel, &[])? {
            self.action(Op::ClickCubeCancel, &[0.2])?;
        }
        Ok(())
    }
    fn use_daily_cube(&self, consume: bool) -> Result<bool> {
        if self.scalar(Op::BudgetRemaining, &[])? == 0. || self.flag(Op::BudgetPending, &[])? {
            return Ok(false);
        }
        let before = self.call(Op::ReadBeforeStamina, &[])?;
        if before[0] < 0. {
            return Ok(false);
        }
        if !self.open_cube()? {
            return Ok(false);
        }
        let Some(owned) = self.cube_stock()?.filter(|stock| *stock > 0) else {
            self.cancel_cube()?;
            return Ok(false);
        };
        if !self.flag(Op::FindCubeConfirm, &[])? {
            self.cancel_cube()?;
            return Ok(false);
        }
        if !consume {
            if self.flag(Op::HasInfo, &[])? {
                self.action(Op::CubeCheckInfo, &[owned as f64])?;
            }
            self.action(Op::Sleep, &[0.8])?;
            self.cancel_cube()?;
            return Ok(true);
        }
        self.action(Op::ReserveCubeBudget, &[])?;
        self.action(Op::ClickCubeConfirm, &[0.5])?;
        if !self.wait_for(
            || {
                self.action(Op::Refresh, &[])?;
                let after = self.call(Op::ReadAfterStamina, &[])?;
                Ok(after[0] >= (before[0] as i32).wrapping_add(60) as f64 && after[1] == before[1])
            },
            5.,
        )? {
            self.fail(412.)?;
        }
        if !self.open_cube()? {
            self.fail(413.)?;
        }
        let remaining = self.cube_stock()?;
        self.cancel_cube()?;
        if remaining != Some(owned - 1) {
            self.fail(414.)?;
        }
        self.action(Op::VerifyCubeBudget, &[])?;
        if self.flag(Op::HasInfo, &[])? {
            self.action(Op::CubeUsedInfo, &[])?;
        }
        Ok(true)
    }
    fn stamina_counters(&self) -> Result {
        let count = self.scalar(Op::StaminaLineCount, &[])? as usize;
        for index in 0..count {
            if !self.flag(Op::ParseStaminaCounter, &[index as f64])? {
                continue;
            }
            let [_, maximum, valid] = self.call(Op::ParsedStaminaCounter, &[])?;
            if valid != 0. && (20. ..=9999.).contains(&maximum) {
                self.action(Op::AppendStaminaCounter, &[index as f64])?;
            }
        }
        Ok(())
    }
    fn journal_integer(&self, high: f64) -> Result<f64> {
        let n = self.scalar(Op::JournalNumber, &[])?;
        if !self.flag(Op::JournalIsNumber, &[])?
            || !n.is_finite()
            || n.floor() != n
            || n < 0.
            || n > high
        {
            self.fail(400.)?;
        }
        Ok(n)
    }
    fn stamina_values(&self) -> Result<[f64; 3]> {
        let count = self.scalar(Op::ReadStaminaCounters, &[])? as usize;
        if count == 0 {
            return Ok([-1.; 3]);
        }
        let mut selected = 0;
        for index in 1..count {
            if self.scalar(Op::CounterMaximum, &[selected as f64])?
                < self.scalar(Op::CounterMaximum, &[index as f64])?
            {
                selected = index;
            }
        }
        self.action(Op::SelectMainCounter, &[selected as f64])?;
        let mut backup = 0;
        let mut rightmost = i32::MIN as f64;
        let count = self.scalar(Op::StaminaLineCount, &[])? as usize;
        for index in 0..count {
            if !self.flag(Op::ParseReserveCounter, &[index as f64])? {
                continue;
            }
            let [x, w, h] = self.call(Op::ReserveLineBounds, &[index as f64])?;
            let [main_x, main_y, main_h] = self.call(Op::MainCounterBounds, &[])?;
            if x + w <= main_x
                && (self.scalar(Op::ReserveCenterY, &[index as f64])? - main_y).abs()
                    <= 1.5 * h.max(main_h)
                && x > rightmost
            {
                backup = self.scalar(Op::ReserveNumber, &[])? as i32;
                rightmost = x;
            }
        }
        let current = self.scalar(Op::MainCounterCurrent, &[])? as i32;
        Ok([
            current as f64,
            backup as f64,
            current.wrapping_add(backup) as f64,
        ])
    }
    fn read_stamina(&self) -> Result<[f64; 3]> {
        let observed = Cell::new([-1.; 3]);
        self.wait_for_interval(
            || {
                self.action(Op::ReadStaminaText, &[])?;
                let mut values = self.call(Op::StaminaTextValues, &[])?;
                if values[0] < 0. {
                    self.action(Op::ReadStaminaScaledText, &[])?;
                    values = self.call(Op::StaminaTextValues, &[])?;
                }
                observed.set(values);
                Ok(values[0] >= 0. && values[1] >= 0. && values[2] == values[0] + values[1])
            },
            10.,
            0.2,
        )?;
        let values = observed.get();
        if values[0] < 0. {
            self.action(Op::StaminaReadDiagnostic, &[])?;
        }
        if values[0] >= 0. && self.flag(Op::HasInfo, &[])? {
            self.action(Op::CurrentStaminaInfo, &[values[0]])?;
            self.action(Op::ReserveStaminaInfo, &[values[1]])?;
        }
        Ok(values)
    }
    fn cube_read(&self, allow_pending: bool) -> Result {
        self.action(Op::FreshCubeJournal, &[])?;
        if !self.flag(Op::CubeFileExists, &[])? {
            return self.action(Op::UseFreshCubeJournal, &[]);
        }
        self.action(Op::ReadCubeJournal, &[])?;
        self.scalar(Op::ValidateReserved, &[4.])?;
        if !self.flag(Op::JournalPendingIsBool, &[])? {
            return self.fail(401.);
        }
        if self.flag(Op::JournalPending, &[])? && !allow_pending {
            return self.fail(402.);
        }
        if !self.flag(Op::JournalDayMatches, &[])? && !self.flag(Op::JournalPending, &[])? {
            return self.action(Op::UseFreshCubeJournal, &[]);
        }
        let verified = self.scalar(Op::ValidateVerified, &[4.])?;
        if verified > self.scalar(Op::JournalReserved, &[])? {
            return self.fail(403.);
        }
        Ok(())
    }
    fn cube_reserve(&self) -> Result {
        self.action(Op::CreateJournalDirectory, &[])?;
        if !self.flag(Op::LockCubeJournal, &[])? {
            return self.fail(404.);
        }
        self.action(Op::ReloadCubeState, &[0.])?;
        if self.flag(Op::CubePending, &[])?
            || self.scalar(Op::CubeRemaining, &[])? == 0.
            || self.flag(Op::CubeOwned, &[])?
        {
            return self.fail(405.);
        }
        let used = self.scalar(Op::CubeUsed, &[])?;
        self.action(Op::SetCubeReserved, &[used + 1.])?;
        self.action(Op::SetCubePending, &[1.])?;
        self.action(Op::WriteCubeJournal, &[])?;
        self.action(Op::SetCubeOwned, &[1.])
    }
    fn cube_verify(&self) -> Result {
        if !self.flag(Op::CubeOwned, &[])? {
            return self.fail(406.);
        }
        if !self.flag(Op::LockCubeJournal, &[])? {
            return self.fail(407.);
        }
        self.action(Op::ReadCurrentCubeJournal, &[1.])?;
        if !self.flag(Op::CurrentCubeStateMatches, &[])? || !self.flag(Op::CubePending, &[])? {
            return self.fail(408.);
        }
        self.action(Op::SetCubePending, &[0.])?;
        let verified = self.scalar(Op::CubeVerified, &[])?;
        self.action(Op::SetCubeVerified, &[verified + 1.])?;
        self.action(Op::WriteCubeJournal, &[])?;
        self.action(Op::SetCubeOwned, &[0.])
    }
    pub(in crate::services) fn run_stamina(&self, method: Method, a: &[f64]) -> Result<[f64; 3]> {
        match method {
            Method::AccountCubeJournal => {
                if !self.flag(Op::CubeAccountKnown, &[])? {
                    self.action(Op::CubeSelectLegacyPath, &[])?;
                    return Ok([0.; 3]);
                }
                self.action(Op::CubeSelectAccountPath, &[])?;
                if !self.flag(Op::CubeAccountFileExists, &[])?
                    && self.flag(Op::CubeLegacyFileExists, &[])?
                {
                    self.action(Op::CubeReadLegacyJournal, &[])?;
                    if !self.flag(Op::CubeLegacyAssigned, &[])?
                        || self.flag(Op::CubeLegacyAssignedHere, &[])?
                    {
                        self.action(Op::CubeCopyLegacyJournal, &[])?;
                        self.action(Op::CubeAssignLegacyJournal, &[])?;
                    }
                }
            }
            Method::ClaimStamina => return self.claim_stamina(a[0], a[1], a[2] != 0.),
            Method::UseDailyCube => {
                return Ok([self.use_daily_cube(a[0] != 0.)? as u8 as f64, 0., 0.])
            }
            Method::StaminaCounters => self.stamina_counters()?,
            Method::JournalInteger => return Ok([self.journal_integer(a[0])?, 0., 0.]),
            Method::StaminaValues => return self.stamina_values(),
            Method::ReadStamina => return self.read_stamina(),
            Method::CubeLimit => return Ok([(a[0] as i32).clamp(0, 4) as f64, 0., 0.]),
            Method::CubeRemaining => {
                return Ok([
                    ((a[0] as i32).wrapping_sub(a[1] as i32)).max(0) as f64,
                    0.,
                    0.,
                ])
            }
            Method::CubeRead => self.cube_read(a[0] != 0.)?,
            Method::CubeReserve => self.cube_reserve()?,
            Method::CubeVerify => self.cube_verify()?,
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
