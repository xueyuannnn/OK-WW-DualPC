use super::*;

impl Host {
    fn sf_long(&self, variant: i32) -> ActionResult<bool> {
        Ok(self.run(Method::LongAction as i32, &[variant as f64])?[0] != 0.)
    }
    fn sf_right(&self, duration: f64) -> ActionResult {
        self.run(Method::RightClicks as i32, &[duration, 0.1, 0.])?;
        Ok(())
    }
    fn sf_heavy_forte(&self) -> ActionResult<bool> {
        Ok(self.run(Method::HeavyForteKind as i32, &[0.])?[0] != 0.)
    }
    fn sf_dodge(&self) -> ActionResult {
        self.action(Op::KeeperDodgeQuery, &[])
    }
    fn sf_break(&self) -> ActionResult<bool> {
        self.truth(Op::BreakF, &[0.])
    }
    fn sf_echo_result(&self) -> ActionResult<bool> {
        Ok(self.virtual_action(&[VIRTUAL_ECHO, 0., 0., 1.])?[0] != 0.)
    }
    fn sf_held_wait(
        &self,
        mouse: bool,
        timeout: f64,
        condition: impl FnMut() -> ActionResult<bool>,
    ) -> ActionResult<bool> {
        let op = if mouse { Op::Mouse } else { Op::Key };
        self.action(op, &[1.])?;
        let result = self.wait_with(condition, timeout, 0., || Ok(()), || Ok(()));
        if result.is_err() {
            let _ = self.action(op, &[0.]);
        }
        let success = result?;
        self.action(op, &[0.])?;
        self.sleep(0.01, true)?;
        Ok(success)
    }
    fn qingxiao_enhanced(&self) -> ActionResult<bool> {
        Ok(self.feature("qingxiao_e", 0.7, "")?
            && self.virtual_action(&[VIRTUAL_RESONANCE, 0., 1., 1., 0.5, 1.5, 1.])?[0] != 0.)
    }
    fn qingxiao_heavy_kind(&self) -> ActionResult<i32> {
        if self.feature("qingxiao_h1", 0.7, "")? {
            Ok(1)
        } else if self.feature("qingxiao_h2", 0.7, "")? {
            Ok(2)
        } else {
            Ok(0)
        }
    }
    fn qingxiao_heavy(&self) -> ActionResult<i32> {
        let kind = self.qingxiao_heavy_kind()?;
        if kind == 0 {
            return Ok(0);
        }
        let start = self.now()?;
        let mut confirmed = false;
        self.action(Op::Mouse, &[1.])?;
        let result: ActionResult = (|| {
            while self.elapsed(start)? < 2. {
                if self.qingxiao_heavy_kind()? != 0 {
                    self.action(Op::NextFrame, &[])?;
                } else {
                    self.sleep(0.25, true)?;
                    self.action(Op::NextFrame, &[])?;
                    if self.qingxiao_heavy_kind()? == 0 {
                        confirmed = true;
                        break;
                    }
                }
            }
            Ok(())
        })();
        if result.is_err() {
            let _ = self.action(Op::Mouse, &[0.]);
        }
        result?;
        self.action(Op::Mouse, &[0.])?;
        self.sleep(0.01, true)?;
        Ok(if confirmed { kind } else { 0 })
    }
    fn aemeath_heavy(&self) -> ActionResult<bool> {
        if !self.sf_long(1)? {
            return Ok(false);
        }
        self.sf_held_wait(true, 1.2, || Ok(!self.sf_long(1)?))
    }
    fn aemeath_lib(&self) -> ActionResult<bool> {
        let second = self.feature("aemeath_lib2", 0.7, "")?;
        let cast = self.stateful_lib(0.)?;
        if cast && second {
            self.stateful_set(1, 1.)?;
        }
        Ok(cast)
    }
    fn aemeath_pending(&self) -> ActionResult<bool> {
        Ok((self.state(INTRO)? != 0. && self.stateful_local(0)? == 0.)
            || (self.stateful_local(2)? != 0. && self.stateful_local(1)? == 0.))
    }
    fn galbrena_res(&self) -> ActionResult<bool> {
        Ok(self.truth(Op::InTeam, &[])?
            && self.observe(2, "has_target\0no_target", "box_target_enemy_long", &[0.6])? != 0.)
    }
    fn sf_outro(&self, names: &str) -> ActionResult<bool> {
        Ok(self.observe(5, names, "", &[])? != 0.)
    }
    pub(super) fn others_perform(&self, kind: i32) -> ActionResult {
        match kind {
            7 => {
                if !(self.all_buffs()? && self.state(INTRO)? != 0.) {
                    self.qingxiao_enhanced()?;
                    return self.stateful_switch();
                }
                let start = self.now()?;
                let mut broke = false;
                while self.elapsed(start)? < 18. {
                    self.stateful_cycle()?;
                    let kind = self.qingxiao_heavy()?;
                    if kind != 0 {
                        self.sf_break()?;
                        if kind == 2 {
                            if !self.skill_available(LIBERATION)? {
                                self.wait_with(
                                    || self.skill_available(LIBERATION),
                                    1.5,
                                    0.,
                                    || Ok(()),
                                    || Ok(()),
                                )?;
                            }
                            self.stateful_lib(0.1)?;
                            broke = true;
                            break;
                        }
                    } else if !self.qingxiao_enhanced()? {
                        self.click(false, -1.)?;
                    }
                    self.stateful_cycle_end()?;
                }
                if !broke {
                    self.qingxiao_heavy()?;
                }
                if broke {
                    self.wait_for_team(1., false, true)?;
                }
                self.stateful_switch()
            }
            8 => {
                self.stateful_set(0, 0.)?;
                self.stateful_set(1, 0.)?;
                self.stateful_set(
                    2,
                    (self.all_buffs()? && self.state(INTRO)? != 0.) as u8 as f64,
                )?;
                let deadline = self.now()? + 60.;
                if self.stateful_local(2)? == 0. {
                    while self.sf_long(1)? {
                        if self.now()? >= deadline {
                            self.action(Op::Interrupted, &[11.])?;
                            return Err(HOST_ERROR);
                        }
                        if self.aemeath_heavy()? {
                            self.sleep(0.3, true)?;
                        }
                    }
                    return self.stateful_switch();
                }
                if self.state(INTRO)? != 0. {
                    self.stateful_normal(2.1)?;
                }
                let mut start = self.now()?;
                while self.elapsed(start)? < 12. {
                    if self.now()? >= deadline {
                        self.action(Op::Interrupted, &[12.])?;
                        return Err(HOST_ERROR);
                    }
                    self.stateful_cycle()?;
                    if self.aemeath_heavy()? {
                        start = self.now()?;
                        if !self.sf_break()? {
                            self.sleep(0.1, true)?;
                        }
                        self.action(Op::CheckCombat, &[])?;
                        continue;
                    }
                    let mut acted = false;
                    if self.aemeath_lib()? {
                        if self.stateful_local(1)? != 0. {
                            break;
                        }
                        acted = true;
                    } else if self.feature("aemeath_e1", 0.7, "")?
                        || self.feature("aemeath_e2", 0.7, "")?
                    {
                        if self.virtual_action(&[VIRTUAL_RESONANCE, 0., 1., 1., 0.5, 1.5, 1.])?[0]
                            != 0.
                        {
                            self.stateful_set(0, 1.)?;
                            self.stateful_echo(0.)?;
                            self.action(Op::NextFrame, &[])?;
                        }
                        self.aemeath_lib()?;
                        if self.stateful_local(1)? != 0. {
                            break;
                        }
                        acted = true;
                    } else {
                        self.click(false, -1.)?;
                    }
                    if acted {
                        if self.sf_long(1)? {
                            start = self.now()?;
                        } else if !self.aemeath_pending()? {
                            break;
                        }
                    }
                    self.stateful_cycle_end()?;
                }
                self.stateful_switch()
            }
            9 => {
                if self.state(INTRO)? != 0. {
                    self.action(Op::Mouse, &[1.])?;
                    let mut held = true;
                    let result: ActionResult<bool> = (|| {
                        self.sleep(1., true)?;
                        if self.need_fast_perform()? {
                            held = false;
                            self.action(Op::Mouse, &[0.])?;
                            self.stateful_switch()?;
                            return Ok(true);
                        }
                        self.sleep(0.44, true)?;
                        held = false;
                        self.action(Op::Mouse, &[0.])?;
                        self.sf_right(0.6)?;
                        Ok(false)
                    })();
                    if held {
                        let _ = self.action(Op::Mouse, &[0.]);
                    }
                    if result? {
                        return Ok(());
                    }
                } else if self.stateful_flying()? {
                    self.stateful_down()?;
                }
                self.stateful_echo(0.)?;
                if self.stateful_forte_full()? && !self.need_fast_perform()? {
                    self.stateful_resonance(true, 0.)?;
                    if self.stateful_lib(0.1)? {
                        self.stateful_normal(1.)?;
                    }
                }
                if self.galbrena_res()? && !self.need_fast_perform()? {
                    self.stateful_lib(0.1)?;
                    let start = self.now()?;
                    while self.galbrena_res()? && self.now()? - start < 10. {
                        if self.stateful_flying()? {
                            self.sf_dodge()?;
                        }
                        self.click(false, -1.)?;
                        self.sleep(0.1, true)?;
                        self.action(Op::CheckCombat, &[])?;
                    }
                    return self.stateful_switch();
                }
                self.stateful_normal(1.)?;
                self.stateful_resonance(true, 0.)?;
                self.stateful_switch()
            }
            10 => {
                let mut under_outro = false;
                if self.state(INTRO)? != 0. {
                    self.stateful_normal(1.2)?;
                    under_outro = self.state(SUB_INTRO)? != 0.
                        && self.sf_outro("char_roccia\0char_sanhua")?;
                }
                self.stateful_lib(0.1)?;
                if self.truth(Op::Forte, &[0.])? || !self.stateful_forte_full()? {
                    self.stateful_resonance(true, 0.)?;
                    if under_outro && self.stateful_flying()? {
                        self.stateful_down()?;
                    }
                    if !self.stateful_flying()? && self.truth(Op::Forte, &[0.])? {
                        if self.sf_heavy_forte()? {
                            self.stateful_set(0, self.now()?)?;
                        }
                    } else if self.sf_echo_result()? {
                        return self.stateful_switch();
                    } else {
                        self.stateful_normal(0.1)?;
                        return self.stateful_switch();
                    }
                }
                let mut forte_delay = self.now()?;
                let mut count = -0.1;
                let result: ActionResult<bool> = (|| {
                    while self.elapsed(self.stateful_local(0)?)? < 8.
                        && !self.truth(Op::Forte, &[0.])?
                    {
                        let now = self.now()?;
                        if self.skill_available(RESONANCE)? {
                            self.stateful_resonance(false, 0.)?;
                            if !under_outro {
                                self.action(Op::Mouse, &[0.])?;
                                self.stateful_switch()?;
                                return Ok(true);
                            }
                        }
                        if !under_outro
                            && self.need_fast_perform()?
                            && self.elapsed(self.state(LAST_PERFORM)?)? > 1.1
                        {
                            break;
                        }
                        if now - forte_delay > count {
                            self.action(Op::Mouse, &[0.])?;
                            self.sleep(0.2, true)?;
                            self.action(Op::Mouse, &[1.])?;
                            count += 1.;
                        }
                        if self.stateful_forte_full()? {
                            forte_delay = now;
                        } else if now - forte_delay > 0.5 {
                            break;
                        }
                        self.action(Op::CheckCombat, &[])?;
                        self.action(Op::NextFrame, &[])?;
                    }
                    Ok(false)
                })();
                if result.is_err() {
                    let _ = self.action(Op::Mouse, &[0.]);
                }
                if result? {
                    return Ok(());
                }
                self.action(Op::Mouse, &[0.])?;
                self.stateful_echo(1.)?;
                self.stateful_switch()
            }
            11..=14 => self.others_last_perform(kind),
            _ => Err(INVALID_ACTION),
        }
    }
}

impl Host {
    pub(super) fn others_priority(&self, args: &[f64]) -> ActionResult<f64> {
        match args[0] as i32 {
            10 => {
                if args[1] != 0. && args[2] != 0. && args[3] != 0. {
                    return Ok(400.);
                }
            }
            11 => {
                if self.elapsed(self.state(LAST_LIBERATION)?)? > 14.
                    && args[1] != 0.
                    && args[2] != 0.
                    && args[3] != 0.
                {
                    return Ok(400.);
                }
                if self.elapsed(self.state(LAST_LIBERATION)?)? < 24. {
                    return Ok(0.);
                }
            }
            12 => {
                let names = args[3] as u32;
                if args[1] != 0. && args[2] != 0. && names & 1 != 0 {
                    return Ok(400.);
                }
                if args[1] != 0.
                    && args[2] != 0.
                    && self.observe(6, "Linnai", "", &[])? != 0.
                    && names & 2 == 0
                {
                    return Ok(400.);
                }
            }
            14 => {
                if self.now()? - self.stateful_local(0)? > 24. && args[1] != 0. {
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
    pub(super) fn others_lifecycle(&self, args: &[f64]) -> ActionResult<f64> {
        match (args[0] as i32, args[1] as i32) {
            (10, 2) => {
                if !self.truth(Op::Forte, &[0.])? && self.stateful_forte_full()? {
                    return Ok((!self.truth(Op::HasCd, &[RESONANCE, -1.])?) as u8 as f64);
                }
                Ok(self.run(Method::Available as i32, &[RESONANCE, 0., 1.])?[0])
            }
            (11, 2) => {
                if self.stateful_local(0)? != 0. {
                    return Ok((!(self.stateful_flying()?
                        || self.truth(Op::HasCd, &[RESONANCE, -1.])?))
                        as u8 as f64);
                }
                Ok(self.run(Method::Available as i32, &[RESONANCE, 0., 1.])?[0])
            }
            (10, 3) => Ok((self.observe(
                3,
                "",
                "",
                &[5120., 2880., 3034., 2640., 56., 60., 250., 246., 244., 1.],
            )? > 0.06) as u8 as f64),
            (13, 3) => Ok((self.observe(
                3,
                "",
                "",
                &[5120., 2880., 3032., 2654., 44., 46., 250., 246., 244., 1.],
            )? > 0.1) as u8 as f64),
            (11, 4) => Ok((self.elapsed(self.state(LAST_LIBERATION)?)? < 2.) as u8 as f64),
            (10, 5) => {
                let size = self.value(Op::LoadParty, &[])? as i64;
                if size != 0 {
                    self.action(
                        Op::TapSlot,
                        &[((self.state(INDEX)? as i64 + 1) % size) as f64],
                    )?;
                }
                Ok(0.)
            }
            (8 | 12, 5) => {
                self.switch_other(false)?;
                Ok(0.)
            }
            _ => Err(INVALID_ACTION),
        }
    }
}

impl Host {
    fn phrolova_heavy_lib(&self) -> ActionResult<bool> {
        if !self.sf_heavy_forte()? {
            return Ok(false);
        }
        self.wait_with(|| self.stateful_lib(0.), 3., 0., || Ok(()), || Ok(()))?;
        Ok(true)
    }
    fn phrolova_res_gone(&self) -> ActionResult {
        self.wait_with(
            || Ok(!self.skill_available(RESONANCE)?),
            0.3,
            0.,
            || self.click(false, -1.),
            || Ok(()),
        )?;
        Ok(())
    }
    fn mornye_air(&self) -> ActionResult<bool> {
        self.sf_long(2)
    }
    fn elbow(&self, detect: bool) -> ActionResult<bool> {
        Ok(detect && self.run(Method::Available as i32, &[ECHO, 1., 1.])?[0] == 0.)
    }
    fn wait_elbow(&self, detect: bool) -> ActionResult {
        self.wait_with(
            || Ok(!self.elbow(detect)?),
            1.5,
            0.,
            || self.sf_right(0.05),
            || Ok(()),
        )?;
        Ok(())
    }
    fn mornye_airborne(&self) -> ActionResult {
        let detect = self.skill_available(ECHO)?;
        let start = self.now()?;
        while self.now()? - start < 10. && self.mornye_air()? {
            if self.elbow(detect)? {
                self.wait_elbow(detect)?;
            }
            self.stateful_lib(0.1)?;
            if self.mornye_air()?
                && self.truth(Op::Forte, &[0.])?
                && self.heavy_forte(|| Ok(self.truth(Op::Forte, &[0.])? && !self.elbow(detect)?))?
            {
                if self.elbow(detect)? {
                    continue;
                }
                if !self.wait_with(|| self.is_con_full(), 1.5, 0., || Ok(()), || Ok(()))? {
                    let fill = self.now()?;
                    while !self.is_con_full()? && self.now()? - fill < 2. && self.mornye_air()? {
                        self.stateful_normal(0.5)?;
                        if self.elbow(detect)? {
                            break;
                        }
                    }
                    if !self.is_con_full()? {
                        self.virtual_action(&[VIRTUAL_ECHO, 0.2, 0., 1.])?;
                    }
                }
                self.stateful_set(0, self.now()?)?;
                self.set(19., 0.)?;
                break;
            }
            self.stateful_resonance(true, 0.)?;
            self.click(false, -1.)?;
            self.sleep(0.01, true)?;
        }
        Ok(())
    }
    fn mornye_ground(&self) -> ActionResult {
        let start = self.now()?;
        let mut dodge = true;
        while self.now()? - start < 10. && !self.mornye_air()? {
            self.click(false, -1.)?;
            self.stateful_resonance(true, 0.)?;
            self.action(Op::CheckCombat, &[])?;
            self.sleep(0.1, true)?;
            if self.truth(Op::Forte, &[0.])? {
                if dodge {
                    self.click(true, -1.)?;
                    dodge = false;
                }
                self.heavy_attack(0.6)?;
                self.sleep(0.3, true)?;
            }
        }
        Ok(())
    }
    fn xigelika_hold(&self) -> ActionResult<bool> {
        let mouse = self.sf_long(1)?;
        if !mouse && self.truth(Op::HasCd, &[RESONANCE, -1.])? {
            self.click(false, 0.1)?;
            self.sleep(0.05, true)?;
            return Ok(false);
        }
        if !mouse {
            self.action(Op::CaptureKey, &[RESONANCE])?;
        }
        self.sf_held_wait(mouse, 1.2, || {
            if mouse {
                Ok(!self.sf_long(1)?)
            } else {
                Ok(!self.stateful_forte_full()?)
            }
        })
    }
    fn xigelika_heavy(&self) -> ActionResult<bool> {
        let mut handled = false;
        let start = self.now()?;
        while self.stateful_forte_full()? && self.elapsed(start)? < 3. {
            self.xigelika_hold()?;
            handled = true;
        }
        Ok(handled)
    }
    fn luhesi_res(&self) -> ActionResult<bool> {
        Ok(self.truth(Op::InTeam, &[])?
            && self.observe(2, "has_target\0no_target", "target_box_long2", &[0.6])? != 0.)
    }
    fn luhesi_lib(&self) -> ActionResult<bool> {
        if self.observe(4, "box_luhesi_lib", "", &[])? > 0.
            && !self.truth(Op::HasCd, &[LIBERATION, -1.])?
            && self.stateful_lib(0.)?
        {
            self.sf_break()?;
            return Ok(true);
        }
        Ok(false)
    }
    fn luhesi_heavy(&self, res_count: i32) -> ActionResult<bool> {
        let start = self.now()?;
        let mut kicked = false;
        while self.feature("luhesi_kick", 0.7, "")? && self.now()? - start < 3. {
            kicked = true;
            if res_count < 3 {
                self.sf_break()?;
            }
            self.click(false, -1.)?;
            self.sleep(0.1, true)?;
        }
        Ok(kicked)
    }
    fn luhesi_everything(&self) -> ActionResult {
        self.stateful_normal(1.1)?;
        if self.state(SUB_INTRO)? == 0. {
            self.send(RESONANCE, 0.1)?;
            return Ok(());
        }
        self.stateful_set(0, self.now()?)?;
        if !self.stateful_flying()? {
            self.wait_with(
                || self.stateful_flying(),
                0.2,
                0.,
                || self.action(Op::Tap, &[3., 0.01, -1., 0.01]),
                || Ok(()),
            )?;
        }
        let detect = self.skill_available(ECHO)?;
        let start = self.now()?;
        let mut res_count = 0;
        let mut try_jump = false;
        while self.elapsed(start)? < 12. {
            if self.elbow(detect)? {
                self.wait_elbow(detect)?;
            } else if self.luhesi_heavy(res_count)? && res_count > 2 {
                self.luhesi_lib()?;
                return Ok(());
            } else if try_jump && !self.luhesi_res()? {
                self.wait_with(
                    || Ok(self.feature("luhesi_kick", 0.7, "")? || self.elbow(detect)?),
                    1.,
                    0.,
                    || self.send(RESONANCE, 0.),
                    || Ok(()),
                )?;
                if self.elbow(detect)? {
                    continue;
                }
                try_jump = false;
                if self.feature("luhesi_kick", 0.7, "")? {
                    res_count += 1;
                } else {
                    if res_count == 2 {
                        self.stateful_down()?;
                    }
                    self.luhesi_lib()?;
                    return Ok(());
                }
            } else if self.luhesi_res()? {
                self.action(Op::Tap, &[3., 0.01, -1., 0.01])?;
                try_jump = true;
            } else {
                self.click(false, -1.)?;
            }
            self.sleep(0.01, true)?;
        }
        Ok(())
    }
    fn others_last_perform(&self, kind: i32) -> ActionResult {
        match kind {
            11 => {
                self.set(LAST_LIBERATION, -1.)?;
                let mut under_outro = false;
                self.stateful_set(0, 0.)?;
                if self.state(INTRO)? != 0. {
                    self.stateful_set(1, 0.)?;
                    under_outro = self.sf_outro("char_cantarella")?;
                    self.stateful_normal(1.7)?;
                    self.sf_right(0.1)?;
                }
                if self.stateful_flying()? {
                    self.stateful_down()?;
                }
                if (self.skill_available(LIBERATION)? && self.stateful_lib(0.)?)
                    || self.phrolova_heavy_lib()?
                {
                    return self.stateful_switch();
                }
                if self.skill_available(RESONANCE)? || self.stateful_local(1)? != 0. {
                    self.stateful_normal(0.1)?;
                    self.stateful_resonance(true, 0.)?;
                    self.stateful_normal(0.1)?;
                    self.phrolova_res_gone()?;
                    if !self.sf_echo_result()? {
                        self.sf_right(0.1)?;
                    }
                }
                self.stateful_set(1, 0.)?;
                let start = self.now()?;
                if under_outro {
                    self.stateful_set(0, 1.)?;
                }
                while if under_outro {
                    self.elapsed(self.state(LAST_PERFORM)?)? < 16.
                } else {
                    self.now()? - start < 4.
                } {
                    if self.skill_available(LIBERATION)? && self.stateful_lib(0.)? {
                        return self.stateful_switch();
                    }
                    if self.stateful_flying()? {
                        self.sf_dodge()?;
                    }
                    if self.phrolova_heavy_lib()? {
                        return self.stateful_switch();
                    }
                    if self.skill_available(RESONANCE)? && self.now()? - start > 1. {
                        if under_outro {
                            self.stateful_normal(0.3)?;
                            if self.stateful_resonance(true, 0.)? {
                                self.stateful_normal(0.1)?;
                                self.phrolova_res_gone()?;
                                if !self.sf_echo_result()? {
                                    self.sf_right(0.1)?;
                                }
                            }
                        } else {
                            self.stateful_set(1, 1.)?;
                            break;
                        }
                    }
                    self.click(false, -1.)?;
                    self.action(Op::CheckCombat, &[])?;
                    self.action(Op::NextFrame, &[])?;
                }
                self.stateful_switch()
            }
            12 => {
                if self.state(INTRO)? != 0. {
                    self.stateful_normal(1.33)?;
                }
                self.set(19., 1.)?;
                if !self.mornye_air()? {
                    if self.elapsed(self.stateful_local(0)?)? < 23. {
                        if self.sf_echo_result()? {
                            return self.stateful_switch();
                        }
                        if self.stateful_resonance(true, 0.)? {
                            return self.stateful_switch();
                        }
                        self.stateful_normal(0.1)?;
                        return self.stateful_switch();
                    }
                    self.mornye_ground()?;
                }
                if self.mornye_air()? {
                    self.mornye_airborne()?;
                }
                self.stateful_switch()
            }
            13 => {
                if self.state(INTRO)? != 0. {
                    self.stateful_normal(0.77)?;
                } else {
                    self.stateful_down()?;
                }
                let mut start = self.now()?;
                let mut timeout = if self.state(SUB_INTRO)? != 0. {
                    15.
                } else {
                    0.5
                };
                let deadline = start + 45.;
                while self.elapsed(start)? < timeout {
                    if self.now()? >= deadline {
                        self.action(Op::Interrupted, &[13.])?;
                        return Err(HOST_ERROR);
                    }
                    self.stateful_echo(0.)?;
                    if self.stateful_flying()? {
                        self.sf_dodge()?;
                    }
                    if self.xigelika_heavy()? {
                        break;
                    }
                    if self.state(INTRO)? == 0. && self.stateful_lib(0.)? {
                        self.sf_break()?;
                        timeout = 15.;
                        start = self.now()?;
                    } else if !self.stateful_resonance(false, 2.)? {
                        self.click(false, 0.1)?;
                    }
                    self.sleep(0.05, true)?;
                }
                self.stateful_switch()
            }
            14 => {
                self.luhesi_everything()?;
                self.stateful_switch()
            }
            _ => Err(INVALID_ACTION),
        }
    }
}
