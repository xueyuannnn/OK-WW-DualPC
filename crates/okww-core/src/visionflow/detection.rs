use super::*;

impl Host {
    pub(super) fn run_dialog(&self) -> Result<bool> {
        Ok(self.flag(Op::RunAnchor, &[0.])?
            && self.flag(Op::RunAnchor, &[1.])?
            && self.flag(Op::RunAnchor, &[2.])?)
    }
    pub(super) fn level_confirmed(&self) -> Result<bool> {
        if self.flag(Op::FullLevel, &[])? {
            return Ok(true);
        }
        let count = self.scalar(Op::TextCount, &[])? as usize;
        for prefix in 0..count {
            if !self.flag(Op::LevelPrefix, &[prefix as f64])? {
                continue;
            }
            for number in 0..count {
                if !self.flag(Op::LevelNumber, &[number as f64])? {
                    continue;
                }
                let [height, gap, difference] =
                    self.call(Op::LevelGeometry, &[prefix as f64, number as f64])?;
                if difference.abs() <= height * 0.6 && gap >= -height * 0.5 && gap <= height * 2. {
                    return Ok(true);
                }
            }
        }
        Ok(false)
    }
    pub(super) fn refresh_cooldowns(&self, index: f64) -> Result {
        if self.flag(Op::CooldownVersionMatches, &[])? || index < 0. {
            return Ok(());
        }
        let hud = self.call(Op::ReadHud, &[])?;
        if hud[0] == 0. || hud[1] != index {
            return Ok(());
        }
        let now = self.scalar(Op::Now, &[])?;
        if self.flag(Op::CooldownExists, &[index])?
            && now - self.scalar(Op::CooldownTimestamp, &[index])? < 0.1
        {
            return Ok(());
        }
        self.action(Op::CopyCooldowns, &[index])?;
        self.action(Op::SetCooldownTimestamp, &[self.scalar(Op::Now, &[])?])?;
        let [w, h, _] = self.call(Op::ImageSize, &[])?;
        self.action(Op::CooldownArea, &[])?;
        let mut recognized = 0;
        let mut seen = 0_u32;
        self.collect_cooldowns(false, w, h, &mut recognized, &mut seen)?;
        let fallback = self.scalar(Op::FallbackAt, &[])?;
        if recognized < 3 && (fallback < 0. || now - fallback >= 0.3) {
            self.action(Op::SetFallbackAt, &[now])?;
            self.collect_cooldowns(true, w, h, &mut recognized, &mut seen)?;
        }
        self.action(Op::CommitCooldowns, &[index])?;
        self.action(Op::StoreCooldownVersion, &[])
    }
    fn collect_cooldowns(
        &self,
        enlarged: bool,
        w: f64,
        h: f64,
        recognized: &mut i32,
        seen: &mut u32,
    ) -> Result {
        let count = self.scalar(
            Op::ReadCooldownText,
            &[enlarged as u8 as f64, if enlarged { 0.65 } else { 0.7 }, 2.],
        )? as usize;
        for line in 0..count {
            let [x, y, _] = self.call(Op::CooldownTextCenter, &[line as f64])?;
            if y < h * 0.885 {
                continue;
            }
            if !self.flag(Op::ParseCooldown, &[line as f64])? {
                continue;
            }
            let seconds = self.scalar(Op::ParsedCooldown, &[])?;
            if !(0. ..=60.).contains(&seconds) {
                continue;
            }
            let skill = if x < w * (1. - (1. - 0.86) * w.min(h * 16. / 9.) / w) {
                0
            } else if x > w * (1. - (1. - 0.91) * w.min(h * 16. / 9.) / w) {
                2
            } else {
                1
            };
            let seconds = if *seen & (1 << skill) != 0 {
                self.scalar(Op::WorkingCooldown, &[skill as f64])?
                    .max(seconds)
            } else {
                seconds
            };
            self.action(Op::SetWorkingCooldown, &[skill as f64, seconds])?;
            *seen |= 1 << skill;
            *recognized += 1;
        }
        Ok(())
    }
    pub(super) fn scaled(&self, a: &[f64]) -> Result {
        let w = self.scalar(Op::PixelRound, &[a[2] * a[4]])?;
        let h = self.scalar(Op::PixelRound, &[a[3] * a[5]])?;
        self.action(
            Op::StoreScaled,
            &[
                self.scalar(Op::PixelRound, &[a[0] + a[2] / 2. - w / 2.])?
                    .max(0.),
                self.scalar(Op::PixelRound, &[a[1] + a[3] / 2. - h / 2.])?
                    .max(0.),
                w,
                h,
            ],
        )
    }
    fn observe_target(&self, area: f64) -> Result<bool> {
        self.action(Op::TargetArea, &[area])?;
        self.action(Op::FindTargetRaw, &[0.6])?;
        if !self.flag(Op::RawExists, &[])? || self.scalar(Op::RawConfidence, &[])? < 0.82 {
            self.action(Op::FindTargetYes, &[0.8, 0.9])?;
            self.action(Op::FindTargetNo, &[0.8, 0.9])?;
            if self.flag(Op::YesExists, &[])?
                && (!self.flag(Op::NoExists, &[])?
                    || self.scalar(Op::YesConfidence, &[])? > self.scalar(Op::NoConfidence, &[])?)
            {
                self.action(Op::AdoptYes, &[])?;
            } else if self.flag(Op::NoExists, &[])? {
                self.action(Op::AdoptNo, &[])?;
            }
        }
        if self.flag(Op::RawExists, &[])?
            && self.scalar(Op::RawConfidence, &[])? >= 0.75
            && self.scalar(Op::RingCount, &[])? > 0.
            && self.flag(Op::RawSizeMatches, &[])?
        {
            let ratio = self.scalar(Op::GoldRatio, &[])?;
            if ratio >= 0.35 {
                self.action(Op::NameRaw, &[1.])?;
            } else if ratio <= 0.1 {
                self.action(Op::NameRaw, &[0.])?;
            }
        }
        self.flag(Op::RawExists, &[])
    }
    pub(super) fn target(&self, short: bool, extended: bool) -> Result<[f64; 3]> {
        if self.flag(Op::CloudMode, &[])? {
            self.action(Op::CloudTargetPrepare, &[])?;
            let matched = self.flag(Op::RawExists, &[])?;
            let locked = matched && self.scalar(Op::CloudTargetGold, &[])? >= 0.02;
            return Ok([matched as u8 as f64, locked as u8 as f64, 0.]);
        }
        let w = self.scalar(Op::ImageSize, &[])?;
        self.action(Op::TargetNames, &[(w == 1600.) as u8 as f64])?;
        self.action(Op::LoadTargetRing, &[])?;
        let mut matched = self.observe_target(0.)?;
        if !matched {
            matched = self.observe_target(1.)?;
        }
        if !matched {
            matched = self.observe_target(2.)?;
        }
        if !matched && short {
            matched = self.observe_target(3.)?;
        }
        let mut expanded = false;
        if !matched && extended {
            matched = self.observe_target(4.)?;
            expanded = matched;
        }
        Ok([
            matched as u8 as f64,
            (matched && self.flag(Op::RawHasTarget, &[])?) as u8 as f64,
            expanded as u8 as f64,
        ])
    }
    pub(super) fn health(&self, in_combat: bool) -> Result<bool> {
        let [w, h, _] = self.call(Op::ImageSize, &[])?;
        let min_height = (h * 9. / 2160.) as i32;
        let min_width = (w * if in_combat { 12. } else { 100. } / 3840.) as i32;
        if self.flag(
            Op::NormalHealth,
            &[min_width as f64, min_height as f64, (min_height * 3) as f64],
        )? {
            return Ok(true);
        }
        if self.scalar(Op::BossHealth, &[min_width as f64, min_height as f64 * 1.3])? == 1. {
            return Ok(true);
        }
        Ok(self.flag(Op::StreamedMode, &[])?
            && self.flag(
                Op::StreamedHealth,
                &[
                    min_width as f64,
                    min_height as f64,
                    (min_height * 3) as f64,
                    0.6,
                ],
            )?)
    }
    pub(super) fn death(&self) -> Result<bool> {
        Ok(self.flag(Op::DeathAnchor, &[0.])? && self.flag(Op::DeathAnchor, &[1.])?)
    }
    pub(super) fn challenge(&self) -> Result<[f64; 3]> {
        self.action(Op::ReadChallengeTitles, &[])?;
        let success = self.flag(Op::ChallengeTitle, &[1.])?;
        let failure = self.flag(Op::ChallengeTitle, &[0.])?;
        if (!success && !failure) || !self.flag(Op::ChallengeButton, &[])? {
            return Ok([0.; 3]);
        }
        Ok([1., success as u8 as f64, 0.])
    }
    pub(super) fn forte(&self, kind: f64) -> Result<bool> {
        if kind == 0. {
            return self.flag(Op::MouseForte, &[0.6, 0.025, 0.015]);
        }
        if kind == 1. {
            return self.flag(Op::ResonanceForte, &[0.6, 0.025]);
        }
        Ok(self.scalar(Op::BarForte, &[])? > 0.08)
    }
    pub(super) fn white(&self) -> Result<f64> {
        self.action(Op::WhiteArea, &[])?;
        let strict = self.scalar(Op::WhiteRatio, &[244.])?;
        if strict > 0. {
            return Ok(strict);
        }
        let tolerant = self.scalar(Op::WhiteRatio, &[222.])?;
        let [w, h, _] = self.call(Op::WhiteSize, &[])?;
        Ok(if tolerant * w * h >= 3. { tolerant } else { 0. })
    }
}
