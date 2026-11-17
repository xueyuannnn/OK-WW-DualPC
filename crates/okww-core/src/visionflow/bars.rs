use super::*;

impl Host {
    pub(super) fn rectangles(&self, args: &[f64]) -> Result {
        self.action(Op::IntersectColorArea, &[])?;
        if self.flag(Op::ColorAreaEmpty, &[])? {
            return Ok(());
        }
        self.action(Op::MakeColorContours, &[])?;
        let count = self.scalar(Op::ContourCount, &[])? as usize;
        for index in 0..count {
            self.action(Op::SelectContour, &[index as f64])?;
            let [w, h, _] = self.call(Op::ContourBounds, &[])?;
            if w < args[0] || h < args[1] || (args[2] >= 0. && h > args[2]) {
                continue;
            }
            let fraction = self.scalar(Op::ContourFill, &[])?;
            if fraction >= args[3] {
                self.action(Op::AppendColorRect, &[fraction])?;
            }
        }
        Ok(())
    }
    fn round(&self, value: f64) -> Result<f64> {
        self.scalar(Op::PixelRound, &[value])
    }
    pub(super) fn shifted_candidates(&self) -> Result {
        let [w, h, _] = self.call(Op::ImageSize, &[])?;
        let scale = h / 1080.;
        self.action(Op::PrepareShifted, &[self.round(3. * w / 1920.)?.max(3.)])?;
        for value in [75., 110., 150., 190., 230.] {
            self.action(
                Op::ShiftedContours,
                &[value, self.round(48. * w / 1920.)?.max(3.)],
            )?;
            let count = self.scalar(Op::ContourCount, &[])? as usize;
            for index in 0..count {
                self.action(Op::SelectContour, &[index as f64])?;
                let [bw, bh, y] = self.call(Op::ContourBounds, &[])?;
                if bw < self.round(50. * w / 1920.)?
                    || bw > self.round(345. * w / 1920.)?
                    || bh < self.round(4. * scale)?.max(2.)
                    || bh > self.round(22. * scale)?.max(3.)
                    || bw < bh * 6.
                    || y <= h * 0.08
                    || y >= h * 0.85
                {
                    continue;
                }
                let filled = self.scalar(Op::ContourFill, &[])?;
                self.action(Op::SelectContourCore, &[(bh > 2.) as u8 as f64])?;
                if filled >= 0.75 && self.scalar(Op::CoreFill, &[])? >= 0.8 {
                    self.action(Op::AppendShiftedRect, &[filled])?;
                }
            }
        }
        let count = self.scalar(Op::CandidateCount, &[])? as usize;
        let mut ordered: Vec<(usize, f64)> = Vec::new();
        if ordered.try_reserve_exact(count).is_err() {
            self.action(Op::AllocationFailure, &[])?;
            return Err(HOST_ERROR);
        }
        for index in 0..count {
            let confidence = self.scalar(Op::BarConfidence, &[index as f64])?;
            // Stable descending order, including equal-confidence contours.
            let at = ordered
                .iter()
                .position(|(_, other)| confidence > *other)
                .unwrap_or(ordered.len());
            ordered.insert(at, (index, confidence));
        }
        let mut selected = [0_usize; 6];
        let mut used = 0;
        for (index, _) in ordered {
            let mut overlap = false;
            for other in &selected[..used] {
                if self.scalar(Op::CandidateOverlap, &[index as f64, *other as f64])? > 0.8 {
                    overlap = true;
                    break;
                }
            }
            if !overlap {
                selected[used] = index;
                used += 1;
                self.action(Op::AppendUnique, &[index as f64])?;
            }
            if used == 6 {
                break;
            }
        }
        Ok(())
    }
    fn level_at(&self) -> Result<bool> {
        if self.flag(Op::ReadLevel, &[0., 0.65, 2.])? {
            return Ok(true);
        }
        self.flag(Op::ReadLevel, &[1., 0.6, 2.])
    }
    pub(super) fn shifted_health(&self) -> Result<bool> {
        let [w, h, _] = self.call(Op::ImageSize, &[])?;
        let scale = h / 1080.;
        self.action(Op::PrepareTopHealth, &[])?;
        let count = self.scalar(Op::ContourCount, &[])? as usize;
        for index in 0..count {
            self.action(Op::SelectContour, &[index as f64])?;
            let [bw, bh, _] = self.call(Op::ContourBounds, &[])?;
            if bw >= w * 0.15
                && bh >= (4. * scale).max(2.)
                && bh <= 16. * scale
                && bw / bh >= 20.
                && self.scalar(Op::ContourAreaRatio, &[])? >= 0.7
            {
                self.action(Op::TopLevelArea, &[])?;
                if self.level_at()? {
                    return Ok(true);
                }
            }
        }
        let count = self.scalar(Op::ReadShiftedBars, &[])? as usize;
        for index in 0..count {
            let [x, y, _] = self.call(Op::ShiftedBarPosition, &[index as f64])?;
            let [bw, _, _] = self.call(Op::ShiftedBarSize, &[index as f64])?;
            let left = (x - self.round(12. * scale)?).max(0.);
            let top = (y - self.round(48. * scale)?).max(0.);
            let right = (x + bw + self.round(55. * scale)?).min(w);
            let bottom = (y - self.round(3. * scale)?).max(top + 1.);
            self.action(Op::SetLevelArea, &[left, top, right - left, bottom - top])?;
            if self.level_at()? {
                return Ok(true);
            }
        }
        Ok(false)
    }
}
