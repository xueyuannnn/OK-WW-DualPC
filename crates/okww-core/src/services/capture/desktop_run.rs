//! Desktop capture lifecycle and the original bounded recovery loop.
use crate::services::{Host, Op, Result, HOST_ERROR};
impl Host {
    fn desktop_capture_iteration(&self, dxgi: bool) -> Result<bool> {
        self.action(Op::DesktopRunReadRect, &[])?;
        if !self.flag(Op::DesktopRunRectInDesktop, &[])? {
            self.action(Op::DesktopRunFailure, &[1.])?;
            return Err(HOST_ERROR);
        }
        if !self.flag(Op::DesktopRunSameSize, &[])? {
            self.action(Op::DesktopRunFailure, &[2.])?;
            return Err(HOST_ERROR);
        }
        if dxgi {
            if !self.flag(Op::DesktopRunHasDuplication, &[])? {
                self.action(Op::DesktopRunCreateDuplication, &[])?;
            }
            self.action(Op::DesktopRunClearChanged, &[])?;
            self.action(Op::DesktopRunDuplicationFrame, &[])?;
            if !self.flag(Op::DesktopRunImageNull, &[])?
                && self.flag(Op::DesktopRunImageChanged, &[])?
            {
                self.action(Op::DesktopRunPublishImage, &[])?;
            } else if !self.flag(Op::DesktopRunImageNull, &[])? {
                self.action(Op::DesktopRunVerifyUnchanged, &[])?;
            }
            if self.flag(Op::DesktopRunImageNull, &[])? {
                self.action(Op::DesktopRunWaitMilliseconds, &[5.])?;
                self.action(Op::DesktopRunDropImage, &[])?;
                self.action(Op::DesktopRunDropRect, &[])?;
                return Ok(true);
            }
            self.action(Op::DesktopRunDropImage, &[])?;
        } else {
            if !self.flag(Op::DesktopRunHasGdi, &[])? {
                self.action(Op::DesktopRunCreateGdi, &[])?;
            }
            self.action(Op::DesktopRunResizeGdi, &[])?;
            if !self.flag(Op::DesktopRunBitBlt, &[])? {
                self.action(Op::DesktopRunFailure, &[3.])?;
                return Err(HOST_ERROR);
            }
            self.action(Op::DesktopRunGdiFlush, &[])?;
            self.action(Op::DesktopRunPublishGdi, &[])?;
        }
        self.action(Op::DesktopRunResetRecovery, &[])?;
        self.action(Op::DesktopRunClearRecoveryStatus, &[])?;
        self.action(Op::DesktopRunDropRect, &[])?;
        Ok(false)
    }
    pub(in crate::services) fn run_capture_desktop_loop(&self) -> Result<[f64; 3]> {
        self.action(Op::DesktopRunSetDpi, &[])?;
        self.action(Op::DesktopRunDpiGuard, &[])?;
        let dxgi = self.flag(Op::DesktopRunIsDxgi, &[])?;
        self.action(Op::DesktopRunIdentity, &[])?;
        self.action(Op::DesktopRunTarget, &[])?;
        self.action(Op::DesktopRunInitialRect, &[])?;
        let size = self.call(Op::DesktopRunInitialSize, &[])?;
        if size[0] > 8192. || size[1] > 8192. {
            self.action(Op::DesktopRunFailure, &[0.])?;
            return Err(HOST_ERROR);
        }
        self.action(
            if dxgi && self.flag(Op::DesktopRunFollowRefresh, &[])? {
                Op::DesktopRunDisplayRate
            } else {
                Op::DesktopRunConfiguredRate
            },
            &[],
        )?;
        self.action(Op::DesktopRunInterval, &[])?;
        self.action(Op::DesktopRunPacer, &[])?;
        self.action(Op::DesktopRunSetNextNow, &[])?;
        while !self.flag(Op::DesktopRunStopped, &[])? {
            match self.desktop_capture_iteration(dxgi) {
                Ok(true) => continue,
                Ok(false) => {}
                Err(_) => {
                    if self.scalar(Op::ExceptionKind, &[])? != 6. {
                        return Err(HOST_ERROR);
                    }
                    self.action(Op::ClearException, &[])?;
                    self.action(Op::DesktopRunDropImage, &[])?;
                    self.action(Op::DesktopRunDropRect, &[])?;
                    if !self.flag(Op::DesktopRunHasRecovery, &[])? {
                        self.action(Op::DesktopRunBeginRecovery, &[])?;
                    }
                    self.action(Op::DesktopRunRecoveryStatus, &[])?;
                    self.action(Op::DesktopRunResetDuplication, &[])?;
                    self.action(Op::DesktopRunResetGdi, &[])?;
                    if self.flag(Op::DesktopRunRecoveryExpired, &[])? {
                        self.action(Op::RethrowHandled, &[])?;
                        return Err(HOST_ERROR);
                    }
                    self.action(Op::DesktopRunWaitMilliseconds, &[100.])?;
                    self.action(Op::DesktopRunSetNextNow, &[])?;
                    self.action(Op::ClearException, &[])?;
                    continue;
                }
            }
            self.action(Op::DesktopRunAdvanceNext, &[])?;
            if self.flag(Op::DesktopRunNextPast, &[])? {
                self.action(Op::DesktopRunSetNextNow, &[])?;
            }
            self.action(Op::DesktopRunWaitNext, &[])?;
        }
        Ok([0.; 3])
    }
}
