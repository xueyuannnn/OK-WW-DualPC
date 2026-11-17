//! DirectShow media-type discrimination and ownership sequencing.
use super::{Host, Method, Op, Result, INVALID_ACTION};
impl Host {
    pub(super) fn run_capture_types(&self, method: Method, args: &[f64]) -> Result<[f64; 3]> {
        match method {
            Method::CaptureFreeType => {
                if !self.flag(Op::CaptureTypeExists, &[])? {
                    return Ok([0.; 3]);
                }
                if self.flag(Op::CaptureTypeHasFormat, &[])? {
                    self.action(Op::CaptureTypeFreeFormat, &[])?;
                }
                if self.flag(Op::CaptureTypeHasUnknown, &[])? {
                    self.action(Op::CaptureTypeReleaseUnknown, &[])?;
                }
                if args[0] != 0. {
                    self.action(Op::CaptureTypeFreeOuter, &[])?;
                }
            }
            Method::CaptureBitmap => {
                let format = self.scalar(Op::CaptureTypeFormat, &[])?;
                if (format == 0. || format == 1.)
                    && self.scalar(Op::CaptureTypeBytes, &[])?
                        >= self.scalar(Op::CaptureTypeMinimum, &[format])?
                {
                    self.action(Op::CaptureTypeBitmap, &[format])?;
                }
            }
            Method::CapturePeriod => {
                let format = self.scalar(Op::CaptureTypeFormat, &[])?;
                if format == 0. || format == 1. {
                    self.action(Op::CaptureTypePeriod, &[format])?;
                }
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
