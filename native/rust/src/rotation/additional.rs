use super::*;

impl Host {
    fn additional_resonance(&self, click: bool, timeout: f64) -> ActionResult<bool> {
        Ok(self.virtual_action(&[
            VIRTUAL_RESONANCE,
            0.,
            0.,
            click as u8 as f64,
            0.,
            timeout,
            1.,
        ])?[0]
            != 0.)
    }
    fn additional_liberation(&self, click: bool, wait: f64) -> ActionResult<bool> {
        Ok(
            self.virtual_action(&[VIRTUAL_LIBERATION, -1., click as u8 as f64, wait, 0., 1.])?[0]
                != 0.,
        )
    }
    fn additional_normal(&self, duration: f64) -> ActionResult {
        self.normal_attacks(duration, 0.1, 0., false, false)
    }
    fn additional_echo(&self, timeout: f64) -> ActionResult {
        self.virtual_action(&[VIRTUAL_ECHO, 0., 0., timeout])?;
        Ok(())
    }
    fn additional_switch(&self, intro: bool) -> ActionResult {
        self.virtual_action(&[VIRTUAL_SWITCH, intro as u8 as f64, 0.])?;
        Ok(())
    }
    fn additional_wait_down(&self) -> ActionResult {
        self.run(Method::WaitDown as i32, &[1.])?;
        Ok(())
    }
    fn mouse_forte(&self) -> ActionResult<bool> {
        self.truth(Op::Forte, &[0.])
    }
    fn additional_heavy_forte(&self) -> ActionResult<bool> {
        Ok(self.run(Method::HeavyForteKind as i32, &[0.])?[0] != 0.)
    }
    pub(super) fn suoming_update_role(&self) -> ActionResult {
        let dps = self.truth(Op::SuomingDps, &[])?;
        self.set(ROLE, if dps { 0. } else { 1. })?;
        self.set(18., if dps { 14. } else { 8. })
    }
    fn suoming_wait(&self, forte: bool, timeout: f64, poke: bool) -> ActionResult {
        let deadline = self.now()? + timeout;
        loop {
            self.action(Op::CheckCombat, &[])?;
            self.action(Op::NextFrame, &[])?;
            let ready = if forte {
                self.truth(Op::SuomingForte, &[])?
            } else {
                self.skill_available(RESONANCE)?
            };
            if ready {
                return Ok(());
            }
            if poke {
                self.click(false, 0.1)?;
            }
            if self.now()? > deadline {
                return Ok(());
            }
        }
    }
    fn suoming_hold_forte(&self) -> ActionResult {
        self.action(Op::CaptureKey, &[RESONANCE])?;
        let start = self.now()?;
        let mut held = false;
        let result = (|| {
            self.action(Op::CheckCombat, &[])?;
            while self.elapsed(start)? < 6. && self.truth(Op::SuomingForte, &[])? {
                if held {
                    held = false;
                    self.action(Op::Key, &[0.])?;
                    self.sleep(0.1, true)?;
                }
                self.action(Op::Key, &[1.])?;
                held = true;
                self.sleep(2., true)?;
            }
            self.sleep(3., true)?;
            self.suoming_wait(false, 8., false)?;
            if held {
                held = false;
                self.action(Op::Key, &[0.])?;
            }
            self.additional_echo(1.)?;
            self.sleep(0.01, true)
        })();
        if held {
            let _ = self.action(Op::Key, &[0.]);
        }
        result
    }
    pub(super) fn additional_perform(&self, kind: i32) -> ActionResult {
        match kind {
            0 => {
                if self.state(INTRO)? != 0. {
                    self.additional_normal(0.4)?;
                }
                self.additional_wait_down()?;
                let start = self.now()?;
                let mut skill_used = self.additional_resonance(true, 1.)?;
                self.additional_echo(0.)?;
                let mut liberated = self.additional_liberation(true, 0.)?;
                while self.elapsed(start)? < 4.5 {
                    if self.is_con_full()? {
                        break;
                    }
                    if !skill_used && self.skill_available(RESONANCE)? {
                        skill_used = self.additional_resonance(true, 1.)?;
                        continue;
                    }
                    if !liberated && self.skill_available(LIBERATION)? {
                        liberated = self.additional_liberation(true, 0.)?;
                        continue;
                    }
                    self.click(false, 0.1)?;
                    self.sleep(0.05, true)?;
                }
                self.additional_switch(false)
            }
            1 => {
                if self.state(INTRO)? != 0. {
                    self.additional_normal(0.8)?;
                }
                self.additional_wait_down()?;
                self.additional_echo(0.)?;
                if self.skill_available(LIBERATION)? && self.additional_liberation(true, 0.1)? {
                    self.additional_normal(1.)?;
                }
                let start = self.now()?;
                while self.elapsed(start)? < 8. {
                    if self.is_con_full()? && self.elapsed(start)? > 2. {
                        break;
                    }
                    if self.mouse_forte()? {
                        if self.additional_heavy_forte()? {
                            self.additional_normal(0.4)?;
                        }
                        continue;
                    }
                    if self.skill_available(RESONANCE)? && self.additional_resonance(true, 1.)? {
                        self.additional_normal(0.35)?;
                        continue;
                    }
                    if self.skill_available(LIBERATION)? {
                        self.additional_liberation(true, 0.)?;
                        continue;
                    }
                    self.click(false, 0.1)?;
                    self.sleep(0.05, true)?;
                }
                self.additional_switch(false)
            }
            2 => {
                if self.state(INTRO)? != 0. {
                    self.additional_normal(0.5)?;
                }
                self.additional_wait_down()?;
                let start = self.now()?;
                self.additional_echo(0.)?;
                self.additional_liberation(true, 0.)?;
                while self.elapsed(start)? < 5. {
                    if self.is_con_full()? && self.elapsed(start)? > 1.5 {
                        break;
                    }
                    if self.skill_available(RESONANCE)? && self.additional_resonance(true, 1.)? {
                        self.additional_normal(0.4)?;
                        continue;
                    }
                    if self.mouse_forte()? {
                        self.additional_heavy_forte()?;
                        continue;
                    }
                    if self.skill_available(LIBERATION)? {
                        self.additional_liberation(true, 0.)?;
                        continue;
                    }
                    self.click(false, 0.1)?;
                    self.sleep(0.05, true)?;
                }
                self.additional_switch(false)
            }
            3 => {
                if self.state(INTRO)? != 0. {
                    self.additional_normal(0.6)?;
                }
                self.additional_wait_down()?;
                self.additional_resonance(true, 1.)?;
                if self.mouse_forte()? {
                    self.additional_heavy_forte()?;
                }
                self.additional_echo(0.)?;
                if self.additional_liberation(true, 0.)? {
                    self.additional_normal(0.5)?;
                } else {
                    self.normal_attacks(0.8, 0.1, 0., true, false)?;
                }
                self.additional_switch(false)
            }
            4 => {
                self.suoming_update_role()?;
                if self.state(INTRO)? != 0. {
                    self.additional_normal(1.6)?;
                }
                let liberated = self.additional_liberation(false, 1.)?;
                let dps = self.truth(Op::SuomingDps, &[])?;
                if liberated && !dps {
                    self.additional_echo(0.)?;
                    return self.additional_switch(true);
                }
                if liberated || self.state(INTRO)? != 0. {
                    if !liberated {
                        self.additional_echo(0.)?;
                    }
                    if dps {
                        self.additional_resonance(false, 1.)?;
                    }
                    self.suoming_wait(true, 15., true)?;
                    self.suoming_hold_forte()?;
                } else {
                    self.additional_normal(1.1)?;
                }
                self.additional_switch(false)
            }
            _ => Err(INVALID_ACTION),
        }
    }
}
