use super::*;
mod camellya;
mod phoebe;
mod zani;

impl Host {
    fn s_res(&self, click: bool) -> ActionResult<bool> {
        Ok(
            self.virtual_action(&[VIRTUAL_RESONANCE, 0., 0., click as u8 as f64, 0., 0., 1.])?[0]
                != 0.,
        )
    }
    fn s_lib(&self, click: bool) -> ActionResult<bool> {
        Ok(
            self.virtual_action(&[VIRTUAL_LIBERATION, -1., click as u8 as f64, 0.1, 0., 1.])?[0]
                != 0.,
        )
    }
    fn s_echo(&self, timeout: f64) -> ActionResult<bool> {
        Ok(self.virtual_action(&[VIRTUAL_ECHO, 0., 0., timeout])?[0] != 0.)
    }
    fn s_normal(&self, duration: f64) -> ActionResult {
        self.normal_attacks(duration, 0.1, 0., false, false)
    }
    fn s_switch(&self) -> ActionResult {
        self.virtual_action(&[VIRTUAL_SWITCH, 0., 0.])?;
        Ok(())
    }
    fn s_flying(&self) -> ActionResult<bool> {
        Ok(self.virtual_action(&[VIRTUAL_FLYING])?[0] != 0.)
    }
    fn s_down(&self) -> ActionResult {
        self.run(Method::WaitDown as i32, &[1.])?;
        Ok(())
    }
    fn s_forte(&self) -> ActionResult<bool> {
        Ok(self.virtual_action(&[VIRTUAL_FORTE])?[0] != 0.)
    }
    fn s_mouse(&self) -> ActionResult<bool> {
        self.truth(Op::Forte, &[0.])
    }
    fn s_local(&self, index: usize) -> ActionResult<f64> {
        self.value(Op::LocalAt, &[index as f64])
    }
    fn s_set(&self, index: usize, value: f64) -> ActionResult {
        self.action(Op::SetLocalAt, &[index as f64, value])
    }
    fn s_elapsed(&self, start: f64, intro: bool) -> ActionResult<f64> {
        self.value(Op::Elapsed, &[start, intro as u8 as f64])
    }
    fn s_dodge(&self) -> ActionResult {
        self.action(Op::KeeperDodgeQuery, &[])
    }
    fn s_right(&self, duration: f64) -> ActionResult {
        self.run(Method::RightClicks as i32, &[duration, 0.1, 0.])?;
        Ok(())
    }
    fn s_base_priority(&self) -> ActionResult<f64> {
        Ok(if self.truth(Op::HealerLocked, &[])? {
            0.
        } else {
            200.
        })
    }
    fn s_hold<T>(&self, mouse: bool, body: impl FnOnce() -> ActionResult<T>) -> ActionResult<T> {
        let op = if mouse { Op::Mouse } else { Op::Key };
        self.action(op, &[1.])?;
        let result = body();
        if result.is_err() {
            let _ = self.action(op, &[0.]);
        }
        let value = result?;
        self.action(op, &[0.])?;
        Ok(value)
    }
    pub(super) fn spectro_perform(&self, kind: i32) -> ActionResult {
        match kind {
            0 => self.camellya_perform(),
            1 => self.phoebe_perform(),
            2 => self.zani_perform(),
            _ => Err(INVALID_ACTION),
        }
    }
    pub(super) fn spectro_priority(&self, a: &[f64]) -> ActionResult<f64> {
        match a[0] as i32 {
            0 => {
                if a[1] != 0. {
                    Ok(400.)
                } else {
                    self.s_base_priority()
                }
            }
            1 => {
                if a[1] == 0.
                    && self.state(21.)? > 0.
                    && self.s_elapsed(self.state(21.)?, true)? < 4.5
                {
                    Ok(0.)
                } else {
                    self.s_base_priority()
                }
            }
            2 => {
                if self.s_local(8)? != 0. {
                    Ok(400.)
                } else if a[1] != 0. && self.zani_crisis_left()? > 0. {
                    Ok(0.)
                } else {
                    self.s_base_priority()
                }
            }
            _ => Err(INVALID_ACTION),
        }
    }
    pub(super) fn spectro_lifecycle(&self, a: &[f64]) -> ActionResult<f64> {
        match (a[0] as i32, a[1] as i32) {
            (0, 0) => {
                self.run(Method::ResetState as i32, &[])?;
                self.s_set(0, 0.)?;
            }
            (0, 1) => {
                if !self.skill_available(ECHO)? {
                    return Ok(0.);
                }
                self.send(ECHO, 0.)?;
                return Ok(1.);
            }
            (0, 2) => {
                self.run(Method::SwitchOther as i32, &[0.])?;
            }
            (1, 0) => {
                self.run(Method::ResetState as i32, &[])?;
                self.s_set(0, 0.)?;
                self.s_set(6, 0.)?;
                self.action(Op::SetRelated, &[0.])?;
                self.s_set(7, 0.)?;
            }
            (1, 1) => {
                return Ok(if self.s_local(6)? != 0. {
                    self.s_mouse()?
                } else {
                    self.truth(Op::Forte, &[2.])?
                } as u8 as f64)
            }
            (1, 2) => return self.s_local(4),
            (1, 3) => {
                if self.s_local(0)? == 2. && self.s_local(5)? == 0. {
                    self.phoebe_lib_now()?;
                }
                if self.s_local(0)? == 2. && self.is_con_full()? {
                    self.s_echo(1.)?;
                    self.s_set(4, self.s_local(4)? + 1.)?;
                }
                self.action(Op::BaseSwitch, &[a[2], a[3]])?;
            }
            (2, 0) => {
                self.action(Op::SetRelated, &[0.])?;
                self.s_set(2, -1.)?;
                self.s_set(5, -1.)?;
                self.run(Method::ResetState as i32, &[])?;
            }
            (2, 1) => return self.s_local(1),
            (2, 2) => return self.zani_time_left(),
            (2, 3) => {
                if self.s_local(9)? == 1. && self.zani_time_left()? <= 0. {
                    self.s_set(1, -1.)?;
                    self.s_set(9, 0.)?;
                }
                return self.s_local(9);
            }
            (2, 4) => return Ok(self.zani_partner_ready()? as u8 as f64),
            (2, 5) => {
                return Ok((self.state(INTRO)? != 0.
                    && self.zani_nightfall_left()? > 0.
                    && self.zani_time_left()? >= 2.) as u8 as f64)
            }
            (2, 6) => {
                self.set(INTRO_MOTION, 1.42)?;
                self.set(19., 0.)?;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok(0.)
    }
}
