use super::*;

impl Host {
    fn a_res(&self, click: bool) -> ActionResult<bool> {
        Ok(
            self.virtual_action(&[VIRTUAL_RESONANCE, 0., 0., click as u8 as f64, 0., 0., 1.])?[0]
                != 0.,
        )
    }
    fn a_lib(&self, click: bool, wait: f64) -> ActionResult<bool> {
        Ok(
            self.virtual_action(&[VIRTUAL_LIBERATION, -1., click as u8 as f64, wait, 0., 1.])?[0]
                != 0.,
        )
    }
    fn a_echo(&self, timeout: f64) -> ActionResult<bool> {
        Ok(self.virtual_action(&[VIRTUAL_ECHO, 0., 0., timeout])?[0] != 0.)
    }
    fn a_normal(&self, duration: f64) -> ActionResult {
        self.normal_attacks(duration, 0.1, 0., false, false)
    }
    fn a_switch(&self) -> ActionResult {
        self.virtual_action(&[VIRTUAL_SWITCH, 0., 0.])?;
        Ok(())
    }
    fn a_flying(&self) -> ActionResult<bool> {
        Ok(self.virtual_action(&[VIRTUAL_FLYING])?[0] != 0.)
    }
    fn a_down(&self) -> ActionResult {
        self.run(Method::WaitDown as i32, &[1.])?;
        Ok(())
    }
    fn a_forte(&self) -> ActionResult<bool> {
        Ok(self.virtual_action(&[VIRTUAL_FORTE])?[0] != 0.)
    }
    fn a_cycle(&self) -> ActionResult {
        self.set(CYCLE_START, self.now()?)
    }
    fn a_cycle_end(&self) -> ActionResult {
        self.run(Method::CycleSleep as i32, &[0.1])?;
        Ok(())
    }
    fn a_local(&self, index: usize) -> ActionResult<f64> {
        self.value(Op::LocalAt, &[index as f64])
    }
    fn a_set(&self, index: usize, value: f64) -> ActionResult {
        self.action(Op::SetLocalAt, &[index as f64, value])
    }
    fn a_break(&self) -> ActionResult<bool> {
        self.truth(Op::BreakF, &[0.])
    }
    fn a_outro(&self, names: &str) -> ActionResult<bool> {
        Ok(self.observe(5, names, "", &[])? != 0.)
    }
    fn a_dodge(&self) -> ActionResult {
        self.action(Op::KeeperDodgeQuery, &[])
    }
    fn a_base_priority(&self) -> ActionResult<f64> {
        Ok(if self.truth(Op::HealerLocked, &[])? {
            0.
        } else {
            200.
        })
    }
    fn augusta_majesty_ready(&self) -> ActionResult<bool> {
        Ok(self.value(Op::Highlighted, &[LIBERATION])? > 0.
            && self.feature("Augusta_lib2", 0.5, "")?)
    }
    fn augusta_prowess_ready(&self) -> ActionResult<bool> {
        self.feature("target_enemy_long_inner", 0.8, "")
    }
    fn augusta_prowess(&self) -> ActionResult<bool> {
        if !self.heavy_forte(|| self.augusta_prowess_ready())? {
            return Ok(false);
        }
        self.a_normal(0.3)?;
        Ok(true)
    }
    fn augusta_majesty(&self) -> ActionResult<bool> {
        self.action(Op::CaptureKey, &[LIBERATION])?;
        self.action(Op::Key, &[1.])?;
        let mut held = true;
        self.set_flag(IN_LIBERATION, true)?;
        let result: ActionResult<bool> = (|| {
            self.wait_for_team(0.6, false, false)?;
            let start = self.now()?;
            held = false;
            self.action(Op::Key, &[0.])?;
            if self.truth(Op::InTeam, &[])? {
                self.set_flag(IN_LIBERATION, false)?;
                return Ok(false);
            }
            self.wait_with(
                || self.truth(Op::InTeam, &[]),
                10.,
                0.,
                || self.click(false, -1.),
                || Ok(()),
            )?;
            let duration = self.now()? - start;
            let now = self.now()?;
            self.action(Op::FreezeAdd, &[now, start, duration])?;
            Ok(true)
        })();
        if result.is_err() {
            let _ = self.set_flag(IN_LIBERATION, false);
        }
        if held {
            let _ = self.action(Op::Key, &[0.]);
        }
        result
    }
    fn linnai_res(&self) -> ActionResult<bool> {
        if !self.truth(Op::InTeam, &[])? {
            return Ok(false);
        }
        self.action(Op::CaptureTarget, &[])?;
        let no_target = if self.observe(7, "has_target_169", "", &[])? != 0. {
            "no_target_169"
        } else {
            "no_target"
        };
        Ok(
            self.observe(8, no_target, "target_box_long2", &[0.6])? != 0.
                || self.observe(8, no_target, "box_target_enemy_long", &[0.6])? != 0.
                || self.feature("target_box_short", 0.6, "")?,
        )
    }
    fn linnai_kick_wait(&self) -> ActionResult {
        self.sleep(0.3, true)?;
        self.a_down()
    }
    fn linnai_under_intro(&self) -> ActionResult {
        if !self.linnai_res()?
            && !self.wait_with(
                || self.linnai_res(),
                if self.state(INTRO)? != 0. && self.a_outro("char_aemeath")? {
                    1.6
                } else {
                    1.
                },
                0.,
                || self.click(false, 0.1),
                || Ok(()),
            )?
        {
            return Ok(());
        }
        self.wait_with(
            || {
                Ok(self.observe(
                    3,
                    "",
                    "",
                    &[5120., 2880., 2846., 2602., 43., 88., 250., 246., 244., 1.],
                )? > 0.06
                    || self.is_con_full()?)
            },
            1.,
            0.,
            || self.click(false, -1.),
            || Ok(()),
        )?;
        if self.wait_with(
            || Ok(!self.a_forte()?),
            3.,
            0.,
            || self.action(Op::Tap, &[3., 0.01, -1., 0.01]),
            || Ok(()),
        )? && self.wait_with(
            || self.a_res(true),
            2.,
            0.,
            || self.click(false, -1.),
            || Ok(()),
        )? {
            self.linnai_kick_wait()?;
            let mut second = false;
            self.wait_with(
                || {
                    if self.is_con_full()? {
                        return Ok(true);
                    }
                    second = self.a_res(true)?;
                    Ok(second)
                },
                3.,
                0.,
                || self.click(false, -1.),
                || Ok(()),
            )?;
            if second {
                self.linnai_kick_wait()?;
            }
        }
        if !self.is_con_full()? && self.a_lib(false, 0.1)? {
            self.wait_with(
                || self.is_con_full(),
                1.2,
                0.,
                || self.click(false, 0.1),
                || Ok(()),
            )?;
        }
        Ok(())
    }
    fn linnai_charge(&self) -> ActionResult {
        self.a_normal(1.)?;
        self.a_echo(0.)?;
        if !self.is_con_full()? {
            self.a_lib(false, 0.1)?;
        }
        if !self.truth(Op::Forte, &[0.])? {
            self.a_res(true)?;
        }
        self.wait_with(
            || self.truth(Op::Forte, &[0.]),
            2.,
            0.,
            || self.click(false, -1.),
            || Ok(()),
        )?;
        self.action(Op::Mouse, &[1.])?;
        let result = self.wait_with(
            || Ok(!self.truth(Op::Forte, &[0.])?),
            5.,
            0.,
            || Ok(()),
            || Ok(()),
        );
        if result.is_err() {
            let _ = self.action(Op::Mouse, &[0.]);
        }
        let success = result?;
        self.action(Op::Mouse, &[0.])?;
        if success {
            self.sleep(0.4, true)?;
            self.linnai_under_intro()?;
        }
        Ok(())
    }
    fn jinhsi_incarnation(&self) -> ActionResult {
        self.a_set(0, 0.)?;
        let start = self.now()?;
        let mut animation = 0.;
        let mut last_resonance = true;
        self.set_flag(IN_LIBERATION, true)?;
        let result: ActionResult = (|| {
            loop {
                if self.now()? - start > 6. {
                    break;
                }
                if self.truth(Op::InTeam, &[])? {
                    if last_resonance {
                        self.click(false, 0.1)?;
                        last_resonance = false;
                    } else {
                        self.send(RESONANCE, 0.)?;
                        last_resonance = true;
                    }
                    if animation != 0. {
                        break;
                    }
                } else {
                    if animation == 0. {
                        animation = self.now()?;
                    }
                    self.set_flag(IN_LIBERATION, true)?;
                }
                self.action(Op::CheckCombat, &[])?;
                self.action(Op::NextFrame, &[])?;
            }
            Ok(())
        })();
        if result.is_err() {
            let _ = self.set_flag(IN_LIBERATION, false);
        }
        result?;
        self.set_flag(IN_LIBERATION, false)?;
        if !self.a_echo(1.)? {
            self.click(false, -1.)?;
        }
        self.action(Op::FreezeAdd, &[self.now()?, animation, -1.])
    }
    fn jinhsi_intro(&self) -> ActionResult {
        let start = self.now()?;
        loop {
            let since = self.now()? - start;
            if since > 15. {
                self.action(Op::Interrupted, &[14.])?;
                return Err(HOST_ERROR);
            }
            if self.truth(Op::HasCd, &[RESONANCE, -1.])? {
                if since > 0.3 && since < 1.5 {
                    self.a_set(1, 1.)?;
                    if !self.a_echo(1.)? {
                        self.click(false, -1.)?;
                    }
                    return Ok(());
                }
                if since > 1.5 {
                    break;
                }
            } else {
                self.action(Op::Send, &[RESONANCE, 0., 0.1, 0.01])?;
            }
            self.action(Op::NextFrame, &[])?;
            self.action(Op::CheckCombat, &[])?;
        }
        if self.a_lib(true, 0.1)? {
            self.a_normal(0.3)?;
        } else {
            self.a_normal(1.4)?;
        }
        self.a_set(0, 1.)?;
        self.a_set(1, 0.)
    }
}

impl Host {
    fn lucy_standard(&self) -> ActionResult {
        if self.a_forte()? {
            return Ok(());
        }
        let start = self.now()?;
        while !self.a_forte()? {
            if self.now()? - start > 8.5 {
                break;
            }
            if self.skill_available(RESONANCE)? {
                self.a_res(true)?;
            }
            self.heavy_attack(0.4)?;
            if self.skill_available(RESONANCE)? {
                self.a_res(true)?;
            }
        }
        self.a_normal(0.1)
    }
    fn lucy_lib_sequence(&self) -> ActionResult {
        if self.skill_available(RESONANCE)? {
            self.a_res(true)?;
        }
        self.a_break()?;
        if self.truth(Op::Forte, &[0.])? {
            self.heavy_attack(0.4)?;
            self.sleep(0.2, false)?;
            self.heavy_attack(0.4)?;
        }
        if !self.truth(Op::Forte, &[0.])? {
            let start = self.now()?;
            while !self.truth(Op::Forte, &[0.])? && self.now()? - start < 6. {
                self.click(false, -1.)?;
                self.sleep(0.1, true)?;
                self.action(Op::NextFrame, &[])?;
            }
        }
        if self.truth(Op::Forte, &[0.])? {
            self.action(Op::Mouse, &[1.])?;
            let held: ActionResult = (|| {
                let start = self.now()?;
                while self.now()? - start < 2.5 {
                    self.sleep(0.05, true)?;
                    self.action(Op::NextFrame, &[])?;
                }
                Ok(())
            })();
            if held.is_err() {
                let _ = self.action(Op::Mouse, &[0.]);
            }
            held?;
            self.action(Op::Mouse, &[0.])?;
            self.sleep(0.1, true)?;
            let wait = self.now()?;
            let mut available = self.skill_available(LIBERATION)?;
            while self.now()? - wait < 2. && !available {
                self.sleep(0.1, true)?;
                available = self.skill_available(LIBERATION)?;
                self.action(Op::NextFrame, &[])?;
            }
            if self.skill_available(ECHO)? {
                self.a_echo(0.)?;
            }
        } else if self.skill_available(ECHO)? {
            self.a_echo(0.)?;
        }
        if self.skill_available(LIBERATION)? {
            self.a_lib(true, 1.5)?;
            self.record(LIBERATION)?;
            for _ in 0..11 {
                self.click(false, -1.)?;
                self.sleep(0.1, false)?;
            }
        }
        Ok(())
    }
    fn brant_still_lib(&self) -> ActionResult<bool> {
        Ok(self.elapsed(self.state(LAST_LIBERATION)?)? < 12.)
    }
    fn brant_landing(&self, timeout: f64, click: bool) -> ActionResult {
        self.wait_with(
            || Ok(!self.a_flying()?),
            timeout,
            0.,
            || {
                if click {
                    self.click(false, 0.1)
                } else {
                    Ok(())
                }
            },
            || Ok(()),
        )?;
        Ok(())
    }
    fn brant_flick(&self, timeout: f64, click: bool) -> ActionResult<bool> {
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
    fn brant_forte_res(&self) -> ActionResult {
        let start = self.now()?;
        while self.skill_available(RESONANCE)? && self.a_forte()? {
            self.send(RESONANCE, 0.)?;
            if self.now()? - start > 1. {
                break;
            }
            self.action(Op::CheckCombat, &[])?;
            self.action(Op::NextFrame, &[])?;
        }
        Ok(())
    }
    fn brant_jumping(&self, duration: f64) -> ActionResult {
        let mut click = false;
        if self.flag(LEVITATOR)? && !self.a_flying()? {
            self.brant_flick(0.2, true)?;
            self.sleep(0.2, true)?;
            click = true;
        }
        let start = self.now()?;
        while self.now()? - start <= duration {
            if click {
                self.click(false, -1.)?;
            } else {
                self.action(Op::Tap, &[3., 0.01, -1., 0.01])?;
            }
            click = !click;
            self.action(Op::NextFrame, &[])?;
        }
        Ok(())
    }
    fn brant_under_outro(&self) -> ActionResult {
        let mut start = self.now()?;
        let mut timeout = if self.brant_still_lib()? { 10. } else { 1.5 };
        let mut liberated = self.brant_still_lib()?;
        let deadline = start + 40.;
        loop {
            if self.now()? >= deadline {
                self.action(Op::Interrupted, &[15.])?;
                return Err(HOST_ERROR);
            }
            if self.a_forte()? && self.skill_available(RESONANCE)? {
                self.brant_forte_res()?;
                self.set(LAST_LIBERATION, -1.)?;
                self.brant_landing(4., true)?;
                if liberated {
                    break;
                }
            }
            if self.skill_available(LIBERATION)? && self.a_lib(false, 0.1)? {
                start = self.now()?;
                timeout = 10.;
                liberated = true;
            }
            if self.now()? - start > timeout {
                break;
            }
            if self.flag(LEVITATOR)? && !self.a_flying()? {
                self.brant_flick(0.2, true)?;
                self.sleep(0.2, true)?;
            } else if !self.flag(LEVITATOR)? {
                self.action(Op::Tap, &[3., 0.01, -1., 0.01])?;
            }
            self.click(false, -1.)?;
            self.action(Op::CheckCombat, &[])?;
            self.action(Op::NextFrame, &[])?;
        }
        self.a_echo(1.)?;
        Ok(())
    }
    fn lucilla_pulse(&self, duration: f64) -> ActionResult {
        let end = self.now()? + duration;
        let mut seen = false;
        while self.now()? < end {
            self.action(Op::Mouse, &[1.])?;
            let result = self
                .now()
                .and_then(|now| self.sleep(0.6_f64.min(end - now), false));
            if result.is_err() {
                let _ = self.action(Op::Mouse, &[0.]);
            }
            result?;
            self.action(Op::Mouse, &[0.])?;
            let con = self.value(Op::Concerto, &[])?;
            if con > 0.1 {
                seen = true;
            } else if seen && con < 0.05 {
                break;
            }
            self.sleep(0.02, false)?;
        }
        Ok(())
    }
    fn lucilla_try_lib(&self) -> ActionResult<bool> {
        if !self.skill_available(LIBERATION)? {
            return Ok(false);
        }
        if self.skill_available(ECHO)? {
            self.a_echo(0.)?;
        }
        if self.flag(USE_LIBERATION)? {
            let start = self.now()?;
            while self.skill_available(LIBERATION)? && self.now()? - start < 1.5 {
                self.send(LIBERATION, 0.)?;
                self.sleep(0.1, false)?;
            }
            self.record(LIBERATION)?;
            self.sleep(3., false)?;
            self.lucilla_pulse(15.)?;
        }
        self.a_switch()?;
        Ok(true)
    }
}

impl Host {
    pub(super) fn advanced_perform(&self, kind: i32) -> ActionResult {
        match kind {
            0 => {
                let mut timeout = 3.;
                if self.state(INTRO)? != 0. {
                    self.a_normal(1.13)?;
                    if self.state(SUB_INTRO)? != 0. && self.a_outro("char_iuno")? {
                        timeout = 14.;
                    }
                }
                if self.a_flying()? {
                    self.a_down()?;
                }
                let start = self.now()?;
                while self.now()? - start < timeout + 3. {
                    self.a_cycle()?;
                    if self.augusta_majesty_ready()? && self.augusta_majesty()? {
                        self.send(ECHO, 0.)?;
                        return self.a_switch();
                    }
                    if self.a_flying()? {
                        self.a_dodge()?;
                    }
                    if self.augusta_prowess_ready()?
                        && self.augusta_prowess()?
                        && self.now()? - start > timeout
                    {
                        return self.a_switch();
                    }
                    if self.skill_available(RESONANCE)? {
                        let before = self.now()?;
                        self.a_res(true)?;
                        if self.now()? - before < 1.4 {
                            if self.a_flying()? {
                                continue;
                            }
                            if self.wait_with(
                                || self.augusta_prowess_ready(),
                                1.,
                                0.,
                                || Ok(()),
                                || Ok(()),
                            )? && self.augusta_prowess()?
                                && self.now()? - start > timeout
                                && !self.a_flying()?
                            {
                                return self.a_switch();
                            }
                        } else if self.augusta_majesty_ready()? {
                            self.a_down()?;
                            if self.augusta_majesty()? {
                                self.send(ECHO, 0.)?;
                            }
                            return self.a_switch();
                        }
                    }
                    if self.skill_available(LIBERATION)?
                        && self.wait_with(
                            || Ok(!self.skill_available(LIBERATION)?),
                            2.,
                            0.,
                            || self.send(LIBERATION, 0.),
                            || Ok(()),
                        )?
                    {
                        self.record(LIBERATION)?;
                        if timeout < 14. {
                            return self.a_switch();
                        }
                    }
                    self.click(false, -1.)?;
                    self.a_cycle_end()?;
                }
                self.send(ECHO, 0.)?;
                self.a_switch()
            }
            1 => {
                if self.state(INTRO)? != 0. && self.linnai_res()? {
                    self.a_normal(1.33)?;
                } else {
                    self.linnai_charge()?;
                }
                if self.skill_available(LIBERATION)? {
                    self.a_lib(false, 0.1)?;
                }
                self.a_switch()
            }
            2 => {
                if self.a_local(0)? != 0. {
                    self.jinhsi_incarnation()?;
                } else if self.state(INTRO)? != 0. || self.a_local(1)? != 0. {
                    self.jinhsi_intro()?;
                } else {
                    self.a_echo(1.)?;
                }
                self.a_switch()
            }
            3 => {
                if self.truth(Op::CheckBreak, &[])? {
                    self.a_break()?;
                }
                if !self.a_forte()? {
                    self.lucy_standard()?;
                }
                self.lucy_lib_sequence()?;
                self.a_switch()
            }
            4 => {
                if self.state(INTRO)? != 0. {
                    self.a_normal(1.3)?;
                    if self.state(SUB_INTRO)? != 0. && self.a_outro("char_lupa")? {
                        self.brant_under_outro()?;
                        return self.a_switch();
                    }
                }
                self.a_break()?;
                if self.a_forte()? && self.skill_available(RESONANCE)? {
                    self.brant_forte_res()?;
                    self.set(LAST_LIBERATION, -1.)?;
                    self.a_set(0, self.now()?)?;
                    if self.skill_available(ECHO)? {
                        self.a_echo(1.)?;
                    }
                }
                if !self.need_fast_perform()? && !self.a_forte()? && self.a_lib(false, 0.1)? {
                    self.a_normal(0.8)?;
                }
                self.brant_jumping(1.3)?;
                if self.a_forte()? && self.skill_available(RESONANCE)? {
                    self.brant_forte_res()?;
                    self.a_set(0, self.now()?)?;
                    return self.a_switch();
                }
                if !self.brant_still_lib()? && self.skill_available(ECHO)? {
                    self.a_echo(1.)?;
                }
                self.a_switch()
            }
            5 => {
                let start = self.now()?;
                self.wait_for_team(0.8, false, true)?;
                self.sleep(0.5, false)?;
                self.action(Op::NextFrame, &[])?;
                if self.lucilla_try_lib()? {
                    return Ok(());
                }
                while self.now()? - start < 7.2 {
                    if self.run(Method::Available as i32, &[LIBERATION, 1., 0.])?[0] != 0.
                        && !self.skill_available(LIBERATION)?
                    {
                        break;
                    }
                    if !self.skill_available(LIBERATION)?
                        && self.value(Op::Cooldown, &[LIBERATION, -1.])? > 1.5
                    {
                        break;
                    }
                    if self.lucilla_try_lib()? {
                        return Ok(());
                    }
                    if self.skill_available(RESONANCE)? {
                        self.action(Op::CaptureKey, &[RESONANCE])?;
                        self.action(Op::Key, &[1.])?;
                        let held = self.sleep(1.4, false);
                        if held.is_err() {
                            let _ = self.action(Op::Key, &[0.]);
                        }
                        held?;
                        self.action(Op::Key, &[0.])?;
                        self.record(RESONANCE)?;
                    } else {
                        self.heavy_attack(1.4)?;
                    }
                    self.action(Op::NextFrame, &[])?;
                }
                self.a_switch()
            }
            _ => Err(INVALID_ACTION),
        }
    }
    pub(super) fn advanced_priority(&self, args: &[f64]) -> ActionResult<f64> {
        match args[0] as i32 {
            1 => {
                if args[2] != 0. && args[3] != 0. {
                    return Ok(400.);
                }
            }
            2 => {
                return Ok(
                    if args[1] != 0. || self.a_local(0)? != 0. || self.a_local(1)? != 0. {
                        400.
                    } else {
                        0.
                    },
                )
            }
            3 | 5 => {
                if args[1] != 0. && args[2] != 0. && args[3] != 0. {
                    return Ok(400.);
                }
            }
            4 => {
                if self.value(Op::Elapsed, &[self.a_local(0)?, 1.])? < 4. {
                    return Ok(0.);
                }
                if self.brant_still_lib()? || (args[1] != 0. && args[2] != 0. && args[3] != 0.) {
                    return Ok(400.);
                }
            }
            _ => return Err(INVALID_ACTION),
        }
        self.a_base_priority()
    }
    pub(super) fn advanced_lifecycle(&self, args: &[f64]) -> ActionResult<f64> {
        match (args[0] as i32, args[1] as i32) {
            (0, 2) => Ok((self.value(Op::Highlighted, &[LIBERATION])? > 0.
                && self.feature("Augusta_lib1", 0.5, "")?) as u8 as f64),
            (0, 3) => Ok((!self.truth(Op::HasCd, &[RESONANCE, -1.])?) as u8 as f64),
            (0, 5) => {
                let size = self.value(Op::LoadParty, &[])? as i64;
                if size != 0 {
                    self.action(
                        Op::TapSlot,
                        &[((self.state(INDEX)? as i64 + 1) % size) as f64],
                    )?;
                }
                Ok(0.)
            }
            (1, 5) => {
                self.switch_other(false)?;
                Ok(0.)
            }
            (2, 0) => {
                self.run(Method::ResetState as i32, &[])?;
                self.a_set(0, 0.)?;
                self.a_set(1, 0.)?;
                self.a_set(2, 0.)?;
                Ok(0.)
            }
            (2, 1) => {
                self.action(Op::BaseSwitch, &[self.a_local(2)?, 1.])?;
                self.a_set(2, 0.)?;
                Ok(0.)
            }
            (4, 0) => {
                self.set(19., 0.)?;
                Ok(0.)
            }
            (4, 6) => {
                self.brant_landing(1.2, args[2] != 0.)?;
                Ok(0.)
            }
            _ => Err(INVALID_ACTION),
        }
    }
}
