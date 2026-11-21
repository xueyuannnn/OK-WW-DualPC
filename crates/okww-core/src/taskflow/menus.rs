use super::*;
const CONFIRMS: &str = "confirm_btn_hcenter_vcenter\0confirm_btn_highlight_hcenter_vcenter";
const LOGIN: &str = "登录|登入|Log|点击连接|Click to Connect";
impl Host {
    fn m_round(&self, value: f64) -> Result<f64> {
        crate::execution::pixel_round(value)
            .map(|n| n as f64)
            .ok_or(INVALID_ACTION)
    }
    fn m_rect(&self, slot: usize, matched: bool) -> Result<[f64; 4]> {
        let op = if matched {
            Op::MatchField
        } else {
            Op::AreaField
        };
        let mut rect = [0.; 4];
        for (index, value) in rect.iter_mut().enumerate() {
            *value = self.value(op, &[slot as f64, index as f64])?;
        }
        Ok(rect)
    }
    fn m_pixels(&self, slot: usize, rect: [f64; 4]) -> Result {
        self.action(
            Op::SetPixelArea,
            &[slot as f64, rect[0], rect[1], rect[2], rect[3]],
        )
    }
    pub(super) fn auto_pick(&self) -> Result<bool> {
        if !self.flag(Op::InTeam, &[])? {
            return Ok(false);
        }
        self.query(5, "pick_up_f_hcenter_vcenter", &[0.])?;
        let r = self.m_rect(0, false)?;
        self.m_pixels(
            0,
            [
                self.m_round(r[0] - 0.3 * r[2])?,
                self.m_round(r[1] - 5. * r[3])?,
                self.m_round(r[2] * 1.65)?,
                self.m_round(r[3] * 7.5)?,
            ],
        )?;
        let end = self.now()? + 1.;
        while self.now()? < end {
            if self.query(
                0,
                "pick_up_f_hcenter_vcenter",
                &[0., 0.8, 0.002, 0.002, 0., 0., 1.],
            )? == 0.
            {
                return Ok(false);
            }
            let r = self.m_rect(0, true)?;
            if self.query(11, "", &[0., 235., 235., 235., 255., 255., 255.])? < 0.5 {
                self.m_sleep(0.02)?;
                continue;
            }
            self.m_pixels(1, [r[0] + r[2] * 3., r[1] - r[3], r[2] * 3., r[3] * 3.])?;
            let speaking =
                self.query(0, "dialog_3_dots", &[1., 0.6, 0.002, 0.002, 0., 1., 1.])? != 0.;
            let entries = self.query(
                6,
                if speaking {
                    "Pick Up White List"
                } else {
                    "Pick Up Black List"
                },
                &[],
            )? as usize;
            let mut matched = false;
            if entries > 0 {
                let text = [
                    self.m_round(r[0] + r[2] * 5.2)?,
                    self.m_round(r[1] - 0.8 * r[3])?,
                    r[2] * 7.,
                    self.m_round(r[3] * 5.5)?,
                ];
                self.m_pixels(2, text)?;
                let count = self.m_text(0, 2)?;
                for line in 0..count {
                    for entry in 0..entries {
                        if self.query(7, "", &[0., line as f64, entry as f64])? != 0. {
                            matched = true;
                            break;
                        }
                    }
                    if matched {
                        if self.value(Op::LineField, &[0., line as f64, 1.])? > text[1] + r[3] {
                            self.action(Op::Scroll, &[1.])?;
                        }
                        break;
                    }
                }
            }
            if (speaking && !matched) || (!speaking && matched) {
                return Ok(false);
            }
            for _ in 0..3 {
                self.query(8, "f", &[0.2, 0.02])?;
            }
            return Ok(true);
        }
        Ok(false)
    }
    pub(super) fn auto_dialog(&self) -> Result<bool> {
        if self.flag(Op::InTeam, &[])? {
            return Ok(false);
        }
        let mut skipped = false;
        let deadline = self.now()? + 5.;
        while self.now()? < deadline {
            if self.query(0, "skip_dialog", &[0., 0.75, 0.02, 0.002, 2., -1., 1.])? == 0.
                && self.query(0, "skip_dialog_new", &[0., 0.75, 0.002, 0.002, 2., -1., 1.])? == 0.
            {
                break;
            }
            self.m_click(0, 0.2)?;
            skipped = true;
        }
        if skipped {
            self.wait_for(
                || {
                    if self.flag(Op::SkipConfirm, &[])? || self.flag(Op::InTeam, &[])? {
                        return Ok(true);
                    }
                    if self.m_any(
                        "skip_quest_confirm\0skip_quest_confirm_new",
                        0,
                        0.8,
                        0.002,
                        0.002,
                    )? {
                        self.m_sleep(0.2)?;
                        self.m_click(0, 0.)?;
                        return Ok(true);
                    }
                    Ok(false)
                },
                3.,
            )?;
            return Ok(true);
        }
        if self.m_find("message", 0, 0.8, 0.15, 0.002)?
            && self.m_find("message_dialog", 1, 0.8, 0.2, 0.4)?
        {
            let r = self.m_rect(1, true)?;
            let size = self.call(Op::FrameSize, &[])?;
            self.action(
                Op::ClickAbsolute,
                &[r[0] + size[0] * 0.315, r[1] + r[3] * 3., 0.2],
            )?;
            return Ok(true);
        }
        if self.query(9, "继续普通NPC对白", &[0.])? != 0. && self.flag(Op::ContinueMarker, &[])?
        {
            self.action(Op::SetArea, &[0., 0.35, 0.73, 0.65, 0.79, 0., 0., 1.])?;
            let size = self.call(Op::FrameSize, &[])?;
            if self.value(Op::HsvCount, &[0.])? < size[0] * size[1] * 0.00008 {
                return Ok(false);
            }
            self.query(2, "", &[0., 0., 0.85])?;
            if !self.m_match("^[\\x{4e00}-\\x{9fff}A-Za-z· :]{1,24}$", 0, 0)? {
                return Ok(false);
            }
            self.m_area(1, [0.12, 0.79, 0.88, 0.92], false, false)?;
            let count = self.query(2, "", &[1., 1., 0.85])? as usize;
            for line in 0..count {
                if self.value(Op::LineField, &[1., line as f64, 4.])? >= 2. {
                    self.action(Op::ClickRelative, &[0.5, 0.945, 0.3, 0.])?;
                    return Ok(true);
                }
            }
        }
        Ok(false)
    }
    fn m_find(&self, name: &str, slot: usize, threshold: f64, h: f64, v: f64) -> Result<bool> {
        Ok(self.query(0, name, &[slot as f64, threshold, h, v, 0., -1., 1.])? != 0.)
    }
    fn m_any(&self, names: &str, slot: usize, threshold: f64, h: f64, v: f64) -> Result<bool> {
        for name in names.split('\0') {
            if self.m_find(name, slot, threshold, h, v)? {
                return Ok(true);
            }
        }
        Ok(false)
    }
    fn m_sleep(&self, seconds: f64) -> Result {
        self.action(Op::Sleep, &[seconds])
    }
    fn m_click(&self, slot: usize, after: f64) -> Result {
        self.action(Op::ClickMatch, &[slot as f64, after])
    }
    fn m_area(&self, slot: usize, coordinates: [f64; 4], hc: bool, vc: bool) -> Result {
        self.action(
            Op::SetArea,
            &[
                slot as f64,
                coordinates[0],
                coordinates[1],
                coordinates[2],
                coordinates[3],
                hc as u8 as f64,
                vc as u8 as f64,
                0.,
            ],
        )
    }
    fn m_text(&self, lines: usize, area: usize) -> Result<usize> {
        Ok(self.query(2, "", &[lines as f64, area as f64, 0.8])? as usize)
    }
    fn m_match(&self, pattern: &str, lines: usize, target: usize) -> Result<bool> {
        Ok(self.query(3, pattern, &[lines as f64, target as f64])? != 0.)
    }
    pub(super) fn skip_confirm(&self) -> Result<bool> {
        if !self.m_any(CONFIRMS, 0, 0.8, 0.1, 0.1)? {
            return Ok(false);
        }
        if !self.m_find("skip_dialog_check", 1, 0.8, 0.1, 0.1)? {
            self.action(Op::SetArea, &[0., 0.35, 0.45, 0.55, 0.65, 0., 0., 1.])?;
            if self.query(
                0,
                "skip_dialog_check",
                &[1., 0.8, 0.002, 0.002, 0., 0., 0.75],
            )? == 0.
            {
                return Ok(false);
            }
        }
        self.m_sleep(0.5)?;
        self.m_click(1, 0.5)?;
        self.m_click(0, 0.2)?;
        Ok(true)
    }
    pub(super) fn travel_button(&self) -> Result<bool> {
        for name in ["fast_travel_custom", "gray_teleport", "remove_custom"] {
            if !self.m_find(name, 0, 0.7, 0.002, 0.002)? {
                continue;
            }
            self.m_sleep(0.5)?;
            if !self.m_find(name, 0, 0.7, 0.002, 0.002)? {
                continue;
            }
            self.m_click(0, 1.)?;
            if self.query(4, "fast_travel_custom", &[0.])? != 0.
                && self.wait_for(|| self.m_any(CONFIRMS, 1, 0.6, 0.002, 0.002), 2.)?
            {
                self.action(Op::ClickRelative, &[0.49, 0.55, 0.5, 0.])?;
                self.m_click(1, 0.5)?;
                if self.wait_for(|| self.m_any(CONFIRMS, 1, 0.6, 0.002, 0.002), 1.)? {
                    self.m_click(1, 0.)?;
                }
            }
            if self.query(4, "remove_custom", &[0.])? == 0. {
                self.wait_for(
                    || Ok(self.flag(Op::SkipConfirm, &[])? || self.flag(Op::InTeam, &[])?),
                    3.,
                )?;
            }
            return Ok(true);
        }
        Ok(false)
    }
    pub(super) fn fast_travel(&self) -> Result<bool> {
        if !self.m_find("gray_teleport", 0, 0.8, 0.002, 0.002)? {
            return Ok(false);
        }
        self.m_area(0, [0.7, 0.89, 1., 1.], false, false)?;
        self.m_text(0, 0)?;
        if !self.m_match("Travel|快速旅行|前往|Proceed", 0, 0)? {
            return Ok(false);
        }
        self.flag(Op::TravelButton, &[])
    }
    pub(super) fn login(&self) -> Result<bool> {
        if self.flag(Op::CloudMode, &[])? && !self.flag(Op::InTeam, &[])? {
            self.m_area(0, [0.2, 0.25, 0.8, 0.75], false, false)?;
            self.m_text(0, 0)?;
            if self.m_match("云服务错误|雲服務錯誤|(?:^|[^0-9])(?:1020|4012)(?:[^0-9]|$)|已和服务器断开连接|cloud.*disconnect", 0, 0)?
                && self.m_match("^(?:退出游戏|退出遊戲|确定|確定|Exit Game|OK|Confirm)$", 0, 0)? {
                self.action(Op::ReleaseInput, &[])?;
                self.action(Op::SetLoggedIn, &[0.])?;
                self.action(Op::ClickText, &[0., 3.])?;
                return Ok(true);
            }
        }
        if self.flag(Op::MonthlyCard, &[])? {
            return Ok(true);
        }
        if (!self.flag(Op::CloudMode, &[])? && self.flag(Op::LoggedIn, &[])?)
            || self.flag(Op::InTeam, &[])?
        {
            return Ok(false);
        }
        if self.m_find("login_close", 0, 0.8, 0.15, 0.1)? {
            self.m_click(0, 1.)?;
            return Ok(true);
        }
        self.m_area(0, [0.3, 0.3, 0.7, 0.7], true, true)?;
        self.m_text(0, 0)?;
        if self.m_match(LOGIN, 0, 0)? {
            if self.m_match("\\+86", 0, 1)? {
                return Ok(false);
            }
            self.m_sleep(4.)?;
            self.m_text(0, 0)?;
            if self.m_match(LOGIN, 0, 0)? && !self.m_match("\\+86", 0, 1)? {
                self.action(Op::ClickText, &[0., 1.])?;
                return Ok(true);
            }
            return Ok(false);
        }
        self.action(Op::SetArea, &[1., 0., 0., 1., 1., 0., 0., 1.])?;
        self.m_text(1, 1)?;
        if self.m_match("游戏即将重启|遊戲即將重啟|game will restart", 1, 0)? {
            if self.m_match("^(?:确认|確認|Confirm)$", 1, 0)? {
                self.action(Op::ClickText, &[0., 0.])?;
                self.m_sleep(90.)?;
                return Ok(true);
            }
            return Ok(false);
        }
        self.m_area(2, [0.5, 0.5, 1., 1.], false, false)?;
        self.m_text(2, 2)?;
        if self.m_match("开始游戏|进入游戏|進入遊戲|Enter Game", 2, 0)?
            && !self.m_match(LOGIN, 2, 1)?
        {
            self.action(Op::ClickText, &[0., 1.])?;
            return Ok(true);
        }
        if self.m_find("switch_account", 0, 0.7, 0.002, 0.1)? {
            self.m_area(3, [0.37, 0.63, 0.63, 0.99], true, true)?;
            if self.m_text(3, 3)? != 0 {
                self.action(Op::ClickPoint, &[0.503, 0.926, 3., 1., 1.])?;
                return Ok(true);
            }
        }
        Ok(false)
    }
}
