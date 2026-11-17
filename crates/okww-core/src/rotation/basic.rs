use super::*;

impl Host {
    fn basic_normal(&self, duration: f64) -> ActionResult {
        self.normal_attacks(duration, 0.1, 0., false, false)
    }
    fn basic_resonance(&self, send_click: bool) -> ActionResult<bool> {
        Ok(self.virtual_action(&[
            VIRTUAL_RESONANCE,
            0.,
            0.,
            send_click as u8 as f64,
            0.,
            0.,
            1.,
        ])?[0]
            != 0.)
    }
    fn basic_liberation(&self, con_less: f64, wait: f64) -> ActionResult<bool> {
        Ok(self.virtual_action(&[VIRTUAL_LIBERATION, con_less, 0., wait, 0., 1.])?[0] != 0.)
    }
    fn basic_echo(&self, timeout: f64, sleep: f64) -> ActionResult {
        self.virtual_action(&[VIRTUAL_ECHO, 0., sleep, timeout])?;
        Ok(())
    }
    fn basic_switch(&self) -> ActionResult {
        self.virtual_action(&[VIRTUAL_SWITCH, 0., 0.])?;
        Ok(())
    }
    fn basic_wait_down(&self, click: bool) -> ActionResult {
        self.run(Method::WaitDown as i32, &[click as u8 as f64])?;
        Ok(())
    }
    pub(super) fn basic_perform(&self, kind: i32) -> ActionResult {
        match kind {
            0 => {
                let start = self.now()?;
                self.basic_normal(0.6)?;
                let stop = || -> ActionResult<bool> {
                    Ok(self.is_con_full()? || self.elapsed(start)? >= 6.5)
                };
                if !stop()? {
                    if self.skill_available(LIBERATION)? {
                        self.basic_liberation(-1., 0.1)?;
                    }
                    if !stop()? {
                        if self.skill_available(RESONANCE)? {
                            self.basic_resonance(true)?;
                        }
                        if !stop()? {
                            if self.skill_available(ECHO)? {
                                self.basic_echo(0., 0.)?;
                            }
                            if !stop()? {
                                self.wait_for_team(2., false, true)?;
                                self.sleep(0.8, true)?;
                                if self.truth(Op::Forte, &[0.])?
                                    && self.elapsed(self.value(Op::Local, &[])?)? >= 8.
                                {
                                    self.heavy_attack(0.7)?;
                                    self.action(Op::SetLocal, &[self.now()?])?;
                                }
                                self.action(Op::Tap, &[3., 0.01, -1., 0.01])?;
                                self.basic_normal(0.5)?;
                            }
                        }
                    }
                }
                self.basic_switch()
            }
            1 => {
                self.basic_wait_down(true)?;
                let liberated = self.basic_liberation(-1., 0.1)?;
                self.basic_resonance(true)?;
                self.basic_echo(1., 0.)?;
                if !liberated {
                    self.basic_liberation(-1., 1.)?;
                }
                self.basic_switch()
            }
            2 => {
                let mut liberated = false;
                let mut sleep_time = 0.85;
                self.sleep(0.02, true)?;
                let mut start = self.now()?;
                self.action(Op::Mouse, &[1.])?;
                let held = (|| {
                    self.basic_wait_down(false)?;
                    if self.basic_liberation(-1., 0.1)? {
                        liberated = true;
                        sleep_time += 0.1;
                        self.sleep(0.15, false)?;
                    } else if self.skill_available(RESONANCE)? {
                        self.action(Op::Mouse, &[0.])?;
                        self.basic_resonance(false)?;
                        start = self.now()?;
                        self.action(Op::Mouse, &[1.])?;
                        self.sleep(0.1, false)?;
                    }
                    sleep_time -= self.elapsed(start)?;
                    if sleep_time > 0. {
                        self.sleep(sleep_time, false)?;
                    }
                    self.action(Op::Mouse, &[0.])
                })();
                if held.is_err() {
                    let _ = self.action(Op::Mouse, &[0.]);
                }
                held?;
                self.sleep(0.8, true)?;
                if liberated {
                    self.basic_resonance(false)?;
                    self.sleep(0.3, true)?;
                }
                if self.is_con_full()? {
                    self.basic_echo(1., 0.)?;
                }
                self.basic_switch()
            }
            3 => {
                if self.state(INTRO)? != 0. {
                    self.normal_attacks(1.2, 0.1, 0., true, false)?;
                }
                self.basic_liberation(1., 0.1)?;
                self.basic_resonance(true)?;
                self.basic_echo(1., 0.)?;
                self.basic_switch()
            }
            4 => {
                if self.basic_liberation(1., 0.1)? {
                    self.basic_resonance(true)?;
                    return self.basic_switch();
                }
                if self.state(INTRO)? != 0. {
                    self.basic_normal(1.2)?;
                    return self.basic_switch();
                }
                self.basic_resonance(true)?;
                self.basic_echo(1., 0.)?;
                self.basic_switch()
            }
            5 => {
                if self.state(INTRO)? != 0. {
                    self.basic_wait_down(true)?;
                    self.basic_normal(2.5)?;
                } else {
                    self.basic_liberation(-1., 0.1)?;
                    self.basic_resonance(true)?;
                    self.basic_echo(1., 0.1)?;
                }
                self.basic_switch()
            }
            6 => {
                if self.state(INTRO)? != 0. {
                    self.sleep(1., true)?;
                    self.wait_for_team(3., false, true)?;
                    self.action(Op::CheckCombat, &[])?;
                }
                self.run(Method::DefaultPerform as i32, &[])?;
                Ok(())
            }
            7 => {
                if self.state(INTRO)? != 0. {
                    self.basic_normal(1.)?;
                }
                self.basic_liberation(-1., 0.1)?;
                if self.skill_available(RESONANCE)? {
                    self.basic_resonance(true)?;
                }
                if self.skill_available(ECHO)? {
                    self.basic_echo(1., 0.)?;
                }
                self.basic_switch()
            }
            8 => {
                if self.state(INTRO)? != 0. {
                    self.sleep(0.4, true)?;
                }
                let liberated = self.basic_liberation(-1., 0.1)?;
                if self.truth(Op::Forte, &[0.])? {
                    if self.state(INTRO)? == 0. && !liberated {
                        self.run(Method::NormalAttack as i32, &[])?;
                    }
                    self.heavy_attack(0.6)?;
                    self.sleep(0.4, true)?;
                } else if self.basic_resonance(false)? {
                    self.sleep(0.1, true)?;
                } else if self.skill_available(ECHO)? {
                    self.basic_echo(1., 0.)?;
                } else {
                    self.heavy_attack(0.6)?;
                }
                self.basic_switch()
            }
            9 => {
                if self.basic_liberation(-1., 0.1)? {
                    self.sleep(1.2, true)?;
                    self.basic_echo(2., 0.)?;
                    return self.basic_switch();
                }
                if self.truth(Op::Forte, &[2.])? && self.state(INTRO)? != 0. {
                    self.heavy_attack(0.8)?;
                    self.sleep(0.2, true)?;
                    self.run(Method::NormalAttack as i32, &[])?;
                    self.sleep(0.1, true)?;
                    return self.basic_switch();
                }
                if self.state(INTRO)? != 0. {
                    self.basic_normal(1.1)?;
                } else {
                    self.basic_wait_down(true)?;
                    self.basic_normal(0.4)?;
                }
                self.virtual_action(&[11., RESONANCE, 1.1, 0.2])?;
                self.basic_switch()
            }
            _ => Err(INVALID_ACTION),
        }
    }
}
