//! Character action sequencing. Observations and virtual character hooks stay
//! in the host; this module owns the original ordering, deadlines and cleanup.
use std::ffi::c_void;
mod additional;
mod advanced;
mod basic;
mod extended;
mod forms;
mod gauges;
mod lily;
mod spectro;
mod stateful;

type ActionResult<T = ()> = Result<T, i32>;
pub const HOST_ERROR: i32 = -1;
pub const INVALID_WAIT: i32 = -2;
pub const INVALID_ACTION: i32 = -3;

include!(concat!(env!("OUT_DIR"), "/rotation_abi.rs"));
#[repr(C)]
pub struct Host {
    pub context: *mut c_void,
    pub invoke: unsafe extern "C" fn(*mut c_void, i32, *const f64, usize, *mut f64) -> i32,
    pub query: Option<Query>,
}
pub type Query = unsafe extern "C" fn(
    *mut c_void,
    i32,
    *const u8,
    usize,
    *const u8,
    usize,
    *const f64,
    usize,
    *mut f64,
) -> i32;

// Field IDs are part of the typed native bridge, never persisted in profiles.
const IN_LIBERATION: f64 = 0.;
const SKIP_COMBAT: f64 = 1.;
const USE_LIBERATION: f64 = 2.;
const LEVITATOR: f64 = 3.;
const OPEN_WORLD: f64 = 4.;
const INTRO: f64 = 0.;
const SUB_INTRO: f64 = 1.;
const CURRENT: f64 = 2.;
const ELEMENT: f64 = 3.;
const CON: f64 = 4.;
const LAST_PERFORM: f64 = 5.;
const INTRO_MOTION: f64 = 6.;
const CYCLE_START: f64 = 7.;
const LIBERATION_READY: f64 = 8.;
const ECHO_READY: f64 = 9.;
const RESONANCE_READY: f64 = 10.;
const TOOLBOX: f64 = 11.;
const LAST_FULL_CON_SWITCH: f64 = 12.;
const LAST_RESONANCE: f64 = 13.;
const LAST_ECHO: f64 = 14.;
const LAST_LIBERATION: f64 = 15.;
const INDEX: f64 = 16.;
const ROLE: f64 = 17.;
const RESONANCE: f64 = 0.;
const ECHO: f64 = 1.;
const LIBERATION: f64 = 2.;

// Virtual hook IDs: preserve derived C++ (and later Rust) overrides. They may
// re-enter this module through another stack-local host, with no borrowed state.
const VIRTUAL_RESONANCE: f64 = 0.;
const VIRTUAL_ECHO: f64 = 1.;
const VIRTUAL_LIBERATION: f64 = 2.;
const VIRTUAL_SWITCH: f64 = 3.;
const VIRTUAL_FORTE: f64 = 4.;
const VIRTUAL_DOWN: f64 = 5.;
const VIRTUAL_FLYING: f64 = 6.;
const VIRTUAL_NORMAL_ATTACKS: f64 = 7.;
const VIRTUAL_INTRO: f64 = 8.;
const VIRTUAL_HEAVY_FORTE: f64 = 9.;

#[inline]
fn cpp_max_zero(value: f64) -> f64 {
    // std::max(0., NaN) returns its first argument.
    if value > 0. {
        value
    } else {
        0.
    }
}

impl Host {
    fn observe(&self, kind: i32, name: &str, area: &str, args: &[f64]) -> ActionResult<f64> {
        Ok(self.observe_values(kind, name, area, args)?[0])
    }
    fn observe_values(
        &self,
        kind: i32,
        name: &str,
        area: &str,
        args: &[f64],
    ) -> ActionResult<[f64; 3]> {
        let Some(query) = self.query else {
            return Err(HOST_ERROR);
        };
        let mut out = [0.; 3];
        // SAFETY: borrowed strings/numbers and the output remain live during
        // this synchronous host query; the callback catches host exceptions.
        let status = unsafe {
            query(
                self.context,
                kind,
                name.as_ptr(),
                name.len(),
                area.as_ptr(),
                area.len(),
                args.as_ptr(),
                args.len(),
                out.as_mut_ptr(),
            )
        };
        if status == 1 {
            Ok(out)
        } else {
            Err(HOST_ERROR)
        }
    }
    fn feature(&self, name: &str, threshold: f64, area: &str) -> ActionResult<bool> {
        Ok(self.observe(0, name, area, &[threshold])? != 0.)
    }
    fn setting_bool(&self, name: &str) -> ActionResult<bool> {
        Ok(self.observe(1, name, "", &[])? != 0.)
    }
    fn call(&self, op: Op, args: &[f64]) -> ActionResult<[f64; 3]> {
        let mut out = [0.; 3];
        // SAFETY: constructed only by the synchronous checked C ABI entrypoint;
        // the host catches every C++ exception before returning a status.
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
    fn value(&self, op: Op, args: &[f64]) -> ActionResult<f64> {
        Ok(self.call(op, args)?[0])
    }
    fn action(&self, op: Op, args: &[f64]) -> ActionResult {
        self.call(op, args).map(|_| ())
    }
    fn truth(&self, op: Op, args: &[f64]) -> ActionResult<bool> {
        Ok(self.value(op, args)? != 0.)
    }
    fn now(&self) -> ActionResult<f64> {
        self.value(Op::Now, &[])
    }
    fn flag(&self, field: f64) -> ActionResult<bool> {
        self.truth(Op::HostFlag, &[field])
    }
    fn set_flag(&self, field: f64, value: bool) -> ActionResult {
        self.action(Op::SetHostFlag, &[field, value as u8 as f64])
    }
    fn state(&self, field: f64) -> ActionResult<f64> {
        self.value(Op::State, &[field])
    }
    fn set(&self, field: f64, value: f64) -> ActionResult {
        self.action(Op::SetState, &[field, value])
    }
    fn click(&self, right: bool, interval: f64) -> ActionResult {
        self.action(Op::Click, &[right as u8 as f64, interval])
    }
    fn sleep(&self, seconds: f64, check_combat: bool) -> ActionResult {
        let previous = self.flag(SKIP_COMBAT)?;
        self.set_flag(SKIP_COMBAT, previous || !check_combat)?;
        let result = self.action(Op::Sleep, &[cpp_max_zero(seconds)]);
        let cleanup = self.set_flag(SKIP_COMBAT, previous);
        result.and(cleanup)
    }
    fn elapsed(&self, start: f64) -> ActionResult<f64> {
        self.value(Op::Elapsed, &[start, 0.])
    }
    fn skill_available(&self, skill: f64) -> ActionResult<bool> {
        self.truth(Op::SkillAvailable, &[skill])
    }
    fn virtual_action(&self, args: &[f64]) -> ActionResult<[f64; 3]> {
        self.call(Op::Virtual, args)
    }
    fn send(&self, skill: f64, after: f64) -> ActionResult {
        self.action(Op::Send, &[skill, after, -1., 0.01])
    }
    fn record(&self, skill: f64) -> ActionResult {
        let field = match skill as i32 {
            0 => LAST_RESONANCE,
            1 => LAST_ECHO,
            2 => LAST_LIBERATION,
            _ => return Ok(()),
        };
        self.set(field, self.now()?)
    }

    pub fn wait(&self, timeout: f64, settle: f64) -> ActionResult<bool> {
        self.wait_with(
            || self.truth(Op::Condition, &[]),
            timeout,
            settle,
            || self.action(Op::Post, &[]),
            || self.action(Op::Pre, &[]),
        )
    }
    fn wait_with(
        &self,
        mut condition: impl FnMut() -> ActionResult<bool>,
        timeout: f64,
        settle: f64,
        mut post: impl FnMut() -> ActionResult,
        mut pre: impl FnMut() -> ActionResult,
    ) -> ActionResult<bool> {
        if timeout < 0. || !timeout.is_finite() || !settle.is_finite() || settle < 0. {
            return Err(INVALID_WAIT);
        }
        let deadline = self.now()? + if timeout == 0. { 10. } else { timeout };
        let mut settled = -1.;
        loop {
            pre()?;
            self.action(Op::NextFrame, &[])?;
            if condition()? {
                if settle == 0. || (settled >= 0. && self.now()? - settled > settle) {
                    return Ok(true);
                }
                if settled < 0. {
                    settled = self.now()?;
                }
                continue;
            }
            settled = -1.;
            post()?;
            if self.now()? > deadline {
                return Ok(false);
            }
        }
    }

    fn resonance(&self, o: &[f64]) -> ActionResult<[f64; 3]> {
        // post_sleep, animation, send_click, animation_minimum, timeout, click_f
        let (post_sleep, animation, send_click, minimum, click_f) =
            (o[0], o[1] != 0., o[2] != 0., o[3], o[5] != 0.);
        let mut timeout = if o[4] == 0. { 15. } else { o[4] };
        let (mut last_click, mut click_time, mut animation_start, mut last_f) = (0., 0., 0., 0.);
        let mut last_was_skill = false;
        let mut clicked = false;
        let start = self.now()?;
        let result = (|| {
            loop {
                if self.now()? - start > timeout
                    || (self.flag(IN_LIBERATION)? && self.now()? - start > 6.)
                {
                    break;
                }
                if animation {
                    if !self.truth(Op::InTeam, &[])? {
                        self.set_flag(IN_LIBERATION, true)?;
                        let now = self.now()?;
                        if animation_start == 0. {
                            animation_start = now;
                        }
                        timeout = 15.;
                        if now - click_time > 6. {
                            self.set_flag(IN_LIBERATION, false)?;
                        }
                        if click_f && now - animation_start >= minimum && now - last_f >= 0.1 {
                            self.action(Op::Tap, &[1., 0., -1., 0.01])?;
                            last_f = now;
                        }
                        self.action(Op::NextFrame, &[])?;
                        self.action(Op::CheckCombat, &[])?;
                        continue;
                    } else if self.flag(IN_LIBERATION)? {
                        break;
                    }
                }
                self.action(Op::CheckCombat, &[])?;
                let now = self.now()?;
                if !self.skill_available(RESONANCE)? && (!animation || now - start > minimum) {
                    break;
                }
                if now - last_click > 0.1 {
                    if send_click && last_was_skill {
                        self.click(false, -1.)?;
                        last_was_skill = false;
                        continue;
                    }
                    if self.skill_available(RESONANCE)? {
                        if click_time == 0. {
                            clicked = true;
                            click_time = now;
                            self.record(RESONANCE)?;
                        }
                        last_was_skill = true;
                        self.send(RESONANCE, 0.)?;
                        if animation {
                            self.sleep(0.2, false)?;
                        }
                    }
                    last_click = now;
                }
                self.action(Op::NextFrame, &[])?;
            }
            self.set_flag(IN_LIBERATION, false)?;
            if clicked {
                self.sleep(post_sleep, true)?;
            }
            let duration = if click_time != 0. {
                self.now()? - click_time
            } else {
                0.
            };
            let animated = animation_start > 0.;
            if animated {
                // Evaluate the original right-most time expression first, as
                // MSVC did for add(now(), click_time, now()-animation_start).
                let duration = self.now()? - animation_start;
                let now = self.now()?;
                self.action(Op::FreezeAdd, &[now, click_time, duration])?;
            }
            Ok([clicked as u8 as f64, duration, animated as u8 as f64])
        })();
        if result.is_err() {
            let _ = self.set_flag(IN_LIBERATION, false);
        }
        result
    }

    fn echo(&self, o: &[f64]) -> ActionResult<bool> {
        let (duration, timeout) = (o[0], o[2]); // sleep_time intentionally unused in the original.
        if timeout == 0. && self.skill_available(ECHO)? {
            self.send(ECHO, 0.)?;
            self.record(ECHO)?;
            return Ok(true);
        }
        if self.flag(OPEN_WORLD)? && self.state(ELEMENT)? == 2. {
            return Ok(false);
        }
        if self.truth(Op::HasCd, &[ECHO, -1.])? {
            return Ok(false);
        }
        let mut clicked = false;
        let start = self.now()?;
        let mut last_click = 0.;
        loop {
            let now = self.now()?;
            if now - start > timeout + duration {
                return Ok(false);
            }
            if !self.skill_available(ECHO)? && (duration == 0. || !clicked) {
                break;
            }
            if duration > 0. && start != 0. && now - start > duration {
                break;
            }
            if now - last_click > 0.1 {
                if !clicked {
                    self.record(ECHO)?;
                    clicked = true;
                }
                self.send(ECHO, 0.)?;
                last_click = now;
            }
            if now - start > 15. {
                self.action(Op::SkillFailed, &[])?;
                break;
            }
            self.action(Op::NextFrame, &[])?;
        }
        Ok(clicked)
    }

    fn wait_for_team(&self, timeout: f64, send_click: bool, present: bool) -> ActionResult<bool> {
        self.wait_with(
            || Ok(self.truth(Op::InTeam, &[])? == present),
            timeout,
            0.,
            || {
                if send_click {
                    self.click(false, 0.1)
                } else {
                    Ok(())
                }
            },
            || Ok(()),
        )
    }

    fn liberation(&self, o: &[f64]) -> ActionResult<bool> {
        let (con_less, send_click, wait_ready, minimum, click_f) =
            (o[0], o[1] != 0., o[2], o[3], o[4] != 0.);
        if !self.flag(USE_LIBERATION)? || (con_less > 0. && self.current_con()? > con_less) {
            return Ok(false);
        }
        let (mut start, mut last_click) = (self.now()?, 0.);
        let mut clicked = false;
        let result = (|| {
            if !self.flag(IN_LIBERATION)? {
                while self.skill_available(LIBERATION)? {
                    if clicked && !self.truth(Op::InTeam, &[])? {
                        break;
                    }
                    if send_click {
                        self.click(false, 0.1)?;
                    }
                    let now = self.now()?;
                    if now - last_click > 0.1 {
                        self.send(LIBERATION, 0.)?;
                        clicked = true;
                        last_click = now;
                    }
                    if self.now()? - start > 15. {
                        self.action(Op::SkillFailed, &[])?;
                        self.action(Op::Interrupted, &[0.])?;
                        return Err(HOST_ERROR);
                    }
                    self.action(Op::NextFrame, &[])?;
                }
                if clicked {
                    let value = self.wait_for_team(0.4, send_click, false)?;
                    self.set_flag(IN_LIBERATION, value)?;
                    if !value {
                        return Ok(false);
                    }
                } else {
                    start = self.now()?;
                    while !self.truth(Op::HasCd, &[LIBERATION, -1.])?
                        && self.now()? - start < wait_ready
                    {
                        self.send(LIBERATION, 0.05)?;
                        if self.wait_for_team(0.1, false, false)? {
                            self.set_flag(IN_LIBERATION, true)?;
                        }
                    }
                    if !self.flag(IN_LIBERATION)? {
                        return Ok(false);
                    }
                }
            }
            start = self.now()?;
            let mut last_f = 0.;
            while !self.truth(Op::InTeam, &[])? {
                self.set_flag(IN_LIBERATION, true)?;
                clicked = true;
                if send_click {
                    self.click(false, 0.1)?;
                }
                let now = self.now()?;
                if click_f && now - start >= minimum && now - last_f >= 0.1 {
                    self.action(Op::Tap, &[1., 0., -1., 0.01])?;
                    last_f = now;
                }
                if now - start > 7. {
                    self.action(Op::Interrupted, &[1.])?;
                    return Err(HOST_ERROR);
                }
                self.action(Op::NextFrame, &[])?;
            }
            let duration = self.now()? - start;
            let now = self.now()?;
            self.action(Op::FreezeAdd, &[now, start, duration])?;
            self.record(LIBERATION)?;
            self.set_flag(IN_LIBERATION, false)?;
            self.set(LIBERATION_READY, 0.)?;
            Ok(clicked)
        })();
        if result.is_err() {
            let _ = self.set_flag(IN_LIBERATION, false);
        }
        result
    }

    fn current_con(&self) -> ActionResult<f64> {
        if self.state(CON)? == 1. {
            return Ok(1.);
        }
        let con = self.value(Op::Concerto, &[])?;
        self.set(CON, con)?;
        Ok(con)
    }
    fn is_con_full(&self) -> ActionResult<bool> {
        Ok(self.state(CON)? == 1. || self.value(Op::Concerto, &[])? == 1.)
    }
    fn normal_attacks(
        &self,
        duration: f64,
        interval: f64,
        after: f64,
        resonance: bool,
        until_full: bool,
    ) -> ActionResult {
        let start = self.now()?;
        while self.now()? - start < duration {
            if resonance && self.skill_available(RESONANCE)? {
                self.virtual_action(&[VIRTUAL_RESONANCE, 0., 0., 1., 0., 0., 1.])?;
                return Ok(());
            }
            if until_full && self.is_con_full()? {
                return Ok(());
            }
            self.click(false, -1.)?;
            self.sleep(interval, true)?;
        }
        self.sleep(after, true)
    }
    fn heavy_attack(&self, duration: f64) -> ActionResult {
        self.action(Op::CheckCombat, &[])?;
        self.action(Op::Mouse, &[1.])?;
        let result = self.sleep(duration, true);
        if result.is_err() {
            let _ = self.action(Op::Mouse, &[0.]);
            return result;
        }
        // Set held=false before mouseUp, matching HeldMouse::release().
        self.action(Op::Mouse, &[0.])?;
        self.sleep(0.01, true)
    }
    fn heavy_forte(&self, condition: impl Fn() -> ActionResult<bool>) -> ActionResult<bool> {
        if !condition()? {
            return Ok(false);
        }
        self.action(Op::Mouse, &[1.])?;
        let result = (|| {
            let deadline = self.now()? + 2.;
            loop {
                self.action(Op::NextFrame, &[])?;
                if !condition()? {
                    return Ok(true);
                }
                if self.now()? > deadline {
                    return Ok(false);
                }
            }
        })();
        if result.is_err() {
            let _ = self.action(Op::Mouse, &[0.]);
            return result;
        }
        self.action(Op::Mouse, &[0.])?;
        self.sleep(0.05, true)?;
        result
    }
    fn party_field(&self, index: usize, field: f64) -> ActionResult<f64> {
        self.value(Op::PartyField, &[index as f64, field])
    }
    fn all_buffs(&self) -> ActionResult<bool> {
        if self.state(INTRO)? == 0. {
            return Ok(false);
        }
        let size = self.value(Op::LoadParty, &[])? as usize;
        let mut count = 0;
        for i in 0..size {
            if self.party_field(i, 0.)? != 0. && self.party_field(i, 1.)? == 0. {
                count += 1;
                if self.party_field(i, 5.)? <= 0. || self.party_field(i, 6.)? == 0. {
                    return Ok(false);
                }
            }
        }
        Ok(count == 2)
    }
    fn need_fast_perform(&self) -> ActionResult<bool> {
        let size = self.value(Op::LoadParty, &[])? as usize;
        for i in 0..size {
            if self.party_field(i, 0.)? != 0. && self.party_field(i, 7.)? != 0. {
                self.action(Op::SelectParty, &[i as f64])?;
                break;
            }
        }
        for i in 0..size {
            if self.party_field(i, 0.)? != 0.
                && self.party_field(i, 9.)? == 0.
                && self.value(Op::PartyPriority, &[i as f64])? >= 400.
            {
                return Ok(true);
            }
        }
        Ok(false)
    }
    fn switch_other(&self, allow_auto_combat: bool) -> ActionResult {
        let size = self.value(Op::LoadParty, &[])? as usize;
        if size == 0 || (self.flag(5.)? && !allow_auto_combat) {
            return Ok(());
        }
        let mut target = (self.state(INDEX)? as i64 + 1) % size as i64;
        for i in 0..size {
            if self.party_field(i, 0.)? != 0.
                && self.party_field(i, 1.)? == 0.
                && self.party_field(i, 2.)? == 2.
            {
                target = self.party_field(i, 3.)? as i64;
                break;
            }
        }
        let start = self.now()?;
        while self.now()? - start < 6. {
            let index = self.value(Op::PartyIndex, &[])?;
            if self.truth(Op::InTeam, &[])? && index != self.state(INDEX)? {
                for i in 0..size {
                    if self.party_field(i, 0.)? != 0. {
                        let current = self.party_field(i, 3.)? == index;
                        self.action(Op::PartySetCurrent, &[i as f64, current as u8 as f64])?;
                    }
                }
                break;
            }
            self.action(Op::TapSlot, &[target as f64])?;
            self.sleep(0.2, false)?;
        }
        Ok(())
    }
    fn find_character(&self) -> ActionResult<bool> {
        let size = self.value(Op::LoadParty, &[])? as usize;
        for i in 0..size {
            if self.party_field(i, 0.)? != 0. && self.party_field(i, 8.)? != 0. {
                self.action(Op::SelectParty, &[i as f64])?;
                return Ok(true);
            }
        }
        Ok(false)
    }
    fn outro_source(&self) -> ActionResult {
        if self.state(INTRO)? == 0. {
            return Ok(());
        }
        let size = self.value(Op::LoadParty, &[])? as usize;
        let mut latest = 0.;
        for i in 0..size {
            if self.party_field(i, 0.)? != 0.
                && self.party_field(i, 1.)? == 0.
                && self.party_field(i, 4.)? > latest
            {
                latest = self.party_field(i, 4.)?;
                self.action(Op::SelectParty, &[i as f64])?;
            }
        }
        Ok(())
    }

    pub fn run(&self, method: i32, args: &[f64]) -> ActionResult<[f64; 3]> {
        let method = Method::try_from(method)?;
        if args.len() != method.arg_count() {
            return Err(INVALID_ACTION);
        }
        let value = match method {
            Method::Wait => self.wait(args[0], args[1])? as u8 as f64,
            Method::DefaultPerform => {
                self.virtual_action(&[VIRTUAL_INTRO, 1.2, 1.])?;
                self.virtual_action(&[VIRTUAL_ECHO, 0., 0., 0.])?;
                self.virtual_action(&[VIRTUAL_LIBERATION, -1., 0., 0.1, 0., 1.])?;
                if self.virtual_action(&[VIRTUAL_RESONANCE, 0., 0., 1., 0., 0., 1.])?[0] == 0. {
                    self.virtual_action(&[VIRTUAL_HEAVY_FORTE, 0.])?;
                }
                self.virtual_action(&[VIRTUAL_SWITCH, 0., 0.])?;
                0.
            }
            Method::Sleep => {
                self.sleep(args[0], args[1] != 0.)?;
                0.
            }
            Method::Available => {
                if self.state(CURRENT)? != 0. {
                    self.value(Op::Available, &[args[0], -1., args[1], args[2]])?
                } else {
                    (!self.truth(Op::HasCd, &[args[0], -2.])?) as u8 as f64
                }
            }
            Method::Resonance => return self.resonance(args),
            Method::Liberation => self.liberation(args)? as u8 as f64,
            Method::CurrentCon => self.current_con()?,
            Method::Flying => {
                (self.flag(LEVITATOR)? && self.value(Op::LevitatorWhite, &[])? < 0.1) as u8 as f64
            }
            Method::Echo => self.echo(&[args[0], 0., args[1]])? as u8 as f64,
            Method::HeavyAttack => {
                self.heavy_attack(args[0])?;
                0.
            }
            Method::CycleSleep => {
                let remaining = args[0] - (self.now()? - self.state(CYCLE_START)?);
                self.action(Op::CheckBreak, &[])?;
                self.sleep(remaining, true)?;
                0.
            }
            Method::NormalAttack => {
                self.action(Op::CheckCombat, &[])?;
                self.click(false, -1.)?;
                0.
            }
            Method::NormalUntilSwitch => {
                self.click(false, -1.)?;
                while self.elapsed(self.state(LAST_PERFORM)?)? < 1.1 {
                    self.click(false, 0.1)?;
                }
                0.
            }
            Method::WaitDown => {
                let click = args[0] != 0.;
                if !self.flag(LEVITATOR)? && self.state(INTRO)? != 0. {
                    let motion = self.state(INTRO_MOTION)?;
                    if click {
                        self.virtual_action(&[VIRTUAL_NORMAL_ATTACKS, motion, 0.1, 0., 0., 0.])?;
                    } else {
                        self.sleep(motion, true)?;
                    }
                } else {
                    let start = self.now()?;
                    while self.virtual_action(&[VIRTUAL_FLYING])?[0] != 0.
                        && self.now()? - start < 2.5
                    {
                        if click {
                            self.click(false, 0.2)?;
                        } else {
                            self.sleep(0.2, true)?;
                        }
                        self.action(Op::NextFrame, &[])?;
                    }
                }
                0.
            }
            Method::WaitIntro => {
                if self.state(INTRO)? != 0. {
                    // Host virtual hook retains an override of down().
                    if args[0] < 0. || !args[0].is_finite() {
                        return Err(INVALID_WAIT);
                    }
                    let deadline = self.now()? + if args[0] == 0. { 10. } else { args[0] };
                    loop {
                        self.action(Op::NextFrame, &[])?;
                        if self.virtual_action(&[VIRTUAL_DOWN])?[0] != 0. {
                            break;
                        }
                        if args[1] != 0. {
                            self.click(false, 0.1)?;
                        }
                        if self.now()? > deadline {
                            break;
                        }
                    }
                }
                0.
            }
            Method::Down => {
                ((self.value(Op::Highlighted, &[RESONANCE])? > 0.
                    && !self.truth(Op::HasCd, &[RESONANCE, -1.])?)
                    || (self.value(Op::Highlighted, &[LIBERATION])? > 0.
                        && !self.truth(Op::HasCd, &[LIBERATION, -1.])?)) as u8
                    as f64
            }
            Method::HeavyForte => self.heavy_forte(|| self.truth(Op::Condition, &[]))? as u8 as f64,
            Method::SwitchNext => {
                self.virtual_action(&[VIRTUAL_FORTE])?;
                self.set(INTRO, 0.)?;
                self.set(SUB_INTRO, 0.)?;
                let ready = self.skill_available(LIBERATION)?;
                self.set(LIBERATION_READY, ready as u8 as f64)?;
                if self.state(TOOLBOX)? != 0. {
                    self.action(Op::Tap, &[2., 0., -1., 0.01])?;
                    self.set(TOOLBOX, 0.)?;
                }
                self.action(Op::Switch, &[args[0], args[1]])?;
                0.
            }
            Method::ResetState => {
                self.set(INTRO, 0.)?;
                self.set(SUB_INTRO, 0.)?;
                self.set(CON, 0.)?;
                self.set(LAST_FULL_CON_SWITCH, -1.)?;
                self.set(TOOLBOX, 0.)?;
                self.set(LIBERATION_READY, 0.)?;
                self.set(ECHO_READY, 0.)?;
                self.set(RESONANCE_READY, 0.)?;
                0.
            }
            Method::RepeatedKey => {
                let start = self.now()?;
                while self.now()? - start < args[0] {
                    self.action(Op::Tap, &[0., 0., args[1], 0.01])?;
                }
                0.
            }
            Method::Record => {
                self.record(args[0])?;
                0.
            }
            Method::NormalAttacks => {
                self.normal_attacks(args[0], args[1], args[2], args[3] != 0., args[4] != 0.)?;
                0.
            }
            Method::RightClicks => {
                let direction = args[2] != 0.;
                let result: ActionResult = (|| {
                    if direction {
                        self.action(Op::Key, &[1.])?;
                        self.action(Op::NextFrame, &[])?;
                    }
                    let start = self.now()?;
                    while self.now()? - start < args[0] {
                        self.click(true, args[1])?;
                    }
                    if direction {
                        self.action(Op::Key, &[0.])?;
                    }
                    Ok(())
                })();
                if result.is_err() && direction {
                    let _ = self.action(Op::Key, &[0.]);
                }
                result?;
                0.
            }
            Method::Perform => {
                self.set(LAST_PERFORM, self.now()?)?;
                self.virtual_action(&[10.])?;
                0.
            }
            Method::Send => {
                match args[0] as i32 {
                    0 => self.set(RESONANCE_READY, 0.)?,
                    1 => self.set(ECHO_READY, 0.)?,
                    2 => self.set(LIBERATION_READY, 0.)?,
                    _ => (),
                }
                self.action(Op::TapSkill, args)?;
                0.
            }
            Method::SwitchPriority => {
                if self.truth(Op::HealerLocked, &[])? {
                    0.
                } else {
                    200.
                }
            }
            Method::AllBuffs => self.all_buffs()? as u8 as f64,
            Method::NeedFastPerform => self.need_fast_perform()? as u8 as f64,
            Method::SwitchOther => {
                self.switch_other(args[0] != 0.)?;
                0.
            }
            Method::FindCharacter => self.find_character()? as u8 as f64,
            Method::OutroSource => {
                self.outro_source()?;
                0.
            }
            Method::LongAction => self.value(
                Op::LongFeature,
                &[if args[0] == 1. {
                    0.
                } else if args[0] == 2. {
                    1.
                } else {
                    2.
                }],
            )?,
            Method::ShoreKeeperDodge => {
                (self.find_character()? && self.truth(Op::DodgeSelected, &[])?) as u8 as f64
            }
            Method::HeavyForteKind => self.heavy_forte(|| {
                if args[0] == 2. {
                    Ok(self.virtual_action(&[VIRTUAL_FORTE])?[0] != 0.)
                } else {
                    self.truth(Op::Forte, &[args[0]])
                }
            })? as u8 as f64,
            Method::Initialize => (self.state(ROLE)? != 2.) as u8 as f64,
            Method::BasicPerform => {
                self.basic_perform(args[0] as i32)?;
                0.
            }
            Method::VerinaPriority => {
                if args[0] != 0. && args[1] != 0. && args[2] != 0. {
                    400.
                } else if self.truth(Op::HealerLocked, &[])? {
                    0.
                } else {
                    200.
                }
            }
            Method::LilyPerform => {
                self.lily_perform()?;
                0.
            }
            Method::LilySkipCheck => self.feature("lily_slumberland", 0.8, "")? as u8 as f64,
            Method::LilyLiberationReady => self.lily_liberation_ready()? as u8 as f64,
            Method::AdditionalPerform => {
                self.additional_perform(args[0] as i32)?;
                0.
            }
            Method::SuomingUpdateRole => {
                self.suoming_update_role()?;
                0.
            }
            Method::ExtendedPerform => {
                self.extended_perform(args[0] as i32)?;
                0.
            }
            Method::ExtendedPriority => self.extended_priority(args)?,
            Method::ExtendedSwitchNext => {
                self.extended_switch_next(args)?;
                0.
            }
            Method::KeeperDodge => self.keeper_dodge()? as u8 as f64,
            Method::KeeperSkipCheck => {
                (self.state(INTRO)? != 0. || self.extended_flying()?) as u8 as f64
            }
            Method::EncoreSwitchOut => {
                self.action(Op::BaseSwitchOut, args)?;
                self.extended_set(2, 0.)?;
                0.
            }
            Method::StatefulPerform => {
                self.stateful_perform(args[0] as i32)?;
                0.
            }
            Method::StatefulPriority => self.stateful_priority(args)?,
            Method::StatefulInitialize => {
                self.stateful_initialize(args[0] as i32)?;
                0.
            }
            Method::DeniaPhase => {
                self.denia_phase(args[0] as i32)?;
                0.
            }
            Method::DeniaReset => {
                self.denia_reset()?;
                0.
            }
            Method::StatefulLifecycle => self.stateful_lifecycle(args)?,
            Method::AdvancedPerform => {
                self.advanced_perform(args[0] as i32)?;
                0.
            }
            Method::AdvancedPriority => self.advanced_priority(args)?,
            Method::AdvancedLifecycle => self.advanced_lifecycle(args)?,
            Method::GaugePerform => {
                self.gauge_perform(args[0] as i32)?;
                0.
            }
            Method::GaugePriority => self.gauge_priority(args)?,
            Method::GaugeLifecycle => self.gauge_lifecycle(args)?,
            Method::GaugeSend => {
                self.gauge_send(args)?;
                0.
            }
            Method::FormsPerform => {
                self.forms_perform(args[0] as i32)?;
                0.
            }
            Method::FormsPriority => self.forms_priority(args)?,
            Method::FormsLifecycle => self.forms_lifecycle(args)?,
            Method::FormsResonance => return self.carlotta_resonance(args),
            Method::FormsLiberation => self.carlotta_liberation(args)? as u8 as f64,
            Method::SpectroPerform => {
                self.spectro_perform(args[0] as i32)?;
                0.
            }
            Method::SpectroPriority => self.spectro_priority(args)?,
            Method::SpectroLifecycle => self.spectro_lifecycle(args)?,
        };
        Ok([value, 0., 0.])
    }
}
