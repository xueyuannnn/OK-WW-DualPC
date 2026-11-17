use super::*;
impl Host {
    fn free_heal(&self, entry_at_heal: bool) -> SessionResult {
        self.action(Op::CreateNavigation, &[])?;
        let realm = self.flag(Op::InRealm, &[])?;
        if realm {
            self.action(Op::ExitRealm, &[120.])?;
        }
        if !realm || !entry_at_heal {
            self.action(Op::HealAtBeacon, &[])?;
        }
        Ok(())
    }
    pub(super) fn recover_starting_popup(&self, entry_at_heal: bool) -> SessionResult {
        if !self.flag(Op::DeathDialog, &[])? {
            return Ok(());
        }
        self.action(Op::ResetCombat, &[])?;
        self.action(Op::CloseRevivalDialog, &[])?;
        self.free_heal(entry_at_heal)?;
        self.action(Op::Log, &[5.])
    }
    pub(super) fn recover_dead(&self, entry_at_heal: bool) -> SessionResult {
        self.action(Op::ReleaseInput, &[])?;
        self.action(Op::ResetCombat, &[])?;
        self.set_time(Time::Deadline, 0.)?;
        self.action(Op::CloseRevivalDialog, &[])?;
        if self.flag(Op::HasRecovery, &[])? {
            self.action(Op::CustomRecovery, &[])?;
        } else {
            self.free_heal(entry_at_heal)?;
        }
        self.action(Op::ResetCombat, &[])?;
        self.action(Op::Log, &[4.])?;
        self.fail(18.)
    }
    pub(super) fn close_revival_dialog(&self) -> SessionResult<bool> {
        if !self.flag(Op::DeathDialog, &[])? {
            return Ok(false);
        }
        self.action(Op::ReleaseInput, &[])?;
        self.guarded(Bool::CheckingDialog, true, || {
            self.guarded(Bool::SkipCombatCheck, true, || {
                self.action(Op::ContextEscape, &[0.5])?;
                if self.flag(Op::WaitDeathClosed, &[2.])? {
                    return Ok(true);
                }
                self.action(Op::ClosePopupClick, &[0.803, 0.196, 0.5])?;
                if !self.flag(Op::WaitDeathClosed, &[2.])? {
                    self.fail(19.)?;
                }
                Ok(true)
            })
        })
    }
    fn round_loop(&self, retry_limit: i32) -> SessionResult {
        let mut interruptions = 0;
        loop {
            let result = (|| {
                while self.flag(Op::CombatActive, &[])? {
                    self.action(Op::Watchdog, &[])?;
                    self.action(Op::PerformCurrent, &[])?;
                    self.action(Op::NextFrame, &[])?;
                }
                Ok::<_, i32>(())
            })();
            if result.is_ok() {
                break;
            }
            match self.call(Op::ExceptionKind, &[])? as i32 {
                3 => {
                    self.action(Op::ClearException, &[])?;
                    self.action(Op::ReleaseInput, &[])?;
                    self.set_bool(Bool::InLiberation, false)?;
                    self.action(Op::LogException, &[])?;
                    interruptions += 1;
                    if interruptions > retry_limit {
                        return self.fail(16.);
                    }
                    if !self.flag(Op::WaitTeamOrDeath, &[5.])? {
                        return self.fail(17.);
                    }
                    self.action(Op::Watchdog, &[])?;
                    if self.flag(Op::DeathDialog, &[])? {
                        return self.fail(5.);
                    }
                    self.action(Op::LogRetry, &[interruptions as f64, retry_limit as f64])?;
                }
                4 | 7 => {
                    self.action(Op::ClearException, &[])?;
                    self.action(Op::LogException, &[])?;
                    break;
                }
                _ => return Err(HOST_ERROR),
            }
        }
        Ok(())
    }
    fn combat_once_body(&self, args: &[f64]) -> SessionResult<bool> {
        if self.flag(Op::DeathDialog, &[])? {
            self.fail(5.)?;
        }
        let until = self.now()? + args[0];
        let mut found = false;
        loop {
            self.action(Op::Watchdog, &[])?;
            if self.flag(Op::CombatActive, &[])? {
                found = true;
                break;
            }
            if args[2] != 0. {
                self.action(Op::ClickMiddle, &[0.5])?;
            }
            self.action(Op::ContextSleep, &[0.02])?;
            if self.now()? < until {
                continue;
            }
            break;
        }
        if !found {
            if args[1] != 0. {
                self.fail(15.)?;
            }
            self.action(Op::ResetCombat, &[])?;
            return Ok(false);
        }
        self.set_time(Time::Deadline, self.now()? + args[5])?;
        let count = self.call(Op::GetCount, &[Count::CombatCount as i32 as f64])? + 1.;
        self.action(Op::SetCount, &[Count::CombatCount as i32 as f64, count])?;
        if self.flag(Op::HasInfo, &[])? {
            self.action(Op::CombatCountInfo, &[count])?;
        }
        self.action(Op::SwitchHealer, &[])?;
        let retry_limit = if self.flag(Op::ResumeInterrupted, &[])? {
            (self.call(Op::RecoveryRetryLimit, &[])? as i32).clamp(1, 10)
        } else {
            0
        };
        self.round_loop(retry_limit)?;
        if self.flag(Op::DeathDialog, &[])? {
            self.fail(5.)?;
        }
        self.action(Op::ReleaseInput, &[])?;
        if self.flag(Op::MacroExists, &[])? {
            self.action(Op::MacroCombatEnd, &[])?;
        }
        if (!self.flag(Op::MacroExists, &[])? || !self.flag(Op::SelectedPython, &[])?)
            && self.flag(Op::CurrentExists, &[])?
        {
            self.action(Op::ActorCombatEnd, &[])?;
        }
        let count = self.call(Op::PartySize, &[])? as usize;
        for index in 0..count {
            self.action(Op::ResetMember, &[index as f64])?;
        }
        self.action(Op::SwitchHealer, &[])?;
        if self.flag(Op::DeathDialog, &[])? {
            self.fail(5.)?;
        }
        self.set_time(Time::Deadline, 0.)?;
        self.action(Op::ResetCombat, &[])?;
        self.action(Op::WaitTeamAfterCombat, &[10.])?;
        Ok(true)
    }
    fn clear_round(&self) -> SessionResult {
        self.action(Op::ReleaseInput, &[])?;
        self.set_time(Time::Deadline, 0.)?;
        self.action(Op::ResetCombat, &[])
    }
    pub(super) fn combat_once(&self, args: &[f64]) -> SessionResult<bool> {
        if !args[0].is_finite()
            || args[0] < 0.
            || !args[5].is_finite()
            || args[5] <= 0.
            || args[5] > 7200.
        {
            self.fail(20.)?;
        }
        self.guarded(Bool::CombatScope, true, || {
            self.guarded(Bool::SkipCombatCheck, false, || {
                self.set_bool(Bool::OpenWorld, false)?;
                self.set_bool(Bool::UseLiberation, args[4] != 0.)?;
                self.set_time(Time::LastDeathCheck, -1.)?;
                match self.combat_once_body(args) {
                    Ok(value) => Ok(value),
                    Err(error) => {
                        let kind = self.call(Op::ExceptionKind, &[])? as i32;
                        self.action(Op::ClearException, &[])?;
                        if kind == 2 {
                            self.action(Op::RecoverDead, &[])?;
                            return Ok(false);
                        }
                        self.clear_round()?;
                        if kind != 1 && self.flag(Op::DeathDialog, &[])? {
                            self.action(Op::RecoverDead, &[])?;
                            return Ok(false);
                        }
                        self.action(Op::RethrowHandled, &[])?;
                        Err(error)
                    }
                }
            })
        })
    }
}
