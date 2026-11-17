// Preserve the original treatment of unordered floating-point bounds.
#![allow(clippy::manual_range_contains)]
use super::*;
impl Host {
    fn retry_limit(&self) -> Result<i32> {
        if self.flag(Op::InCombat, &[])?
            || self.flag(Op::RetryingStep, &[])?
            || !self.flag(Op::RecoveryEnabled, &[])?
        {
            return Ok(0);
        }
        let count = self.scalar(Op::CatalogCount, &[])? as usize;
        for index in 0..count {
            if self.flag(Op::IsCurrentTrigger, &[index as f64])? {
                return Ok(0);
            }
        }
        Ok((self.scalar(Op::RecoveryLimit, &[])? as i32).clamp(1, 10))
    }
    fn retry(&self, label: f64, operation: impl Fn() -> Result) -> Result {
        let limit = self.retry_limit()?;
        let previous = self.flag(Op::RetryingStep, &[])?;
        self.action(Op::SetRetryingStep, &[1.])?;
        let result = (|| {
            let mut attempt = 0;
            loop {
                if operation().is_ok() {
                    break;
                }
                if self.scalar(Op::ExceptionKind, &[])? != 2. || attempt >= limit {
                    return Err(HOST_ERROR);
                }
                self.action(Op::ClearException, &[])?;
                if self.flag(Op::HasInfo, &[])? {
                    self.action(
                        Op::RecoveryNotice,
                        &[label, (attempt + 1) as f64, limit as f64],
                    )?;
                }
                self.action(Op::Sleep, &[0.5])?;
                self.action(Op::Refresh, &[])?;
                attempt += 1;
            }
            Ok(())
        })();
        let restored = self.action(Op::SetRetryingStep, &[previous as u8 as f64]);
        result.and(restored)
    }
    fn in_realm(&self) -> Result<bool> {
        Ok(self.flag(Op::RealmIcon, &[0.7])?
            && self.flag(Op::InTeam, &[])?
            && !self.flag(Op::WorldIcon, &[0.55])?)
    }
    fn in_world(&self) -> Result<bool> {
        Ok(self.flag(Op::WorldIcon, &[0.55])?
            && self.flag(Op::InTeam, &[])?
            && !self.flag(Op::RealmIcon, &[0.7])?)
    }
    fn wait_team_step(&self, timeout: f64, required: bool, back: bool) -> Result<bool> {
        let success = self.observe(
            || {
                if self.flag(Op::InTeam, &[])? {
                    return Ok(true);
                }
                if back {
                    self.action(Op::Key, &[0., 2.])?;
                }
                Ok(false)
            },
            timeout,
            0.,
        )?;
        if !success && required {
            self.fail(0.)?;
        }
        if success {
            self.action(Op::Sleep, &[0.5])?;
        }
        Ok(success)
    }
    fn wait_team(&self, timeout: f64, required: bool, back: bool) -> Result<bool> {
        if !required {
            return self.wait_team_step(timeout, required, back);
        }
        let success = Cell::new(false);
        self.retry(0., || {
            success.set(self.wait_team_step(timeout, required, back)?);
            Ok(())
        })?;
        Ok(success.get())
    }
    fn confirm(&self, right: bool) -> Result {
        if !self.flag(Op::FindConfirm, &[right as u8 as f64])? {
            return self.fail(1.);
        }
        self.action(Op::ClickFound, &[2.])
    }
    fn exit_realm(&self, timeout: f64) -> Result {
        self.action(Op::Key, &[0., 1.])?;
        if self.observe(|| self.flag(Op::FindExit, &[0.7]), 3., 0.)? {
            self.action(Op::Sleep, &[0.5])?;
            self.action(Op::ClickExitLeft, &[1.])?;
        } else if self.flag(Op::FindConfirm, &[1.])? {
            self.confirm(true)?;
        }
        if !self.observe(
            || Ok(self.flag(Op::InTeam, &[])? && !self.in_realm()?),
            timeout,
            0.,
        )? {
            return self.fail(2.);
        }
        self.action(Op::Sleep, &[0.5])
    }
    fn ensure_main(&self, timeout: f64, back: bool) -> Result {
        self.retry(1., || {
            let duration = if self.flag(Op::LoggedIn, &[])? {
                timeout
            } else {
                600.
            };
            if !self.observe(
                || {
                    if self.flag(Op::InTeam, &[])? {
                        self.action(Op::SetLoggedIn, &[1.])?;
                        if self.in_realm()? {
                            self.exit_realm(120.)?;
                        }
                        return Ok(true);
                    }
                    if self.flag(Op::Login, &[])? || self.flag(Op::MonthlyCard, &[])? {
                        return Ok(false);
                    }
                    if back {
                        self.action(Op::Key, &[0., 2.])?;
                    }
                    Ok(false)
                },
                duration,
                0.,
            )? {
                return self.fail(3.);
            }
            self.action(Op::Sleep, &[0.5])
        })
    }
    fn guarded_keys(&self, operation: impl Fn() -> Result) -> Result {
        match operation() {
            Ok(()) => Ok(()),
            Err(code) => {
                self.action(Op::ReleaseInput, &[])?;
                Err(code)
            }
        }
    }
    fn open_book(&self) -> Result {
        self.retry(2., || {
            self.ensure_main(30., true)?;
            if self.flag(Op::InTeam, &[])? {
                self.action(Op::Key, &[5., 4.])?;
            }
            if self.flag(Op::InTeam, &[])? {
                self.action(Op::KeyDown, &[1.])?;
                self.guarded_keys(|| {
                    self.action(Op::Sleep, &[0.05])?;
                    self.action(Op::ClickRelative, &[0.77, 0.05, 0.])?;
                    self.action(Op::Sleep, &[0.02])?;
                    self.action(Op::KeyUp, &[1.])
                })?;
                self.action(Op::Sleep, &[4.])?;
            }
            let opened = self.observe(
                || {
                    self.action(Op::ClearTab, &[])?;
                    if self.flag(Op::InTeam, &[])? {
                        return Ok(false);
                    }
                    self.flag(Op::FindBookTab, &[0.6])
                },
                3.,
                0.5,
            )?;
            if !opened {
                return self.fail(4.);
            }
            self.action(Op::ClickTab, &[1.5])?;
            if self.flag(Op::InTeam, &[])? {
                return self.fail(5.);
            }
            Ok(())
        })
    }
    fn select_monster(&self, max_pages: f64) -> Result {
        if self.flag(Op::NormalizedNameEmpty, &[])? || !(1. ..=100.).contains(&max_pages) {
            return self.fail(6.);
        }
        self.action(Op::ClickRelative, &[0.13, 0.14, 0.2])?;
        self.action(Op::KeyDown, &[2.])?;
        self.guarded_keys(|| {
            self.action(Op::Key, &[3., 0.])?;
            self.action(Op::KeyUp, &[2.])
        })?;
        self.action(Op::Key, &[4., 0.])?;
        self.action(Op::ClickRelative, &[0.354, 0.137, 0.5])?;
        self.action(Op::Scroll, &[80., 0.25, 0.5])?;
        self.action(Op::Sleep, &[0.6])?;
        self.action(Op::BuildExpectedName, &[])?;
        let mut repeated = 0;
        for _page in 0..max_pages as i32 {
            self.action(Op::Refresh, &[])?;
            let count = self.scalar(Op::ReadMonsterText, &[0.7])? as usize;
            self.action(Op::ClearFingerprint, &[])?;
            for index in 0..count {
                self.action(Op::NormalizeMonsterLine, &[index as f64])?;
                if self.flag(Op::MonsterNameMatches, &[])? {
                    self.action(Op::ClickMonsterLine, &[index as f64, 0.5])?;
                    return Ok(());
                }
                let y = self.scalar(Op::MonsterLineY, &[index as f64])?;
                let rounded = self.scalar(Op::PixelRound, &[y / 8.])?;
                self.action(Op::AppendFingerprint, &[rounded])?;
            }
            repeated = if !self.flag(Op::FingerprintEmpty, &[])?
                && self.flag(Op::FingerprintMatches, &[])?
            {
                repeated + 1
            } else {
                0
            };
            if repeated >= 2 {
                break;
            }
            self.action(Op::StoreFingerprint, &[])?;
            self.action(Op::Scroll, &[-4., 0.25, 0.65])?;
            self.action(Op::Sleep, &[0.45])?;
        }
        self.fail(7.)
    }
    fn category(&self, after: f64) -> Result {
        let kind = self.scalar(Op::CategoryKind, &[])? as i32;
        let row = match kind {
            0 => 0.4,
            1 => 0.3,
            2 => 0.49,
            3 => 0.73,
            4 => 0.61,
            5 => 0.83,
            6 => 0.86,
            _ => return self.fail(8.),
        };
        self.action(Op::Sleep, &[0.4])?;
        if kind == 6 {
            self.action(Op::ClickRelative, &[0.356, 0.882, after])?;
        }
        self.action(Op::ClickRelative, &[0.24, row, after])
    }
    fn waypoint(&self) -> Result<bool> {
        self.action(Op::ClearWaypoint, &[])?;
        for index in 0..2 {
            if self.flag(Op::FindWaypoint, &[index as f64, 0.6])?
                && (!self.flag(Op::WaypointExists, &[])?
                    || self.scalar(Op::ItemConfidence, &[])?
                        > self.scalar(Op::WaypointConfidence, &[])?)
            {
                self.action(Op::StoreWaypoint, &[])?;
            }
        }
        self.flag(Op::WaypointExists, &[])
    }
    fn teleport(&self) -> Result {
        self.action(Op::Sleep, &[2.])?;
        if !self.waypoint()? {
            return self.fail(9.);
        }
        self.action(Op::ClickWaypoint, &[1.])?;
        let mut travel = self.observe(|| self.flag(Op::FindTravel, &[]), 3., 0.)?;
        if !travel {
            if self.flag(Op::FindWaypointPopup, &[])? {
                self.action(Op::ClickFound, &[1.])?;
            }
            travel = self.observe(|| self.flag(Op::FindTravel, &[]), 3., 0.)?;
        }
        if !travel {
            return self.fail(10.);
        }
        self.action(Op::ClickTravelRight, &[1.])?;
        self.wait_team(20., true, false)?;
        self.action(Op::Sleep, &[2.])
    }
    fn heal(&self) -> Result {
        self.ensure_main(120., true)?;
        self.action(Op::DefaultBookName, &[])?;
        self.open_book()?;
        self.action(Op::CrownlessName, &[self.scalar(Op::LanguageKind, &[])?])?;
        self.select_monster(40.)?;
        self.action(Op::ClickRelative, &[0.89, 0.92, 1.])?;
        if !self.observe(|| self.waypoint(), 6., 0.)? {
            return self.fail(11.);
        }
        self.teleport()?;
        self.action(Op::Sleep, &[5.])
    }
    fn wait_travel(&self) -> Result {
        self.retry(3., || {
            if !self.observe(|| self.flag(Op::TravelButton, &[]), 10., 0.)? {
                return self.fail(12.);
            }
            Ok(())
        })
    }
    fn start_challenge(&self) -> Result {
        self.retry(4., || {
            let found = self.observe(
                || {
                    if self.flag(Op::FindStartChallenge, &[])? {
                        self.action(Op::StoreMatchPoint, &[])?;
                        return Ok(true);
                    }
                    if self.flag(Op::FindChallengeText, &[])? {
                        self.action(Op::StoreTextPoint, &[])?;
                        return Ok(true);
                    }
                    if self.flag(Op::NoEchoDrop, &[])? {
                        if !self.flag(Op::AdvancedSkillMode, &[])? {
                            return self.fail(13.).map(|_| false);
                        }
                        self.confirm(true)?;
                    }
                    Ok(false)
                },
                10.,
                0.2,
            )?;
            if !found {
                return self.fail(14.);
            }
            self.action(Op::Sleep, &[0.5])?;
            self.action(Op::ClickPoint, &[1.])?;
            self.observe(|| self.flag(Op::SkipConfirm, &[]), 3., 0.)?;
            Ok(())
        })
    }
    fn book_scroll(&self, serial: f64, total: f64, top: f64) -> Result<f64> {
        if serial < 1. || total < 1. || serial > total || top < 0. || top >= 0.8806 {
            self.fail(15.)?;
        }
        let count = self.scalar(Op::StructureCount, &[])? as usize;
        let mut sum = 0_i32;
        let mut cross = 0;
        for index in 0..count {
            let n = self.scalar(Op::StructureValue, &[index as f64])? as i32;
            if n <= 0 {
                self.fail(16.)?;
            }
            sum = sum.wrapping_add(n);
            if serial > sum as f64 {
                cross += 1;
            } else {
                break;
            }
        }
        let height = 0.8806
            - top
            - if count == 0 {
                0.
            } else {
                (count - 1) as f64 * 0.028
            };
        if height <= 0. {
            self.fail(17.)?;
        }
        let value = top + height / total * serial + cross as f64 * 0.028;
        // std::min keeps the first operand when the second is unordered.
        Ok(if value < 0.8806 { value } else { 0.8806 })
    }
    fn select_entry(&self, serial: f64, total: f64) -> Result<bool> {
        self.action(Op::Sleep, &[0.5])?;
        let top = self.scalar(Op::BookScrollTop, &[])?;
        let scroll = self.book_scroll(serial, total, top)?;
        let mut target = if serial <= 4. { serial - 1. } else { -1. };
        if target < 0. {
            self.action(Op::ClickRelative, &[0.973, scroll, 1.])?;
        }
        let mut count = self.scalar(Op::ReadProceedRows, &[])?;
        if target < 0.
            && count != 0.
            && (count == 3. || count == 4.)
            && self.scalar(Op::StructureCount, &[])? > 0.
            && self.flag(Op::LastRowAbove, &[0.73])?
        {
            let item_height =
                (0.8806 - top - (self.scalar(Op::StructureCount, &[])? - 1.) * 0.028) / total;
            self.action(
                Op::ClickRelative,
                &[0.973, (scroll + item_height * 0.55).min(0.8806), 1.],
            )?;
            count = self.scalar(Op::ReadProceedRows, &[])?;
        }
        if count == 0. {
            self.fail(18.)?;
        }
        if target >= count {
            self.action(Op::ClickRelative, &[0.973, scroll, 1.])?;
            count = self.scalar(Op::ReadProceedRows, &[])?;
            target = -1.;
        }
        if count == 0. {
            self.fail(19.)?;
        }
        self.action(
            Op::ClickProceedRow,
            &[if target < 0. { count - 1. } else { target }, 1.],
        )?;
        let confirmed = Cell::new(false);
        if !self.observe(
            || {
                if self.flag(Op::FindNextInterface, &[])? {
                    return Ok(true);
                }
                if !confirmed.get()
                    && self.flag(Op::WeeklyTeleport, &[])?
                    && self.flag(Op::EarlyTeleportWarning, &[])?
                {
                    self.confirm(true)?;
                    confirmed.set(true);
                }
                Ok(false)
            },
            10.,
            0.5,
        )? {
            self.fail(20.)?;
        }
        self.flag(Op::NextIsTeamClose, &[])
    }
    pub(super) fn run_navigation(&self, method: Method, a: &[f64]) -> Result<[f64; 3]> {
        let mut value = 0.;
        match method {
            Method::BookScroll => value = self.book_scroll(a[0], a[1], a[2])?,
            Method::RetryLimit => value = self.retry_limit()? as f64,
            Method::Retry => self.retry(-1., || self.action(Op::Operation, &[]))?,
            Method::Observe => {
                value = self.observe(|| self.flag(Op::Condition, &[]), a[0], a[1])? as u8 as f64
            }
            Method::WaitMatch => {
                value = self.observe(|| self.flag(Op::FindNamed, &[]), a[0], a[1])? as u8 as f64
            }
            Method::InRealm => value = self.in_realm()? as u8 as f64,
            Method::InWorld => value = self.in_world()? as u8 as f64,
            Method::WaitTeam => value = self.wait_team(a[0], a[1] != 0., a[2] != 0.)? as u8 as f64,
            Method::Confirm => self.confirm(a[0] != 0.)?,
            Method::ExitRealm => self.exit_realm(a[0])?,
            Method::EnsureMain => self.ensure_main(a[0], a[1] != 0.)?,
            Method::OpenBook => self.open_book()?,
            Method::SelectMonster => self.select_monster(a[0])?,
            Method::Category => self.category(a[0])?,
            Method::Waypoint => value = self.waypoint()? as u8 as f64,
            Method::Teleport => self.teleport()?,
            Method::Heal => self.heal()?,
            Method::Travel => self.wait_travel()?,
            Method::StartChallenge => self.start_challenge()?,
            Method::SelectEntry => value = self.select_entry(a[0], a[1])? as u8 as f64,
            _ => return Err(INVALID_ACTION),
        }
        Ok([value, 0., 0.])
    }
}
