use super::*;

// Slots retain the original reset scope: waiting, drop time, last gauge, delta sum.
impl Host {
    fn camellya_gauge(&self, budding: bool) -> ActionResult<f64> {
        let color = if budding {
            [168., 161., 220., 225., 213., 255.]
        } else {
            [127., 46., 193., 163., 93., 255.]
        };
        let value = self.observe(
            15,
            "",
            "",
            &[
                2., 3840., 2160., 1630., 2002., 546., 2., 1., color[0], color[1], color[2],
                color[3], color[4], color[5],
            ],
        )?;
        Ok(((value.abs() * 1000. + 0.5).floor() / 1000.).copysign(value))
    }
    fn camellya_ephemeral(&self) -> ActionResult<bool> {
        Ok(self.observe(
            15,
            "",
            "",
            &[
                1., 2560., 1440., 2110., 1236., 107., 107., 1., 168., 71., 234., 179., 82., 245.,
                0.395, 0.496, 1.,
            ],
        )? > 0.1)
    }
    fn camellya_cast(&self) -> ActionResult {
        self.action(Op::CheckCombat, &[])?;
        let start = self.now()?;
        while self.camellya_ephemeral()? {
            if self.now()? - start > 8. {
                self.action(Op::Interrupted, &[23.])?;
                return Err(HOST_ERROR);
            }
            self.send(RESONANCE, 0.)?;
            self.sleep(0.1, true)?;
        }
        self.sleep(1.1, true)
    }
    fn camellya_target(&self, held: bool) -> ActionResult {
        if !self.truth(Op::TargetLocked, &[])? {
            if held {
                self.action(Op::Mouse, &[0.])?;
            }
            self.action(Op::CheckCombat, &[])?;
            self.action(Op::NextFrame, &[])?;
            if held {
                self.action(Op::Mouse, &[1.])?;
            }
        }
        Ok(())
    }
    fn camellya_retry(&self, budding: bool) -> ActionResult<i32> {
        let current = self.camellya_gauge(budding)?;
        if current < 0. {
            return Ok(-1);
        }
        let diff = self.s_local(2)? - current;
        if self.s_local(0)? == 0. && (0. ..=0.01).contains(&diff) {
            self.s_set(0, 1.)?;
            self.s_set(1, self.now()?)?;
            self.s_set(3, 0.)?;
        }
        if self.s_local(0)? != 0. {
            self.s_set(3, self.s_local(3)? + diff)?;
            if self.s_local(3)? > 0.01 {
                self.s_set(0, 0.)?;
            }
        }
        if self.s_local(0)? != 0. && self.elapsed(self.s_local(1)?)? > 0.6 {
            self.s_set(0, 0.)?;
            self.action(Op::Mouse, &[0.])?;
            self.sleep(0.1, false)?;
            self.action(Op::Mouse, &[1.])?;
            self.sleep(0.1, false)?;
        }
        self.s_set(2, current)?;
        Ok(0)
    }
    fn camellya_heavy(&self, duration: f64, until_full: bool) -> ActionResult {
        self.s_set(2, 0.)?;
        let mut frozen = false;
        let mut frozen_at = 0.;
        self.s_hold(true, || {
            let start = self.now()?;
            while self.now()? - start < duration {
                if until_full && self.is_con_full()? {
                    break;
                }
                let forte = self.camellya_gauge(false)?;
                if (0. ..=0.01).contains(&forte) {
                    break;
                }
                self.camellya_target(true)?;
                self.action(Op::NextFrame, &[])?;
                if frozen && self.now()? - frozen_at >= 0.2 {
                    frozen = false;
                }
                if !frozen && self.camellya_retry(false)? < 0 {
                    frozen = true;
                    frozen_at = self.now()?;
                }
            }
            self.sleep(0.1, false)
        })?;
        self.s_set(0, 0.)
    }
    pub(super) fn camellya_perform(&self) -> ActionResult {
        if self.state(INTRO)? != 0. {
            self.s_normal(1.2)?;
            self.sleep(0.1, true)?;
            self.camellya_heavy(4.6, true)?;
        }
        if self.skill_available(LIBERATION)? {
            self.virtual_action(&[VIRTUAL_LIBERATION, 0.82, 0., 0.1, 0., 1.])?;
        }
        let con = self.current_con()?;
        let mut duration = if con < 0.82 { 1.1 } else { 4.6 };
        if con < 0.82 && self.skill_available(RESONANCE)? {
            self.s_res(true)?;
        }
        let mut start = self.now()?;
        let mut frozen_at = 0.;
        let deadline = start + 30.;
        let (mut budding, mut frozen, mut held) = (false, false, false);
        self.s_set(2, 0.)?;
        let result = (|| {
            while self.now()? - start < duration || self.feature("camellya_budding", 0.7, "")? {
                if self.now()? > deadline {
                    self.action(Op::Interrupted, &[24.])?;
                    return Err(HOST_ERROR);
                }
                if !budding {
                    if self.camellya_ephemeral()? && self.is_con_full()? {
                        self.camellya_cast()?;
                        budding = true;
                    } else {
                        self.click(false, 0.1)?;
                        if self.current_con()? < 0.82 {
                            if !self.is_con_full()? {
                                self.s_echo(1.)?;
                                self.s_switch()?;
                                return Ok(());
                            } else if duration < 3.1 {
                                duration += 1.;
                            }
                        }
                    }
                    if budding {
                        self.camellya_target(false)?;
                        start = self.now()?;
                        duration = 5.1;
                    }
                }
                if budding {
                    if !held {
                        self.action(Op::Mouse, &[1.])?;
                        held = true;
                    }
                    if self.now()? - start < 1.5
                        && self.skill_available(LIBERATION)?
                        && self.s_lib(false)?
                    {
                        self.action(Op::Mouse, &[0.])?;
                        self.sleep(0.2, false)?;
                        self.action(Op::Mouse, &[1.])?;
                    }
                }
                self.camellya_target(held)?;
                self.action(Op::NextFrame, &[])?;
                if frozen && self.now()? - frozen_at >= 0.2 {
                    frozen = false;
                }
                if !frozen && held && self.camellya_retry(budding)? < 0 {
                    frozen = true;
                    frozen_at = self.now()?;
                }
            }
            self.s_set(0, 0.)?;
            if held {
                held = false;
                self.action(Op::Mouse, &[0.])?;
                self.sleep(0.1, true)?;
            }
            if budding {
                self.s_res(true)?;
                self.sleep(0.1, true)?;
            }
            self.s_echo(1.)?;
            self.s_switch()
        })();
        if held {
            let _ = self.action(Op::Mouse, &[0.]);
        }
        result
    }
}
