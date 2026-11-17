use super::*;

impl Host {
    fn g_res(&self, click: bool) -> ActionResult<bool> {
        Ok(
            self.virtual_action(&[VIRTUAL_RESONANCE, 0., 0., click as u8 as f64, 0., 0., 1.])?[0]
                != 0.,
        )
    }
    fn g_lib(&self, wait: f64) -> ActionResult<bool> {
        Ok(self.virtual_action(&[VIRTUAL_LIBERATION, -1., 0., wait, 0., 1.])?[0] != 0.)
    }
    fn g_echo(&self, timeout: f64) -> ActionResult<bool> {
        Ok(self.virtual_action(&[VIRTUAL_ECHO, 0., 0., timeout])?[0] != 0.)
    }
    fn g_normal(&self, duration: f64) -> ActionResult {
        self.normal_attacks(duration, 0.1, 0., false, false)
    }
    fn g_switch(&self) -> ActionResult {
        self.virtual_action(&[VIRTUAL_SWITCH, 0., 0.])?;
        Ok(())
    }
    fn g_flying(&self) -> ActionResult<bool> {
        Ok(self.virtual_action(&[VIRTUAL_FLYING])?[0] != 0.)
    }
    fn g_down(&self) -> ActionResult {
        self.run(Method::WaitDown as i32, &[1.])?;
        Ok(())
    }
    fn g_forte(&self) -> ActionResult<bool> {
        Ok(self.virtual_action(&[VIRTUAL_FORTE])?[0] != 0.)
    }
    fn g_local(&self, index: usize) -> ActionResult<f64> {
        self.value(Op::LocalAt, &[index as f64])
    }
    fn g_set(&self, index: usize, value: f64) -> ActionResult {
        self.action(Op::SetLocalAt, &[index as f64, value])
    }
    fn g_related(&self, field: f64) -> ActionResult<bool> {
        self.truth(Op::Related, &[field])
    }
    fn g_right(&self, duration: f64) -> ActionResult {
        self.run(Method::RightClicks as i32, &[duration, 0.1, 0.])?;
        Ok(())
    }
    fn g_heavy_forte(&self) -> ActionResult<bool> {
        Ok(self.run(Method::HeavyForteKind as i32, &[0.])?[0] != 0.)
    }
    fn g_long(&self, variant: i32) -> ActionResult<bool> {
        Ok(self.run(Method::LongAction as i32, &[variant as f64])?[0] != 0.)
    }
    fn g_outro(&self, names: &str) -> ActionResult<bool> {
        Ok(self.observe(5, names, "", &[])? != 0.)
    }
    fn g_base_priority(&self) -> ActionResult<f64> {
        Ok(if self.truth(Op::HealerLocked, &[])? {
            0.
        } else {
            200.
        })
    }
    fn zhezhi_blue(&self) -> ActionResult<bool> {
        Ok(self.observe(
            3,
            "",
            "",
            &[5120., 2880., 2242., 2664., 58., 26., 244., 244., 244., 1.],
        )? > 0.3)
    }
    fn zhezhi_gauge(&self) -> ActionResult<f64> {
        let gauge = self.observe(
            10,
            "",
            "",
            &[
                5120., 2880., 2164., 2675., 736., 10., 235., 240., 185., 255., 255., 215., 3., 12.,
                14., 100.,
            ],
        )?;
        self.g_set(1, gauge)?;
        Ok(gauge)
    }
    fn zhezhi_con_lock(&self) -> ActionResult<bool> {
        Ok(self.g_related(1.)?
            || self.current_con()? < 0.6
            || (self.is_con_full()? && !self.g_related(1.)?))
    }
    fn zhezhi_until_not_blue(&self) -> ActionResult {
        let start = self.now()?;
        while !self.zhezhi_blue()? {
            if self.now()? - start > 0.3 {
                break;
            }
            self.action(Op::CheckCombat, &[])?;
            self.action(Op::NextFrame, &[])?;
        }
        let mut jump = self.now()? + 0.4;
        self.action(Op::Mouse, &[1.])?;
        let result: ActionResult = (|| {
            while self.skill_available(RESONANCE)? && self.zhezhi_blue()? {
                self.send(RESONANCE, 0.)?;
                if self.need_fast_perform()? && self.now()? - start > 1.1 {
                    break;
                }
                if self.is_con_full()? && (!self.g_related(0.)? || self.zhezhi_con_lock()?) {
                    break;
                }
                if self.now()? - start > 4. {
                    break;
                }
                if self.now()? > jump {
                    self.action(Op::Tap, &[3., 0.01, -1., 0.01])?;
                    jump += 0.4;
                }
                self.action(Op::CheckCombat, &[])?;
                self.action(Op::NextFrame, &[])?;
            }
            Ok(())
        })();
        if result.is_err() {
            let _ = self.action(Op::Mouse, &[0.]);
        }
        result?;
        self.action(Op::Mouse, &[0.])
    }
    fn zhezhi_interlock(&self) -> ActionResult {
        if self.state(INTRO)? != 0. {
            self.g_normal(1.3)?;
        }
        if self.g_flying()? {
            self.wait_with(
                || Ok(!self.g_flying()?),
                1.2,
                0.,
                || self.click(false, 0.1),
                || Ok(()),
            )?;
            self.g_right(0.05)?;
        }
        if !self.zhezhi_blue()?
            && self.zhezhi_gauge()? < 3.
            && !(self.is_con_full()? && self.g_related(1.)?)
        {
            self.g_normal(1.4)?;
            if !self.g_related(1.)? {
                return self.g_switch();
            }
        }
        if !self.zhezhi_blue()?
            && self.skill_available(RESONANCE)?
            && self.zhezhi_gauge()? > 1.
            && !(self.is_con_full()? && self.g_related(1.)?)
        {
            if self.zhezhi_con_lock()? && self.g_lib(0.1)? {
                self.sleep(0.2, true)?;
            }
            self.g_res(true)?;
            self.g_normal(0.8)?;
        }
        if self.zhezhi_con_lock()? && self.zhezhi_blue()? && self.skill_available(RESONANCE)? {
            self.zhezhi_until_not_blue()?;
            if self.zhezhi_con_lock()? && self.g_lib(0.5)? {
                self.sleep(0.2, true)?;
            } else if self.skill_available(ECHO)? {
                self.g_right(0.05)?;
            }
        }
        self.g_echo(2.)?;
        self.g_switch()
    }
    fn lupa_gauge(&self) -> ActionResult<f64> {
        if !self.g_forte()? {
            return Ok(0.);
        }
        self.observe(
            10,
            "",
            "",
            &[
                3840., 2160., 1633., 2004., 527., 12., 75., 75., 235., 105., 105., 255., 2., 19.,
                21., 400.,
            ],
        )
    }
    fn lupa_still_lib(&self) -> ActionResult<bool> {
        Ok(self.elapsed(self.state(LAST_LIBERATION)?)? < 12.)
    }
    fn lupa_wolf_res(&self, timeout: f64) -> ActionResult<bool> {
        let start = self.now()?;
        let mut clicked = false;
        loop {
            if !(self.g_local(0)? != 0. && self.now()? - start < 0.2)
                && !self.feature("lupa_wolf_icon2", 0.85, "")?
            {
                break;
            }
            self.send(RESONANCE, 0.)?;
            clicked = true;
            if self.now()? - start > timeout {
                break;
            }
            self.action(Op::CheckCombat, &[])?;
            self.action(Op::NextFrame, &[])?;
        }
        if clicked {
            self.set(LAST_LIBERATION, -1.)?;
            self.g_set(0, 0.)?;
            self.sleep(1.2, true)?;
        }
        Ok(clicked)
    }
    fn lupa_jumping(&self, duration: f64) -> ActionResult {
        let start = self.now()?;
        let mut click = false;
        while self.now()? - start <= duration {
            if click {
                self.click(false, -1.)?;
            } else {
                self.action(Op::Tap, &[3., 0.01, -1., 0.01])?;
            }
            click = !click;
            if self.lupa_gauge()? == 2. {
                return Ok(());
            }
            self.action(Op::CheckCombat, &[])?;
            self.action(Op::NextFrame, &[])?;
        }
        Ok(())
    }
    fn lupa_consume(&self, outro: bool) -> ActionResult {
        if self.g_flying()? {
            self.wait_with(
                || Ok(!self.g_forte()?),
                2.,
                0.,
                || self.click(false, 0.1),
                || Ok(()),
            )?;
        } else {
            self.heavy_attack(0.6)?;
            self.wait_with(
                || Ok(!self.g_forte()?),
                1.4,
                0.,
                || self.click(false, 0.1),
                || Ok(()),
            )?;
        }
        if !self.g_forte()? {
            self.g_set(0, 1.)?;
        }
        if outro {
            self.wait_with(
                || self.feature("lupa_wolf_icon2", 0.85, ""),
                0.5,
                0.,
                || Ok(()),
                || Ok(()),
            )?;
            self.lupa_wolf_res(1.)?;
        }
        Ok(())
    }
}

impl Host {
    pub(super) fn gauge_perform(&self, kind: i32) -> ActionResult {
        match kind {
            0 => {
                if self.g_related(0.)? {
                    return self.zhezhi_interlock();
                }
                if self.state(INTRO)? != 0. {
                    self.g_normal(1.5)?;
                }
                self.g_lib(0.1)?;
                if (self.g_local(0)? != 0. || self.zhezhi_blue()?)
                    && self.skill_available(RESONANCE)?
                {
                    self.g_set(0, 0.)?;
                    self.zhezhi_until_not_blue()?;
                    return self.g_switch();
                }
                if self.skill_available(RESONANCE)? && !self.g_forte()? {
                } else if self.skill_available(RESONANCE)? && self.g_forte()? {
                    self.g_res(true)?;
                    self.g_normal(0.8)?;
                    self.g_set(0, 1.)?;
                    return self.g_switch();
                }
                if !self.g_echo(1.)? {
                    self.g_normal(0.1)?;
                }
                self.g_switch()
            }
            1 => {
                let mut outro = false;
                if self.state(INTRO)? != 0. {
                    self.g_normal(1.)?;
                    outro = self.g_outro("chang_changli")?;
                }
                self.g_echo(0.)?;
                if self.lupa_wolf_res(1.)? && !outro {
                    return self.g_switch();
                }
                if self.lupa_gauge()? == 2. && !self.feature("lupa_wolf_icon2", 0.85, "")? {
                    self.lupa_consume(outro)?;
                    if !outro {
                        return self.g_switch();
                    }
                }
                if self.value(Op::Highlighted, &[RESONANCE])? > 0.05 && self.g_res(true)? {
                    self.set(LAST_LIBERATION, -1.)?;
                    if self.skill_available(LIBERATION)? {
                        self.g_down()?;
                    } else {
                        return self.g_switch();
                    }
                }
                self.action(Op::BreakF, &[0.])?;
                if (outro || !self.need_fast_perform()?) && self.g_lib(0.1)? {
                    self.g_normal(0.3)?;
                    if outro {
                        self.g_normal(1.)?;
                    } else {
                        return self.g_switch();
                    }
                }
                if self.lupa_still_lib()? {
                    self.lupa_jumping(4.)?;
                    self.lupa_consume(outro)?;
                    return self.g_switch();
                }
                self.g_normal(0.1)?;
                self.g_switch()
            }
            2 => self.ciaccona_perform(),
            3 => {
                if self.state(INTRO)? != 0. {
                    self.g_normal(1.)?;
                }
                if self.g_long(1)? && self.value(Op::Cooldown, &[LIBERATION, -1.])? <= 3.5 {
                    self.hiyuki_standard()?;
                }
                if self.g_long(2)?
                    && (self.hiyuki_perform_lib()?
                        || (self.skill_available(LIBERATION)? && self.hiyuki_hold_lib()?))
                {
                    self.sleep(0.5, true)?;
                }
                self.g_switch()
            }
            _ => Err(INVALID_ACTION),
        }
    }
    pub(super) fn gauge_priority(&self, args: &[f64]) -> ActionResult<f64> {
        match args[0] as i32 {
            0 => {
                if self.g_related(0.)? && self.g_related(1.)? {
                    return Ok(400.);
                }
            }
            1 => {
                if self.lupa_still_lib()? || (args[1] != 0. && args[2] != 0. && args[3] != 0.) {
                    return Ok(400.);
                }
            }
            2 => {
                if self.g_local(0)? == 2.
                    && self.g_local(1)? != 0.
                    && self.elapsed(self.state(LAST_LIBERATION)?)? < 20.
                {
                    return Ok(0.);
                }
                if self.g_local(0)? == 3.
                    && self.g_local(1)? != 0.
                    && (self.elapsed(self.state(LAST_LIBERATION)?)? < 8. || !self.g_related(2.)?)
                {
                    return Ok(0.);
                }
            }
            3 => {
                if args[1] != 0. && args[2] != 0. && args[3] != 0. {
                    return Ok(400.);
                }
            }
            _ => return Err(INVALID_ACTION),
        }
        self.g_base_priority()
    }
    pub(super) fn gauge_lifecycle(&self, args: &[f64]) -> ActionResult<f64> {
        match (args[0] as i32, args[1] as i32) {
            (0, 0) => {
                self.run(Method::ResetState as i32, &[])?;
                self.g_set(0, 0.)?;
                self.action(Op::SetRelated, &[0.])?;
                self.g_set(1, 0.)?;
                Ok(0.)
            }
            (0, 1) => {
                self.g_set(1, 0.)?;
                Ok(0.)
            }
            (0, 2) => Ok(((self.state(CURRENT)? != 0. && self.zhezhi_blue()?)
                || self.run(Method::Available as i32, &[RESONANCE, 0., 1.])?[0] != 0.)
                as u8 as f64),
            (1, 0) => {
                self.run(Method::ResetState as i32, &[])?;
                self.g_set(0, 0.)?;
                Ok(0.)
            }
            (1, 4) => {
                self.set(19., 0.)?;
                Ok(0.)
            }
            (2, 0) => {
                self.run(Method::ResetState as i32, &[])?;
                self.g_set(0, 0.)?;
                self.action(Op::SetRelated, &[0.])?;
                self.g_set(2, 0.)?;
                Ok(0.)
            }
            (2, 1) => {
                self.g_set(2, 0.)?;
                self.action(Op::BaseSwitchOut, &[args[2]])?;
                Ok(0.)
            }
            (2, 2) => Ok((self.elapsed(self.state(LAST_LIBERATION)?)? < 2.) as u8 as f64),
            (2, 3) => Ok(0.),
            (2, 4) => {
                self.set(INTRO_MOTION, 0.73)?;
                Ok(0.)
            }
            (2, 5) => {
                if self.g_local(2)? != 0. {
                    self.g_set(2, 0.)?;
                    self.switch_other(false)?;
                }
                Ok(0.)
            }
            (2, 6) => {
                if self.is_con_full()? {
                    self.g_set(3, self.now()?)?;
                }
                self.action(Op::BaseSwitch, &[args[2], args[3]])?;
                Ok(0.)
            }
            (2, 7) => Ok((self.elapsed(self.g_local(3)?)? < 30.) as u8 as f64),
            (3, 1) => {
                self.action(Op::BaseSwitchOut, &[args[2]])?;
                self.g_set(0, 0.)?;
                Ok(0.)
            }
            _ => Err(INVALID_ACTION),
        }
    }
    pub(super) fn gauge_send(&self, args: &[f64]) -> ActionResult {
        if args[0] == LIBERATION {
            self.g_set(2, 1.)?;
        }
        self.run(Method::Send as i32, args)?;
        Ok(())
    }
}

impl Host {
    fn ciaccona_gauge(&self) -> ActionResult<f64> {
        if self.truth(Op::Forte, &[0.])? {
            return Ok(3.);
        }
        self.observe(
            10,
            "",
            "",
            &[
                3840., 2160., 1612., 1987., 576., 21., 180., 240., 70., 210., 255., 100., 3., 12.,
                14., 100.,
            ],
        )
    }
    fn ciaccona_decide(&self) -> ActionResult {
        let size = self.value(Op::LoadParty, &[])? as usize;
        for i in 0..size {
            if self.party_field(i, 0.)? != 0. {
                if self.observe(9, "Cartethyia", "", &[i as f64])? != 0. {
                    self.action(Op::SelectParty, &[i as f64])?;
                    self.action(Op::SetRelated, &[1.])?;
                    self.g_set(0, 3.)?;
                    return Ok(());
                }
                if self.observe(9, "Phoebe", "", &[i as f64])? != 0.
                    || self.observe(9, "Zani", "", &[i as f64])? != 0.
                {
                    self.g_set(0, 2.)?;
                    return Ok(());
                }
            }
        }
        self.g_set(0, 1.)
    }
    fn ciaccona_jumping(&self, duration: f64) -> ActionResult {
        let start = self.now()?;
        let mut click = true;
        while self.now()? - start <= duration {
            if click {
                self.click(false, -1.)?;
            } else {
                self.action(Op::Tap, &[3., 0.01, -1., 0.01])?;
            }
            click = !click;
            self.action(Op::CheckCombat, &[])?;
            self.action(Op::NextFrame, &[])?;
        }
        Ok(())
    }
    fn ciaccona_perform(&self) -> ActionResult {
        self.g_set(1, 0.)?;
        let (mut wait, mut jump) = (false, true);
        if self.g_local(0)? == 0. {
            self.ciaccona_decide()?;
        }
        if self.state(INTRO)? != 0. {
            self.g_normal(0.8)?;
            // Preserve the original override's constant false result.
            self.g_normal(0.7)?;
        }
        if self.value(Op::Highlighted, &[ECHO])? < 0.22 {
            self.g_echo(0.)?;
        }
        if self.state(INTRO)? == 0. && !self.truth(Op::Forte, &[0.])? {
            self.ciaccona_jumping(0.4)?;
            self.wait_with(
                || Ok(!self.g_flying()?),
                1.2,
                0.,
                || self.click(false, 0.1),
                || Ok(()),
            )?;
            self.g_normal(0.2)?;
        }
        if self.g_res(true)? {
            jump = false;
            wait = true;
        }
        if self.ciaccona_gauge()? >= 3. {
            if jump {
                let start = self.now()?;
                while !self.g_flying()? {
                    self.action(Op::Tap, &[3., 0.01, -1., 0.01])?;
                    if self.now()? - start > 0.3 {
                        break;
                    }
                    self.action(Op::NextFrame, &[])?;
                }
            }
            self.g_heavy_forte()?;
            wait = true;
        }
        if self.skill_available(LIBERATION)? {
            if wait {
                self.sleep(0.4, true)?;
            }
            if self.g_lib(0.1)? {
                self.g_set(1, 1.)?;
                if self.g_local(0)? == 2. {
                    let start = self.now()?;
                    while self.now()? - start < 0.6 {
                        self.action(Op::Tap, &[0., 0., -1., 0.01])?;
                    }
                }
            }
        }
        if self.g_local(1)? == 0. && self.value(Op::Highlighted, &[ECHO])? > 0.25 {
            self.g_echo(1.)?;
        }
        self.g_switch()
    }
    fn hiyuki_timeout(&self) -> ActionResult<f64> {
        Ok(
            if self.state(INTRO)? != 0. && self.g_outro("char_linnai")? {
                18.
            } else {
                16.
            },
        )
    }
    fn hiyuki_heavy_ready(&self) -> ActionResult<bool> {
        self.feature("hiyuki_lib_forte", 0.7, "")
    }
    fn hiyuki_locked(&self) -> ActionResult<bool> {
        self.wait_with(
            || self.g_long(2),
            2.,
            0.,
            || self.click(false, -1.),
            || Ok(()),
        )
    }
    fn hiyuki_key(&self, down: bool) -> ActionResult {
        self.action(Op::CaptureKey, &[LIBERATION])?;
        self.action(Op::Key, &[down as u8 as f64])
    }
    fn hiyuki_hold_lib(&self) -> ActionResult<bool> {
        if !self.flag(USE_LIBERATION)? {
            return Ok(false);
        }
        let start = self.now()?;
        let mut last = 0.;
        let result: ActionResult = (|| {
            while self.truth(Op::InTeam, &[])?
                && (self.skill_available(LIBERATION)?
                    || self.value(Op::Cooldown, &[LIBERATION, -1.])? <= 1.5)
                && self.now()? - start < 8.
            {
                if self.now()? - start > last {
                    self.hiyuki_key(true)?;
                    last += 0.2;
                }
                self.sleep(0.05, true)?;
            }
            self.set_flag(IN_LIBERATION, true)?;
            self.hiyuki_key(false)?;
            self.wait_for_team(3., false, true)?;
            self.set_flag(IN_LIBERATION, false)
        })();
        if result.is_err() {
            let _ = self.set_flag(IN_LIBERATION, false);
            let _ = self.hiyuki_key(false);
        }
        result?;
        let duration = self.now()? - start;
        let now = self.now()?;
        self.action(Op::FreezeAdd, &[now, start, duration])?;
        Ok(true)
    }
    fn hiyuki_standard(&self) -> ActionResult {
        let timeout = self.hiyuki_timeout()?;
        while self.g_long(1)? && self.elapsed(self.state(LAST_PERFORM)?)? < timeout {
            self.g_echo(0.)?;
            self.g_res(false)?;
            if self.skill_available(LIBERATION)? && self.g_lib(0.1)? {
                return Ok(());
            }
            if self.truth(Op::Forte, &[0.])? {
                self.click(true, -1.)?;
                self.g_heavy_forte()?;
                self.wait_with(
                    || self.skill_available(LIBERATION),
                    16.,
                    0.,
                    || self.click(false, 0.1),
                    || Ok(()),
                )?;
                if self.g_lib(0.1)? {
                    return Ok(());
                }
            } else {
                self.click(false, 0.1)?;
            }
            self.sleep(0.05, true)?;
        }
        Ok(())
    }
    fn hiyuki_perform_lib(&self) -> ActionResult<bool> {
        let start = self.now()?;
        let timeout = self.hiyuki_timeout()?;
        while self.g_long(2)? && self.elapsed(start)? < timeout {
            self.action(Op::BreakF, &[0.])?;
            self.g_echo(0.)?;
            self.hiyuki_heavy_ready()?;
            let timed = self.elapsed(start)? >= timeout - 0.5;
            if self.skill_available(LIBERATION)?
                && (self.g_local(0)? >= 4. || timed)
                && self.hiyuki_hold_lib()?
            {
                self.g_set(0, 0.)?;
                return Ok(true);
            }
            if self.g_res(false)? {
                if timed {
                    break;
                }
                self.g_normal(0.3)?;
            } else if self.hiyuki_heavy_ready()? {
                if timed {
                    break;
                }
                if self.hiyuki_locked()? {
                    self.heavy_forte(|| self.hiyuki_heavy_ready())?;
                }
                if self.wait_with(
                    || self.skill_available(LIBERATION),
                    0.5,
                    0.,
                    || self.click(false, -1.),
                    || Ok(()),
                )? && self.hiyuki_locked()?
                    && self.hiyuki_hold_lib()?
                {
                    self.g_set(0, 0.)?;
                    return Ok(true);
                }
                self.g_set(0, self.g_local(0)? + 1.)?;
                self.sleep(0.1, true)?;
            } else if self.feature("hiyuki_left", 0.5, "")? {
                self.wait_with(
                    || Ok(!self.feature("hiyuki_left", 0.5, "")?),
                    3.,
                    0.,
                    || self.click(false, -1.),
                    || Ok(()),
                )?;
                if timed {
                    break;
                }
                self.sleep(0.1, true)?;
            } else if self.feature("hiyuki_right", 0.5, "")? {
                self.click(true, 1.)?;
                self.sleep(0.1, true)?;
            } else {
                self.click(false, -1.)?;
            }
            self.sleep(0.05, true)?;
        }
        if self.skill_available(LIBERATION)? && self.hiyuki_hold_lib()? {
            self.g_set(0, 0.)?;
            return Ok(true);
        }
        Ok(false)
    }
}
