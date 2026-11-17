//! Desktop capture pacing and GDI ownership; timestamps stay native i64 values.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    pub(in crate::services) fn run_capture_desktop(&self, method: Method) -> Result<[f64; 3]> {
        match method {
            Method::DesktopPacerCreate => self.action(Op::DesktopPacerCreateTimer, &[])?,
            Method::DesktopPacerDestroy => {
                if self.flag(Op::DesktopPacerHasTimer, &[])? {
                    self.action(Op::DesktopPacerCloseTimer, &[])?;
                }
            }
            Method::DesktopPacerWait => {
                self.action(Op::DesktopPacerReadRemaining, &[])?;
                if !self.flag(Op::DesktopPacerPositiveRemaining, &[])?
                    || self.flag(Op::DesktopPacerStopped, &[])?
                {
                    return Ok([0.; 3]);
                }
                self.action(Op::DesktopPacerPrepareDue, &[])?;
                if self.flag(Op::DesktopPacerHasTimer, &[])?
                    && self.flag(Op::DesktopPacerSetTimer, &[])?
                {
                    while !self.flag(Op::DesktopPacerStopped, &[])? {
                        let result = self.scalar(Op::DesktopPacerWaitTimer, &[])?;
                        if result == 0. {
                            return Ok([0.; 3]);
                        }
                        if result != 258. {
                            self.action(Op::DesktopPacerFailure, &[])?;
                            return Err(HOST_ERROR);
                        }
                    }
                } else {
                    while !self.flag(Op::DesktopPacerStopped, &[])?
                        && self.flag(Op::DesktopPacerBeforeDeadline, &[])?
                    {
                        self.action(Op::DesktopPacerSleep, &[])?;
                    }
                }
            }
            Method::DesktopGdiCreate => {
                self.action(Op::DesktopGdiGetScreen, &[])?;
                if self.flag(Op::DesktopGdiHasScreen, &[])? {
                    self.action(Op::DesktopGdiCreateDc, &[])?;
                }
            }
            Method::DesktopGdiDestroy => {
                if self.flag(Op::DesktopGdiHasOld, &[])? {
                    self.action(Op::DesktopGdiRestoreOld, &[])?;
                }
                if self.flag(Op::DesktopGdiHasBitmap, &[])? {
                    self.action(Op::DesktopGdiDeleteBitmap, &[])?;
                }
                if self.flag(Op::DesktopGdiHasDc, &[])? {
                    self.action(Op::DesktopGdiDeleteDc, &[])?;
                }
                if self.flag(Op::DesktopGdiHasScreen, &[])? {
                    self.action(Op::DesktopGdiReleaseScreen, &[])?;
                }
            }
            Method::DesktopGdiResize => {
                if !self.flag(Op::DesktopGdiHasScreen, &[])?
                    || !self.flag(Op::DesktopGdiHasDc, &[])?
                {
                    self.action(Op::DesktopGdiFailure, &[0.])?;
                    return Err(HOST_ERROR);
                }
                if self.flag(Op::DesktopGdiSameSize, &[])? {
                    return Ok([0.; 3]);
                }
                if self.flag(Op::DesktopGdiHasOld, &[])? {
                    self.action(Op::DesktopGdiRestoreOld, &[])?;
                    self.action(Op::DesktopGdiClearOld, &[])?;
                }
                if self.flag(Op::DesktopGdiHasBitmap, &[])? {
                    self.action(Op::DesktopGdiDeleteBitmap, &[])?;
                    self.action(Op::DesktopGdiClearBitmap, &[])?;
                }
                self.action(Op::DesktopGdiCreateBitmap, &[])?;
                if !self.flag(Op::DesktopGdiHasBitmap, &[])?
                    || !self.flag(Op::DesktopGdiHasData, &[])?
                {
                    self.action(Op::DesktopGdiFailure, &[1.])?;
                    return Err(HOST_ERROR);
                }
                self.action(Op::DesktopGdiSelectBitmap, &[])?;
                self.action(Op::DesktopGdiSetSize, &[])?;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
