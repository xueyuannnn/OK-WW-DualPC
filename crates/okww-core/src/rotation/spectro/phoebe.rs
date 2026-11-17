use super::*;

#[derive(PartialEq, Eq)]
enum Prayer {
    Success,
    Unavailable,
    Timeout,
}
impl Host {
    fn phoebe_star_ratio(&self, blue: bool) -> ActionResult<f64> {
        let c = if blue {
            [240., 240., 240., 255., 255., 255.]
        } else {
            [160., 220., 235., 190., 250., 255.]
        };
        self.observe(
            15,
            "",
            "",
            &[
                0., 3840., 2160., 1890., 2010., 25., 20., 1., c[0], c[1], c[2], c[3], c[4], c[5],
            ],
        )
    }
    fn phoebe_middle(&self) -> ActionResult<bool> {
        if self.s_local(6)? != 0. {
            return Ok(true);
        }
        if self.s_local(0)? > 0. && self.phoebe_star_ratio(self.s_local(0)? == 2.)? > 0.25 {
            self.s_set(6, 1.)?;
            return Ok(true);
        }
        Ok(false)
    }
    fn phoebe_confession(&self) -> ActionResult<bool> {
        Ok(self.observe(
            15,
            "",
            "",
            &[
                1., 2560., 1440., 2110., 1236., 107., 107., 0., 250., 176., 124., 255., 186., 134.,
                0.425, 0.490, 0.,
            ],
        )? > 0.15)
    }
    fn phoebe_condition(&self) -> ActionResult<i32> {
        Ok(if !self.phoebe_middle()? {
            0
        } else if self.phoebe_confession()? {
            1
        } else {
            2
        })
    }
    fn phoebe_check(&self, condition: i32) -> ActionResult<bool> {
        match condition {
            0 => self.s_forte(),
            1 => self.phoebe_confession(),
            _ => Ok(false),
        }
    }
    fn phoebe_land(&self) -> ActionResult {
        self.wait_with(
            || Ok(!self.s_flying()?),
            2.,
            0.,
            || {
                self.click(false, 0.1)?;
                self.action(Op::Sleep, &[0.1])
            },
            || Ok(()),
        )?;
        Ok(())
    }
    fn phoebe_prayer(&self) -> ActionResult<Prayer> {
        self.wait_for_team(3., false, true)?;
        let condition = self.phoebe_condition()?;
        if !self.phoebe_check(condition)? {
            return Ok(Prayer::Unavailable);
        }
        let mut start = self.now()?;
        let deadline = start + 10.;
        while self.phoebe_check(condition)? {
            if self.now()? - start > 2. || self.now()? > deadline {
                return Ok(Prayer::Timeout);
            }
            let mouse = self.s_local(0)? != 2.;
            if !mouse {
                self.action(Op::CaptureKey, &[RESONANCE])?;
            }
            self.s_hold(mouse, || {
                let down = self.now()?;
                while self.phoebe_check(condition)? || self.now()? - down < 0.4 {
                    if self.now()? - down > 1. {
                        break;
                    }
                    self.action(Op::NextFrame, &[])?;
                }
                Ok(())
            })?;
            if self.s_flying()? {
                self.phoebe_land()?;
                start = self.now()?;
            }
            self.action(Op::NextFrame, &[])?;
        }
        self.s_right(0.05)?;
        self.s_set(6, 1.)?;
        if self.s_local(0)? == 2. {
            for i in 1..=5 {
                self.s_set(i, 0.)?;
            }
        }
        self.s_set(1, self.s_local(1)? + 1.)?;
        Ok(Prayer::Success)
    }
    fn phoebe_heavy(&self) -> ActionResult<bool> {
        if self.phoebe_prayer()? != Prayer::Unavailable {
            return Ok(false);
        }
        let mut start = self.now()?;
        let deadline = start + 10.;
        while self.s_forte()? {
            if self.now()? - start > 2. || self.now()? > deadline {
                return Ok(false);
            }
            let (mut flew, mut consumed) = (false, false);
            self.s_hold(true, || {
                let down = self.now()?;
                while self.now()? - down < 0.5 {
                    if !self.s_forte()? {
                        consumed = true;
                        return Ok(());
                    }
                    flew = self.s_flying()?;
                    if flew {
                        break;
                    }
                    self.action(Op::NextFrame, &[])?;
                }
                Ok(())
            })?;
            if consumed {
                return Ok(true);
            }
            if flew {
                self.phoebe_land()?;
                start = self.now()?;
            }
            self.action(Op::CheckCombat, &[])?;
            self.action(Op::NextFrame, &[])?;
        }
        Ok(true)
    }
    fn phoebe_gauge(&self) -> ActionResult<f64> {
        let (c, segments, min, max, amplitude) = if self.s_local(0)? == 1. {
            ([165., 240., 240., 195., 255., 255.], 4., 9., 11., 25.)
        } else {
            ([190., 225., 225., 225., 255., 255.], 2., 18., 20., 50.)
        };
        self.observe(
            15,
            "",
            "",
            &[
                3., 3840., 2160., 1633., 2004., 527., 10., 1., c[0], c[1], c[2], c[3], c[4], c[5],
                segments, min, max, amplitude,
            ],
        )
    }
    fn phoebe_starflash(&self) -> ActionResult {
        let start = self.now()?;
        let mut check = start;
        let condition = self.phoebe_condition()?;
        if !self.phoebe_check(condition)? && !self.s_forte()? {
            while !self.s_forte()? {
                if self.s_flying()? {
                    self.s_dodge()?;
                }
                self.click(false, -1.)?;
                if self.now()? - start > 5. {
                    return Ok(());
                }
                if self.now()? - check > 1. {
                    if self.phoebe_check(condition)? || self.phoebe_gauge()? == 0. {
                        return Ok(());
                    }
                } else {
                    check = self.now()?;
                }
                self.action(Op::CheckCombat, &[])?;
                self.action(Op::NextFrame, &[])?;
            }
            self.s_right(0.05)?;
        }
        if self.phoebe_heavy()? {
            self.s_set(2, self.s_local(2)? + 1.)?;
        }
        Ok(())
    }
    fn phoebe_remaining(&self, liber: bool) -> ActionResult {
        let attribute = self.s_local(0)?;
        if attribute != 1. && attribute != 2. {
            return Ok(());
        }
        for _ in 0..if attribute == 1. { 4 } else { 2 } {
            if liber
                && self.s_local(3)? < 1.
                && self.skill_available(LIBERATION)?
                && self.s_lib(false)?
            {
                self.s_set(3, self.s_local(3)? + 1.)?;
            }
            if self.phoebe_gauge()? > 0. {
                self.phoebe_starflash()?;
                self.action(Op::NextFrame, &[])?;
            }
        }
        Ok(())
    }
    fn phoebe_mismatch(&self) -> ActionResult<bool> {
        let light = self.phoebe_star_ratio(false)?;
        let blue = self.phoebe_star_ratio(true)?;
        if light <= 0.25 && blue <= 0.25 {
            self.s_set(6, 0.)?;
            return Ok(false);
        }
        let attribute = if light > blue { 1. } else { 2. };
        if attribute != self.s_local(0)? {
            let previous = self.s_local(0)?;
            self.s_set(0, attribute)?;
            let result = self.phoebe_remaining(false);
            if result.is_err() {
                let _ = self.s_set(0, previous);
            }
            result?;
            self.s_set(0, previous)?;
            return Ok(true);
        }
        Ok(false)
    }
    pub(super) fn phoebe_lib_now(&self) -> ActionResult<bool> {
        if self.s_local(6)? != 0.
            && !self.s_flying()?
            && self.skill_available(LIBERATION)?
            && self.s_lib(true)?
        {
            self.s_set(3, self.s_local(3)? + 1.)?;
            self.s_set(5, 1.)?;
            self.action(Op::CheckCombat, &[])?;
            return Ok(true);
        }
        Ok(false)
    }
    fn phoebe_zani_phase(&self) -> ActionResult<f64> {
        if self.s_local(0)? == 2. && self.truth(Op::Related, &[0.])? {
            self.value(Op::Related, &[5.])
        } else {
            Ok(-1.)
        }
    }
    fn phoebe_linkage(&self) -> ActionResult<bool> {
        let phase = self.phoebe_zani_phase()?;
        if self.value(Op::Related, &[6.])? >= 0.9 {
            if !self.skill_available(RESONANCE)? {
                if phase == 0. || self.value(Op::Related, &[7.])? > 3. {
                    self.normal_attacks(1., 0.15, 0., false, false)?;
                }
            } else if self.s_local(7)? != 0. && !self.phoebe_confession()? {
                self.s_res(false)?;
            }
            return Ok(true);
        }
        if phase == 1. {
            self.phoebe_remaining(true)?;
            return Ok(true);
        }
        Ok(false)
    }
    fn phoebe_first_con(&self) -> ActionResult {
        if self.s_local(7)? == 0. {
            self.s_set(7, 1.)?;
            if !self.is_con_full()? {
                self.normal_attacks(5., 0.1, 0., false, true)?;
            }
        }
        Ok(())
    }
    fn phoebe_once_res(&self) -> ActionResult {
        let start = self.now()?;
        while self.skill_available(RESONANCE)? {
            self.action(Op::CheckCombat, &[])?;
            if self.now()? - start > 0.5 {
                return Ok(());
            }
            self.send(RESONANCE, 0.)?;
            self.action(Op::NextFrame, &[])?;
        }
        Ok(())
    }
    fn phoebe_decide(&self) -> ActionResult {
        let found = self.observe(12, "Zani", "", &[])? != 0.;
        self.action(Op::SetRelated, &[1.])?;
        let blue = found
            || (self.observe(6, "Cartethyia", "", &[])? != 0.
                && self.observe(6, "Rover", "", &[])? != 0.);
        self.s_set(0, if blue { 2. } else { 1. })
    }
    pub(super) fn phoebe_perform(&self) -> ActionResult {
        self.set(21., -1.)?;
        let start = self.now()?;
        if self.s_local(0)? == 0. {
            self.phoebe_decide()?;
        }
        if self.state(INTRO)? != 0. {
            self.s_normal(1.5)?;
        } else {
            self.sleep(0.01, true)?;
        }
        self.phoebe_lib_now()?;
        if self.s_local(0)? == 1. {
            self.s_echo(0.)?;
        }
        if self.s_flying()? {
            self.s_normal(0.1)?;
            return self.s_switch();
        }
        let wrong = self.phoebe_mismatch()?;
        if self.s_local(0)? == 2. && self.truth(Op::Related, &[0.])? {
            if self.s_local(6)? == 0. {
                self.phoebe_prayer()?;
            }
            if self.phoebe_linkage()? {
                return self.s_switch();
            }
        }
        let ui_wait = 0.35 - (self.now()? - start);
        if ui_wait > 0. && self.s_local(6)? != 0. && self.phoebe_gauge()? == 0. {
            self.s_normal(ui_wait)?;
        }
        let entered = self.phoebe_prayer()?;
        self.action(Op::CheckCombat, &[])?;
        if (!wrong || entered == Prayer::Success) && self.s_local(6)? != 0. && self.s_lib(true)? {
            self.s_set(3, self.s_local(3)? + 1.)?;
            self.s_set(5, 1.)?;
            self.action(Op::CheckCombat, &[])?;
        }
        if entered == Prayer::Success || self.phoebe_gauge()? > 0. {
            self.phoebe_starflash()?;
            self.phoebe_lib_now()?;
            if self.s_local(0)? == 2. && self.s_local(2)? < 2. && self.phoebe_zani_phase()? != 1. {
                self.phoebe_starflash()?;
            }
        }
        self.phoebe_lib_now()?;
        if self.skill_available(RESONANCE)? {
            if self.s_local(0)? == 2. {
                if !self.phoebe_confession()? && self.s_local(7)? != 0. {
                    self.phoebe_once_res()?;
                }
            } else {
                self.s_res(true)?;
            }
            self.phoebe_first_con()?;
            return self.s_switch();
        }
        self.s_normal(0.1)?;
        self.phoebe_first_con()?;
        self.s_switch()
    }
}
