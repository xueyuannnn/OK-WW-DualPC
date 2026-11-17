//! Foreground task scheduling and the transition back to enabled triggers.
//! This preserves the original epoch/lock/input-release order.
use std::ffi::c_void;
const HOST_ERROR: i32 = -1;
const INVALID_ACTION: i32 = -3;
include!(concat!(env!("OUT_DIR"), "/engine_abi.rs"));
mod catalog;
pub mod commands;
mod lifecycle;
mod reporting;
mod scripts;
mod session;
mod setup;
mod teams;
type EngineResult<T = ()> = Result<T, i32>;
#[repr(C)]
pub struct Host {
    pub context: *mut c_void,
    pub invoke: unsafe extern "C" fn(*mut c_void, i32, *const f64, usize, *mut f64) -> i32,
}
impl Host {
    fn call(&self, op: Op, args: &[f64]) -> EngineResult<f64> {
        let mut out = [0.; 3];
        // SAFETY: the host borrows these live buffers synchronously and catches
        // exceptions, which remain host-owned until explicitly consumed.
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
            Ok(out[0])
        } else {
            Err(HOST_ERROR)
        }
    }
    fn action(&self, op: Op, args: &[f64]) -> EngineResult {
        self.call(op, args)?;
        Ok(())
    }
    fn flag(&self, op: Op, args: &[f64]) -> EngineResult<bool> {
        Ok(self.call(op, args)? != 0.)
    }
    fn now(&self) -> EngineResult<f64> {
        self.call(Op::Now, &[])
    }
    fn resume_triggers(&self) -> EngineResult {
        self.action(Op::ReleaseInput, &[])?;
        self.action(Op::LockLifecycle, &[])?;
        let result = (|| {
            if !self.flag(Op::GateActive, &[])? {
                self.action(Op::ThrowCancelled, &[])?;
                return Err(HOST_ERROR);
            }
            self.action(Op::SetForeground, &[0.])?;
            self.action(Op::SetCancelForeground, &[0.])
        })();
        let unlock = self.action(Op::UnlockLifecycle, &[]);
        result.and(unlock)?;
        self.action(Op::ClearSelected, &[])?;
        self.action(Op::ClearActiveTask, &[])?;
        self.action(Op::ClearContextTask, &[])?;
        self.action(Op::ClearOverrides, &[])?;
        self.action(Op::ClearContextCombat, &[])?;
        self.action(Op::ResetRunner, &[])?;
        self.action(Op::CombatPending, &[])?;
        self.action(Op::ClearLimit, &[])?;
        self.action(Op::ClearNext, &[])?;
        self.action(Op::PostStatus, &[])
    }
    fn cancel_task(&self) -> EngineResult {
        self.action(Op::SetStoppedFailure, &[])?;
        self.action(Op::RecordResult, &[0.])?;
        self.action(Op::SetResultRecorded, &[1.])?;
        self.resume_triggers()?;
        self.action(Op::LogStopped, &[])
    }
    fn run_loop(&self) -> EngineResult {
        let mut last_status = 0.;
        loop {
            self.action(Op::Checkpoint, &[])?;
            if self.flag(Op::HasSelected, &[])? && self.flag(Op::CancelForeground, &[])? {
                self.cancel_task()?;
            }
            self.action(Op::Refresh, &[])?;
            let count = self.call(Op::TaskCount, &[])? as usize;
            for index in 0..count {
                self.action(Op::SelectTask, &[index as f64])?;
                if self.flag(Op::HasSelected, &[])? && !self.flag(Op::TaskSelected, &[])? {
                    continue;
                }
                if !self.flag(Op::HasSelected, &[])? && !self.flag(Op::TaskTrigger, &[])? {
                    continue;
                }
                if !self.flag(Op::HasSelected, &[])? {
                    let disabled = if self.flag(Op::TaskCustom, &[])? {
                        !self.flag(Op::CustomTrigger, &[])? || !self.flag(Op::ScriptEnabled, &[])?
                    } else {
                        !self.flag(Op::ProfileEnabled, &[0.])?
                    };
                    if disabled {
                        continue;
                    }
                }
                if self.now()? < self.call(Op::NextTime, &[])? {
                    continue;
                }
                self.action(Op::SetTaskContext, &[])?;
                self.action(Op::SetTaskRunning, &[1.])?;
                let handled = match self.flag(Op::RunTask, &[]) {
                    Ok(value) => value,
                    Err(error) => match self.call(Op::ExceptionKind, &[])? as i32 {
                        1 => {
                            if !self.flag(Op::GateActive, &[])?
                                || !self.flag(Op::CancelForeground, &[])?
                            {
                                return Err(error);
                            }
                            self.action(Op::ClearException, &[])?;
                            self.action(Op::SetTaskRunning, &[0.])?;
                            self.cancel_task()?;
                            break;
                        }
                        2 => {
                            if !self.flag(Op::TaskAutoCombat, &[])?
                                || self.flag(Op::ProfileEnabled, &[1.])?
                            {
                                return Err(error);
                            }
                            self.action(Op::ClearException, &[])?;
                            self.action(Op::ReleaseInput, &[])?;
                            self.action(Op::LogException, &[])?;
                            false
                        }
                        _ => return Err(error),
                    },
                };
                self.action(Op::SetTaskRunning, &[0.])?;
                let next = self.now()? + self.call(Op::TaskInterval, &[])?;
                self.action(Op::SetNextTime, &[next])?;
                if !self.flag(Op::TaskTrigger, &[])? {
                    self.action(Op::Checkpoint, &[])?;
                    self.action(Op::ReleaseInput, &[])?;
                    let exit = self.flag(Op::ExitOption, &[])?
                        || (!self.flag(Op::TaskCustom, &[])? && self.flag(Op::ExitProfile, &[])?);
                    self.action(Op::SetExitAfter, &[exit as u8 as f64])?;
                    self.action(Op::RecordResult, &[1.])?;
                    self.action(Op::SetResultRecorded, &[1.])?;
                    self.action(Op::TaskDone, &[])?;
                    self.action(Op::CompletionNameReset, &[])?;
                    let names = self.call(Op::CatalogCount, &[])? as usize;
                    for name in 0..names {
                        self.action(Op::SelectCatalog, &[name as f64])?;
                        if self.flag(Op::CatalogSelected, &[])? {
                            self.action(Op::CompletionNameSet, &[])?;
                        }
                    }
                    self.action(Op::NotifyDone, &[])?;
                    if exit {
                        self.action(Op::SetCompleted, &[1.])?;
                        self.action(Op::StopGate, &[])?;
                        break;
                    }
                    self.resume_triggers()?;
                    self.action(Op::LogCompleted, &[])?;
                    break;
                }
                if handled {
                    break;
                }
            }
            if self.now()? - last_status > 0.25 {
                self.action(Op::PostStatusSelected, &[])?;
                last_status = self.now()?;
            }
            let interval = self.call(Op::TriggerInterval, &[])?.clamp(1., 60000.) / 1000.;
            self.action(Op::Wait, &[interval])?;
        }
    }
    fn script_iterations(&self) -> EngineResult<bool> {
        let trigger = self.flag(Op::ScriptTrigger, &[])?;
        let repeat = (self.call(Op::ScriptRepeat, &[])? as i32).clamp(1, 1_000_000);
        let continuous = !trigger && self.flag(Op::ScriptContinuous, &[])?;
        let (mut handled, mut iteration) = (false, 0_u64);
        while if trigger {
            iteration < 1
        } else {
            continuous || iteration < repeat as u64
        } {
            self.action(Op::Checkpoint, &[])?;
            let next = iteration.wrapping_add(1);
            let low = (next as u32) as f64;
            let high = (next >> 32) as f64;
            if !trigger {
                self.action(
                    Op::ScriptProgress,
                    &[
                        iteration as f64,
                        repeat as f64,
                        continuous as u8 as f64,
                        low,
                        high,
                    ],
                )?;
            }
            if !trigger && self.flag(Op::HasInfo, &[])? {
                self.action(
                    Op::ScriptInfo,
                    &[low, high, continuous as u8 as f64, repeat as f64],
                )?;
            }
            match self.flag(Op::ScriptRun, &[trigger as u8 as f64]) {
                Ok(value) => handled = value || handled,
                Err(error) => {
                    if self.call(Op::ExceptionKind, &[])? != 4. {
                        return Err(error);
                    }
                    self.action(Op::ClearException, &[])?;
                    if self.flag(Op::HasInfo, &[])? {
                        self.action(Op::ScriptDisabledInfo, &[])?;
                    }
                    return Ok(false);
                }
            }
            iteration = next;
        }
        if !trigger && !continuous {
            self.action(Op::ScriptComplete, &[repeat as f64])?;
        }
        Ok(handled)
    }
    pub fn run(&self, method: i32, args: &[f64]) -> EngineResult<f64> {
        let method = Method::try_from(method)?;
        if args.len() != method.arg_count() {
            return Err(INVALID_ACTION);
        }
        match method {
            Method::Run => self.run_loop()?,
            Method::ResumeTriggers => self.resume_triggers()?,
            Method::CancelTask => self.cancel_task()?,
            Method::ScriptIterations => return Ok(self.script_iterations()? as u8 as f64),
            Method::Pause => self.pause()?,
            Method::Start => self.start()?,
            Method::TryLaunch => self.try_launch()?,
            Method::Destroy => self.destroy()?,
            Method::Notify => self.notify()?,
            Method::PostStatus => self.post_status()?,
            Method::Load => self.load()?,
            Method::BeginPreload => self.begin_preload()?,
            Method::LoadModels => self.load_models()?,
            Method::Catalog => self.catalog()?,
            Method::Status => self.status()?,
            Method::Command => self.command()?,
            Method::ScriptCommand => self.script_command()?,
            Method::ScriptConfigure => self.script_configure()?,
            Method::ScriptReply => self.script_reply()?,
            Method::ScriptManage => self.script_manage()?,
            Method::Initialize => self.initialize()?,
            Method::ExitPoll => self.exit_poll()?,
            Method::InvalidateParty => self.invalidate_party()?,
            Method::SwitchTeam => self.switch_team()?,
            Method::TeamReply => self.team_reply()?,
            Method::TeamWork => self.team_work()?,
            Method::Idle => return Ok(self.idle()? as u8 as f64),
            Method::Prepare => self.prepare()?,
            Method::Health => self.health()?,
            Method::Progress => self.progress()?,
            Method::ProgressDelivery => self.progress_delivery()?,
            Method::CombatDelivery => self.combat_delivery()?,
            Method::InfoProgress => self.info_progress()?,
            Method::AddTemplates => self.add_templates()?,
            Method::BuiltinDispatch => return self.dispatch_builtin(),
            Method::RegisterScripts => self.register_scripts()?,
            Method::RequireFrame => {
                if !self.flag(Op::FrameAvailable, &[])? {
                    self.fail(20.)?;
                }
            }
            Method::RecordResult => self.record_result()?,
            Method::StepDiagnostic => self.step_diagnostic()?,
            Method::ErrorScreenshot => self.error_screenshot()?,
            Method::SessionLifetime => self.session_lifetime()?,
            Method::Disarm => self.disarm()?,
            Method::ScheduleExit => self.schedule_exit()?,
        }
        Ok(0.)
    }
}
