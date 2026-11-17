use super::*;
impl Host {
    fn text_has(&self, word: &str) -> Result<bool> {
        Ok(self.query(20, word, &[])? != 0.)
    }
    pub(super) fn monthly_text(&self) -> Result<bool> {
        let (mut claim, mut days) = (false, false);
        self.query(
            22,
            "(剩余|剩餘)\\d+天|remaining\\d+days?|\\d+days?remaining",
            &[],
        )?;
        let count = self.value(Op::TextCount, &[])? as usize;
        for index in 0..count {
            self.action(Op::TextNormalize, &[index as f64])?;
            let matches = ((self.text_has("点击领取")? || self.text_has("點擊領取")?)
                && (self.text_has("今日")? || self.text_has("本日")?)
                && (self.text_has("月相")? || self.text_has("月卡")?))
                || (self.text_has("claim")?
                    && (self.text_has("today")? || self.text_has("daily")?)
                    && (self.text_has("lunite")? || self.text_has("subscription")?));
            claim |= matches;
            days |= self.flag(Op::RegexMatches, &[])?;
        }
        Ok(claim && days)
    }
    pub(super) fn reward_text(&self) -> Result<bool> {
        let mut heading = false;
        self.query(22,"^(获得(?:物品|奖励)?|獲得(?:物品|獎勵)?|rewardsobtained|itemsobtained|obtained)[.:：·]*$",&[])?;
        let count = self.value(Op::TextCount, &[])? as usize;
        for index in 0..count {
            self.action(Op::TextNormalize, &[index as f64])?;
            heading |= self.flag(Op::RegexMatches, &[])?;
            self.action(Op::TextAppend, &[])?;
        }
        if !heading {
            return Ok(false);
        }
        self.action(Op::TextCombined, &[])?;
        for word in [
            "点击空白",
            "點擊空白",
            "clickanywhere",
            "clickthedark",
            "tapanywhere",
        ] {
            if self.text_has(word)? {
                return Ok(true);
            }
        }
        Ok(false)
    }
    pub(super) fn matching_text(&self) -> Result<f64> {
        self.action(Op::RequestedRegex, &[])?;
        let count = self.value(Op::TextCount, &[])? as usize;
        for index in 0..count {
            self.action(Op::TextRaw, &[index as f64])?;
            if self.flag(Op::RegexMatches, &[])? {
                return Ok(index as f64);
            }
        }
        Ok(-1.)
    }
    pub(super) fn continue_marker(&self) -> Result<bool> {
        let size = self.call(Op::FrameSize, &[])?;
        let count = self.value(Op::ContourCount, &[])? as usize;
        for index in 0..count {
            let b = self.call(Op::ContourMetrics, &[index as f64])?;
            let aspect = b[0] / b[1];
            let fill = b[2] / (b[0] * b[1]);
            if b[0] >= size[0] * 0.006
                && b[0] <= size[0] * 0.023
                && b[1] >= size[1] * 0.012
                && b[1] <= size[1] * 0.04
                && (0.5..=1.9).contains(&aspect)
                && (0.12..=0.8).contains(&fill)
            {
                return Ok(true);
            }
        }
        Ok(false)
    }
}
pub fn monthly_day(hour: i32, valid: bool, milliseconds: i32) -> i32 {
    if !(1..=24).contains(&hour) || !valid {
        -1
    } else {
        (hour == 24 && milliseconds >= 86_370_000) as i32
    }
}
pub fn monthly_offset(seconds: i64) -> bool {
    (-30..90).contains(&seconds)
}
pub fn marker_mask(
    image: &[u8],
    width: usize,
    height: usize,
    stride: usize,
    output: &mut [u8],
    output_stride: usize,
) {
    for y in 0..height {
        for x in 0..width {
            let at = y * stride + x * 3;
            let p = &image[at..at + 3];
            let low = p[0].min(p[1]).min(p[2]);
            let high = p[0].max(p[1]).max(p[2]);
            output[y * output_stride + x] = if low >= 180 && high - low < 65 {
                255
            } else {
                0
            };
        }
    }
}
