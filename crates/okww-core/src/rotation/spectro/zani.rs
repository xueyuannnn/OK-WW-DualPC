use super::*;

#[derive(Clone, Copy, PartialEq, Eq)]
enum Outcome {
    Done,
    ForteFull,
    Failed,
    Interrupted,
}
#[derive(PartialEq, Eq)]
enum WaitResult {
    False,
    True,
    Interrupted,
}
// Slots: liberation time, blazes, threshold, crisis, nightfall, chair,
// last second liberation, dodge, liberated, phase. Reset deliberately only
// clears threshold/chair/partner, exactly as in the original implementation.
impl Host {
    pub(super) fn zani_crisis_left(&self) -> ActionResult<f64> {
        let t = self.s_local(3)?;
        Ok(if t <= 0. {
            0.
        } else {
            1.6 - self.s_elapsed(t, true)?
        })
    }
    pub(super) fn zani_nightfall_left(&self) -> ActionResult<f64> {
        let t = self.s_local(4)?;
        Ok(if t <= 0. {
            0.
        } else {
            2.2 - self.s_elapsed(t, true)?
        })
    }
    pub(super) fn zani_time_left(&self) -> ActionResult<f64> {
        Ok(if self.s_local(8)? == 0. || self.s_local(0)? <= 0. {
            0.
        } else {
            20. - self.elapsed(self.s_local(0)?)?
        })
    }
    fn zani_not_lib(&self) -> ActionResult<bool> {
        Ok(self.observe(
            16,
            "box_target_enemy_inner",
            "",
            &[2560., 1440., 1909., 1274., 48., 48., 0.75],
        )? != 0.)
    }
    fn zani_check_lib(&self) -> ActionResult<bool> {
        if !self.truth(Op::InTeam, &[])? {
            return Ok(self.s_local(8)? != 0.);
        }
        if self.zani_not_lib()? {
            self.s_set(8, 0.)?;
        } else if self.observe(
            16,
            "box_target_enemy_inner",
            "",
            &[2560., 1440., 1779., 1273., 51., 49., 0.75],
        )? != 0.
        {
            self.s_set(8, 1.)?;
        }
        Ok(self.s_local(8)? != 0.)
    }
    fn zani_blazes(&self) -> ActionResult {
        let ratio = self.observe(
            15,
            "",
            "",
            &[
                0., 3840., 2160., 1627., 2014., 549., 3., 1., 171., 239., 231., 201., 255., 257.,
            ],
        )?;
        self.s_set(1, (ratio * 100.).ceil() / 100.)
    }
    fn zani_gauge(&self) -> ActionResult<f64> {
        let value = self.observe(
            15,
            "",
            "",
            &[
                0., 3840., 2160., 1628., 1997., 555., 6., 1., 156., 222., 239., 196., 255., 255.,
            ],
        )?;
        Ok(((value.abs() * 1000. + 0.5).floor() / 1000.).copysign(value))
    }
    fn zani_nightfall_ready(&self, threshold: f64) -> ActionResult<bool> {
        Ok(self.observe(
            15,
            "",
            "",
            &[
                1., 2560., 1440., 1853., 1233., 111., 111., 1., 205., 245., 245., 225., 255., 255.,
                0.425, 0.490, 0.,
            ],
        )? > threshold)
    }
    fn zani_wait(
        &self,
        condition: impl Fn() -> ActionResult<bool>,
        timeout: f64,
        post: impl Fn() -> ActionResult,
        mut interrupt: impl FnMut() -> ActionResult<bool>,
        settle: f64,
    ) -> ActionResult<WaitResult> {
        if timeout <= 0. {
            return Ok(WaitResult::False);
        }
        let start = self.now()?;
        let mut stable = -1.;
        let mut once = true;
        while self.now()? - start < timeout {
            if condition()? {
                if settle == 0. {
                    return Ok(WaitResult::True);
                }
                if stable < 0. {
                    stable = self.now()?;
                } else if self.now()? - stable >= settle {
                    return Ok(WaitResult::True);
                }
            } else {
                stable = -1.;
            }
            if interrupt()? {
                return Ok(WaitResult::Interrupted);
            }
            if once {
                self.action(Op::CheckCombat, &[])?;
                once = false;
            }
            post()?;
            self.action(Op::NextFrame, &[])?;
        }
        Ok(WaitResult::False)
    }
    fn zani_wait_forte(
        &self,
        timeout: f64,
        click: bool,
        check: bool,
        settle: f64,
    ) -> ActionResult<Outcome> {
        if timeout <= 0. {
            return Ok(Outcome::Done);
        }
        let start = self.now()?;
        let (mut last_check, mut last_value) = (0., -1.);
        let result = self.zani_wait(
            || self.truth(Op::Forte, &[1.]),
            timeout,
            || {
                if click {
                    self.click(false, 0.1)
                } else {
                    Ok(())
                }
            },
            || {
                if self.s_flying()? {
                    return Ok(true);
                }
                let now = self.now()?;
                if check && now - start > 0.8 && now - last_check >= 0.2 {
                    let value = self.zani_gauge()?;
                    if last_value > 0.
                        && value - last_value < 0.01
                        && !self.truth(Op::Forte, &[1.])?
                    {
                        self.s_right(0.05)?;
                        self.s_set(7, self.now()?)?;
                        return Ok(true);
                    }
                    last_value = value;
                    last_check = now;
                }
                Ok(false)
            },
            settle,
        )?;
        Ok(match result {
            WaitResult::Interrupted => Outcome::Interrupted,
            WaitResult::True => Outcome::ForteFull,
            WaitResult::False => Outcome::Done,
        })
    }
    fn zani_wait_res(&self, click: bool, check_time: bool) -> ActionResult {
        self.zani_wait(
            || Ok(self.value(Op::Highlighted, &[RESONANCE])? != 0.),
            2.5,
            || {
                if click {
                    self.click(false, 0.1)
                } else {
                    Ok(())
                }
            },
            || Ok(check_time && self.zani_time_left()? < 1.7),
            0.1,
        )?;
        Ok(())
    }
    fn zani_wait_crisis(&self) -> ActionResult {
        if self.zani_crisis_left()? <= 0. {
            return Ok(());
        }
        if self.state(LAST_RESONANCE)? > 0. && self.elapsed(self.state(LAST_RESONANCE)?)? < 5. {
            self.zani_wait(
                || Ok(self.zani_crisis_left()? <= 0.),
                2.,
                || Ok(()),
                || Ok(false),
                0.,
            )?;
        } else {
            self.zani_wait_res(false, false)?;
        }
        Ok(())
    }
    fn zani_defense(&self) -> ActionResult<Outcome> {
        if self.truth(Op::Forte, &[1.])? {
            return Ok(Outcome::ForteFull);
        }
        if self.skill_available(RESONANCE)? {
            self.s_res(false)?;
            self.sleep(0.2, true)?;
            self.s_normal(0.2)?;
            return Ok(Outcome::Done);
        }
        Ok(Outcome::Failed)
    }
    fn zani_breakthrough(&self) -> ActionResult<Outcome> {
        let mut chair = 1.2;
        if self.s_local(5)? == -1. {
            let mut result = self.zani_defense()?;
            if result == Outcome::Failed {
                result =
                    self.zani_wait_forte(0.3 - (self.now()? - self.s_local(7)?), false, false, 0.)?;
                if result != Outcome::Done {
                    return Ok(result);
                }
                result = self.s_hold(true, || self.zani_wait_forte(0.6, false, false, 0.))?;
                if result != Outcome::Done {
                    return Ok(result);
                }
                chair = 1.15;
                result = self.zani_wait_forte(0.85, true, false, 0.)?;
                if result != Outcome::Done {
                    return Ok(result);
                }
            } else if result == Outcome::ForteFull {
                return Ok(result);
            }
        } else {
            chair -= self.now()? - self.s_local(5)?;
            self.s_set(5, -1.)?;
        }
        let result = self.zani_wait_forte(chair, false, false, 0.)?;
        if result != Outcome::Done {
            return Ok(result);
        }
        self.s_normal(0.2)?;
        Ok(result)
    }
    fn zani_crisis_combo(&self) -> ActionResult<bool> {
        self.action(Op::CheckCombat, &[])?;
        if !self.truth(Op::Forte, &[1.])? {
            let mut result = self.zani_breakthrough()?;
            if result == Outcome::Done {
                result = self.zani_wait_forte(2.2, false, true, 0.)?;
                if result == Outcome::Done {
                    self.s_right(0.05)?;
                    self.s_set(7, self.now()?)?;
                }
            }
            if result != Outcome::ForteFull && !self.truth(Op::Forte, &[1.])? {
                return Ok(false);
            }
        }
        let start = self.now()?;
        self.zani_wait(
            || Ok(!self.truth(Op::Forte, &[1.])?),
            1.,
            || self.send(RESONANCE, 0.),
            || Ok(false),
            0.,
        )?;
        if self.now()? - start < 0.35 {
            return Ok(false);
        }
        self.s_set(3, self.now()?)?;
        Ok(true)
    }
    fn zani_lib2(&self) -> ActionResult {
        let mut start = self.now()?;
        let deadline = start + 12.;
        let mut send_key = true;
        self.set_flag(IN_LIBERATION, true)?;
        let result = (|| {
            while !self.zani_not_lib()? {
                if self.now()? > deadline {
                    self.action(Op::Interrupted, &[25.])?;
                    return Err(HOST_ERROR);
                }
                if self.now()? - start > 6. {
                    self.set_flag(IN_LIBERATION, false)?;
                    if !self.zani_check_lib()? {
                        self.zani_blazes()?;
                    }
                    return Ok(());
                }
                if self.value(Op::Highlighted, &[RESONANCE])? == 0. {
                    start = self.now()?;
                } else if self.now()? - start > 1.5 {
                    send_key = false;
                }
                if send_key {
                    self.send(LIBERATION, 0.)?;
                }
                self.action(Op::NextFrame, &[])?;
            }
            self.set_flag(IN_LIBERATION, false)?;
            let now = self.now()?;
            if now - start >= 2.25 {
                self.s_set(6, now)?;
                self.action(Op::FreezeThreshold, &[now, now - 2.25, 2.25, 0.])?;
            }
            self.s_set(8, 0.)?;
            self.s_set(1, -1.)?;
            self.s_set(0, -1.)?;
            self.s_set(9, 0.)?;
            Ok(())
        })();
        if result.is_err() {
            let _ = self.set_flag(IN_LIBERATION, false);
        }
        result
    }
    fn zani_should_end(&self, time_only: bool) -> ActionResult<bool> {
        if self.zani_time_left()? < 1.7 {
            return Ok(true);
        }
        if time_only || self.zani_nightfall_ready(0.15)? {
            return Ok(false);
        }
        self.zani_wait_res(true, true)?;
        Ok(!self.s_mouse()?)
    }
    fn zani_nightfall(&self, cancel: bool) -> ActionResult {
        let mut start = self.now()?;
        if !self.zani_nightfall_ready(0.15)? {
            while !self.zani_nightfall_ready(0.15)? || self.now()? - start < 1.6 {
                self.click(false, -1.)?;
                if self.now()? - start > 3.5 || self.s_local(8)? == 0. {
                    return Ok(());
                }
                if self.zani_should_end(true)? {
                    self.zani_lib2()?;
                }
                self.action(Op::CheckCombat, &[])?;
                self.action(Op::NextFrame, &[])?;
            }
        }
        self.s_normal(0.5)?;
        if cancel {
            start = self.now()?;
            while self.zani_nightfall_ready(0.035)? {
                if self.now()? - start > 2.5 {
                    break;
                }
                self.click(false, -1.)?;
                self.action(Op::NextFrame, &[])?;
            }
            self.sleep(0.25, false)?;
            self.s_right(0.1)?;
        } else {
            self.s_set(4, self.now()?)?;
        }
        Ok(())
    }
    fn zani_try_lib(&self, crisis: bool) -> ActionResult<bool> {
        if crisis {
            self.zani_wait_crisis()?;
            self.zani_blazes()?;
        }
        if self.skill_available(ECHO)? {
            self.s_echo(0.)?;
        }
        if !self.s_lib(true)? {
            return Ok(false);
        }
        self.s_set(3, -1.)?;
        self.s_set(9, 1.)?;
        self.s_set(8, 1.)?;
        self.s_set(0, self.now()?)?;
        self.zani_check_lib()?;
        self.s_right(0.05)?;
        self.s_normal(0.15)?;
        self.zani_nightfall(true)?;
        self.sleep(0.1, true)?;
        if self.s_mouse()? {
            self.zani_nightfall(false)?;
        }
        Ok(true)
    }
    pub(super) fn zani_perform(&self) -> ActionResult {
        if self.s_local(2)? == -1. {
            let found = self.observe(12, "Phoebe", "", &[])? != 0.;
            self.action(Op::SetRelated, &[1.])?;
            self.s_set(2, if found { 0.6 } else { 0.4 })?;
        }
        self.s_down()?;
        self.zani_check_lib()?;
        if self.s_local(8)? != 0. {
            self.s_set(9, 1.)?;
            if self.zani_should_end(false)? {
                self.zani_lib2()?;
            } else {
                self.zani_nightfall(false)?;
            }
            return self.s_switch();
        }
        self.s_set(9, 0.)?;
        self.action(Op::BreakF, &[0.])?;
        self.s_set(3, -1.)?;
        self.zani_blazes()?;
        let full = self.truth(Op::Forte, &[1.])?;
        let mut available = self.value(Op::Highlighted, &[RESONANCE])? > 0.05;
        let mut lib = self.skill_available(LIBERATION)?;
        if self.state(INTRO)? != 0. && self.s_local(1)? >= 1. && !lib {
            self.sleep(0.2, false)?;
            lib = self.skill_available(LIBERATION)?;
            available = self.value(Op::Highlighted, &[RESONANCE])? > 0.05;
        }
        let predicted = self.s_local(1)? + 0.1;
        if self.s_local(1)? >= 1. && lib {
            if !self.zani_try_lib(false)? {
                self.sleep(0.1, true)?;
                self.zani_try_lib(false)?;
            }
            return self.s_switch();
        }
        if full {
            if self.zani_crisis_combo()?
                && predicted >= self.s_local(2)?
                && self.skill_available(LIBERATION)?
            {
                self.zani_try_lib(true)?;
            }
            return self.s_switch();
        }
        if available {
            if self.zani_crisis_combo()?
                && self.s_local(1)? >= self.s_local(2)?
                && self.skill_available(LIBERATION)?
            {
                self.zani_try_lib(true)?;
            }
            return self.s_switch();
        }
        self.run(Method::NormalUntilSwitch as i32, &[])?;
        self.s_switch()
    }
    pub(super) fn zani_partner_ready(&self) -> ActionResult<bool> {
        if self.state(CURRENT)? != 0. {
            self.zani_blazes()?;
        }
        Ok(self.s_local(1)? >= self.s_local(2)?
            || (self.truth(Op::Related, &[0.])?
                && self.value(Op::Related, &[8.])? >= 1.
                && self.s_local(1)? >= 0.4))
    }
}
