//! Shared native team rotation timing and lifecycle.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
fn minimum(a: f64, b: f64) -> f64 {
    if b < a {
        b
    } else {
        a
    }
}
fn maximum(a: f64, b: f64) -> f64 {
    if a < b {
        b
    } else {
        a
    }
}
impl Host {
    fn macro_number(&self, index: f64) -> Result<f64> {
        self.scalar(Op::MacroNumber, &[index])
    }
    fn macro_flag(&self, index: f64) -> Result<bool> {
        self.flag(Op::MacroFlag, &[index])
    }
    fn macro_set_flag(&self, index: f64, value: bool) -> Result {
        self.action(Op::SetMacroFlag, &[index, f64::from(value)])
    }
    fn macro_wait(&self, seconds: f64) -> Result {
        self.action(Op::MacroPreciseWait, &[maximum(0., seconds)])
    }
    fn macro_cleanup(&self, previous: bool) -> Result {
        self.macro_set_flag(1., false)?;
        self.macro_set_flag(4., previous)?;
        self.macro_set_flag(5., false)?;
        self.action(Op::MacroReleaseNested, &[])
    }
    fn macro_failure<T>(&self, reason: f64) -> Result<T> {
        self.action(Op::MacroFailure, &[reason])?;
        Err(HOST_ERROR)
    }
    pub(in crate::services) fn run_team_macro(
        &self,
        method: Method,
        a: &[f64],
    ) -> Result<[f64; 3]> {
        let mut result = [0.; 3];
        match method {
            Method::MacroWait => self.macro_wait(a[0])?,
            Method::MacroRecord => {
                if (0..=2).contains(&(a[0] as i32)) {
                    let now = self.scalar(Op::Now, &[])?;
                    self.action(Op::SetMacroSkillTime, &[a[0], now])?;
                }
            }
            Method::MacroHold => {
                self.action(Op::MacroMouseDown, &[])?;
                self.macro_set_flag(0., true)?;
            }
            Method::MacroRelease => {
                if self.macro_flag(0.)? {
                    self.macro_set_flag(0., false)?;
                    self.action(Op::MacroMouseUp, &[])?;
                }
            }
            Method::MacroStart => {
                self.action(Op::SetMacroStart, &[self.scalar(Op::Now, &[])?])?;
                self.action(Op::ClearMacroMarks, &[])?;
            }
            Method::MacroMark => self.action(
                Op::AppendMacroMark,
                &[self.scalar(Op::Now, &[])? - self.macro_number(13.)?],
            )?,
            Method::MacroNeed => {
                if a[0] == 0. {
                    return self.macro_failure(0.);
                }
            }
            Method::MacroSpam => {
                let interval = if a[1] < 0. {
                    self.macro_number(0.)?
                } else {
                    a[1]
                };
                if !a[0].is_finite() || a[0] < 0. || a[0] > 120. || interval < 0. || a[3] < 1. {
                    return self.macro_failure(1.);
                }
                let value = a[0] / maximum(interval, 0.01);
                // MSVC's dynamic floating-to-int conversion returns INT_MIN for
                // non-finite input; normal timing is bounded by the checks above.
                let cap = if value.is_finite() {
                    value as i32
                } else {
                    i32::MIN
                } + 2;
                let maximum = if a[2] < 0. { cap } else { cap.min(a[2] as i32) };
                let start = self.scalar(Op::Now, &[])?;
                let mut n = 0;
                while self.scalar(Op::Now, &[])? - start < a[0] && n < maximum {
                    self.action(Op::MacroClick, &[])?;
                    n += 1;
                    if self.flag(Op::MacroHasSample, &[])? && n % (a[3] as i32) == 0 {
                        self.action(Op::MacroSample, &[])?;
                    }
                    let remaining = a[0] - (self.scalar(Op::Now, &[])? - start);
                    if remaining <= 0. {
                        break;
                    }
                    self.macro_wait(minimum(interval, remaining))?;
                }
                result[0] = f64::from(n);
            }
            Method::MacroSpamKey => {
                let interval = self.scalar(Op::MacroSpamOption, &[0.])?;
                let click_interval = self.scalar(Op::MacroSpamOption, &[1.])?;
                let presses = self.scalar(Op::MacroSpamOption, &[2.])?;
                if !a[0].is_finite()
                    || a[0] < 0.
                    || a[0] > 120.
                    || interval < 0.
                    || click_interval <= 0.
                    || presses > 12000.
                {
                    return self.macro_failure(2.);
                }
                let start = self.scalar(Op::Now, &[])?;
                let interval = if presses > 0. {
                    maximum(interval, a[0] / presses)
                } else {
                    interval
                };
                let (mut n, mut next_click) = (0, 0.);
                loop {
                    self.action(Op::MacroSender, &[])?;
                    n += 1;
                    if self.flag(Op::MacroHasStop, &[])? && self.flag(Op::MacroStop, &[])? {
                        break;
                    }
                    let mut left = a[0] - (self.scalar(Op::Now, &[])? - start);
                    let presses = self.scalar(Op::MacroSpamOption, &[2.])?;
                    if presses >= 0. {
                        if f64::from(n) >= presses {
                            break;
                        }
                        left = interval;
                    } else if left <= 0. {
                        break;
                    }
                    if n > 12000 {
                        return self.macro_failure(3.);
                    }
                    let end = self.scalar(Op::Now, &[])? + minimum(interval, left);
                    while self.scalar(Op::Now, &[])? < end {
                        let now = self.scalar(Op::Now, &[])?;
                        if self.flag(Op::MacroSpamPoke, &[])? && now >= next_click {
                            self.action(Op::MacroClick, &[])?;
                            next_click = now + self.scalar(Op::MacroSpamOption, &[1.])?;
                        }
                        self.macro_wait(minimum(0.02, end - self.scalar(Op::Now, &[])?))?;
                    }
                }
                result[0] = f64::from(n);
            }
            Method::MacroGap => {
                if a[0] <= 0. {
                    return Ok(result);
                }
                if self.macro_flag(1.)? {
                    result[0] = self.scalar(Op::MacroSpamNested, &[a[0]])?;
                } else {
                    self.macro_wait(a[0])?;
                }
            }
            Method::MacroTeammate => {
                self.action(Op::LoadMacroCharacters, &[])?;
                let count = self.scalar(Op::MacroCharacterCount, &[])? as usize;
                for i in 0..count {
                    self.action(Op::LoadMacroCharacter, &[i as f64])?;
                    if self.flag(Op::MacroCharacterPresent, &[])?
                        && (self.flag(Op::MacroCharacterClassMatches, &[])?
                            || self.flag(Op::MacroCharacterNameMatches, &[])?)
                    {
                        self.action(Op::ReturnMacroCharacter, &[])?;
                        break;
                    }
                }
            }
            Method::MacroHandoff => {
                self.macro_set_flag(6., false)?;
                self.action(Op::MacroSwitchSourceOut, &[a[0]])?;
                self.action(Op::SetMacroTargetFlag, &[0., 1.])?;
                self.action(Op::SetMacroTargetFlag, &[1., a[0]])?;
                let intro = a[0] != 0. && self.scalar(Op::MacroSourceRole, &[])? == 1.;
                self.action(Op::SetMacroTargetFlag, &[2., f64::from(intro)])?;
                self.action(Op::SetMacroTargetSwitchIn, &[self.scalar(Op::Now, &[])?])?;
                if a[0] != 0. {
                    self.action(Op::MacroIntroFreeze, &[])?;
                    self.action(Op::SetMacroSourceOutro, &[self.scalar(Op::Now, &[])?])?;
                }
                self.action(Op::SetCurrentMacroTarget, &[])?;
            }
            Method::MacroQuickSwitch => {
                if !self.flag(Op::MacroTargetPresent, &[])? {
                    return Ok(result);
                }
                if self.flag(Op::MacroTargetCurrent, &[])? {
                    result[0] = 1.;
                    return Ok(result);
                }
                if !self.flag(Op::MacroCurrentPresent, &[])? {
                    self.action(Op::FindMacroCurrent, &[])?;
                }
                if !self.flag(Op::MacroCurrentPresent, &[])? {
                    return Ok(result);
                }
                self.action(Op::SaveMacroSwitchSource, &[])?;
                self.action(Op::SetMacroSwitchPress, &[self.scalar(Op::Now, &[])?])?;
                let mut i = 0;
                while f64::from(i) < self.scalar(Op::MacroSwitchOption, &[0.])? {
                    self.action(Op::MacroClick, &[])?;
                    if f64::from(i + 1) < self.scalar(Op::MacroSwitchOption, &[0.])? {
                        self.macro_wait(self.scalar(Op::MacroSwitchOption, &[1.])?)?;
                    }
                    i += 1;
                }
                self.action(Op::MacroSwitchKey, &[])?;
                let full = self.flag(Op::MacroAssumeConcerto, &[])?
                    || (self.flag(Op::MacroReadConcerto, &[])?
                        && self.flag(Op::MacroSourceConcerto, &[])?);
                let start = self.scalar(Op::Now, &[])?;
                let (mut last, mut next_click, mut untrusted) = (start, 0., 0);
                let expected = self.scalar(Op::MacroLivePartySize, &[])?;
                while self.scalar(Op::Now, &[])? - start
                    < self.scalar(Op::MacroSwitchOption, &[2.])?
                {
                    let now = self.scalar(Op::Now, &[])?;
                    if now - last >= self.macro_number(1.)? {
                        self.action(Op::MacroSwitchKey, &[])?;
                        last = now;
                    }
                    if self.flag(Op::MacroSwitchPoke, &[])? && now >= next_click {
                        self.action(Op::MacroClick, &[])?;
                        next_click = now + self.scalar(Op::MacroSwitchOption, &[1.])?;
                    }
                    self.action(Op::MacroNextFrame, &[])?;
                    let visible = self.flag(Op::MacroInTeam, &[])?;
                    let hit = visible && self.flag(Op::MacroPartyTarget, &[])?;
                    let trusted = visible && self.scalar(Op::MacroPartyCount, &[])? >= expected;
                    untrusted = if hit && !trusted { untrusted + 1 } else { 0 };
                    if hit && (trusted || f64::from(untrusted) >= self.macro_number(3.)?) {
                        self.action(Op::MacroHandoffNested, &[f64::from(full)])?;
                        if self.flag(Op::MacroSwitchClick, &[])? {
                            self.action(Op::MacroClick, &[])?;
                        }
                        self.action(
                            Op::MacroGapNested,
                            &[self.scalar(Op::MacroSwitchOption, &[3.])?],
                        )?;
                        result[0] = 1.;
                        return Ok(result);
                    }
                }
            }
            Method::MacroOnField => {
                let end = self.scalar(Op::Now, &[])? + a[0];
                self.action(Op::LoadMacroCharacters, &[])?;
                loop {
                    self.action(Op::MacroNextFrame, &[])?;
                    if self.flag(Op::MacroInTeam, &[])? {
                        // QVector::size() uses signed qsizetype on this host.
                        let count = self.scalar(Op::MacroCharacterCount, &[])? as usize;
                        let visible = self.scalar(Op::MacroPartyCount, &[])? as i32 as i64;
                        if visible >= count as i64 {
                            for i in 0..count {
                                self.action(Op::LoadMacroCharacter, &[i as f64])?;
                                if self.flag(Op::MacroCharacterPresent, &[])?
                                    && self.flag(Op::MacroCharacterOnField, &[])?
                                {
                                    self.action(Op::ReturnMacroCharacter, &[])?;
                                    return Ok(result);
                                }
                            }
                        }
                    }
                    if self.scalar(Op::Now, &[])? >= end {
                        break;
                    }
                    self.macro_wait(0.03)?;
                }
            }
            Method::MacroWaitHud => {
                let timeout = if a[1] < 0. {
                    self.macro_number(4.)?
                } else {
                    a[1]
                };
                let start = self.scalar(Op::Now, &[])?;
                let (mut gone, mut back, mut next_click) = (false, false, 0.);
                while self.scalar(Op::Now, &[])? - start < timeout {
                    let now = self.scalar(Op::Now, &[])?;
                    if a[0] != 0. && now >= next_click {
                        self.action(Op::MacroClick, &[])?;
                        next_click = now + self.macro_number(0.)?;
                    }
                    self.action(Op::MacroNextFrame, &[])?;
                    if !self.flag(Op::MacroInTeam, &[])? {
                        gone = true;
                    } else if gone || self.scalar(Op::Now, &[])? - start >= self.macro_number(5.)? {
                        back = true;
                        break;
                    }
                    self.macro_wait(0.01)?;
                }
                self.action(Op::MacroHudFreeze, &[start])?;
                if a[2] > 0. {
                    if a[0] != 0. {
                        self.action(Op::MacroSpamNested, &[a[2]])?;
                    } else {
                        self.macro_wait(a[2])?;
                    }
                }
                result[0] = f64::from(back);
            }
            Method::MacroWaitHudUp => {
                let start = self.scalar(Op::Now, &[])?;
                while self.scalar(Op::Now, &[])? - start < a[0] {
                    self.action(Op::MacroNextFrame, &[])?;
                    if self.flag(Op::MacroInTeam, &[])? {
                        result[0] = 1.;
                        return Ok(result);
                    }
                    self.macro_wait(0.02)?;
                }
            }
            Method::MacroConcertoAtLeast => {
                let cap = if a[1] < 0. {
                    self.macro_number(6.)?
                } else {
                    a[1]
                };
                let interval = if a[2] < 0. {
                    self.macro_number(7.)?
                } else {
                    a[2]
                };
                let start = self.scalar(Op::Now, &[])?;
                let mut next_click = 0.;
                while self.scalar(Op::MacroActorElapsed, &[start])? < cap
                    && self.scalar(Op::Now, &[])? - start < cap + 30.
                {
                    self.action(Op::MacroRefreshObservation, &[])?;
                    if self.scalar(Op::MacroActorConcerto, &[])? >= a[0] {
                        result[0] = 1.;
                        return Ok(result);
                    }
                    let now = self.scalar(Op::Now, &[])?;
                    if a[3] != 0. && now >= next_click {
                        self.action(Op::MacroClick, &[])?;
                        next_click = now + self.macro_number(0.)?;
                    }
                    self.macro_wait(interval)?;
                }
            }
            Method::MacroPressUntilCd => {
                let start = self.scalar(Op::Now, &[])?;
                let (mut seen, mut live, mut next_click) = (false, 0, 0.);
                while self.scalar(Op::Now, &[])? - start < a[0] {
                    self.action(Op::MacroNextFrame, &[])?;
                    let hud = self.flag(Op::MacroInTeam, &[])?;
                    if hud {
                        if self.flag(Op::MacroAvailable, &[])? {
                            seen = true;
                        } else if seen && live > 0 {
                            result[0] = 1.;
                            return Ok(result);
                        } else if !seen && self.scalar(Op::Now, &[])? - start >= a[1] {
                            return Ok(result);
                        }
                    }
                    self.action(Op::MacroSender, &[])?;
                    if hud {
                        live += 1;
                    }
                    let end = self.scalar(Op::Now, &[])? + self.macro_number(8.)?;
                    while self.scalar(Op::Now, &[])? < end {
                        let now = self.scalar(Op::Now, &[])?;
                        if a[2] != 0. && now >= next_click {
                            self.action(Op::MacroClick, &[])?;
                            next_click = now + self.macro_number(0.)?;
                        }
                        self.macro_wait(minimum(0.02, end - now))?;
                    }
                }
            }
            Method::MacroPressResonance => {
                let grace = if a[1] < 0. {
                    self.macro_number(9.)?
                } else {
                    a[1]
                };
                result[0] = self.scalar(Op::MacroPressSkillNested, &[0., a[0], grace, a[2]])?;
            }
            Method::MacroPressEcho => {
                result[0] = self.scalar(Op::MacroPressSkillNested, &[1., a[0], a[1], 0.])?
            }
            Method::MacroEchoNow => {
                self.action(Op::MacroEchoSpamNested, a)?;
                self.action(Op::MacroRecordEcho, &[])?;
            }
            Method::MacroOpenerPending => {
                if !self.macro_flag(2.)? {
                    return Ok(result);
                }
                if self.macro_number(11.)? > self.macro_number(10.)? {
                    result[0] = f64::from(
                        self.scalar(Op::Now, &[])? - self.macro_number(11.)?
                            >= self.macro_number(12.)?,
                    );
                    return Ok(result);
                }
                if self.macro_number(10.)? >= 0.
                    && self.scalar(Op::Now, &[])? - self.macro_number(10.)? < a[0]
                {
                    return Ok(result);
                }
                result[0] = f64::from(self.macro_flag(3.)?);
            }
            Method::MacroExecute => {
                let previous = self.macro_flag(4.)?;
                self.macro_set_flag(4., true)?;
                self.macro_set_flag(5., true)?;
                self.action(Op::SetMacroEntryCurrent, &[])?;
                self.macro_set_flag(1., false)?;
                self.action(Op::LoadMacroCharacters, &[])?;
                let count = self.scalar(Op::MacroCharacterCount, &[])? as usize;
                for i in 0..count {
                    self.action(Op::LoadMacroCharacter, &[i as f64])?;
                    if self.flag(Op::MacroCharacterPresent, &[])? {
                        self.action(Op::DisableMacroSwitchF, &[])?;
                    }
                }
                self.action(Op::MacroStartNested, &[])?;
                let mut body = self.action(Op::MacroBody, &[]);
                if body.is_err() && self.flag(Op::MacroErrorBroken, &[])? {
                    self.action(Op::ClearException, &[])?;
                    body = self.action(Op::ReportMacroBroken, &[]);
                }
                if body.is_err() {
                    self.action(Op::ClearException, &[])?;
                    self.macro_cleanup(previous)?;
                    self.action(Op::RethrowHandled, &[])?;
                    return Err(HOST_ERROR);
                }
                self.macro_cleanup(previous)?;
                self.action(Op::ReportMacroMarks, &[])?;
                result[0] = 1.;
            }
            Method::MacroReset => self.macro_set_flag(2., true)?,
            Method::MacroCombatEnd => {
                self.action(Op::MacroReleaseNested, &[])?;
                self.macro_set_flag(1., false)?;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok(result)
    }
}
