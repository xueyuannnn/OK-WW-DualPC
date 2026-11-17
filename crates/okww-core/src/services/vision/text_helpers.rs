//! Qt date and contour values stay native; the window/empty decisions stay here.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    pub(in crate::services) fn run_text_helpers(&self, method: Method) -> Result<[f64; 3]> {
        let mut out = [0.; 3];
        match method {
            Method::MonthlyWindowKey => {
                let day = self.scalar(Op::MonthlyWindowDay, &[])?;
                if day < 0. {
                    self.action(Op::MonthlyWindowInvalidHour, &[])?;
                    return Err(HOST_ERROR);
                }
                self.action(Op::MonthlyWindowCopyPopup, &[])?;
                self.action(Op::MonthlyWindowSetTime, &[])?;
                if day != 0. {
                    self.action(Op::MonthlyWindowAddDays, &[day])?;
                }
                if !self.flag(Op::MonthlyWindowOffsetAllowed, &[])? {
                    return Ok(out);
                }
                self.action(Op::MonthlyWindowFormat, &[])?;
            }
            Method::ContinueMarkerPipeline => {
                self.action(Op::ContinueMarkerRegion, &[])?;
                if self.flag(Op::ContinueMarkerRegionEmpty, &[])? {
                    return Ok(out);
                }
                self.action(Op::ContinueMarkerPrepareArea, &[])?;
                self.action(Op::ContinueMarkerMask, &[])?;
                self.action(Op::ContinueMarkerContours, &[])?;
                out[0] = self.scalar(Op::ContinueMarkerExistingRule, &[])?;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok(out)
    }
}
