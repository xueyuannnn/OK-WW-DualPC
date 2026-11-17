use super::*;
mod carlotta;
mod cartethyia;
mod rover;

impl Host {
    fn elapsed_with_intro(&self, start: f64) -> ActionResult<f64> {
        self.value(Op::Elapsed, &[start, 1.])
    }
    pub(super) fn forms_perform(&self, kind: i32) -> ActionResult {
        match kind {
            0 => self.changli_perform(),
            1 => self.rover_perform(),
            2 => self.cart_perform(),
            3 => self.carlotta_perform(),
            _ => Err(INVALID_ACTION),
        }
    }
    pub(super) fn forms_priority(&self, a: &[f64]) -> ActionResult<f64> {
        match a[0] as i32 {
            0 | 3 => {
                if a[0] == 3. && self.f_local(1)? == -1. {
                    self.carlotta_decide()?;
                }
                if a[1] != 0. && a[2] != 0. && a[3] != 0. {
                    Ok(400.)
                } else {
                    self.f_base_priority()
                }
            }
            2 => {
                if self.f_local(0)? != 0. {
                    self.f_base_priority()
                } else {
                    Ok(400.)
                }
            }
            _ => Err(INVALID_ACTION),
        }
    }
    pub(super) fn forms_lifecycle(&self, a: &[f64]) -> ActionResult<f64> {
        match (a[0] as i32, a[1] as i32) {
            (0, 0) => {
                self.run(Method::ResetState as i32, &[])?;
                self.f_set(0, 0.)?;
            }
            (1, 0) => {
                let form = self.state(ELEMENT)?;
                if form >= 0. {
                    self.f_set(0, form)?;
                }
                self.set(ELEMENT, -1.)?;
                self.run(Method::ResetState as i32, &[])?;
                self.rover_role()?;
            }
            (1, 1) => {
                self.f_set(1, self.state(ROLE)?)?;
                self.rover_role()?;
            }
            (2, 1) => return self.f_local(0),
            (2, 2) => self.f_set(0, a[2])?,
            (2, 3) => return Ok(if self.f_local(0)? != 0. { 0.6 } else { 0.78 }),
            (2, 4) => self.cart_combat_end()?,
            (3, 0) => {
                self.run(Method::ResetState as i32, &[])?;
                for (i, value) in [-1., -1., 0., 0., 0.].iter().enumerate() {
                    self.f_set(i, *value)?;
                }
                self.action(Op::SetRelated, &[0.])?;
            }
            (3, 1) => return Ok(self.carlotta_partner_ready()? as u8 as f64),
            (3, 2) => {
                return Ok((!self.truth(
                    Op::HasCd,
                    &[
                        ECHO,
                        if self.state(CURRENT)? != 0. {
                            -1.
                        } else {
                            self.state(INDEX)?
                        },
                    ],
                )?) as u8 as f64)
            }
            (3, 3) => {
                return Ok((self.state(INTRO)? != 0.
                    && self.elapsed_with_intro(self.f_local(0)?)? < 2.5)
                    as u8 as f64)
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok(0.)
    }
    fn f_res(&self, click: bool, timeout: f64) -> ActionResult<bool> {
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
    fn f_lib(&self, click: bool) -> ActionResult<bool> {
        Ok(
            self.virtual_action(&[VIRTUAL_LIBERATION, -1., click as u8 as f64, 0.1, 0., 1.])?[0]
                != 0.,
        )
    }
    fn f_echo(&self, timeout: f64) -> ActionResult<bool> {
        Ok(self.virtual_action(&[VIRTUAL_ECHO, 0., 0., timeout])?[0] != 0.)
    }
    fn f_normal(&self, duration: f64) -> ActionResult {
        self.normal_attacks(duration, 0.1, 0., false, false)
    }
    fn f_switch(&self) -> ActionResult {
        self.virtual_action(&[VIRTUAL_SWITCH, 0., 0.])?;
        Ok(())
    }
    fn f_flying(&self) -> ActionResult<bool> {
        Ok(self.virtual_action(&[VIRTUAL_FLYING])?[0] != 0.)
    }
    fn f_down(&self, click: bool) -> ActionResult {
        self.run(Method::WaitDown as i32, &[click as u8 as f64])?;
        Ok(())
    }
    fn f_forte(&self) -> ActionResult<bool> {
        Ok(self.virtual_action(&[VIRTUAL_FORTE])?[0] != 0.)
    }
    fn f_mouse_forte(&self) -> ActionResult<bool> {
        self.truth(Op::Forte, &[0.])
    }
    fn f_local(&self, index: usize) -> ActionResult<f64> {
        self.value(Op::LocalAt, &[index as f64])
    }
    fn f_set(&self, index: usize, value: f64) -> ActionResult {
        self.action(Op::SetLocalAt, &[index as f64, value])
    }
    fn f_outro(&self, names: &str) -> ActionResult<bool> {
        Ok(self.observe(5, names, "", &[])? != 0.)
    }
    fn f_heavy_forte(&self) -> ActionResult<bool> {
        Ok(self.run(Method::HeavyForteKind as i32, &[0.])?[0] != 0.)
    }
    fn f_dodge(&self) -> ActionResult {
        self.action(Op::KeeperDodgeQuery, &[])
    }
    fn f_base_priority(&self) -> ActionResult<f64> {
        Ok(if self.truth(Op::HealerLocked, &[])? {
            0.
        } else {
            200.
        })
    }
    fn f_hold<T>(&self, mouse: bool, body: impl FnOnce() -> ActionResult<T>) -> ActionResult<T> {
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
    fn changli_gauge(&self) -> ActionResult<f64> {
        if self.f_mouse_forte()? {
            return Ok(4.);
        }
        self.observe(
            10,
            "",
            "",
            &[
                3840., 2160., 1633., 2004., 527., 12., 95., 85., 240., 115., 105., 255., 4., 9.,
                11., 400.,
            ],
        )
    }
    fn changli_flick(&self, timeout: f64, click: bool) -> ActionResult<bool> {
        if click && self.skill_available(RESONANCE)? {
            self.wait_with(
                || Ok(self.value(Op::Highlighted, &[RESONANCE])? > 0.),
                0.2,
                0.,
                || self.click(false, 0.1),
                || Ok(()),
            )?;
        }
        if self.value(Op::Highlighted, &[RESONANCE])? > 0. && self.skill_available(RESONANCE)? {
            self.wait_with(
                || Ok(!self.skill_available(RESONANCE)?),
                timeout,
                0.,
                || self.send(RESONANCE, 0.),
                || Ok(()),
            )?;
            return Ok(true);
        }
        Ok(false)
    }
    fn changli_lib_heavy(&self) -> ActionResult<bool> {
        let (mut start, mut last) = (self.now()?, 0.);
        let (mut clicked, mut held) = (false, false);
        let result = (|| {
            while self.skill_available(LIBERATION)? && self.truth(Op::InTeam, &[])? {
                let now = self.now()?;
                if now - last > 0.1 {
                    self.send(LIBERATION, 0.)?;
                    if !clicked {
                        clicked = true;
                        self.record(LIBERATION)?;
                    }
                    last = now;
                }
                if self.now()? - start > 5. {
                    self.action(Op::Interrupted, &[16.])?;
                    return Err(HOST_ERROR);
                }
                self.action(Op::NextFrame, &[])?;
            }
            if clicked {
                if self.wait_for_team(0.4, false, false)? {
                    self.set_flag(IN_LIBERATION, true)?;
                } else {
                    self.set_flag(IN_LIBERATION, false)?;
                    return Ok(false);
                }
            }
            start = self.now()?;
            while !self.truth(Op::InTeam, &[])? {
                self.set_flag(IN_LIBERATION, true)?;
                if !clicked {
                    clicked = true;
                    self.record(LIBERATION)?;
                }
                if self.now()? - start > 1.5 && !held {
                    self.action(Op::Mouse, &[1.])?;
                    held = true;
                }
                if self.now()? - start > 7. {
                    self.action(Op::Interrupted, &[17.])?;
                    return Err(HOST_ERROR);
                }
                self.action(Op::NextFrame, &[])?;
            }
            let duration = self.now()? - start;
            let now = self.now()?;
            self.action(Op::FreezeAdd, &[now, start, duration])?;
            self.set_flag(IN_LIBERATION, false)?;
            if clicked {
                self.wait_with(
                    || Ok(self.truth(Op::InTeam, &[])? && self.f_mouse_forte()?),
                    0.6,
                    0.,
                    || Ok(()),
                    || Ok(()),
                )?;
                self.wait_with(
                    || Ok(self.truth(Op::InTeam, &[])? && !self.f_mouse_forte()?),
                    0.6,
                    0.,
                    || Ok(()),
                    || Ok(()),
                )?;
            }
            self.action(Op::Mouse, &[0.])?;
            held = false;
            self.action(Op::CheckCombat, &[])?;
            Ok(clicked)
        })();
        if result.is_err() {
            let _ = self.set_flag(IN_LIBERATION, false);
            if held {
                let _ = self.action(Op::Mouse, &[0.]);
            }
        }
        result
    }
    fn changli_outro(&self, mut forte: f64) -> ActionResult {
        if forte == 3. {
            let start = self.now()?;
            let mut full = start;
            let mut res = true;
            while self.now()? - start < 5. {
                if res && self.changli_flick(0.2, false)? {
                    res = false;
                    continue;
                }
                self.click(false, 0.1)?;
                if self.f_mouse_forte()? {
                    if self.now()? - full > 0.2 {
                        break;
                    }
                } else {
                    full = self.now()?;
                }
                self.action(Op::CheckCombat, &[])?;
                self.action(Op::NextFrame, &[])?;
            }
            if self.f_mouse_forte()? {
                self.f_heavy_forte()?;
                self.sleep(1., true)?;
            }
        } else if forte >= 4. || self.f_mouse_forte()? {
            if self.f_flying()? {
                self.heavy_attack(0.6)?;
            }
            self.f_heavy_forte()?;
            forte = 0.;
            self.sleep(1., true)?;
        }
        if self.skill_available(LIBERATION)? && self.changli_lib_heavy()? {
            self.sleep(0.6, true)?;
            forte = 0.;
        }
        if forte < 3. && self.changli_flick(0.2, false)? {
            self.f_set(0, 1.)?;
            return Ok(());
        }
        self.f_echo(1.)?;
        Ok(())
    }
    fn changli_perform(&self) -> ActionResult {
        self.set(19., 1.)?;
        if self.state(INTRO)? != 0. {
            self.f_normal(0.3)?;
            self.f_set(0, 1.)?;
        }
        let mut forte = self.changli_gauge()?;
        if self.f_local(0)? != 0. {
            self.f_normal(0.2)?;
            self.sleep(0.2, true)?;
            if self.f_outro("char_brant")? {
                self.sleep(0.2, true)?;
                self.changli_outro(self.changli_gauge()?)?;
                return self.f_switch();
            }
            if forte == 3. {
                self.sleep(0.2, true)?;
                forte = self.changli_gauge()?;
            }
        }
        self.f_set(0, 0.)?;
        if forte == 4. || self.f_mouse_forte()? {
            if self.f_flying()? {
                self.heavy_attack(0.6)?;
            }
            self.f_heavy_forte()?;
            self.set(19., 0.)?;
            self.action(Op::CheckCombat, &[])?;
            return self.f_switch();
        }
        if !(forte >= 3. && self.skill_available(RESONANCE)?)
            && self.skill_available(LIBERATION)?
            && self.changli_lib_heavy()?
        {
            self.set(19., 0.)?;
            return self.f_switch();
        }
        if self.changli_flick(0.2, false)? {
            self.f_set(0, 1.)?;
            return self.f_switch();
        }
        if self.f_echo(1.)? {
            return self.f_switch();
        }
        self.f_normal(0.1)?;
        self.f_switch()
    }
}
