use super::*;
impl Host {
    pub(super) fn restore_condition(&self) -> SessionResult<bool> {
        self.action(Op::Watchdog, &[])?;
        if self.flag(Op::InTeam, &[])? {
            return Ok(true);
        }
        if !self.flag(Op::ResultChecked, &[])?
            || self.now()? - self.call(Op::ResultCheckedAt, &[])? >= 0.5
        {
            self.action(Op::SetResultChecked, &[1.])?;
            self.action(Op::SetResultCheckedAt, &[self.now()?])?;
            let result = self.call(Op::ChallengeResult, &[])?;
            if result != 0. {
                self.fail(if result == 2. { 10. } else { 11. })?;
            }
        }
        Ok(false)
    }
    pub(super) fn combat_active(&self) -> SessionResult<bool> {
        if self.bool(Bool::InLiberation)? {
            return Ok(true);
        }
        if self.bool(Bool::Engaged)? {
            if self.flag(Op::CheckedVersionMatches, &[])? {
                return Ok(true);
            }
            if !self.flag(Op::InTeam, &[])? {
                self.action(Op::ReleaseInput, &[])?;
                self.action(Op::Log, &[2.])?;
                if !self.flag(Op::WaitHudRestore, &[15., 0.1])? {
                    self.fail(12.)?;
                }
                self.action(Op::Log, &[3.])?;
            }
            self.action(Op::CheckBreak, &[])?;
            let current = self.flag(Op::CurrentExists, &[])?;
            let python = self.flag(Op::PythonMacro, &[])?;
            if current
                && if python {
                    self.flag(Op::PythonSkipCombatCheck, &[])?
                } else {
                    self.flag(Op::BuiltinSkipCombatCheck, &[])?
                }
            {
                return Ok(true);
            }
            if self.flag(Op::HasOnCombatCheck, &[])? && !self.flag(Op::OnCombatCheck, &[])? {
                self.action(Op::ResetCombat, &[])?;
                return Ok(false);
            }
            if self.flag(Op::TargetLocked, &[])? {
                self.action(Op::SetCheckedVersionFrame, &[])?;
                return Ok(true);
            }
            if self.flag(Op::HasEndCondition, &[])? && self.flag(Op::EndCondition, &[])? {
                self.action(Op::ResetCombat, &[])?;
                return Ok(false);
            }
            if self.flag(Op::TargetEnemy, &[])? {
                self.action(Op::SetCheckedVersionFrame, &[])?;
                return Ok(true);
            }
            if self.flag(Op::MonthlyCard, &[])? {
                return Ok(true);
            }
            self.action(Op::ResetCombat, &[])?;
            return Ok(false);
        }
        if !self.flag(Op::LoadParty, &[])? {
            return Ok(false);
        }
        let target = self.flag(Op::TargetLocked, &[])?;
        let auto_target =
            !self.flag(Op::AutoTask, &[])? || self.flag(Op::AutoTargetSetting, &[])?;
        let boss = self.flag(Op::BossFeatures, &[])?;
        if !target && !(auto_target && (self.flag(Op::SessionHealthBar, &[])? || boss)) {
            return Ok(false);
        }
        if !target && !self.flag(Op::TargetEnemy, &[])? {
            return Ok(false);
        }
        self.set_bool(Bool::HasLevitator, self.flag(Op::LevitatorFeature, &[])?)?;
        self.set_bool(Bool::Engaged, true)?;
        self.set_bool(Bool::ContextCombat, true)?;
        Ok(true)
    }
    fn auto_loop(&self, ran: &mut bool) -> SessionResult {
        let mut switched = false;
        while self.flag(Op::CombatActive, &[])? {
            *ran = true;
            if !switched {
                self.action(Op::SwitchHealer, &[])?;
                switched = true;
            }
            self.action(Op::PerformCurrent, &[])?;
            self.action(Op::NextFrame, &[])?;
        }
        Ok(())
    }
    fn release_reset(&self) -> SessionResult {
        self.action(Op::ReleaseInput, &[])?;
        self.action(Op::ResetCombat, &[])
    }
    fn release_rethrow(&self) -> SessionResult {
        self.action(Op::ClearException, &[])?;
        self.release_reset()?;
        self.action(Op::RethrowHandled, &[])?;
        Err(HOST_ERROR)
    }
    fn auto_end(&self, ran: bool) -> SessionResult {
        if ran {
            if self.flag(Op::AutoEnabled, &[])? {
                if self.flag(Op::MacroExists, &[])? {
                    self.action(Op::MacroCombatEnd, &[])?;
                }
                if (!self.flag(Op::MacroExists, &[])? || !self.flag(Op::SelectedPython, &[])?)
                    && self.flag(Op::CurrentExists, &[])?
                {
                    self.action(Op::ActorCombatEnd, &[])?;
                }
            }
            let count = self.call(Op::PartySize, &[])? as usize;
            for index in 0..count {
                self.action(Op::ResetMember, &[index as f64])?;
            }
            if self.flag(Op::AutoEnabled, &[])? {
                self.action(Op::SwitchHealer, &[])?;
            }
        }
        Ok(())
    }
    pub(super) fn run_auto_combat(&self) -> SessionResult<bool> {
        if !self.flag(Op::InTeam, &[])? {
            return Ok(false);
        }
        let earth = self.flag(Op::EarthFeature, &[])?;
        let exit = self.flag(Op::RealmExitFeature, &[])?;
        let world = earth && !exit;
        let realm = exit && !earth;
        self.set_bool(Bool::OpenWorld, !realm)?;
        self.set_bool(
            Bool::UseLiberation,
            self.flag(Op::UseLiberationSetting, &[])? || !world,
        )?;
        let mut ran = false;
        if self.auto_loop(&mut ran).is_err() {
            match self.call(Op::ExceptionKind, &[])? as i32 {
                3 => {
                    self.action(Op::ClearException, &[])?;
                    self.release_reset()?;
                    self.action(Op::LogException, &[])?;
                    return Ok(ran);
                }
                4 | 7 => {
                    self.action(Op::ClearException, &[])?;
                    self.action(Op::LogException, &[])?;
                }
                _ => {
                    self.release_rethrow()?;
                    return Err(HOST_ERROR);
                }
            }
        }
        self.action(Op::ReleaseInput, &[])?;
        if self.auto_end(ran).is_err() {
            self.release_rethrow()?;
            return Err(HOST_ERROR);
        }
        self.action(Op::ResetCombat, &[])?;
        Ok(ran)
    }
}
