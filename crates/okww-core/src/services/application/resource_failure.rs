//! Allocation failure recognition and the original one-reserve/one-retry policy.
use crate::services::{Host, Method, Op, Result, INVALID_ACTION};
impl Host {
    pub(in crate::services) fn run_resource_failure(&self, method: Method) -> Result<[f64; 3]> {
        match method {
            Method::ResourceMemoryFailure => {
                if self.flag(Op::ResourceIsBadAlloc, &[])? {
                    return Ok([1., 0., 0.]);
                }
                if self.flag(Op::ResourceIsCvException, &[])? {
                    return self.call(Op::ResourceCvNoMemory, &[]);
                }
                self.action(Op::ResourceReadErrorText, &[])?;
                return Ok([
                    f64::from(
                        self.flag(Op::ResourceTextContains, &[0.])?
                            || self.flag(Op::ResourceTextContains, &[1.])?,
                    ),
                    0.,
                    0.,
                ]);
            }
            Method::ResourceReserveCreate => {
                if self.flag(Op::ResourceMemoryStatus, &[])?
                    && self.flag(Op::ResourceCapacityAvailable, &[])?
                {
                    self.action(Op::ResourceAllocate, &[])?;
                }
            }
            Method::ResourceReserveRelease => {
                if !self.flag(Op::ResourceHasBuffer, &[])? || !self.flag(Op::ResourceFree, &[])? {
                    return Ok([0.; 3]);
                }
                self.action(Op::ResourceClearBuffer, &[])?;
                return Ok([1., 0., 0.]);
            }
            Method::ResourceReserveRead => {
                if let Err(error) = self.action(Op::ResourceReadAction, &[]) {
                    if self.scalar(Op::ExceptionKind, &[])? != 6.
                        || !self.flag(Op::ResourceCaughtMemoryFailure, &[])?
                        || !self.flag(Op::ResourceRelease, &[])?
                    {
                        return Err(error);
                    }
                    self.action(Op::ClearException, &[])?;
                    self.action(Op::ResourceReport, &[])?;
                    self.action(Op::ResourceReadAction, &[])?;
                }
            }
            Method::ResourceFailureText => {
                if self.flag(Op::ResourceGivenMemoryFailure, &[])? {
                    self.action(Op::ResourceLowMemoryText, &[])?;
                } else {
                    self.action(Op::ResourceOriginalText, &[])?;
                }
            }
            // A nonmovable C++ return value must retain native return-value elision.
            // Rust still owns the retry decision and reporting before that return.
            Method::ResourceRetryNonmovable => {
                if !self.flag(Op::ResourceCaughtMemoryFailure, &[])?
                    || !self.flag(Op::ResourceRelease, &[])?
                {
                    return Ok([0.; 3]);
                }
                self.action(Op::ResourceReport, &[])?;
                return Ok([1., 0., 0.]);
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
