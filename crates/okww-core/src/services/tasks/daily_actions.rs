use crate::services::*;

#[derive(Clone, Copy)]
struct Progress {
    points: i32,
    stamina: Option<i32>,
    echo: Option<bool>,
}
impl Progress {
    fn ready(self) -> bool {
        self.points >= 100
    }
}
struct DailyPage<'a> {
    host: &'a Host,
    navigation_attempts: i32,
    collect: bool,
}
impl DailyPage<'_> {
    fn open_page(&mut self) -> Result {
        self.navigation_attempts += 1;
        if self.host.flag(Op::HasInfo, &[])? {
            self.host
                .action(Op::DailyPageInfo, &[self.navigation_attempts as f64])?;
        }
        self.host.action(Op::DailyOpenQuestBook, &[])?;
        self.host.action(Op::ClickRelative, &[0.17, 0.12, 0.7])
    }
    fn read(&mut self) -> Result<Progress> {
        loop {
            for sample in 0..3 {
                self.host.action(Op::ReadDailyPage, &[])?;
                self.host.action(Op::AppendDailyPageObservation, &[])?;
                if self.host.scalar(Op::DailyObservationCount, &[])? > 6. {
                    self.host.action(Op::RemoveFirstDailyObservation, &[])?;
                }
                let value = self.host.daily_load()?;
                if value.points >= 0 {
                    return Ok(value);
                }
                if sample < 2 {
                    self.host.action(Op::Sleep, &[0.4])?;
                }
            }
            if !self.host.flag(Op::DailyPageConfirmed, &[])? && self.navigation_attempts < 2 {
                self.open_page()?;
                continue;
            }
            let confirmed = self.host.flag(Op::DailyPageConfirmed, &[])?;
            self.host.action(
                Op::StoreDailyFailedObservations,
                &[self.navigation_attempts as f64],
            )?;
            self.host
                .action(Op::DailyActionFailure, &[if confirmed { 0. } else { 1. }])?;
            return Err(HOST_ERROR);
        }
    }
    fn collect(&mut self, mut value: Progress) -> Result<Progress> {
        if !self.collect {
            return Ok(value);
        }
        for _ in 0..12 {
            self.host.action(Op::ReadDailyObjectiveValues, &[])?;
            let count = self.host.scalar(Op::DailyObjectiveValueCount, &[])? as usize;
            let mut target = false;
            for index in 0..count {
                if !self
                    .host
                    .flag(Op::DailyValidObjectiveBox, &[index as f64])?
                    || !self.host.flag(Op::DailyObjectiveClaimText, &[])?
                {
                    continue;
                }
                self.host.action(Op::ReadDailyObjectiveRect, &[])?;
                if self.host.flag(Op::DailyObjectiveClaimInside, &[])? {
                    self.host.action(Op::StoreDailyObjectiveTarget, &[])?;
                    target = true;
                    break;
                }
            }
            if !target {
                return Ok(value);
            }
            self.host.action(Op::StorePreviousDailyObjectives, &[])?;
            self.host.action(Op::ClickDailyObjective, &[0.7])?;
            let mut next = self.read()?;
            if self.host.flag(Op::DailyObjectivesUnchanged, &[])? {
                self.host.action(Op::Sleep, &[0.7])?;
                next = self.read()?;
                if self.host.flag(Op::DailyObjectivesUnchanged, &[])? {
                    self.host.action(Op::DailyActionFailure, &[2.])?;
                }
            }
            if next.stamina.is_none() {
                next.stamina = value.stamina;
            }
            if next.echo.is_none() {
                next.echo = value.echo;
            }
            value = next;
        }
        self.host.action(Op::DailyActionFailure, &[3.])?;
        Err(HOST_ERROR)
    }
    fn read_collect(&mut self) -> Result<Progress> {
        let value = self.read()?;
        self.collect(value)
    }
    fn scan(&mut self, steps: f64, value: &mut Progress) -> Result {
        self.host.action(Op::ScrollDailyPage, &[steps])?;
        self.host.action(Op::Sleep, &[0.5])?;
        let next = self.read_collect()?;
        self.host.action(Op::AppendDailyObjectivePage, &[])?;
        if next.stamina.is_some() {
            value.stamina = next.stamina;
        }
        if next.echo.is_some() {
            value.echo = next.echo;
        }
        value.points = next.points;
        Ok(())
    }
}
impl Host {
    fn daily_load(&self) -> Result<Progress> {
        let [points, stamina, echo] = self.call(Op::LoadDailyProgress, &[])?;
        Ok(Progress {
            points: points as i32,
            stamina: (stamina != -1.).then_some(stamina as i32),
            echo: (echo != -1.).then_some(echo != 0.),
        })
    }
    fn daily_store(&self, value: Progress) -> Result {
        self.action(
            Op::StoreDailyProgress,
            &[
                value.points as f64,
                value.stamina.map_or(-1., |v| v as f64),
                value.echo.map_or(-1., |v| v as u8 as f64),
            ],
        )
    }
    fn daily_open(&self, collect: bool) -> Result {
        let mut page = DailyPage {
            host: self,
            navigation_attempts: 0,
            collect,
        };
        self.action(Op::Refresh, &[])?;
        if self.flag(Op::InTeam, &[])? {
            page.open_page()?;
        }
        let mut value = page.read_collect()?;
        self.action(Op::BeginDailyObjectivePages, &[])?;
        if !value.ready() && value.stamina.is_none() {
            page.scan(24., &mut value)?;
        }
        let need_echo = self.flag(Op::DailyEchoEnabled, &[])?;
        if !value.ready() && need_echo && value.echo.is_none() {
            page.scan(-24., &mut value)?;
        }
        self.daily_store(value)?;
        self.action(Op::StoreDailyObjectivePages, &[])?;
        if self.flag(Op::HasInfo, &[])? {
            self.action(Op::DailyProgressInfo, &[])?;
        }
        self.action(Op::ReportDailyProgress, &[])
    }
    fn daily_claim(&self) -> Result {
        self.action(Op::OpenDailyProgress, &[1.])?;
        let before = self.daily_load()?;
        if before.points <= 0 || before.points <= self.scalar(Op::DailyClaimedPoints, &[])? as i32 {
            return Ok(());
        }
        if self.flag(Op::HasInfo, &[])? {
            self.action(Op::DailyClaimInfo, &[])?;
        }
        self.action(Op::ClickRelative, &[0.5, 0.175, 0.3])?;
        for tier in 1..=5.min(before.points / 20) {
            if tier * 20 <= self.scalar(Op::DailyClaimedPoints, &[])? as i32 {
                continue;
            }
            self.action(
                Op::ClickRelative,
                &[0.392 + (tier - 1) as f64 * 0.1335, 0.889, 0.7],
            )?;
            if self.flag(Op::DailyRewardText, &[])? {
                self.action(Op::ClickRelative, &[0.5, 0.89, 0.5])?;
            }
            self.action(Op::ClickRelative, &[0.5, 0.175, 0.35])?;
            if !self.wait_for_interval(
                || {
                    self.action(Op::ReadDailyPage, &[])?;
                    Ok(self.daily_load()?.points >= 0)
                },
                3.,
                0.2,
            )? {
                self.action(Op::DailyActionFailure, &[4.])?;
            }
        }
        self.action(Op::OpenDailyProgress, &[0.])?;
        self.action(Op::StoreDailyClaimedPoints, &[before.points as f64])
    }
    fn daily_battle_pass(&self) -> Result {
        self.action(Op::DailyAltDown, &[])?;
        let clicking = (|| {
            self.action(Op::Sleep, &[0.05])?;
            self.action(Op::ClickRelative, &[0.86, 0.05, 0.])
        })();
        if clicking.is_err() {
            self.action(Op::ClearException, &[])?;
            self.action(Op::DailyAltUp, &[])?;
            self.action(Op::RethrowHandled, &[])?;
            return Err(HOST_ERROR);
        }
        self.action(Op::DailyAltUp, &[])?;
        self.action(Op::Sleep, &[1.])?;
        if self.wait_for(|| self.flag(Op::DailyBattlePassText, &[]), 5.)? {
            self.action(Op::ClickRelative, &[0.04, 0.3, 1.])?;
            self.action(Op::DailyBattlePassCollect, &[])?;
            self.action(Op::ClickRelative, &[0.04, 0.17, 2.])?;
            self.action(Op::DailyBattlePassCollect, &[])?;
            self.wait_for(
                || {
                    if self.flag(Op::DailyBattlePassText, &[])? {
                        return Ok(true);
                    }
                    self.action(Op::DailyBattlePassCollect, &[])?;
                    Ok(false)
                },
                5.,
            )?;
        } else if self.flag(Op::HasInfo, &[])? {
            self.action(Op::DailyBattlePassUnavailable, &[])?;
        }
        self.action(Op::DailyEnsureMainDefault, &[])
    }
    fn daily_validate(&self) -> Result {
        self.action(Op::ReadDailySelectedExtras, &[])?;
        for kind in [0., 1.] {
            if self.flag(Op::DailyHasEchoExtra, &[kind])?
                && self.flag(Op::DailyEchoExtraNeedsTarget, &[kind])?
            {
                self.action(Op::DailyActionFailure, &[5.])?;
            }
        }
        if self.flag(Op::DailyHasNestExtra, &[])? && self.flag(Op::DailyNestTargetsEmpty, &[])? {
            self.action(Op::DailyActionFailure, &[6.])?;
        }
        Ok(())
    }
    fn daily_stamina_observation(&self, previous: Option<i32>, claim: bool) -> Result<Progress> {
        let mut value = Progress {
            points: -1,
            stamina: None,
            echo: None,
        };
        for sample in 0..4 {
            if sample > 0 {
                self.action(Op::Sleep, &[0.7])?;
            }
            self.action(Op::OpenDailyProgress, &[0.])?;
            value = self.daily_load()?;
            if claim
                && value.points > 0
                && value.points > self.scalar(Op::DailyClaimedPoints, &[])? as i32
            {
                self.action(Op::ClaimDailyProgress, &[])?;
                // Claiming objectives can change both the displayed points and
                // the visible rows. Use the newly observed page, not the one
                // that initiated the claim.
                value = self.daily_load()?;
            }
            if value.ready()
                || (value.points >= 0
                    && value
                        .stamina
                        .is_some_and(|v| previous.is_none_or(|old| v > old)))
            {
                break;
            }
        }
        Ok(value)
    }
    fn daily_complete_stamina(&self) -> Result {
        if self.flag(Op::DailyNoProgress, &[])? {
            return Ok(());
        }
        let create_budget: Result = (|| {
            if self.flag(Op::DailyUseCubes, &[])? {
                self.action(Op::CreateDailyCubeBudget, &[])?;
            }
            Ok(())
        })();
        if create_budget.is_err() {
            let kind = self.scalar(Op::ExceptionKind, &[])? as i32;
            if kind == 7 {
                return Err(HOST_ERROR);
            }
            self.action(Op::ClearException, &[])?;
            self.action(Op::SetDailyNoProgress, &[])?;
            if self.flag(Op::HasInfo, &[])? {
                self.action(Op::DailyCubeErrorInfo, &[])?;
            }
            if self.flag(Op::HasNotify, &[])? {
                self.action(Op::DailyCubeErrorNotify, &[])?;
            }
            return Ok(());
        }
        if self.flag(Op::HasInfo, &[])? && self.flag(Op::HasDailyCubeBudget, &[])? {
            self.action(Op::DailyCubeUsageInfo, &[])?;
        }
        let kind = self.scalar(Op::ReadDailyDomainKind, &[])?;
        let cost = self.scalar(Op::ReadDailyDomainCost, &[])? as i32;
        for _ in 0..6 {
            let before = self.daily_stamina_observation(None, false)?;
            if before.ready() {
                return Ok(());
            }
            if before.points < 0 || before.stamina.is_none() {
                self.action(Op::DailyActionFailure, &[7.])?;
            }
            let progress = before.stamina.ok_or(HOST_ERROR)?;
            if progress >= 180 {
                return Ok(());
            }
            self.action(Op::ReadDailyDomainQuota, &[kind])?;
            let remaining = if self.flag(Op::DailyDomainQuotaNumber, &[])? {
                let quota = self.scalar(Op::DailyDomainQuota, &[])?;
                if quota != quota.floor() || !(0. ..=100000.).contains(&quota) {
                    self.action(Op::DailyActionFailure, &[11.])?;
                    return Err(HOST_ERROR);
                }
                let spent = self.scalar(Op::DailyDomainSpent, &[kind])? as i32;
                (quota > 0.).then_some((quota as i32 - spent / cost).max(0))
            } else {
                None
            };
            if remaining == Some(0) {
                return Ok(());
            }
            self.action(Op::DailyOpenBossBook, &[])?;
            let [current, reserve, mut total] = self.call(Op::ReadDailyStamina, &[])?;
            if current < 0. || reserve < 0. || total < 0. {
                self.action(Op::DailyActionFailure, &[8.])?;
            }
            while total < cost as f64 {
                if !self.flag(Op::HasDailyCubeBudget, &[])?
                    || !self.flag(Op::DailyCubeRemaining, &[])?
                    || !self.flag(Op::UseDailyCubeBudget, &[])?
                {
                    self.action(Op::DailyEnsureMainDefault, &[])?;
                    return Ok(());
                }
                total = self.call(Op::ReadDailyStamina, &[])?[2];
                if total < 0. {
                    self.action(Op::DailyActionFailure, &[9.])?;
                }
            }
            self.action(Op::CreateDailyStaminaFarm, &[])?;
            let prefer = self.flag(Op::DailyPreferDouble, &[])?;
            let batch = if let Some(remaining) = remaining {
                // The configured quota covers both daily objectives and the
                // later farming phase; do not force a single claim at 120/180.
                cost * (if prefer { 2 } else { 1 })
                    .min(remaining)
                    .min(total as i32 / cost)
            } else {
                self.daily_stamina_batch(progress, cost, total as i32, prefer)?
            };
            self.action(Op::FarmDailyStamina, &[batch as f64])?;
            // Re-read without consuming more stamina while the remote page
            // settles. A missing counter alone is not proof of no progress.
            let after = self.daily_stamina_observation(Some(progress), true)?;
            if after.ready() {
                return Ok(());
            }
            if after.stamina.is_none_or(|value| value <= progress) {
                self.action(Op::SetDailyNoProgress, &[])?;
                if self.flag(Op::HasInfo, &[])? {
                    self.action(Op::DailyStaminaNoProgressInfo, &[])?;
                }
                if self.flag(Op::HasNotify, &[])? {
                    self.action(Op::DailyStaminaNoProgressNotify, &[])?;
                }
                return Ok(());
            }
        }
        Ok(())
    }
    fn daily_stamina_batch(
        &self,
        progress: i32,
        cost: i32,
        available: i32,
        prefer: bool,
    ) -> Result<i32> {
        if !(0..=180).contains(&progress) || cost <= 0 || available < 0 {
            self.action(Op::DailyActionFailure, &[10.])?;
            return Err(HOST_ERROR);
        }
        Ok(cost
            * (if prefer { 2 } else { 1 })
                .min((180 - progress + cost - 1) / cost)
                .min(available / cost))
    }
    fn daily_farm_count(&self, kind: f64) -> Result {
        self.action(Op::ReadDailyDomainQuota, &[kind])?;
        if !self.flag(Op::DailyDomainQuotaNumber, &[])? {
            self.action(Op::DailyActionFailure, &[11.])?;
        }
        let quota = self.scalar(Op::DailyDomainQuota, &[])?;
        if quota != quota.floor() || !(0. ..=100000.).contains(&quota) {
            self.action(Op::DailyActionFailure, &[11.])?;
        }
        let spent = self.scalar(Op::DailyDomainSpent, &[kind])? as i32;
        let cost = self.scalar(Op::DailyKindCost, &[kind])? as i32;
        let requested = 0.max(quota as i32 - spent / cost);
        if requested == 0 {
            return Ok(());
        }
        self.action(Op::DailyOpenBossBook, &[])?;
        let total = self.call(Op::ReadDailyStamina, &[])?[2];
        let limit = self.scalar(
            Op::DailyDomainCountLimit,
            &[requested as f64, total, cost as f64],
        )?;
        self.action(Op::StoreDailyRemainingQuota, &[limit * cost as f64])?;
        self.action(Op::CreateDailyQuotaFarm, &[])?;
        self.action(Op::RunDailyQuotaStage, &[kind])
    }
    pub(in crate::services) fn run_daily_actions(
        &self,
        method: Method,
        a: &[f64],
    ) -> Result<[f64; 3]> {
        match method {
            Method::DailyOpen => self.daily_open(a[0] != 0.)?,
            Method::DailyClaim => self.daily_claim()?,
            Method::DailyCompleteStamina => self.daily_complete_stamina()?,
            Method::DailyFarmCount => self.daily_farm_count(a[0])?,
            Method::DailyMail => {
                self.action(Op::DailyEnsureMainDefault, &[])?;
                self.action(Op::DailyMailEscape, &[])?;
                self.action(Op::ClickRelative, &[0.64, 0.95, 1.])?;
                self.action(Op::ClickRelative, &[0.14, 0.9, 1.])?;
                self.action(Op::NavEnsureMain, &[10.])?;
            }
            Method::DailyBattlePass => self.daily_battle_pass()?,
            Method::DailyValidate => self.daily_validate()?,
            Method::DailyStaminaBatch => {
                return Ok([
                    self.daily_stamina_batch(a[0] as i32, a[1] as i32, a[2] as i32, a[3] != 0.)?
                        as f64,
                    0.,
                    0.,
                ])
            }
            Method::DailyQuotaOperation => {
                if self.scalar(Op::DailyRemainingQuota, &[])? != 0. {
                    self.action(Op::FarmDailyQuota, &[])?;
                }
            }
            Method::DailyQuotaSpent => {
                let remaining = self.scalar(Op::DailyRemainingQuota, &[])? as i32;
                self.action(
                    Op::StoreDailyRemainingQuota,
                    &[0.max(remaining - a[0] as i32) as f64],
                )?;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
