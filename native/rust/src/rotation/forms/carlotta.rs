use super::*;

// Private slots: switch lock, movement mode, forte, continuing chain, chain ready.
impl Host {
    pub(super) fn carlotta_decide(&self) -> ActionResult {
        self.f_set(1, 0.)?;
        if self.observe(11, "FarmEchoTask", "", &[])? != 0. {
            self.f_set(1, 1.)?;
        } else {
            let found = self.observe(12, "Zhezhi", "", &[])? != 0.;
            self.action(Op::SetRelated, &[1.])?;
            if found {
                self.action(Op::Related, &[4.])?;
            }
        }
        Ok(())
    }
    fn carlotta_gauge(&self) -> ActionResult<f64> {
        if self.f_mouse_forte()? {
            return Ok(4.);
        }
        let gauge = self.observe(
            10,
            "",
            "",
            &[
                5120., 2880., 2164., 2670., 736., 10., 235., 195., 70., 255., 225., 100., 4., 9.,
                11., 100.,
            ],
        )?;
        self.f_set(2, gauge)?;
        Ok(gauge)
    }
    fn carlotta_lib_one(&self) -> ActionResult<bool> {
        if self.f_local(1)? == 1. {
            self.f_hold(false, || self.f_lib(false))
        } else {
            self.f_lib(false)
        }
    }
    fn carlotta_lib_loop(&self, press_w: bool) -> ActionResult<bool> {
        let mut dodge = -1.;
        let start = self.now()?;
        let mut cast = false;
        while self.skill_available(LIBERATION)? {
            if self.now()? - start > 20. {
                self.action(Op::Interrupted, &[19.])?;
                return Err(HOST_ERROR);
            }
            if dodge != -1. && self.now()? - dodge > 0.5 && self.f_flying()? {
                self.f_dodge()?;
            }
            if if press_w {
                self.carlotta_lib_one()?
            } else {
                self.f_lib(false)?
            } {
                dodge = self.now()?;
                cast = true;
                if !press_w {
                    self.f_set(3, 0.)?;
                    self.f_set(4, 0.)?;
                }
            }
            self.action(Op::CheckCombat, &[])?;
        }
        Ok(cast)
    }
    fn carlotta_outro(&self) -> ActionResult {
        self.action(Op::Related, &[3.])?;
        self.carlotta_gauge()?;
        let deadline = self.now()? + 40.;
        if self.f_local(4)? == 0. {
            while !self.f_mouse_forte()? {
                if self.skill_available(RESONANCE)? {
                    self.f_res(true, 0.)?;
                } else {
                    self.click(false, 0.1)?;
                }
                if self.elapsed(self.state(LAST_PERFORM)?)? > 6. {
                    break;
                }
                if self.now()? > deadline {
                    self.action(Op::Interrupted, &[20.])?;
                    return Err(HOST_ERROR);
                }
                self.action(Op::CheckCombat, &[])?;
            }
        }
        if self.f_heavy_forte()? {
            self.f_set(4, 1.)?;
            self.f_set(2, 0.)?;
        }
        self.action(Op::CheckCombat, &[])?;
        // Original comparisons with False intentionally do not clear these flags.
        let (mut clicked, mut liberated) = (false, false);
        if self.f_local(4)? != 0. {
            while self.elapsed(self.state(LAST_PERFORM)?)? < 14. {
                if self.now()? > deadline {
                    self.action(Op::Interrupted, &[21.])?;
                    return Err(HOST_ERROR);
                }
                if self.skill_available(LIBERATION)?
                    && !liberated
                    && self.carlotta_lib_loop(false)?
                {
                    liberated = true;
                    self.f_set(2, 0.)?;
                    self.sleep(0.2, true)?;
                }
                if self.f_res(true, 0.)? {
                    self.f_normal(0.8)?;
                    self.f_set(2, self.f_local(2)? + 1.)?;
                    clicked = true;
                }
                self.click(false, 0.1)?;
                if !self.skill_available(LIBERATION)?
                    && !self.skill_available(RESONANCE)?
                    && clicked
                {
                    break;
                }
                self.action(Op::CheckCombat, &[])?;
            }
        }
        if self.f_echo(2.)? {
            self.f_set(0, self.now()?)?;
        }
        self.f_set(3, (!liberated) as u8 as f64)
    }
    fn carlotta_interlock(&self) -> ActionResult {
        if self.state(INTRO)? != 0. {
            self.f_normal(1.3)?;
            if self.f_outro("char_zhezhi")? {
                self.carlotta_outro()?;
                return self.f_switch();
            }
        }
        if self.carlotta_gauge()? < 4. && self.skill_available(RESONANCE)? && self.f_local(4)? == 0.
        {
            if self.state(INTRO)? == 0. {
                self.heavy_attack(0.6)?;
            }
            if self.f_res(true, 0.)? {
                self.f_set(2, self.f_local(2)? + 2.)?;
                self.f_set(0, self.now()?)?;
                return self.f_switch();
            }
        }
        if self.carlotta_partner_ready()? {
            self.f_set(3, 0.)?;
        }
        if self.f_heavy_forte()? {
            self.f_set(4, 1.)?;
            return self.f_switch();
        }
        if self.skill_available(LIBERATION)? && self.f_local(3)? != 0. {
            self.carlotta_lib_loop(false)?;
        }
        if self.skill_available(ECHO)? {
            self.f_echo(1.)?;
        }
        self.f_normal(0.31)?;
        self.f_switch()
    }
    pub(super) fn carlotta_partner_ready(&self) -> ActionResult<bool> {
        Ok(self.f_local(2)? > 2.
            || (self.skill_available(RESONANCE)? && self.f_local(2)? > 0.)
            || self.f_local(4)? != 0.)
    }
    pub(super) fn carlotta_perform(&self) -> ActionResult {
        if self.f_local(1)? == -1. {
            self.carlotta_decide()?;
        }
        if self.truth(Op::Related, &[0.])? {
            return self.carlotta_interlock();
        }
        if self.state(INTRO)? != 0. {
            self.f_normal(1.3)?;
        }
        if self.f_heavy_forte()? {
            return self.f_switch();
        }
        if self.skill_available(LIBERATION)? && !self.need_fast_perform()? {
            self.carlotta_lib_loop(true)?;
            self.f_echo(1.)?;
            self.f_set(0, self.now()?)?;
            return self.f_switch();
        }
        if self.skill_available(RESONANCE)? {
            if self.state(INTRO)? == 0. {
                self.heavy_attack(0.6)?;
            }
            if self.f_res(true, 0.)? {
                return self.f_switch();
            }
        }
        if self.f_echo(1.)? {
            return self.f_switch();
        }
        self.f_normal(0.31)?;
        self.f_switch()
    }
    pub(in crate::rotation) fn carlotta_liberation(&self, a: &[f64]) -> ActionResult<bool> {
        let (mut start, mut last) = (self.now()?, 0.);
        let mut clicked = false;
        let result = (|| {
            while self.skill_available(LIBERATION)? {
                let now = self.now()?;
                if now - last > 0.1 {
                    self.send(LIBERATION, 0.)?;
                    clicked = true;
                    last = now;
                }
                if self.now()? - start > 0.4 {
                    return Ok(false);
                }
                self.action(Op::NextFrame, &[])?;
            }
            if !clicked {
                return Ok(false);
            }
            if !self.wait_for_team(0.4, true, false)? {
                self.set_flag(IN_LIBERATION, false)?;
                return Ok(false);
            }
            self.set_flag(IN_LIBERATION, true)?;
            start = self.now()?;
            let mut last_f = 0.;
            while !self.truth(Op::InTeam, &[])? {
                let now = self.now()?;
                if a[4] != 0. && now - start >= a[3] && now - last_f >= 0.1 {
                    self.action(Op::Tap, &[1., 0., -1., 0.01])?;
                    last_f = now;
                }
                if now - start > 7. {
                    self.action(Op::Interrupted, &[22.])?;
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
    pub(in crate::rotation) fn carlotta_resonance(&self, a: &[f64]) -> ActionResult<[f64; 3]> {
        let (mut last, mut clicked_at) = (0., 0.);
        let start = self.now()?;
        let (mut clicked, mut last_res) = (false, false);
        loop {
            if self.now()? - start > if a[4] == 0. { 10. } else { a[4] } {
                self.set_flag(IN_LIBERATION, false)?;
                self.action(Op::SkillFailed, &[])?;
                break;
            }
            self.action(Op::CheckCombat, &[])?;
            let now = self.now()?;
            let white = self.value(Op::Highlighted, &[RESONANCE])?;
            if self.truth(Op::HasCd, &[RESONANCE, -1.])? {
                break;
            }
            if now - last > 0.1 {
                if a[2] != 0. && (white == 0. || last_res) {
                    self.click(false, -1.)?;
                    last_res = false;
                    continue;
                }
                if white > 0. && self.skill_available(RESONANCE)? {
                    if clicked_at == 0. {
                        clicked = true;
                        clicked_at = now;
                        self.record(RESONANCE)?;
                    }
                    last_res = true;
                    self.send(RESONANCE, 0.)?;
                    if a[1] != 0. {
                        self.sleep(0.2, false)?;
                    }
                }
                last = now;
            }
            self.action(Op::NextFrame, &[])?;
        }
        self.set_flag(IN_LIBERATION, false)?;
        if clicked {
            self.sleep(a[0], true)?;
        }
        Ok([
            clicked as u8 as f64,
            if clicked_at != 0. {
                self.now()? - clicked_at
            } else {
                0.
            },
            0.,
        ])
    }
}
