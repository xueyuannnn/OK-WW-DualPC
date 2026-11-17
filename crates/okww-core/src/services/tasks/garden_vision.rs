use crate::services::*;
impl Host {
    fn reward_choice(&self) -> Result {
        self.action(Op::ReadGardenRewardButtons, &[])?;
        if !self.flag(Op::FindGardenRewardConfirm, &[])?
            || !self.flag(Op::HasGardenRewardSkip, &[])?
        {
            return Ok(());
        }
        let count = self.scalar(Op::ReadGardenRewardLabels, &[])? as usize;
        let mut ys = Vec::new();
        if ys.try_reserve_exact(count).is_err() {
            self.action(Op::GardenAllocationFailure, &[])?;
            return Err(HOST_ERROR);
        }
        for index in 0..count {
            if self.flag(Op::GardenLabelIsEcho, &[index as f64])? {
                ys.push(self.scalar(Op::GardenLabelY, &[index as f64])? as i32);
            }
        }
        if ys.len() < 2 {
            return Ok(());
        }
        self.action(Op::PrepareGardenGreenRegion, &[])?;
        let selected = self.scalar(Op::GardenSelectedGreenPixels, &[])?;
        let area = self.scalar(Op::GardenGreenRegionArea, &[])?;
        if selected / area > 0.003 {
            self.action(Op::StoreGardenRewardConfirm, &[])?;
            return Ok(());
        }
        ys.sort_unstable();
        let y = if ys.len() % 2 == 1 {
            ys[ys.len() / 2]
        } else {
            ys[ys.len() / 2 - 1].wrapping_add(ys[ys.len() / 2]) / 2
        };
        let [w, h, _] = self.call(Op::ImageSize, &[])?;
        self.action(Op::ReadPreferredGardenStyle, &[])?;
        for index in 0..3 {
            let i = index as f64;
            let x = self.scalar(Op::PixelRound, &[w * (0.335 + 0.235 * i)])?;
            let bw = self.scalar(Op::PixelRound, &[w * 0.035])?;
            let bh = self.scalar(Op::PixelRound, &[h * 0.028])?;
            self.action(Op::GardenCardBox, &[x, y as f64, bw, bh])?;
            let left = (x - 0.06 * w) as i32;
            let right = (x + 0.06 * w) as i32;
            let top = (y as f64 - 0.13 * h) as i32;
            let bottom = (y as f64 + 0.01 * h) as i32;
            let recommended = self.scalar(
                Op::GardenCardRecommendation,
                &[
                    left as f64,
                    top as f64,
                    (right - left) as f64,
                    (bottom - top) as f64,
                    w / 1920.,
                ],
            )? >= 0.88;
            self.action(Op::ClearGardenCardText, &[])?;
            for label in 0..count {
                let [width, cx, _] = self.call(Op::GardenLabelGeometry, &[label as f64])?;
                if width <= 0.23 * w && cx >= (0.15 + 0.235 * i) * w && cx <= (0.39 + 0.235 * i) * w
                {
                    self.action(Op::AppendGardenCardText, &[label as f64])?;
                }
            }
            let rarity = self.scalar(Op::GardenCardRarity, &[])?;
            let preferred = !self.flag(Op::GardenStyleEmpty, &[])?
                && self.flag(Op::GardenCardMatchesStyle, &[])?;
            self.action(
                Op::AppendGardenCard,
                &[i, rarity, recommended as u8 as f64, preferred as u8 as f64],
            )?;
        }
        let index = self.scalar(Op::SelectGardenCard, &[])?;
        self.action(Op::CopyChosenGardenCard, &[index])?;
        let mut preferred = false;
        for i in 0..3 {
            if self.flag(Op::GardenCardPreferred, &[i as f64])? {
                preferred = true;
                break;
            }
        }
        self.action(Op::NameChosenGardenCard, &[preferred as u8 as f64])?;
        if self.flag(Op::HasInfo, &[])? {
            for i in 0..3 {
                self.action(Op::AppendGardenCardRank, &[i as f64])?;
            }
            self.action(Op::PublishGardenCardRank, &[index])?;
        }
        self.action(Op::StoreChosenGardenCard, &[])
    }
    fn garden_points_complete(&self) -> Result<bool> {
        let count = self.scalar(Op::GardenLineCount, &[])? as usize;
        for index in 0..count {
            self.action(Op::AppendGardenLine, &[index as f64])?;
        }
        if self.flag(Op::GardenCompleteBanner, &[])? {
            return Ok(true);
        }
        self.action(Op::StartGardenPointMatches, &[])?;
        while self.flag(Op::GardenPointHasNext, &[])? {
            let [current, maximum, _] = self.call(Op::NextGardenPointMatch, &[])?;
            if current == 6000. && maximum == 6000. {
                return Ok(true);
            }
        }
        Ok(false)
    }
    fn garden_rarity(&self, a: &[f64]) -> Result<f64> {
        let x = ((a[0] - 0.07 * a[2]) as i32).max(0);
        let right = ((a[0] - 0.015 * a[2]) as i32).max(0);
        let y = ((a[1] - 0.015 * a[3]) as i32).max(0);
        let bottom = (a[3] as i32).min((a[1] + 0.04 * a[3]) as i32);
        self.action(
            Op::RarityArea,
            &[x as f64, y as f64, (right - x) as f64, (bottom - y) as f64],
        )?;
        if self.flag(Op::GardenAreaEmpty, &[])? {
            return Ok(0.);
        }
        self.action(Op::RarityHsv, &[])?;
        self.scalar(Op::RarityPixels, &[])
    }
    fn recommendation(&self, scale: f64) -> Result<f64> {
        self.action(Op::ClipRecommendationArea, &[])?;
        if self.flag(Op::BadgeEmpty, &[])? || self.flag(Op::GardenAreaEmpty, &[])? {
            return Ok(0.);
        }
        self.action(Op::RecommendationGray, &[])?;
        let mut best = 0_f64;
        for adjustment in [0.85, 1., 1.1] {
            let [w, h, _] = self.call(Op::BadgeSize, &[])?;
            let w = self
                .scalar(Op::PixelRound, &[w * scale * adjustment])?
                .max(1.);
            let h = self
                .scalar(Op::PixelRound, &[h * scale * adjustment])?
                .max(1.);
            self.action(Op::ResizeRecommendation, &[w, h])?;
            let [gw, gh, _] = self.call(Op::RecommendationGraySize, &[])?;
            if gh < h || gw < w {
                continue;
            }
            let maximum = self.scalar(Op::RecommendationMaximum, &[])?;
            if maximum.is_finite() {
                best = best.max(maximum);
            }
        }
        Ok(best)
    }
    fn purchase_controls(&self) -> Result {
        let count = self.scalar(Op::GardenLineCount, &[])? as usize;
        let mut prices = 0;
        let mut last = 0.;
        for index in 0..count {
            self.action(Op::SelectPurchaseLine, &[index as f64])?;
            if self.flag(Op::SoldPurchaseLine, &[])? {
                self.action(Op::SetPurchaseSold, &[])?;
            }
            if self.flag(Op::MergedPurchaseLine, &[])? {
                self.action(Op::StorePurchaseButton, &[index as f64])?;
                last = self.scalar(Op::MergedPurchasePrice, &[])?;
                prices += 1;
            } else if self.flag(Op::IsPurchaseButton, &[])? {
                self.action(Op::StorePurchaseButton, &[index as f64])?;
            } else if self.flag(Op::IsPurchaseNumber, &[])? {
                last = self.scalar(Op::PurchaseNumber, &[])?;
                prices += 1;
            }
        }
        if prices == 1 && last > 0. {
            self.action(Op::StorePurchasePrice, &[last])?;
        }
        Ok(())
    }
    fn weekly_points(&self) -> Result<[f64; 3]> {
        self.action(Op::PrepareWeeklyAreas, &[])?;
        let count = self.scalar(Op::GardenLineCount, &[])? as usize;
        for index in 0..count {
            if self.flag(Op::WeeklyOverviewContains, &[index as f64])? {
                self.action(Op::AppendWeeklyLocal, &[index as f64])?;
            }
        }
        if self.flag(Op::WeeklyLocalComplete, &[])? {
            return Ok([1., 6000., 0.]);
        }
        let count = self.scalar(Op::WeeklyLocalCount, &[])? as usize;
        for index in 0..count {
            if !self.flag(Op::WeeklyCounterContains, &[index as f64])? {
                continue;
            }
            if self.flag(Op::WeeklyIntegerMatches, &[index as f64])? {
                let [number, valid, _] = self.call(Op::WeeklyIntegerValue, &[])?;
                if valid != 0. && number <= 6000. {
                    return Ok([1., number, 0.]);
                }
            }
        }
        for index in 0..count {
            self.action(Op::AppendWeeklyLocalText, &[index as f64])?;
        }
        self.action(Op::StartGardenPointMatches, &[])?;
        while self.flag(Op::GardenPointHasNext, &[])? {
            let [current, maximum, _] = self.call(Op::NextGardenPointMatch, &[])?;
            if maximum == 6000. && (0. ..=6000.).contains(&current) {
                return Ok([1., current, 0.]);
            }
        }
        Ok([0.; 3])
    }
    fn weekly_progress(&self) -> Result<[f64; 3]> {
        self.action(Op::ReadWeeklyHeading, &[])?;
        let weekly = self.flag(Op::IsWeeklyHeading, &[])?;
        self.action(Op::ReadWeeklyStrip, &[])?;
        self.action(Op::PrepareWeeklyOverview, &[])?;
        let count = self.scalar(Op::GardenLineCount, &[])? as usize;
        for index in 0..count {
            if self.flag(Op::WeeklyOverviewContains, &[index as f64])? {
                self.action(Op::AppendWeeklyLocal, &[index as f64])?;
            }
        }
        let label = self.flag(Op::HasWeeklyCounterLabel, &[])?
            || self.flag(Op::WeeklyLocalComplete, &[])?;
        let count = self.scalar(Op::WeeklyHeadingCount, &[])? as usize;
        for index in 0..count {
            self.action(Op::AppendWeeklyHeadingJson, &[index as f64])?;
        }
        let count = self.scalar(Op::GardenLineCount, &[])? as usize;
        for index in 0..count {
            self.action(Op::AppendWeeklyStripJson, &[index as f64])?;
        }
        self.action(
            Op::StoreWeeklyObservation,
            &[weekly as u8 as f64, label as u8 as f64],
        )?;
        if !weekly || !label {
            return Ok([0.; 3]);
        }
        let result = self.call(Op::ReadWeeklyPoints, &[])?;
        if result[0] != 0. {
            self.action(Op::StoreWeeklyProgress, &[result[1]])?;
        }
        Ok(result)
    }
    pub(in crate::services) fn run_garden_vision(
        &self,
        method: Method,
        a: &[f64],
    ) -> Result<[f64; 3]> {
        match method {
            Method::GardenRewardChoice => self.reward_choice()?,
            Method::PreferredGardenStyle => {
                if self.flag(Op::PreferredStyleCustom, &[])? {
                    self.action(Op::CustomGardenStyle, &[])?;
                } else {
                    self.action(Op::MappedGardenStyle, &[])?;
                }
            }
            Method::GardenPointsComplete => {
                return Ok([self.garden_points_complete()? as u8 as f64, 0., 0.])
            }
            Method::GardenRarity => return Ok([self.garden_rarity(a)?, 0., 0.]),
            Method::RecommendationScore => return Ok([self.recommendation(a[0])?, 0., 0.]),
            Method::GardenPurchaseControls => self.purchase_controls()?,
            Method::GardenWeeklyPoints => return self.weekly_points(),
            Method::GardenInitialize => {
                self.action(Op::ReadGardenBadge, &[])?;
                let [w, h, _] = self.call(Op::GardenBadgeImageSize, &[])?;
                if w < 41. || h < 42. {
                    self.fail(500.)?;
                }
                self.action(Op::ExtractGardenBadge, &[])?;
            }
            Method::GardenWeeklyProgress => return self.weekly_progress(),
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
