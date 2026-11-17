use super::*;

impl Host {
    fn extended_local(&self, index: usize) -> ActionResult<f64> {
        self.value(Op::LocalAt, &[index as f64])
    }
    pub(super) fn extended_set(&self, index: usize, value: f64) -> ActionResult {
        self.action(Op::SetLocalAt, &[index as f64, value])
    }
    fn extended_resonance(&self, post: f64, click: bool, timeout: f64) -> ActionResult<bool> {
        Ok(self.virtual_action(&[
            VIRTUAL_RESONANCE,
            post,
            0.,
            click as u8 as f64,
            0.,
            timeout,
            1.,
        ])?[0]
            != 0.)
    }
    fn extended_echo(&self, timeout: f64) -> ActionResult {
        self.virtual_action(&[VIRTUAL_ECHO, 0., 0., timeout])?;
        Ok(())
    }
    fn extended_liberation(&self, wait: f64) -> ActionResult<bool> {
        Ok(self.virtual_action(&[VIRTUAL_LIBERATION, -1., 0., wait, 0., 1.])?[0] != 0.)
    }
    fn extended_normal(&self, duration: f64) -> ActionResult {
        self.normal_attacks(duration, 0.1, 0., false, false)
    }
    fn extended_switch(&self) -> ActionResult {
        self.virtual_action(&[VIRTUAL_SWITCH, 0., 0.])?;
        Ok(())
    }
    fn extended_wait_down(&self) -> ActionResult {
        self.run(Method::WaitDown as i32, &[1.])?;
        Ok(())
    }
    pub(super) fn extended_flying(&self) -> ActionResult<bool> {
        Ok(self.virtual_action(&[VIRTUAL_FLYING])?[0] != 0.)
    }
    fn extended_mouse_forte(&self) -> ActionResult<bool> {
        self.truth(Op::Forte, &[0.])
    }
    fn extended_bar_forte(&self) -> ActionResult<bool> {
        self.truth(Op::Forte, &[2.])
    }
    fn encore_in_liberation(&self) -> ActionResult<bool> {
        Ok(self.elapsed(self.extended_local(1)?)? < 9.5)
    }
    fn encore_resonance_step2(&self, delay: f64) -> ActionResult<bool> {
        Ok(self.value(Op::Elapsed, &[self.extended_local(2)?, 1.])? < delay)
    }
    fn encore_n4(&self) -> ActionResult {
        let duration = if self.extended_resonance(0., true, 0.)? {
            2.7
        } else {
            2.4
        };
        if self.elapsed(self.extended_local(1)?)? < 6. {
            self.extended_normal(duration)?;
        } else if self.extended_mouse_forte()? {
            self.heavy_attack(0.6)?;
            self.extended_set(0, self.now()?)?;
        } else {
            self.extended_resonance(0., true, 0.)?;
        }
        Ok(())
    }
    fn roccia_plunge(&self) -> ActionResult {
        if self.need_fast_perform()? {
            self.run(Method::NormalUntilSwitch as i32, &[])?;
            return Ok(());
        }
        let mut start = self.now()?;
        let deadline = start + 20.;
        self.action(Op::Key, &[1.])?;
        let result = (|| {
            while self.extended_mouse_forte()? && self.now()? - start < 6. {
                if self.now()? >= deadline {
                    self.action(Op::Interrupted, &[2.])?;
                    return Err(HOST_ERROR);
                }
                if self.now()? - start > 2.
                    && !self.truth(Op::HasCd, &[RESONANCE, -1.])?
                    && !self.truth(Op::HasCd, &[LIBERATION, -1.])?
                    && self.extended_liberation(0.1)?
                {
                    self.extended_resonance(0., true, 0.)?;
                    start = self.now()?;
                    continue;
                }
                self.click(false, 0.1)?;
            }
            Ok(())
        })();
        if result.is_err() {
            let _ = self.action(Op::Key, &[0.]);
            return result;
        }
        self.action(Op::Key, &[0.])
    }
    pub(super) fn extended_priority(&self, args: &[f64]) -> ActionResult<f64> {
        match args[0] as i32 {
            0 => {
                if self.extended_local(2)? == 0. {
                    self.extended_set(2, 1.)?;
                    let size = self.value(Op::LoadParty, &[])? as usize;
                    for i in 0..size {
                        if self.party_field(i, 0.)? != 0. && self.party_field(i, 10.)? != 0. {
                            self.extended_set(2, 2.)?;
                        }
                    }
                }
                if self.extended_local(2)? == 2. && args[1] != 0. && args[2] != 0. && args[3] != 0.
                {
                    return Ok(400.);
                }
            }
            1 => {
                if self.value(Op::Elapsed, &[self.extended_local(0)?, 1.])? < 4.6 {
                    return Ok(0.);
                }
                if self.encore_in_liberation()? || self.encore_resonance_step2(2.)? {
                    return Ok(400.);
                }
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok(if self.truth(Op::HealerLocked, &[])? {
            0.
        } else {
            200.
        })
    }
    pub(super) fn extended_switch_next(&self, args: &[f64]) -> ActionResult {
        if args[0] == 0. {
            if self.is_con_full()? {
                self.extended_set(0, self.now()?)?;
                self.extended_set(1, 5.)?;
            }
            self.action(Op::BaseSwitch, &args[1..])
        } else if args[0] == 2. {
            self.action(Op::RocciaSwitch, &args[1..])
        } else {
            Err(INVALID_ACTION)
        }
    }
    pub(super) fn keeper_dodge(&self) -> ActionResult<bool> {
        let mut clicked = false;
        if self.elapsed(self.extended_local(0)?)? < 30. && self.extended_local(1)? > 0. {
            let start = self.now()?;
            while self.now()? - start < 1.5 {
                if !self.truth(Op::Condition, &[])? {
                    break;
                }
                self.run(Method::RightClicks as i32, &[0.05, 0.1, 0.])?;
                self.sleep(0.05, true)?;
                clicked = true;
                self.action(Op::NextFrame, &[])?;
            }
        }
        if clicked {
            self.extended_set(1, self.extended_local(1)? - 1.)?;
        }
        Ok(clicked)
    }
    pub(super) fn extended_perform(&self, kind: i32) -> ActionResult {
        match kind {
            0 => {
                if self.state(INTRO)? != 0. {
                    self.set_flag(SKIP_COMBAT, true)?;
                    let result = (|| {
                        self.sleep(0.1, false)?;
                        if !self.truth(Op::InTeam, &[])? {
                            self.wait_for_team(4., false, true)?;
                        } else {
                            self.extended_normal(1.2)?;
                        }
                        Ok(())
                    })();
                    if result.is_err() {
                        let _ = self.set_flag(SKIP_COMBAT, false);
                        return result;
                    }
                    self.set_flag(SKIP_COMBAT, false)?;
                }
                self.extended_echo(0.)?;
                self.extended_liberation(0.1)?;
                self.extended_resonance(0., true, 0.)?;
                self.extended_switch()
            }
            1 => {
                if self.state(INTRO)? != 0. {
                    let since_lib = self.elapsed(self.extended_local(1)?)?;
                    if since_lib > 6. && since_lib < 10. && self.extended_mouse_forte()? {
                        self.heavy_attack(1.2)?;
                        self.sleep(0.1, true)?;
                        self.extended_set(0, self.now()?)?;
                        return self.extended_switch();
                    }
                    self.extended_wait_down()?;
                }
                if self.encore_in_liberation()? {
                    self.encore_n4()?;
                    return self.extended_switch();
                }
                if self.extended_resonance(0., true, 0.)? && !self.encore_resonance_step2(4.)? {
                    self.extended_set(2, self.now()?)?;
                    return self.extended_switch();
                }
                if !self.need_fast_perform()?
                    && !self.flag(OPEN_WORLD)?
                    && self.extended_liberation(0.4)?
                {
                    self.extended_set(1, self.now()?)?;
                    self.encore_n4()?;
                    return self.extended_switch();
                }
                if self.skill_available(ECHO)? {
                    self.extended_echo(1.)?;
                }
                self.extended_switch()
            }
            2 => {
                if self.state(INTRO)? != 0. {
                    self.heavy_attack(1.6)?;
                    self.sleep(0.1, true)?;
                    self.roccia_plunge()?;
                    if !self.skill_available(LIBERATION)? && !self.skill_available(RESONANCE)? {
                        return self.extended_switch();
                    }
                }
                let liberated = self.extended_liberation(0.1)?;
                if self.extended_resonance(0., true, 0.)? || !liberated {
                    self.roccia_plunge()?;
                    return self.extended_switch();
                }
                self.extended_echo(1.)?;
                self.extended_switch()
            }
            3 => {
                if self.state(INTRO)? != 0. {
                    self.extended_normal(2.)?;
                }
                if self.extended_liberation(0.1)? {
                    let start = self.now()?;
                    while self.now()? - start < 12. {
                        if self.extended_resonance(0., true, 0.)? {
                            self.action(Op::Click, &[2., -1.])?;
                        }
                        self.run(Method::NormalAttack as i32, &[])?;
                    }
                    return self.extended_switch();
                }
                let mut i = 0;
                let start = self.now()?;
                while !self.extended_bar_forte()? && !self.is_con_full()? {
                    if self.now()? - start >= 15. {
                        self.action(Op::Interrupted, &[3.])?;
                        return Err(HOST_ERROR);
                    }
                    if i % 4 == 0 {
                        self.heavy_attack(0.6)?;
                        if self.skill_available(RESONANCE)? || self.skill_available(ECHO)? {
                            self.action(Op::Click, &[2., -1.])?;
                            break;
                        }
                        i = 0;
                    }
                    self.run(Method::NormalAttack as i32, &[])?;
                    i += 1;
                }
                if !self.extended_bar_forte()? && self.skill_available(RESONANCE)? {
                    self.extended_resonance(1., true, 0.)?;
                }
                if self.skill_available(ECHO)? {
                    self.extended_echo(1.)?;
                }
                self.extended_switch()
            }
            4 => {
                self.extended_wait_down()?;
                if self.extended_liberation(0.1)? {
                    self.extended_set(0, self.now()?)?;
                }
                if self.elapsed(self.extended_local(0)?)? < 25. {
                    let deadline = self.now()? + 30.;
                    while !self.extended_resonance(0., true, 0.)? {
                        if self.now()? >= deadline {
                            self.action(Op::Interrupted, &[4.])?;
                            return Err(HOST_ERROR);
                        }
                        self.extended_normal(1.)?;
                    }
                } else if self.skill_available(ECHO)? {
                    self.extended_echo(1.)?;
                } else {
                    self.extended_resonance(0., true, 0.)?;
                }
                self.extended_switch()
            }
            5 => {
                if self.state(INTRO)? != 0. {
                    self.extended_normal(1.17)?;
                }
                if self.extended_flying()? {
                    self.extended_wait_down()?;
                }
                let mut start = self.now()?;
                let duration = if self.state(SUB_INTRO)? != 0. {
                    4.
                } else {
                    1.2
                };
                let deadline = start + 20.;
                while self.now()? - start < duration {
                    if self.now()? >= deadline {
                        self.action(Op::Interrupted, &[5.])?;
                        return Err(HOST_ERROR);
                    }
                    self.extended_echo(0.)?;
                    if self.now()? - start < 0.5 && self.extended_liberation(0.1)? {
                        start = self.now()?;
                    }
                    if self.run(Method::HeavyForteKind as i32, &[0.])?[0] != 0. {
                        return self.extended_switch();
                    }
                    if self.extended_flying()? && !self.extended_mouse_forte()? {
                        let size = self.value(Op::LoadParty, &[])? as usize;
                        for i in 0..size {
                            if self.party_field(i, 11.)? != 0. {
                                self.action(Op::PartyDodge, &[i as f64])?;
                                break;
                            }
                        }
                    }
                    self.click(false, -1.)?;
                    self.action(Op::CheckCombat, &[])?;
                    self.action(Op::NextFrame, &[])?;
                }
                self.extended_resonance(0., true, 0.)?;
                self.extended_switch()
            }
            6 => {
                self.run(Method::WaitIntro as i32, &[1.2, 1.])?;
                self.extended_echo(0.)?;
                let start = self.now()?;
                let mut liberation_start = None;
                while self.elapsed(start)? < 23. {
                    if match liberation_start {
                        None => self.elapsed(start)? >= 8.,
                        Some(time) => self.elapsed(time)? >= 15.,
                    } {
                        break;
                    }
                    self.set(CYCLE_START, self.now()?)?;
                    if self.extended_bar_forte()? {
                        self.run(Method::HeavyForteKind as i32, &[2.])?;
                    } else if liberation_start.is_none() && self.extended_liberation(0.)? {
                        liberation_start = Some(self.now()?);
                    } else if self.extended_resonance(0., false, 1.)? {
                        self.extended_normal(0.8)?;
                    } else {
                        self.click(false, 0.1)?;
                    }
                    self.run(Method::CycleSleep as i32, &[0.1])?;
                }
                self.extended_switch()
            }
            7 => {
                let mut duration = if self.state(INTRO)? != 0. { 8. } else { 3.2 };
                let start = self.now()?;
                let mut resonance_since = 0.;
                let mut echo_used = false;
                self.action(Op::Mouse, &[1.])?;
                let result = (|| {
                    while self.elapsed(start)? < duration {
                        if self.now()? - start > 60. {
                            self.action(Op::Interrupted, &[6.])?;
                            return Err(HOST_ERROR);
                        }
                        if self.skill_available(LIBERATION)? {
                            if self.extended_liberation(0.)? {
                                duration += 2.;
                            }
                        } else if self.skill_available(RESONANCE)? {
                            if resonance_since == 0. {
                                resonance_since = self.now()?;
                            }
                            if resonance_since != 0. && self.now()? - resonance_since > 0.2 {
                                self.extended_resonance(0., false, 1.)?;
                            }
                        } else if !echo_used {
                            resonance_since = 0.;
                            echo_used = self.virtual_action(&[VIRTUAL_ECHO, 0., 0., 0.])?[0] != 0.;
                        } else {
                            resonance_since = 0.;
                        }
                        self.sleep(0.05, true)?;
                    }
                    Ok(())
                })();
                if result.is_err() {
                    let _ = self.action(Op::Mouse, &[0.]);
                    return result;
                }
                self.action(Op::Mouse, &[0.])?;
                self.sleep(0.1, false)?;
                self.extended_switch()
            }
            _ => Err(INVALID_ACTION),
        }
    }
}
