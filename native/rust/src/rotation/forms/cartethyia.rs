use super::*;

impl Host {
    fn cart_small(&self) -> ActionResult<bool> {
        let small = self.observe(13, "forte_cartethyia_sword3", "", &[0.5, 0.6, 0.5])? != 0.;
        self.f_set(0, small as u8 as f64)?;
        Ok(small)
    }
    fn cart_buffs(&self) -> ActionResult {
        for (index, name) in [
            "forte_cartethyia_sword1",
            "forte_cartethyia_sword2",
            "forte_cartethyia_sword3",
        ]
        .iter()
        .enumerate()
        {
            self.f_set(3 + index, self.feature(name, 0.9, "")? as u8 as f64)?;
        }
        Ok(())
    }
    fn cart_first(&self) -> ActionResult<bool> {
        let gap = self.state(LAST_PERFORM)? - self.value(Op::CombatStart, &[])?;
        Ok((0. ..0.4).contains(&gap))
    }
    fn cart_n4(&self) -> ActionResult<f64> {
        let duration = if self.f_local(2)? == 0. && self.state(INTRO)? != 0. {
            3.9 - (self.now()? - self.state(LAST_PERFORM)?)
        } else if self.f_local(2)? != 0.
            || self.cart_first()?
            || self.elapsed_with_intro(self.f_local(7)?)? < 1.5
        {
            3.25
        } else {
            let backswing = self.elapsed_with_intro(self.f_local(6)?)?;
            if backswing < 2.5 {
                2. + cpp_max_zero(1.6 - backswing)
            } else {
                1.9 - (self.now()? - self.state(LAST_PERFORM)?)
            }
        };
        self.f_set(7, -1.)?;
        self.f_set(6, -1.)?;
        Ok(duration)
    }
    fn cart_big_lib(&self) -> ActionResult<bool> {
        if !self.feature("lib_cartethyia_big", 0.8, "")? {
            return Ok(false);
        }
        self.set(LIBERATION_READY, 1.)?;
        if self.f_lib(false)? {
            self.f_set(0, 1.)?;
            self.f_res(true, 0.)?;
            return Ok(true);
        }
        Ok(false)
    }
    fn cart_big_res(&self) -> ActionResult<bool> {
        if self.truth(Op::HasCd, &[RESONANCE, -1.])? {
            return Ok(false);
        }
        let (mut last, mut clicked_at) = (0., 0.);
        let start = self.now()?;
        let mut clicked = false;
        loop {
            if clicked_at != 0. && self.now()? - clicked_at > 8. {
                self.set_flag(IN_LIBERATION, false)?;
                break;
            }
            if self.now()? - start > 15. {
                self.action(Op::Interrupted, &[18.])?;
                return Err(HOST_ERROR);
            }
            self.action(Op::CheckCombat, &[])?;
            let now = self.now()?;
            let white = self.value(Op::Highlighted, &[RESONANCE])?;
            if !self.skill_available(RESONANCE)? {
                break;
            }
            if now - last > 0.1 {
                if white > 0. && self.skill_available(RESONANCE)? {
                    if white < 0.17 && self.now()? - clicked_at < 2.5 {
                        self.click(false, -1.)?;
                        continue;
                    }
                    if clicked_at == 0. {
                        clicked = true;
                        clicked_at = now;
                    }
                    self.send(RESONANCE, 0.)?;
                }
                last = now;
            }
            if self.cart_big_lib()? {
                break;
            }
            self.action(Op::NextFrame, &[])?;
        }
        if clicked {
            self.record(RESONANCE)?;
            self.f_set(6, self.now()?)?;
        }
        Ok(clicked)
    }
    fn cart_mid_available(&self) -> ActionResult<bool> {
        if self.f_local(0)? == 0.
            || self.observe(
                3,
                "",
                "",
                &[3840., 2160., 2298., 1997., 63., 25., 250., 246., 244., 1.],
            )? <= 0.15
        {
            return Ok(false);
        }
        let gray = self.observe_values(14, "", "", &[3840., 2160., 2298., 1997., 63., 25., 1.])?;
        Ok(gray[2] != 0. && gray[0] > 190. && gray[1] < 45.)
    }
    fn cart_mid_action(&self) -> ActionResult {
        self.action(Op::Tap, &[3., 0.1, -1., 0.01])?;
        if self.skill_available(ECHO)? {
            self.f_echo(0.)?;
        }
        self.click(false, -1.)?;
        self.action(Op::Sleep, &[0.1])
    }
    fn cart_mid_air(&self) -> ActionResult {
        self.cart_buffs()?;
        if !self.skill_available(LIBERATION)?
            && !(self.f_local(3)? != 0. && self.f_local(4)? != 0. && self.f_local(5)? != 0.)
            && self.f_local(1)? == 0.
        {
            return Ok(());
        }
        let start = self.now()?;
        if self.cart_mid_available()? {
            loop {
                self.cart_mid_action()?;
                if !self.cart_mid_available()? {
                    self.sleep(0.4, true)?;
                    break;
                }
                if self.now()? - start > 2. {
                    break;
                }
                self.sleep(0.1, true)?;
            }
        } else if self.f_local(1)? != 0. {
            while self.now()? - start < 0.8 {
                self.cart_mid_action()?;
            }
        }
        self.f_set(1, 0.)
    }
    fn cart_acquire(&self) -> ActionResult<bool> {
        self.cart_buffs()?;
        if self.f_local(3)? != 0. && self.f_local(4)? != 0. && self.f_local(5)? != 0. {
            return Ok(false);
        }
        let action = !(self.f_local(4)? != 0. && self.f_local(5)? != 0.);
        if self.f_local(4)? == 0. {
            let once = self.observe(13, "forte_cartethyia_sword2", "", &[0.5, 0.6, 0.85])? != 0.;
            let mut timeout = if once {
                if self.cart_first()? {
                    2.5
                } else {
                    2.
                }
            } else {
                3.5
            };
            let mut start = self.now()?;
            let mut interrupted = false;
            while self.now()? - start < timeout {
                if !once
                    && self.observe(13, "forte_cartethyia_sword2", "", &[0.5, 0.6, 0.85])? != 0.
                {
                    break;
                }
                if !interrupted && self.f_flying()? {
                    if timeout == 2. {
                        timeout = 2.5;
                    }
                    interrupted = true;
                    self.wait_with(|| Ok(!self.f_flying()?), 3., 0., || Ok(()), || Ok(()))?;
                    start = self.now()?;
                }
                self.click(false, 0.1)?;
                self.action(Op::Sleep, &[0.01])?;
                self.action(Op::CheckCombat, &[])?;
                self.action(Op::NextFrame, &[])?;
            }
        }
        let mut res = false;
        if self.f_local(5)? == 0. {
            res = self.f_res(true, 0.)?;
            self.action(Op::CheckCombat, &[])?;
        }
        if self.skill_available(LIBERATION)? {
            if res {
                self.sleep(0.2, true)?;
            }
        } else if action {
            return Ok(true);
        }
        if self.f_local(3)? == 0. {
            self.f_hold(true, || {
                let start = self.now()?;
                while self.now()? - start < 1.5 {
                    if self.feature("forte_cartethyia_sword1", 0.9, "")? {
                        break;
                    }
                    self.action(Op::NextFrame, &[])?;
                }
                Ok(())
            })?;
            self.action(Op::CheckCombat, &[])?;
        }
        if self.f_local(3)? == 0. && self.f_local(4)? == 0. && self.f_local(5)? == 0. {
            self.f_set(1, 1.)?;
        }
        Ok(!self.skill_available(LIBERATION)?)
    }
    pub(super) fn cart_perform(&self) -> ActionResult {
        self.f_set(2, 0.)?;
        if self.state(INTRO)? != 0. {
            self.f_normal(1.2)?;
        } else {
            self.f_echo(0.)?;
        }
        if self.cart_small()? {
            self.set(
                INTRO_MOTION,
                if self.f_local(0)? != 0. { 0.6 } else { 0.78 },
            )?;
            self.f_down(true)?;
            if self.cart_acquire()? {
                return self.f_switch();
            }
            self.action(Op::CheckCombat, &[])?;
            self.cart_mid_air()?;
            self.action(Op::CheckCombat, &[])?;
            if self.f_lib(false)? {
                self.f_set(0, 0.)?;
                self.set(LAST_RESONANCE, -1.)?;
                self.f_set(2, 1.)?;
            } else if !self.cart_small()? {
                self.f_set(2, 1.)?;
            }
        }
        if !self.cart_big_res()? {
            let timeout = if self.cart_small()? {
                1.1
            } else {
                self.cart_n4()?
            };
            let start = self.now()?;
            while self.now()? - start < timeout {
                if self.cart_big_lib()? {
                    return self.f_switch();
                }
                self.click(false, 0.1)?;
                self.action(Op::CheckCombat, &[])?;
                self.action(Op::NextFrame, &[])?;
            }
            self.f_set(7, self.now()?)?;
        }
        self.cart_big_lib()?;
        self.f_switch()
    }
    pub(super) fn cart_combat_end(&self) -> ActionResult {
        if self.f_local(0)? != 0. {
            return Ok(());
        }
        let count = self.value(Op::LoadParty, &[])?;
        if count < 2. {
            return Ok(());
        }
        let index = self.state(INDEX)?;
        let next = (index as i32 + 1) % (count as i32);
        let start = self.now()?;
        while self.now()? - start < 6. {
            let current = self.value(Op::PartyIndex, &[])?;
            if current >= 0. && current != index {
                break;
            }
            self.action(Op::TapSlot, &[next as f64])?;
            self.sleep(0.2, false)?;
        }
        Ok(())
    }
}
