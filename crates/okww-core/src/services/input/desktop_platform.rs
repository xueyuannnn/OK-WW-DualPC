//! Desktop platform selection, visibility checks and refresh-rate fallback.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    pub(in crate::services) fn run_desktop_platform(&self, method: Method) -> Result<[f64; 3]> {
        match method {
            Method::DesktopProcessPath => {
                self.action(Op::DesktopProcessOpen, &[])?;
                if !self.flag(Op::DesktopProcessExists, &[])? {
                    return Ok([0.; 3]);
                }
                let success = self.flag(Op::DesktopProcessQuery, &[])?;
                self.action(Op::DesktopProcessClose, &[])?;
                if success {
                    self.action(Op::DesktopProcessText, &[])?;
                }
            }
            Method::DesktopClientRect => {
                if !self.flag(Op::DesktopClientVisible, &[])?
                    || self.flag(Op::DesktopClientIconic, &[])?
                    || !self.flag(Op::DesktopClientGetRect, &[])?
                    || !self.flag(Op::DesktopClientOrigin, &[])?
                    || self.scalar(Op::DesktopClientWidth, &[])? < 2.
                    || self.scalar(Op::DesktopClientHeight, &[])? < 2.
                {
                    self.action(Op::DesktopPlatformFailure, &[0.])?;
                    return Err(HOST_ERROR);
                }
                self.action(Op::DesktopClientResult, &[])?;
            }
            Method::DesktopForegroundClient => {
                self.action(Op::DesktopForegroundRead, &[])?;
                if !self.flag(Op::DesktopForegroundExists, &[])? {
                    self.action(Op::DesktopPlatformFailure, &[1.])?;
                    return Err(HOST_ERROR);
                }
                self.action(Op::DesktopForegroundRect, &[])?;
            }
            Method::DesktopDisplayRefresh => {
                for _ in 0..3 {
                    self.action(Op::DesktopRefreshResetCounts, &[])?;
                    if !self.flag(Op::DesktopRefreshBufferSizes, &[])? {
                        break;
                    }
                    self.action(Op::DesktopRefreshAllocatePaths, &[])?;
                    self.action(Op::DesktopRefreshAllocateModes, &[])?;
                    let status = self.scalar(Op::DesktopRefreshQuery, &[])?;
                    if status == 122. {
                        self.action(Op::DesktopRefreshDropBuffers, &[])?;
                        continue;
                    }
                    if status != 0. {
                        self.action(Op::DesktopRefreshDropBuffers, &[])?;
                        break;
                    }
                    let count = self.scalar(Op::DesktopRefreshPathCount, &[])? as u32;
                    for index in 0..count {
                        self.action(Op::DesktopRefreshSelectPath, &[index as f64])?;
                        self.action(Op::DesktopRefreshSourceRequest, &[])?;
                        if !self.flag(Op::DesktopRefreshGetSource, &[])?
                            || !self.flag(Op::DesktopRefreshNameMatches, &[])?
                        {
                            continue;
                        }
                        self.action(Op::DesktopRefreshPathRate, &[])?;
                        if self.flag(Op::DesktopRefreshModeIndexValid, &[])? {
                            self.action(Op::DesktopRefreshSelectMode, &[])?;
                            if self.flag(Op::DesktopRefreshModeIsTarget, &[])? {
                                self.action(Op::DesktopRefreshModeRate, &[])?;
                            }
                        }
                        let rate = self.call(Op::DesktopRefreshRateRatio, &[])?;
                        if rate[0] != 0. && rate[1] != 0. {
                            return Ok([rate[0] / rate[1], 0., 0.]);
                        }
                    }
                    self.action(Op::DesktopRefreshDropBuffers, &[])?;
                    break;
                }
                self.action(Op::DesktopRefreshFallbackInit, &[])?;
                if self.flag(Op::DesktopRefreshFallbackQuery, &[])? {
                    let rate = self.scalar(Op::DesktopRefreshFallbackRate, &[])?;
                    if rate > 1. {
                        return Ok([rate, 0., 0.]);
                    }
                }
                self.action(Op::DesktopPlatformFailure, &[2.])?;
                return Err(HOST_ERROR);
            }
            Method::DesktopDxgiDisplay => {
                self.action(Op::DesktopDisplaySelectedName, &[])?;
                self.action(Op::DesktopDisplayListTargets, &[])?;
                let count = self.scalar(Op::DesktopDisplayTargetCount, &[])? as usize;
                for index in 0..count {
                    self.action(Op::DesktopDisplaySelectTarget, &[index as f64])?;
                    if !self.flag(Op::DesktopDisplayIsMonitor, &[])? {
                        self.action(Op::DesktopDisplayDropTarget, &[])?;
                        continue;
                    }
                    if self.flag(Op::DesktopDisplayNameMatches, &[])? {
                        self.action(Op::DesktopDisplayReturnTarget, &[])?;
                        return Ok([0.; 3]);
                    }
                    if self.flag(Op::DesktopDisplayIsPrimary, &[])? {
                        self.action(Op::DesktopDisplaySavePrimary, &[])?;
                    }
                    self.action(Op::DesktopDisplayDropTarget, &[])?;
                }
                self.action(Op::DesktopDisplayDropTargets, &[])?;
                if !self.flag(Op::DesktopDisplayNameEmpty, &[])? {
                    self.action(Op::DesktopPlatformFailure, &[3.])?;
                    return Err(HOST_ERROR);
                }
                if self.flag(Op::DesktopDisplayPrimaryEmpty, &[])? {
                    self.action(Op::DesktopPlatformFailure, &[4.])?;
                    return Err(HOST_ERROR);
                }
                self.action(Op::DesktopDisplayReturnPrimary, &[])?;
            }
            Method::DesktopCaptureTarget => {
                if self.flag(Op::DesktopTargetCaptureDxgi, &[])? {
                    self.action(Op::DesktopTargetConfiguredDisplay, &[])?;
                } else {
                    self.action(Op::DesktopTargetLocalIdentity, &[])?;
                    if self.flag(Op::DesktopTargetIdentityEmpty, &[])? {
                        self.action(Op::DesktopTargetDefaultDisplay, &[])?;
                    } else {
                        self.action(Op::DesktopTargetReturnIdentity, &[])?;
                    }
                }
            }
            Method::DesktopInputEnvironment => {
                self.action(Op::DesktopEnvironmentForeground, &[])?;
                self.action(Op::DesktopEnvironmentProcess, &[])?;
                for index in 0..8 {
                    if self.flag(Op::DesktopEnvironmentKeyDown, &[index as f64])? {
                        self.action(Op::DesktopEnvironmentAddModifier, &[index as f64])?;
                    }
                }
                self.action(Op::DesktopEnvironmentResult, &[])?;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
