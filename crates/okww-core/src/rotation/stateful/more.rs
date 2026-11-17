use super::*;

impl Host {
    fn wait_observation(
        &self,
        mut condition: impl FnMut() -> ActionResult<bool>,
        timeout: f64,
    ) -> ActionResult<bool> {
        if timeout < 0. || !timeout.is_finite() {
            return Err(INVALID_WAIT);
        }
        let deadline = self.now()? + if timeout == 0. { 10. } else { timeout };
        loop {
            self.action(Op::NextFrame, &[])?;
            if condition()? {
                return Ok(true);
            }
            if self.now()? > deadline {
                return Ok(false);
            }
        }
    }
    fn hsin_lib(&self) -> ActionResult<bool> {
        let second = self.feature("hsin_lib2", 0.7, "")?;
        if !(second || self.feature("hsin_lib1", 0.7, "")?) || !self.skill_available(LIBERATION)? {
            return Ok(false);
        }
        let cast = self.stateful_lib(0.)?;
        if cast && second {
            self.stateful_set(0, 1.)?;
        }
        Ok(cast)
    }
    fn hsin_heavy_ready(&self) -> ActionResult<bool> {
        Ok(self.feature("hsin_h1", 0.7, "")? || self.feature("hsin_h2", 0.7, "")?)
    }
    fn hsin_heavy(&self, start: f64, duration: f64) -> ActionResult<bool> {
        if !self.hsin_heavy_ready()? {
            return Ok(false);
        }
        let remaining = duration - self.elapsed(start)?;
        if remaining <= 0. {
            return Ok(false);
        }
        self.action(Op::CheckCombat, &[])?;
        self.action(Op::Mouse, &[1.])?;
        let result =
            self.wait_observation(|| Ok(!self.hsin_heavy_ready()?), 1.2_f64.min(remaining));
        if result.is_err() {
            let _ = self.action(Op::Mouse, &[0.]);
        }
        result?;
        self.action(Op::Mouse, &[0.])?;
        self.sleep(0.01, true)?;
        Ok(true)
    }
    fn suisui_third(&self) -> ActionResult<bool> {
        self.feature("suisui_forte3", 0.75, "")
    }
    fn suisui_second(&self) -> ActionResult<bool> {
        self.feature("suisui_forte2", 0.75, "")
    }
    fn suisui_try_e(&self) -> ActionResult<bool> {
        if !self.feature("suisui_e1", 0.8, "")? {
            return Ok(false);
        }
        let start = self.now()?;
        while self.feature("suisui_e1", 0.8, "")? && self.elapsed(start)? < 2. {
            self.send(RESONANCE, 0.)?;
            self.sleep(0.05, true)?;
        }
        Ok(true)
    }
    fn suisui_rotation(&self) -> ActionResult {
        let start = self.now()?;
        if self.stateful_local(1)? != 0. {
            if self.state(INTRO)? != 0. {
                self.heavy_attack(1.5)?;
            }
            self.stateful_resonance(true, 0.)?;
            self.stateful_set(1, 0.)?;
        }
        let mut ready = false;
        while self.elapsed(start)? < 26. {
            if self.suisui_third()? {
                ready = true;
                break;
            }
            if self.suisui_second()? || self.suisui_third()? {
                for _ in 0..7 {
                    if self.suisui_third()? {
                        break;
                    }
                    self.click(false, -1.)?;
                    self.sleep(0.1, true)?;
                }
                ready = true;
                break;
            }
            self.stateful_cycle()?;
            if self.state(INTRO)? == 0. && self.suisui_try_e()? {
                return self.stateful_switch();
            }
            self.click(false, -1.)?;
            self.stateful_cycle_end()?;
        }
        if !ready {
            return Ok(());
        }
        let mut liberated = false;
        let fill = self.now()?;
        while self.elapsed(fill)? < 12. {
            if !liberated {
                liberated = self.stateful_lib(0.)?;
                if liberated {
                    self.action(Op::NextFrame, &[])?;
                    continue;
                }
            }
            if self.is_con_full()? && self.suisui_third()? {
                return Ok(());
            }
            self.click(false, -1.)?;
            self.sleep(0.1, true)?;
        }
        Ok(())
    }
    fn rebecca_reenter(&self) -> ActionResult<bool> {
        Ok(self.now()? - self.stateful_local(0)? < 17.)
    }
    fn rebecca_hmg(&self) -> ActionResult {
        let entry = self.now()?;
        while self.now()? - entry < 0.8 {
            self.send(LIBERATION, 0.)?;
            self.sleep(0.1, false)?;
        }
        self.record(LIBERATION)?;
        let start = self.now()?;
        let mut last = start;
        while self.now()? - start < 5.2 {
            self.click(false, 0.08)?;
            let now = self.now()?;
            if now - last > 0.9 {
                self.send(LIBERATION, 0.)?;
                last = now;
            }
            self.sleep(0.01, false)?;
        }
        self.stateful_set(0, self.now()?)
    }
    fn rebecca_sequence(&self) -> ActionResult {
        self.stateful_normal(if self.state(INTRO)? != 0. { 1.3 } else { 1.8 })?;
        self.wait_observation(|| self.skill_available(RESONANCE), 2.)?;
        self.virtual_action(&[VIRTUAL_RESONANCE, 1.5, 0., 1., 0., 0., 1.])?;
        self.virtual_action(&[VIRTUAL_RESONANCE, 1.5, 0., 1., 0., 0., 1.])?;
        if self.state(INTRO)? == 0. {
            self.virtual_action(&[VIRTUAL_RESONANCE, 1., 0., 1., 0., 0., 1.])?;
        }
        self.send(RESONANCE, 0.)?;
        let start = self.now()?;
        while !self.truth(Op::Forte, &[0.])? && self.now()? - start < 4. {
            self.click(false, -1.)?;
            self.sleep(0.1, true)?;
            self.action(Op::NextFrame, &[])?;
        }
        if self.truth(Op::Forte, &[0.])? {
            self.heavy_attack(1.5)?;
        }
        self.stateful_echo(1.)?;
        self.rebecca_hmg()
    }
    pub(super) fn more_perform(&self, kind: i32) -> ActionResult {
        match kind {
            4 => {
                self.stateful_set(0, 0.)?;
                let start = self.now()?;
                let duration = if self.state(INTRO)? != 0. { 12. } else { 6. };
                while self.elapsed(start)? < duration {
                    self.stateful_cycle()?;
                    if self.skill_available(RESONANCE)?
                        && self.virtual_action(&[VIRTUAL_RESONANCE, 0., 0., 1., 0.5, 0., 1.])?[0]
                            != 0.
                    {
                    } else if self.hsin_lib()? {
                        if self.stateful_local(0)? != 0. {
                            break;
                        }
                    } else if !self.hsin_heavy(start, duration)?
                        && self.virtual_action(&[VIRTUAL_ECHO, 0., 0., 0.])?[0] == 0.
                    {
                        self.click(false, -1.)?;
                    }
                    self.stateful_cycle_end()?;
                }
                self.stateful_switch()
            }
            5 => {
                if self.stateful_local(1)? == 0. {
                    self.stateful_set(1, self.state(INTRO)?)?;
                }
                self.suisui_rotation()?;
                self.stateful_switch()
            }
            6 => {
                if self.rebecca_reenter()? {
                    self.stateful_resonance(true, 0.)?;
                    self.stateful_normal(0.5)?;
                } else {
                    self.rebecca_sequence()?;
                }
                if self.rebecca_reenter()? {
                    let start = self.now()?;
                    while !self.is_con_full()? && self.now()? - start < 2.2 {
                        self.stateful_normal(1.)?;
                        self.sleep(0.1, true)?;
                    }
                }
                self.stateful_switch()
            }
            _ => Err(INVALID_ACTION),
        }
    }
    pub(super) fn suisui_priority(&self, args: &[f64]) -> ActionResult<f64> {
        let since = self.elapsed(self.stateful_local(0)?)?;
        if since > 40. {
            return Ok(400.);
        }
        if since < 16. {
            return Ok(0.);
        }
        let mut main = false;
        let size = self.value(Op::LoadParty, &[])? as usize;
        for i in 0..size {
            if self.party_field(i, 0.)? != 0.
                && self.party_field(i, 1.)? == 0.
                && self.party_field(i, 2.)? == 0.
            {
                main = true;
            }
        }
        if main && args[2] != 0. && args[3] != 0. && args[1] != 0. {
            return Ok(400.);
        }
        Ok(if self.truth(Op::HealerLocked, &[])? {
            0.
        } else {
            200.
        })
    }
    pub(super) fn more_lifecycle(&self, args: &[f64]) -> ActionResult {
        if args[0] != 5. {
            return Err(INVALID_ACTION);
        }
        match args[1] as i32 {
            0 => {
                self.run(Method::ResetState as i32, &[])?;
                self.stateful_set(0, -1.)?;
                self.stateful_set(1, 0.)
            }
            1 => {
                self.action(Op::BaseSwitchOut, &[args[2]])?;
                if args[2] != 0. {
                    self.stateful_set(0, self.now()?)?;
                }
                Ok(())
            }
            _ => Err(INVALID_ACTION),
        }
    }
}
