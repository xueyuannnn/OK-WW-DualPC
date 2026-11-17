//! Task-wide sequencing, with the original observation/recovery order.
use std::ffi::c_void;
const HOST_ERROR: i32 = -1;
pub const INVALID_WAIT: i32 = -2;
const INVALID_ACTION: i32 = -3;
include!(concat!(env!("OUT_DIR"), "/taskflow_abi.rs"));
mod menus;
mod text;
pub(crate) use text::{marker_mask, monthly_day, monthly_offset};
mod image;
mod workflow;
type Result<T = ()> = std::result::Result<T, i32>;
#[repr(C)]
pub struct Host {
    pub context: *mut c_void,
    pub invoke: unsafe extern "C" fn(*mut c_void, i32, *const f64, usize, *mut f64) -> i32,
    pub query: Option<
        unsafe extern "C" fn(
            *mut c_void,
            i32,
            *const u8,
            usize,
            *const f64,
            usize,
            *mut f64,
        ) -> i32,
    >,
}
impl Host {
    fn query(&self, kind: i32, name: &str, args: &[f64]) -> Result<f64> {
        let Some(query) = self.query else {
            return Err(HOST_ERROR);
        };
        let mut out = [0.; 3];
        // SAFETY: string and numeric buffers remain live throughout this
        // synchronous query; the host catches every exception.
        if unsafe {
            query(
                self.context,
                kind,
                name.as_ptr(),
                name.len(),
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
    fn wait_for(&self, condition: impl Fn() -> Result<bool>, timeout: f64) -> Result<bool> {
        crate::execution::until_sequence(
            || self.now(),
            || self.action(Op::Checkpoint, &[]),
            |seconds| self.action(Op::Wait, &[seconds]),
            || {
                self.action(Op::Refresh, &[])?;
                condition()
            },
            timeout,
            0.05,
        )
    }
    fn call(&self, op: Op, args: &[f64]) -> Result<[f64; 3]> {
        let mut out = [0.; 3];
        // SAFETY: all buffers are borrowed for this synchronous call; the host
        // catches exceptions and keeps its data alive until the call returns.
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
    fn action(&self, op: Op, args: &[f64]) -> Result {
        self.call(op, args)?;
        Ok(())
    }
    fn scalar(&self, op: Op, args: &[f64]) -> Result<f64> {
        Ok(self.call(op, args)?[0])
    }
    fn value(&self, op: Op, args: &[f64]) -> Result<f64> {
        Ok(self.call(op, args)?[0])
    }
    fn flag(&self, op: Op, args: &[f64]) -> Result<bool> {
        Ok(self.value(op, args)? != 0.)
    }
    fn now(&self) -> Result<f64> {
        self.value(Op::Now, &[])
    }
    fn refresh(&self) -> Result {
        self.action(Op::Checkpoint, &[])?;
        let mut present = self.flag(Op::ReadFrame, &[])?;
        let mut recovering = false;
        if !present && self.flag(Op::HasRecovery, &[])? {
            recovering = self.flag(Op::Recovering, &[])?;
            if !recovering {
                present = self.flag(Op::ReadFrame, &[])?;
            }
        }
        if !present && recovering {
            self.action(Op::ReleaseInput, &[])?;
            let began = self.now()?;
            let mut reported = -1.;
            while !present && self.now()? - began < 120. {
                let elapsed = self.now()? - began;
                // The info callback can be absent; that also preserves the
                // original reported timestamp semantics in headless runs.
                if (reported < 0. || elapsed - reported >= 1.)
                    && self.flag(Op::RecoveryInfo, &[0., elapsed])?
                {
                    reported = elapsed;
                }
                self.action(Op::Wait, &[0.1])?;
                present = self.flag(Op::ReadFrame, &[])?;
                if !present && !self.flag(Op::Recovering, &[])? {
                    present = self.flag(Op::ReadFrame, &[])?;
                    break;
                }
            }
            if present {
                self.action(Op::RecoveryInfo, &[1., 0.])?;
            }
        }
        if !present {
            return Err(-4);
        }
        self.action(Op::CommitFrame, &[])
    }
    fn sleep(&self, seconds: f64) -> Result {
        if !seconds.is_finite() || !(0. ..=86400.).contains(&seconds) {
            return Err(INVALID_WAIT);
        }
        let began = self.now()?;
        self.action(Op::MonthlyWindow, &[])?;
        self.action(Op::Wait, &[(seconds - (self.now()? - began)).max(0.)])?;
        self.action(Op::Refresh, &[])
    }
    fn party(&self) -> Result<[f64; 3]> {
        let (mut found, mut current) = (0, -1);
        for index in 0..3 {
            if self.flag(Op::PartyProbe, &[index as f64, 0.])?
                || self.flag(Op::PartyProbe, &[index as f64, 1.])?
            {
                found += 1;
            } else if current < 0 {
                current = index;
            }
        }
        let visible = found == 1 || found == 2;
        if visible {
            self.action(Op::SetLoggedIn, &[1.])?;
        }
        Ok([
            visible as u8 as f64,
            if visible { current as f64 } else { -1. },
            (found + 1) as f64,
        ])
    }
    fn monthly_card(&self) -> Result<bool> {
        if !self.flag(Op::MonthlyEnabled, &[])? {
            return Ok(false);
        }
        let mut clicked = false;
        for _ in 0..3 {
            let monthly =
                self.flag(Op::MonthlyTemplate, &[])? || self.flag(Op::MonthlyText, &[])?;
            let reward = !monthly && self.flag(Op::RewardText, &[])?;
            if !monthly && !reward {
                return Ok(clicked);
            }
            self.action(Op::MonthlyInfo, &[monthly as u8 as f64])?;
            self.action(Op::ClickRelative, &[0.5, 0.89, 0.8, 0.])?;
            clicked = true;
        }
        Ok(clicked)
    }
    fn monthly_window(&self) -> Result {
        if self.flag(Op::HandlingMonthly, &[])?
            || self.flag(Op::FrameEmpty, &[])?
            || self.flag(Op::InCombat, &[])?
            || !self.flag(Op::InputIdle, &[])?
            || !self.flag(Op::MonthlyEnabled, &[])?
        {
            return Ok(());
        }
        if !self.flag(Op::MonthlyKey, &[])?
            || self.flag(Op::MonthlyHandled, &[])?
            || !self.flag(Op::InTeam, &[])?
        {
            return Ok(());
        }
        self.action(Op::RememberMonthly, &[])?;
        self.action(Op::SetHandlingMonthly, &[1.])?;
        let result: Result = (|| {
            self.action(Op::RecoveryInfo, &[2., 0.])?;
            let until = self.now()? + 120.;
            loop {
                self.action(Op::Refresh, &[])?;
                if self.flag(Op::MonthlyCard, &[])? {
                    break;
                }
                self.action(Op::Wait, &[0.25])?;
                if self.now()? >= until {
                    break;
                }
            }
            Ok(())
        })();
        if result.is_err() {
            let _ = self.action(Op::SetHandlingMonthly, &[0.]);
        }
        result?;
        self.action(Op::SetHandlingMonthly, &[0.])
    }
    pub fn run(&self, method: i32, args: &[f64]) -> Result<[f64; 3]> {
        let method = Method::try_from(method)?;
        if args.len() != method.arg_count() {
            return Err(INVALID_ACTION);
        }
        match method {
            Method::Refresh => self.refresh()?,
            Method::Sleep => self.sleep(args[0])?,
            Method::Party => return self.party(),
            Method::MonthlyCard => return Ok([self.monthly_card()? as u8 as f64, 0., 0.]),
            Method::MonthlyWindow => self.monthly_window()?,
            Method::SkipConfirm => return Ok([self.skip_confirm()? as u8 as f64, 0., 0.]),
            Method::TravelButton => return Ok([self.travel_button()? as u8 as f64, 0., 0.]),
            Method::Login => return Ok([self.login()? as u8 as f64, 0., 0.]),
            Method::FastTravel => return Ok([self.fast_travel()? as u8 as f64, 0., 0.]),
            Method::AutoPick => return Ok([self.auto_pick()? as u8 as f64, 0., 0.]),
            Method::AutoDialog => return Ok([self.auto_dialog()? as u8 as f64, 0., 0.]),
            Method::BeginWorkflow => self.begin_workflow()?,
            Method::WorkflowStep => self.workflow_step()?,
            Method::PublishWorkflow => self.publish_workflow()?,
            Method::ReportProgress => self.report_progress(args[0], args[1])?,
            Method::MonthlyText => return Ok([self.monthly_text()? as u8 as f64, 0., 0.]),
            Method::RewardText => return Ok([self.reward_text()? as u8 as f64, 0., 0.]),
            Method::MatchingText => return Ok([self.matching_text()?, 0., 0.]),
            Method::ContinueMarker => return Ok([self.continue_marker()? as u8 as f64, 0., 0.]),
            Method::Find => self.find()?,
            Method::FindAny => self.find_any()?,
            Method::ReadText => self.read_text()?,
            Method::TextScaled => self.text_scaled(args[0])?,
            Method::InputAfter => {
                self.action(Op::SubmitInput, &[])?;
                if args[0] != 0. {
                    self.action(Op::Sleep, &[args[0]])?;
                }
            }
            Method::WaitCondition => {
                self.action(Op::Refresh, &[])?;
                return self.call(Op::Predicate, &[]);
            }
            Method::Setting => {
                if self.flag(Op::HasOverride, &[])? {
                    self.action(Op::ReadOverride, &[])?;
                } else {
                    self.action(Op::ReadProfileSetting, &[])?;
                }
            }
        }
        Ok([0.; 3])
    }
}
