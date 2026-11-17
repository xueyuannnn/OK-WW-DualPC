use super::*;

impl Host {
    pub(super) fn rover_role(&self) -> ActionResult {
        let element = self.state(ELEMENT)?;
        let form = if element >= 0. {
            element
        } else {
            self.f_local(0)?
        };
        self.set(
            ROLE,
            if form == -1. || form == 1. {
                1.
            } else {
                self.f_local(1)?
            },
        )
    }
    fn rover_electric_ready(&self) -> ActionResult<bool> {
        Ok(self.f_forte()? && self.truth(Op::Forte, &[1.])?)
    }
    fn rover_electric_short(&self) -> ActionResult<bool> {
        for _ in 0..2 {
            if !self.rover_electric_ready()? {
                return Ok(false);
            }
            let start = self.now()?;
            self.action(Op::Send, &[RESONANCE, 0., -1., 0.05])?;
            if self.wait_with(
                || {
                    Ok(self.elapsed(start)? >= 1.5
                        && !self.f_forte()?
                        && !self.truth(Op::Forte, &[1.])?)
                },
                2.6,
                0.2,
                || Ok(()),
                || self.action(Op::CheckCombat, &[]),
            )? {
                self.record(RESONANCE)?;
                return Ok(true);
            }
        }
        Ok(false)
    }
    fn rover_electric_lib(&self) -> ActionResult<bool> {
        Ok(self.virtual_action(&[VIRTUAL_LIBERATION, -1., 0., 0.1, 0., 0.])?[0] != 0.)
    }
    fn rover_electric(&self) -> ActionResult {
        self.f_down(false)?;
        self.f_echo(1.)?;
        self.rover_electric_lib()?;
        if !self.rover_electric_ready()?
            && self.skill_available(RESONANCE)?
            && self.f_res(false, 1.)?
        {
            self.sleep(0.1, true)?;
        }
        let start = self.now()?;
        let duration = if self.state(INTRO)? != 0. { 8. } else { 12. };
        while !self.rover_electric_ready()? && self.elapsed(start)? < duration {
            self.click(false, -1.)?;
            self.sleep(0.1, true)?;
        }
        if !self.rover_electric_ready()? || !self.rover_electric_short()? {
            return Ok(());
        }
        if !self.is_con_full()? {
            self.rover_electric_lib()?;
        }
        if !self.is_con_full()? {
            self.normal_attacks(2., 0.1, 0., false, true)?;
        }
        Ok(())
    }
    fn rover_wind_flying(&self) -> ActionResult<bool> {
        if self.flag(LEVITATOR)? {
            self.f_flying()
        } else {
            Ok(self.value(Op::Highlighted, &[RESONANCE])? > 0.15)
        }
    }
    fn rover_wind_click(&self, duration: f64) -> ActionResult<bool> {
        let start = self.now()?;
        while self.now()? - start < duration {
            if !self.rover_wind_flying()? {
                return Ok(false);
            }
            self.click(false, 0.1)?;
            self.sleep(0.1, true)?;
        }
        Ok(true)
    }
    fn rover_wind_down(&self, check_forte: bool) -> ActionResult {
        if self.rover_wind_flying()? {
            if self.flag(LEVITATOR)? {
                self.f_down(true)?;
            } else {
                self.wait_with(
                    || Ok(self.value(Op::Highlighted, &[RESONANCE])? < 0.15),
                    2.5,
                    0.,
                    || {
                        self.click(false, 0.1)?;
                        self.action(Op::Sleep, &[0.01])
                    },
                    || Ok(()),
                )?;
            }
        }
        if check_forte {
            self.sleep(0.03, true)?;
            if self.f_forte()? {
                self.send(RESONANCE, 0.)?;
            }
        } else {
            self.sleep(0.01, true)?;
        }
        Ok(())
    }
    fn rover_wind(&self) -> ActionResult {
        if !(self.state(INTRO)? != 0. && self.rover_wind_click(2.)?) {
            self.rover_wind_down(false)?;
            if self.skill_available(RESONANCE)? && !self.f_forte()? {
                self.f_echo(0.)?;
                let start = self.now()?;
                let mut flew = false;
                while self.now()? - start < 1. {
                    self.action(Op::Send, &[RESONANCE, 0., 0.1, 0.01])?;
                    self.action(Op::NextFrame, &[])?;
                    self.click(false, 0.1)?;
                    flew = self.rover_wind_flying()?;
                    if flew {
                        break;
                    }
                }
                if flew {
                    self.rover_wind_click(if self.f_local(2)? != 0. { 1.6 } else { 1.74 })?;
                }
                if self.f_local(2)? != 0. && self.f_res(false, 0.)? {
                    self.rover_wind_click(1.)?;
                }
            }
        }
        self.f_lib(true)?;
        self.rover_wind_down(true)
    }
    fn rover_spectro(&self) -> ActionResult {
        if self.state(INTRO)? != 0. {
            self.f_normal(1.)?;
        }
        self.f_down(true)?;
        self.heavy_attack(0.6)?;
        self.sleep(0.4, true)?;
        self.f_normal(0.7)?;
        self.f_echo(0.)?;
        if self.f_forte()? {
            self.action(Op::CheckCombat, &[])?;
            if self.skill_available(RESONANCE)? && self.f_res(true, 0.)? {
                self.f_normal(1.4)?;
                self.sleep(0.1, true)?;
            }
        }
        self.action(Op::CheckCombat, &[])?;
        if !self.f_lib(true)? {
            self.f_res(true, 0.)?;
        }
        Ok(())
    }
    fn rover_havoc(&self) -> ActionResult {
        self.f_down(true)?;
        self.f_heavy_forte()?;
        self.f_lib(true)?;
        if self.f_res(true, 0.)? {
            return Ok(());
        }
        if !self.f_echo(1.)? {
            self.click(false, -1.)?;
        }
        self.f_normal(1.1 - self.elapsed(self.state(20.)?)?)
    }
    fn rover_basic(&self) -> ActionResult {
        if self.state(INTRO)? != 0. {
            self.f_normal(self.state(INTRO_MOTION)? + 0.2)?;
        }
        self.f_down(true)?;
        self.f_echo(1.)?;
        let lib = self.f_lib(true)?;
        let res = self.f_res(true, 0.)?;
        if !lib && !res {
            self.f_normal(1.)?;
        }
        Ok(())
    }
    pub(super) fn rover_perform(&self) -> ActionResult {
        self.action(Op::EnsureElement, &[])?;
        self.rover_role()?;
        if self.state(ELEMENT)? == 4. {
            self.f_set(
                2,
                (self.observe(6, "Cartethyia", "", &[])? != 0.
                    && self.observe(6, "Phoebe", "", &[])? != 0.) as u8 as f64,
            )?;
        }
        if self.state(INTRO)? == 0. {
            self.sleep(0.01, true)?;
        }
        match self.state(ELEMENT)? as i32 {
            5 => {
                self.set(INTRO_MOTION, 0.64)?;
                self.rover_havoc()?;
            }
            0 => {
                self.set(INTRO_MOTION, 0.92)?;
                self.rover_spectro()?;
            }
            4 => {
                self.set(INTRO_MOTION, 0.52)?;
                self.rover_wind()?;
            }
            1 => self.rover_electric()?,
            _ => self.rover_basic()?,
        }
        self.f_switch()
    }
}
