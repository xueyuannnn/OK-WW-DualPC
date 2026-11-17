use super::*;
impl Host {
    pub(super) fn guarded<T>(
        &self,
        field: Bool,
        value: bool,
        body: impl FnOnce() -> SessionResult<T>,
    ) -> SessionResult<T> {
        let previous = self.bool(field)?;
        self.set_bool(field, value)?;
        let result = body();
        let restore = self.set_bool(field, previous);
        match result {
            Ok(value) => {
                restore?;
                Ok(value)
            }
            Err(error) => Err(error),
        }
    }
    pub(super) fn target_locked(&self) -> SessionResult<bool> {
        let current = self.flag(Op::CurrentExists, &[])?;
        let python = self.flag(Op::PythonMacro, &[])?;
        let short = current
            && if python {
                self.flag(Op::PythonShortTarget, &[])?
            } else {
                self.flag(Op::BuiltinShortTarget, &[])?
            };
        let target = self.values(Op::ObserveTarget, &[short as u8 as f64])?;
        if target[0] == 0.
            || target[1] == 0.
            || self.call(Op::GetCount, &[Count::TargetEscCount as i32 as f64])? != 0.
        {
            return Ok(target[0] != 0.);
        }
        self.guarded(Bool::Checking, true, || {
            self.action(Op::Sleep, &[1.])?;
            let target = self.values(Op::ObserveTarget, &[short as u8 as f64])?;
            if target[0] == 0. || target[1] == 0. {
                return Ok(target[0] != 0.);
            }
            self.action(Op::TapEscape, &[2.])?;
            self.action(Op::TapEscape, &[1.5])?;
            self.action(Op::SetCount, &[Count::TargetEscCount as i32 as f64, 1.])?;
            Ok(false)
        })
    }
    pub(super) fn check_break(&self) -> SessionResult<bool> {
        if self.bool(Bool::CanBreak)? {
            return Ok(true);
        }
        if self.flag(Op::FullBreakFeature, &[])? {
            self.set_bool(Bool::CanBreak, true)?;
            return Ok(true);
        }
        if !self.bool(Bool::InLiberation)? && self.now()? - self.time(Time::LastBreakCheck)? > 1. {
            self.set_time(Time::LastBreakCheck, self.now()?)?;
            if self.flag(Op::BreakFeature, &[])? && !self.flag(Op::DialogInteract, &[])? {
                self.set_bool(Bool::CanBreak, true)?;
                return Ok(true);
            }
        }
        Ok(false)
    }
    pub(super) fn macro_break_prompt(&self) -> SessionResult<bool> {
        if self.flag(Op::VisibleBreakPrompt, &[0.8])? {
            self.set_bool(Bool::CanBreak, true)?;
        }
        self.bool(Bool::CanBreak)
    }
    pub(super) fn visible_break_prompt(&self, threshold: f64) -> SessionResult<bool> {
        Ok(self.flag(Op::FindBreakPrompt, &[threshold])? && !self.flag(Op::DialogInteract, &[])?)
    }
    pub(super) fn break_f(&self, force: bool) -> SessionResult<bool> {
        if force {
            self.action(Op::TapF, &[0.05])?;
        }
        if !self.bool(Bool::CanBreak)? && !self.flag(Op::CheckBreak, &[])? {
            return Ok(false);
        }
        let start = self.now()?;
        while self.now()? - start < 0.5
            || (self.now()? - start < 5.
                && (self.bool(Bool::CanBreak)? || self.flag(Op::CheckBreak, &[])?))
        {
            self.action(Op::TapF, &[0.1])?;
            self.action(Op::ClickDefault, &[])?;
            self.action(Op::Sleep, &[0.1])?;
            self.set_bool(Bool::CanBreak, false)?;
        }
        Ok(true)
    }
    pub(super) fn check_combat(&self) -> SessionResult {
        self.action(Op::Checkpoint, &[])?;
        self.action(Op::Watchdog, &[])?;
        if self.bool(Bool::SkipCombatCheck)?
            || self.bool(Bool::Checking)?
            || !self.bool(Bool::Engaged)?
        {
            return Ok(());
        }
        self.guarded(Bool::Checking, true, || {
            if !self.flag(Op::CombatActive, &[])? {
                self.fail(8.)?;
            }
            Ok(())
        })
    }
    pub(super) fn reset_combat(&self) -> SessionResult {
        self.set_bool(Bool::ContextCombat, false)?;
        self.action(Op::SetCount, &[Count::TargetEscCount as i32 as f64, 0.])?;
        for field in [
            Bool::CanBreak,
            Bool::HasLevitator,
            Bool::InLiberation,
            Bool::Engaged,
        ] {
            self.set_bool(field, false)?;
        }
        self.action(Op::ClearCheckedVersion, &[])?;
        self.action(Op::VisionReset, &[])?;
        self.set_time(Time::LastHealthCheck, -1.)?;
        self.set_bool(Bool::HealthCache, false)
    }
    pub(super) fn sync_current_members(&self, current: f64) -> SessionResult {
        let count = self.call(Op::PartySize, &[])? as usize;
        for index in 0..count {
            let matches = self.call(Op::MemberIndex, &[index as f64])? == current;
            self.action(Op::SetMemberCurrent, &[index as f64, matches as u8 as f64])?;
        }
        Ok(())
    }
    pub(super) fn perform_current(&self) -> SessionResult {
        let hud = self.values(Op::ReadHud, &[])?;
        if hud[0] != 0.
            && hud[1] == self.call(Op::PartySize, &[])?
            && hud[2] >= 0.
            && hud[2] < hud[1]
        {
            self.sync_current_members(hud[2])?;
        }
        if !self.flag(Op::CurrentExists, &[])? {
            return self.fail(9.);
        }
        self.action(Op::SetActorPerform, &[self.now()?])?;
        if !self.flag(Op::MacroExists, &[])? || !self.flag(Op::MacroPerform, &[])? {
            self.action(Op::ActorPerform, &[])?;
        }
        self.action(Op::PublishParty, &[])
    }
    pub(super) fn update_portraits(&self) -> SessionResult {
        let count = self.call(Op::PartySize, &[])? as usize;
        for index in 0..count {
            let index = index as f64;
            if !self.flag(Op::MemberCurrent, &[index])?
                && self.call(Op::MemberRing, &[index])? >= 0.
                && !self.flag(Op::MemberLiberationReady, &[index])?
                && self.flag(Op::FindLiberationPortrait, &[index])?
            {
                self.action(Op::SetMemberLiberationReady, &[index])?;
            }
        }
        Ok(())
    }
    fn is_healer(&self, index: f64) -> SessionResult<bool> {
        if self.flag(Op::PythonMacro, &[])? {
            self.flag(Op::PythonHealer, &[index])
        } else {
            Ok(self.call(Op::MemberRole, &[index])? == 2.)
        }
    }
    pub(super) fn switch_healer(&self) -> SessionResult {
        if !self.flag(Op::HudVisible, &[])? {
            return Ok(());
        }
        let task = self.flag(Op::TaskIdMatch, &[0.])? || self.flag(Op::TaskIdMatch, &[1.])?;
        let own = task || self.flag(Op::AutoTask, &[])?;
        let enabled = if own {
            self.flag(Op::HealerTaskSetting, &[])?
        } else {
            self.flag(Op::HealerGlobalSetting, &[])?
        };
        if !enabled || !self.flag(Op::CurrentExists, &[])? || self.is_healer(-1.)? {
            return Ok(());
        }
        let count = self.call(Op::PartySize, &[])? as usize;
        let mut healer = None;
        for index in 0..count {
            if self.is_healer(index as f64)? {
                healer = Some(index);
                break;
            }
        }
        let Some(healer) = healer else {
            return Ok(());
        };
        self.guarded(Bool::SkipCombatCheck, true, || {
            let start = self.now()?;
            while self.now()? - start < 6. {
                let hud = self.values(Op::ReadHud, &[])?;
                if hud[0] != 0. && hud[2] != self.call(Op::CurrentIndex, &[])? {
                    self.sync_current_members(hud[2])?;
                    return Ok(());
                }
                self.action(Op::TapMember, &[healer as f64])?;
                self.action(Op::Sleep, &[0.2])?;
            }
            Ok(())
        })
    }
}
