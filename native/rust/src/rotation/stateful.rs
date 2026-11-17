use super::*;
mod more;
mod others;

impl Host {
    fn stateful_resonance(&self, click: bool, timeout: f64) -> ActionResult<bool> {
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
    fn stateful_lib(&self, wait: f64) -> ActionResult<bool> {
        Ok(self.virtual_action(&[VIRTUAL_LIBERATION, -1., 0., wait, 0., 1.])?[0] != 0.)
    }
    fn stateful_echo(&self, timeout: f64) -> ActionResult {
        self.virtual_action(&[VIRTUAL_ECHO, 0., 0., timeout])?;
        Ok(())
    }
    fn stateful_normal(&self, duration: f64) -> ActionResult {
        self.normal_attacks(duration, 0.1, 0., false, false)
    }
    fn stateful_switch(&self) -> ActionResult {
        self.virtual_action(&[VIRTUAL_SWITCH, 0., 0.])?;
        Ok(())
    }
    fn stateful_flying(&self) -> ActionResult<bool> {
        Ok(self.virtual_action(&[VIRTUAL_FLYING])?[0] != 0.)
    }
    fn stateful_down(&self) -> ActionResult {
        self.run(Method::WaitDown as i32, &[1.])?;
        Ok(())
    }
    fn stateful_cycle(&self) -> ActionResult {
        self.set(CYCLE_START, self.now()?)
    }
    fn stateful_cycle_end(&self) -> ActionResult {
        self.run(Method::CycleSleep as i32, &[0.1])?;
        Ok(())
    }
    fn stateful_forte_full(&self) -> ActionResult<bool> {
        Ok(self.virtual_action(&[VIRTUAL_FORTE])?[0] != 0.)
    }
    fn stateful_local(&self, index: usize) -> ActionResult<f64> {
        self.value(Op::LocalAt, &[index as f64])
    }
    fn stateful_set(&self, index: usize, value: f64) -> ActionResult {
        self.action(Op::SetLocalAt, &[index as f64, value])
    }
    fn has_buff(&self) -> ActionResult<bool> {
        self.truth(Op::HasBuff, &[])
    }
    fn douling_cycle(&self, duration: f64) -> ActionResult {
        let start = self.now()?;
        while self.now()? - start < duration {
            self.stateful_cycle()?;
            if self.stateful_flying()? {
                self.stateful_down()?;
                break;
            }
            self.click(false, -1.)?;
            self.stateful_cycle_end()?;
        }
        Ok(())
    }
    fn douling_heavy(&self, duration: f64) -> ActionResult {
        for _ in 0..3 {
            self.action(Op::CheckCombat, &[])?;
            self.action(Op::Mouse, &[1.])?;
            let held: ActionResult<bool> = (|| {
                let start = self.now()?;
                let mut interrupted = false;
                while self.now()? - start < duration {
                    if self.stateful_flying()? {
                        interrupted = true;
                        break;
                    }
                    self.sleep(0.1, true)?;
                }
                Ok(interrupted)
            })();
            if held.is_err() {
                let _ = self.action(Op::Mouse, &[0.]);
            }
            let interrupted = held?;
            self.action(Op::Mouse, &[0.])?;
            self.sleep(0.01, true)?;
            if !interrupted {
                return Ok(());
            }
            self.stateful_down()?;
        }
        Ok(())
    }
    fn chisa_forte(&self) -> ActionResult<bool> {
        if self.stateful_flying()? {
            self.stateful_down()?;
        }
        self.action(Op::TapSkill, &[RESONANCE, 0., -1., 1.2])?;
        if self.stateful_forte_full()? {
            return Ok(false);
        }
        self.heavy_attack(3.5)?;
        Ok(true)
    }
    fn chisa_support(&self) -> ActionResult {
        let long_actions = self.state(INTRO)? != 0. && !self.has_buff()?;
        if self.state(INTRO)? != 0. {
            self.stateful_normal(2.)?;
        }
        if self.stateful_flying()?
            && !self.skill_available(LIBERATION)?
            && !self.skill_available(RESONANCE)?
        {
            self.stateful_down()?;
        }
        self.stateful_echo(0.)?;
        let start = self.now()?;
        let duration = if long_actions { 10. } else { 1.2 };
        while self.elapsed(start)? < duration {
            self.stateful_cycle()?;
            if self.is_con_full()? {
                return self.stateful_switch();
            }
            if self.skill_available(LIBERATION)? {
                self.stateful_lib(0.)?;
            } else if self.stateful_forte_full()? {
                if self.chisa_forte()? {
                    break;
                }
            } else if !self.stateful_resonance(true, 0.)? {
                self.click(false, -1.)?;
            }
            self.stateful_cycle_end()?;
        }
        self.stateful_switch()
    }
    fn iuno_icon(&self, name: &str) -> ActionResult<bool> {
        self.feature(name, 0.6, "box_extra_action")
    }
    fn iuno_everything(&self) -> ActionResult {
        let mut timeout = if self.state(INTRO)? != 0. { 5.5 } else { 1.5 };
        let mut start = self.now()?;
        let deadline = start + 30.;
        let mut last_was_click = true;
        self.stateful_echo(1.)?;
        let (mut c6_performed, mut jumped) = (false, false);
        while self.elapsed(start)? < timeout {
            if self.now()? >= deadline {
                self.action(Op::Interrupted, &[8.])?;
                return Err(HOST_ERROR);
            }
            let cycle = self.now()?;
            let mut heavy = false;
            while self.elapsed(self.stateful_local(0)?)? > 20. && self.iuno_icon("iuno_heavy")? {
                if self.now()? >= deadline {
                    self.action(Op::Interrupted, &[9.])?;
                    return Err(HOST_ERROR);
                }
                self.sleep(0.05, true)?;
                self.heavy_attack(0.6)?;
                self.sleep(0.05, true)?;
                heavy = true;
            }
            if heavy {
                self.stateful_set(0, self.now()?)?;
                if !c6_performed && self.setting_bool("Iuno C6")? {
                    c6_performed = true;
                    start = self.now()?;
                    timeout = 5.;
                } else {
                    return Ok(());
                }
            }
            if !jumped && self.iuno_icon("iuno_jump")? {
                while self.iuno_icon("iuno_jump")? {
                    if self.now()? >= deadline {
                        self.action(Op::Interrupted, &[10.])?;
                        return Err(HOST_ERROR);
                    }
                    self.action(Op::Tap, &[3., 0.1, -1., 0.01])?;
                }
                timeout += 3.;
                jumped = true;
                if self.state(INTRO)? != 0. {
                    continue;
                }
                return Ok(());
            }
            if self.elapsed(self.state(LAST_LIBERATION)?)? > 20. && self.stateful_lib(0.)? {
                start = self.now()?;
                timeout = 3.;
            }
            if last_was_click {
                last_was_click = false;
                self.send(RESONANCE, 0.)?;
            } else {
                last_was_click = true;
                self.click(false, -1.)?;
            }
            self.sleep(0.1 - (self.now()? - cycle), true)?;
        }
        Ok(())
    }
    pub(super) fn stateful_perform(&self, kind: i32) -> ActionResult {
        match kind {
            0 => {
                if self.state(INTRO)? != 0. {
                    self.stateful_normal(2.)?;
                } else if self.stateful_local(1)? != 0. {
                    self.stateful_normal(1.3)?;
                }
                let duration = if self.stateful_local(1)? != 0. {
                    4.4
                } else {
                    1.2
                };
                let start = self.now()?;
                while self.elapsed(start)? < duration {
                    self.stateful_cycle()?;
                    if self.elapsed(self.stateful_local(0)?)? < 10. && self.is_con_full()? {
                        return self.stateful_switch();
                    }
                    if self.stateful_resonance(true, 0.)? {
                    } else if self.skill_available(LIBERATION)? {
                        let second = self.feature("denia_end_lib", 0.8, "")?;
                        if self.stateful_lib(0.)? {
                            if second {
                                self.stateful_set(0, self.now()?)?;
                                self.stateful_set(1, 0.)?;
                                self.stateful_echo(0.)?;
                            } else {
                                self.stateful_set(1, 1.)?;
                                self.stateful_normal(1.9)?;
                            }
                            return self.stateful_switch();
                        }
                    } else {
                        self.click(false, -1.)?;
                    }
                    self.stateful_cycle_end()?;
                }
                self.stateful_switch()
            }
            1 => {
                if self.stateful_local(0)? == 1. {
                    self.douling_cycle(1.2)?;
                    if self.stateful_flying()? {
                        self.stateful_down()?;
                    }
                    self.action(Op::CheckCombat, &[])?;
                    if !self.stateful_resonance(true, 0.5)? {
                        self.stateful_echo(0.)?;
                        self.stateful_lib(0.1)?;
                        self.stateful_set(0, 1.)?;
                        return self.stateful_switch();
                    }
                    self.sleep(0.2, true)?;
                    if self.stateful_flying()? {
                        self.stateful_down()?;
                    }
                    self.action(Op::CheckCombat, &[])?;
                    self.douling_cycle(1.)?;
                    self.stateful_set(0, 2.)?;
                    self.stateful_switch()
                } else {
                    self.action(Op::CheckCombat, &[])?;
                    self.action(Op::Tap, &[3., 0.01, -1., 0.01])?;
                    self.sleep(0.05, true)?;
                    self.action(Op::CheckCombat, &[])?;
                    if self.stateful_flying()? {
                        self.click(false, -1.)?;
                        self.sleep(0.05, true)?;
                    } else {
                        self.stateful_down()?;
                    }
                    self.action(Op::CheckCombat, &[])?;
                    self.douling_heavy(2.5)?;
                    self.stateful_echo(0.)?;
                    self.stateful_lib(0.1)?;
                    self.stateful_set(0, 1.)?;
                    self.stateful_switch()
                }
            }
            2 => {
                if !self.setting_bool("Chisa DPS")? {
                    return self.chisa_support();
                }
                let mut timeout = 2.5;
                self.set(19., 1.)?;
                if self.state(INTRO)? != 0. {
                    self.stateful_normal(0.8)?;
                    timeout = 2.3;
                }
                if self.stateful_flying()?
                    && !self.skill_available(LIBERATION)?
                    && !self.skill_available(RESONANCE)?
                {
                    self.stateful_down()?;
                }
                self.stateful_echo(1.)?;
                let mut start = self.now()?;
                let deadline = start + 30.;
                let mut under_lib = false;
                while self.now()? - start < timeout {
                    if self.now()? >= deadline {
                        self.action(Op::Interrupted, &[7.])?;
                        return Err(HOST_ERROR);
                    }
                    if self.now()? - start < 0.5 && self.stateful_lib(0.1)? {
                        start = self.now()?;
                        under_lib = true;
                        timeout = 10.;
                        self.sleep(0.2, true)?;
                    }
                    if self.now()? - start < 0.5
                        && !self.stateful_forte_full()?
                        && self.stateful_resonance(true, 0.)?
                    {
                        start = self.now()?;
                        if timeout != 10. {
                            timeout = 1.7;
                        }
                    }
                    if (under_lib || self.setting_bool("Chisa DPS")?)
                        && self.stateful_forte_full()?
                        && self.chisa_forte()?
                    {
                        self.set(19., 0.)?;
                        return self.stateful_switch();
                    }
                    self.click(false, -1.)?;
                    self.action(Op::CheckCombat, &[])?;
                    self.action(Op::NextFrame, &[])?;
                }
                self.stateful_switch()
            }
            3 => {
                self.stateful_down()?;
                self.iuno_everything()?;
                self.stateful_switch()
            }
            4..=6 => self.more_perform(kind),
            7..=14 => self.others_perform(kind),
            _ => Err(INVALID_ACTION),
        }
    }
    pub(super) fn stateful_priority(&self, args: &[f64]) -> ActionResult<f64> {
        match args[0] as i32 {
            0 => {
                if args[1] != 0. {
                    if args[2] != 0. && args[3] != 0. && !self.has_buff()? {
                        return Ok(200.);
                    }
                    return Ok(1.);
                }
                if self.has_buff()? {
                    return Ok(100.);
                }
                Ok(if self.truth(Op::HealerLocked, &[])? {
                    0.
                } else {
                    200.
                })
            }
            1 => Ok(if self.elapsed(self.state(LAST_PERFORM)?)? < 8. {
                0.
            } else {
                200.
            }),
            5 => self.suisui_priority(args),
            10 | 11 | 12 | 14 => self.others_priority(args),
            _ => Err(INVALID_ACTION),
        }
    }
    pub(super) fn stateful_initialize(&self, kind: i32) -> ActionResult {
        if kind == 6 {
            return self.set(19., 0.);
        }
        let enabled = match kind {
            2 => self.setting_bool("Chisa DPS")?,
            3 => self.setting_bool("Iuno C6")?,
            _ => return Err(INVALID_ACTION),
        };
        if enabled {
            self.set(ROLE, 0.)?;
            self.set(18., 14.)?;
            self.set(19., 1.)?;
        }
        Ok(())
    }
    pub(super) fn denia_phase(&self, phase: i32) -> ActionResult {
        self.stateful_set(1, (phase == 1) as u8 as f64)?;
        if phase == 2 {
            self.stateful_set(0, self.now()?)?;
        }
        Ok(())
    }
    pub(super) fn denia_reset(&self) -> ActionResult {
        self.run(Method::ResetState as i32, &[])?;
        self.stateful_set(0, -1.)?;
        self.stateful_set(1, 0.)
    }
    pub(super) fn stateful_lifecycle(&self, args: &[f64]) -> ActionResult<f64> {
        if args[0] >= 7. {
            self.others_lifecycle(args)
        } else {
            self.more_lifecycle(args).map(|_| 0.)
        }
    }
}
