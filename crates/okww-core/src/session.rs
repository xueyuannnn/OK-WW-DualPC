//! Combat-session policy. Host objects supply SDK observations and actions;
//! numeric state identifiers come from the same definitions as the C++ binding.
use std::ffi::c_void;
const HOST_ERROR: i32 = -1;
const INVALID_ACTION: i32 = -3;
include!(concat!(env!("OUT_DIR"), "/session_abi.rs"));
include!(concat!(env!("OUT_DIR"), "/session_state.rs"));
type SessionResult<T = ()> = Result<T, i32>;
mod battle;
mod detection;
mod helpers;
mod party;
mod recovery;
mod switching;
#[repr(C)]
pub struct Host {
    pub context: *mut c_void,
    pub invoke: unsafe extern "C" fn(*mut c_void, i32, *const f64, usize, *mut f64) -> i32,
}
impl Host {
    fn values(&self, op: Op, args: &[f64]) -> SessionResult<[f64; 3]> {
        let mut out = [0.; 3];
        // SAFETY: buffers are live for this synchronous, exception-catching call.
        if unsafe {
            (self.invoke)(
                self.context,
                op as i32,
                args.as_ptr(),
                args.len(),
                out.as_mut_ptr(),
            )
        } == 1
        {
            Ok(out)
        } else {
            Err(HOST_ERROR)
        }
    }
    fn call(&self, op: Op, args: &[f64]) -> SessionResult<f64> {
        Ok(self.values(op, args)?[0])
    }
    fn action(&self, op: Op, args: &[f64]) -> SessionResult {
        self.call(op, args)?;
        Ok(())
    }
    fn flag(&self, op: Op, args: &[f64]) -> SessionResult<bool> {
        Ok(self.call(op, args)? != 0.)
    }
    fn bool(&self, field: Bool) -> SessionResult<bool> {
        self.flag(Op::GetBool, &[field as i32 as f64])
    }
    fn set_bool(&self, field: Bool, value: bool) -> SessionResult {
        self.action(Op::SetBool, &[field as i32 as f64, value as u8 as f64])
    }
    fn time(&self, field: Time) -> SessionResult<f64> {
        self.call(Op::GetTime, &[field as i32 as f64])
    }
    fn set_time(&self, field: Time, value: f64) -> SessionResult {
        self.action(Op::SetTime, &[field as i32 as f64, value])
    }
    fn now(&self) -> SessionResult<f64> {
        self.call(Op::Now, &[])
    }
    fn fail(&self, reason: f64) -> SessionResult {
        self.action(Op::Throw, &[reason])?;
        Err(HOST_ERROR)
    }
    fn initialize(&self) -> SessionResult {
        if self.flag(Op::CalibrationExists, &[])? {
            self.action(Op::ReadCalibration, &[])?;
            for index in 0..6 {
                self.action(Op::CalibrationValue, &[index as f64])?;
            }
        }
        self.action(Op::CreateConcerto, &[])?;
        self.action(Op::InitialRevision, &[])
    }
    fn poll_controls(&self) -> SessionResult {
        if self.flag(Op::AutoTask, &[])? && !self.flag(Op::AutoEnabled, &[])? {
            return self.fail(0.);
        }
        if self.flag(Op::RevisionChanged, &[])? {
            self.action(Op::StoreRevision, &[])?;
            if self.flag(Op::AutoTask, &[])? {
                self.set_time(Time::MacroRearmAt, self.now()?)?;
                self.set_bool(Bool::PendingMacroReset, true)?;
            }
        }
        if self.bool(Bool::PendingMacroReset)? {
            if self.bool(Bool::MacroBusy)? {
                return self.fail(1.);
            }
            self.set_bool(Bool::PendingMacroReset, false)?;
            if self.flag(Op::MacroExists, &[])? {
                self.action(Op::MacroReset, &[])?;
            }
        }
        let down = self.flag(Op::PhysicalFExists, &[])? && self.flag(Op::PhysicalFRead, &[])?;
        if down && !self.bool(Bool::PhysicalFWasDown)? && !self.bool(Bool::MacroBusy)? {
            self.set_time(Time::MacroRearmAt, self.now()?)?;
        }
        self.set_bool(Bool::PhysicalFWasDown, down)
    }
    fn current(&self) -> SessionResult<f64> {
        let count = self.call(Op::PartySize, &[])? as usize;
        for index in 0..count {
            if self.flag(Op::MemberCurrent, &[index as f64])? {
                return Ok(index as f64);
            }
        }
        Ok(-1.)
    }
    fn interval(&self, seconds: f64) -> SessionResult<bool> {
        if seconds <= 0. {
            return Ok(true);
        }
        if self.now()? - self.time(Time::LastInterval)? < seconds {
            self.action(Op::Wait, &[0.001])?;
            self.action(Op::Refresh, &[])?;
            return Ok(false);
        }
        self.set_time(Time::LastInterval, self.now()?)?;
        Ok(true)
    }
    fn tap(&self, args: &[f64]) -> SessionResult {
        if !self.flag(Op::Interval, &[args[1]])? {
            return Ok(());
        }
        self.action(Op::InputKey, &[args[2]])?;
        self.action(Op::Refresh, &[])?;
        if args[0] > 0. {
            self.action(Op::Sleep, &[args[0]])?;
        }
        Ok(())
    }
    fn click(&self, between: f64) -> SessionResult {
        if !self.flag(Op::Interval, &[between])? {
            return Ok(());
        }
        self.action(Op::InputClick, &[0.01])?;
        self.action(Op::Refresh, &[])
    }
    fn watchdog_body(&self) -> SessionResult {
        if self.flag(Op::ReadRunDialog, &[])? {
            self.action(Op::ReleaseInput, &[])?;
            self.action(Op::Log, &[0.])?;
            for _ in 0..3 {
                self.action(Op::InputEscape, &[0.05])?;
                self.action(Op::Wait, &[0.4])?;
                self.action(Op::Refresh, &[])?;
                if !self.flag(Op::ReadRunDialog, &[])? {
                    self.action(Op::Log, &[1.])?;
                    return Ok(());
                }
            }
            return self.fail(4.);
        }
        if self.flag(Op::DeathDialog, &[])? {
            self.action(Op::ReleaseInput, &[])?;
            return self.fail(5.);
        }
        if self.bool(Bool::MacroBusy)? && !self.flag(Op::HudVisible, &[])? {
            let result = self.call(Op::ChallengeResult, &[])?;
            if result != 0. {
                self.action(Op::ReleaseInput, &[])?;
                return self.fail(if result == 2. { 6. } else { 7. });
            }
        }
        Ok(())
    }
    fn watchdog(&self) -> SessionResult {
        self.action(Op::Checkpoint, &[])?;
        let deadline = self.time(Time::Deadline)?;
        if deadline > 0. && self.now()? > deadline {
            return self.fail(3.);
        }
        if (!self.bool(Bool::Engaged)?
            && !self.bool(Bool::CombatScope)?
            && !self.bool(Bool::MacroBusy)?)
            || self.bool(Bool::CheckingDialog)?
            || self.now()? - self.time(Time::LastDeathCheck)? < 1.
        {
            return Ok(());
        }
        self.set_time(Time::LastDeathCheck, self.now()?)?;
        let previous = self.bool(Bool::CheckingDialog)?;
        self.set_bool(Bool::CheckingDialog, true)?;
        let result = self.watchdog_body();
        let restore = self.set_bool(Bool::CheckingDialog, previous);
        result.and(restore)
    }
    fn next_frame(&self) -> SessionResult {
        self.action(Op::Wait, &[0.001])?;
        self.action(Op::Refresh, &[])?;
        self.action(Op::Watchdog, &[])
    }
    // The original nested min/max maps a NaN clock observation to zero wait.
    #[allow(clippy::manual_clamp)]
    fn sleep(&self, seconds: f64) -> SessionResult {
        if !seconds.is_finite() || !(0. ..=86400.).contains(&seconds) {
            return self.fail(2.);
        }
        let end = self.now()? + seconds;
        loop {
            let remaining = (end - self.now()?).max(0.).min(0.4);
            self.action(Op::Wait, &[remaining])?;
            self.action(Op::Refresh, &[])?;
            self.action(Op::Watchdog, &[])?;
            if !self.bool(Bool::SkipCombatCheck)? && !self.bool(Bool::Checking)? {
                self.action(Op::CheckCombat, &[])?;
            }
            if self.now()? < end {
                continue;
            }
            break;
        }
        Ok(())
    }
    fn cooldown(&self, skill: f64, index: f64) -> SessionResult<f64> {
        if !self.flag(Op::CurrentExists, &[])? {
            return Ok(0.);
        }
        let current = self.call(Op::CurrentIndex, &[])?;
        self.action(Op::RefreshCooldowns, &[current])?;
        self.call(
            Op::Cooldown,
            &[skill, if index < 0. { current } else { index }],
        )
    }
    fn available(&self, args: &[f64]) -> SessionResult<bool> {
        Ok(
            (args[2] == 0. || self.call(Op::Highlighted, &[args[0]])? > 0.)
                && (args[3] == 0. || !self.flag(Op::HasCd, &args[..2])?),
        )
    }
    fn health_bar(&self) -> SessionResult<bool> {
        let engaged = self.bool(Bool::Engaged)?;
        if self.flag(Op::HealthBarNative, &[engaged as u8 as f64])? {
            return Ok(true);
        }
        if !self.flag(Op::InTeam, &[])? {
            return Ok(false);
        }
        if self.now()? - self.time(Time::LastHealthCheck)? >= 0.2 {
            self.set_bool(Bool::HealthCache, self.flag(Op::ShiftedHealthBar, &[])?)?;
            self.set_time(Time::LastHealthCheck, self.now()?)?;
        }
        self.bool(Bool::HealthCache)
    }
    fn target_enemy(&self) -> SessionResult<bool> {
        if self.bool(Bool::Engaged)?
            && self.flag(Op::InTeam, &[])?
            && self.flag(Op::SessionHealthBar, &[])?
        {
            return Ok(true);
        }
        if self.flag(Op::TargetLocked, &[])? {
            return Ok(true);
        }
        let start = self.now()?;
        while self.now()? - start < self.time(Time::TargetEnemyTimeout)? {
            self.action(Op::ClickMiddle, &[0.2])?;
            if self.flag(Op::InTeam, &[])? && self.flag(Op::SessionHealthBar, &[])? {
                return Ok(true);
            }
            if self.flag(Op::TargetLocked, &[])? {
                return Ok(true);
            }
            self.action(Op::NextFrame, &[])?;
        }
        Ok(false)
    }
    fn refresh_observation(&self) -> SessionResult {
        self.action(Op::Refresh, &[])?;
        self.action(Op::Watchdog, &[])
    }
    fn workshop_checkpoint(&self) -> SessionResult {
        self.action(Op::Checkpoint, &[])?;
        let last = self.time(Time::LastWorkshopObservation)?;
        if last < 0. || self.now()? - last >= 0.1 {
            self.set_time(Time::LastWorkshopObservation, self.now()?)?;
            self.action(Op::RefreshObservation, &[])?;
        }
        Ok(())
    }
    pub fn run(&self, method: i32, args: &[f64]) -> SessionResult<f64> {
        let method = Method::try_from(method)?;
        if args.len() != method.arg_count() {
            return Err(INVALID_ACTION);
        }
        match method {
            Method::Initialize => self.initialize()?,
            Method::PollControls => self.poll_controls()?,
            Method::Current => return self.current(),
            Method::Interval => return Ok(self.interval(args[0])? as u8 as f64),
            Method::Tap => self.tap(args)?,
            Method::Click => self.click(args[0])?,
            Method::Watchdog => self.watchdog()?,
            Method::NextFrame => self.next_frame()?,
            Method::Sleep => self.sleep(args[0])?,
            Method::Cooldown => return self.cooldown(args[0], args[1]),
            Method::Available => return Ok(self.available(args)? as u8 as f64),
            Method::HealthBar => return Ok(self.health_bar()? as u8 as f64),
            Method::TargetEnemy => return Ok(self.target_enemy()? as u8 as f64),
            Method::RefreshObservation => self.refresh_observation()?,
            Method::WorkshopCheckpoint => self.workshop_checkpoint()?,
            Method::HasCd => return Ok((self.cooldown(args[0], args[1])? > 0.2) as u8 as f64),
            Method::TargetLocked => return Ok(self.target_locked()? as u8 as f64),
            Method::CheckBreak => return Ok(self.check_break()? as u8 as f64),
            Method::MacroBreakPrompt => return Ok(self.macro_break_prompt()? as u8 as f64),
            Method::VisibleBreakPrompt => {
                return Ok(self.visible_break_prompt(args[0])? as u8 as f64)
            }
            Method::BreakF => return Ok(self.break_f(args[0] != 0.)? as u8 as f64),
            Method::CheckCombat => self.check_combat()?,
            Method::ResetCombat => self.reset_combat()?,
            Method::PerformCurrent => self.perform_current()?,
            Method::UpdatePortraits => self.update_portraits()?,
            Method::SwitchHealer => self.switch_healer()?,
            Method::CombatActive => return Ok(self.combat_active()? as u8 as f64),
            Method::RestoreCondition => return Ok(self.restore_condition()? as u8 as f64),
            Method::RunAutoCombat => return Ok(self.run_auto_combat()? as u8 as f64),
            Method::Active => {
                self.action(Op::Watchdog, &[])?;
                return Ok(self.flag(Op::CombatActive, &[])? as u8 as f64);
            }
            Method::SwitchNext => self.switch_next(args[0] != 0., args[1] != 0.)?,
            Method::RecoverStartingPopup => self.recover_starting_popup(args[0] != 0.)?,
            Method::CloseRevivalDialog => return Ok(self.close_revival_dialog()? as u8 as f64),
            Method::CombatOnce => return Ok(self.combat_once(args)? as u8 as f64),
            Method::RecoverDead => self.recover_dead(args[0] != 0.)?,
            Method::DeathClosed => return Ok((!self.flag(Op::DeathDialog, &[])?) as u8 as f64),
            Method::TeamOrDeath => {
                return Ok(
                    (self.flag(Op::InTeam, &[])? || self.flag(Op::DeathDialog, &[])?) as u8 as f64,
                )
            }
            Method::LoadParty => return Ok(self.load_party()? as u8 as f64),
            Method::PublishParty => self.publish_party()?,
            Method::NamedImage => {
                if !self.flag(Op::NamedRegionEmpty, &[])? {
                    self.action(Op::CloneNamedImage, &[])?;
                }
            }
            Method::Snapshot => self.snapshot()?,
            Method::Characters => self.characters()?,
            Method::Hotkey => self.hotkey(args[0])?,
            Method::Feature => return Ok(self.feature()? as u8 as f64),
            Method::BestInBox => self.best_in_box(args[0])?,
            Method::ImageRegion => {
                if Self::region_inside(args) {
                    self.action(Op::ExtractRegionImage, &[])?;
                }
            }
            Method::SegmentedGauge => {
                return if Self::region_inside(args) {
                    self.call(Op::MeasureSegmentedGauge, &[])
                } else {
                    Ok(0.)
                }
            }
            Method::Concerto => return self.concerto(),
            Method::ConcertoAreaRatio => return self.concerto_area_ratio(),
            Method::WorkshopTaskConfig => self.workshop_task_config()?,
            Method::EnsureElement => {
                if self.call(Op::CharacterRing, &[])? < 0. {
                    self.action(Op::CallCharacterConcerto, &[])?;
                }
            }
            Method::LogIfInfo => {
                if self.flag(Op::HasInfo, &[])? {
                    self.action(Op::EmitCombatLog, &[])?;
                }
            }
            Method::TargetName => return Ok((args[0] == 1600.) as u8 as f64),
        }
        Ok(0.)
    }
}
