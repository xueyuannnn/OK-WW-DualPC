//! Capture lifecycle, immutable frame publication and wait predicates.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn capture_flow_fail<T>(&self, code: f64) -> Result<T> {
        self.action(Op::CaptureFlowFailure, &[code])?;
        Err(HOST_ERROR)
    }
    pub(in crate::services) fn run_capture_flow(
        &self,
        method: Method,
        args: &[f64],
    ) -> Result<[f64; 3]> {
        match method {
            Method::CapturePublish => {
                if self.flag(Op::CaptureStopped, &[])? {
                    return Ok([0.; 3]);
                }
                match self.scalar(Op::CapturePayloadState, &[])? as i32 {
                    1 => {
                        self.action(Op::CaptureLock, &[])?;
                        self.action(Op::CaptureSetShortError, &[])?;
                        self.action(Op::CaptureNotifyAll, &[])?;
                        self.action(Op::CaptureUnlock, &[])?;
                        return Ok([0.; 3]);
                    }
                    2 => return Ok([0.; 3]),
                    _ => {}
                }
                if args[5] != 0. {
                    self.action(Op::CaptureLock, &[])?;
                    let same = if self.flag(Op::CaptureHasLatest, &[])? {
                        let dimensions = self.call(Op::CaptureLatestDimensions, &[])?;
                        dimensions[0] == args[0]
                            && dimensions[1] == args[1]
                            && dimensions[2] == args[2]
                            && self.scalar(Op::CaptureLatestFormat, &[])? == args[3]
                            && self.flag(Op::CaptureLatestBottom, &[])? == (args[4] != 0.)
                            && self.flag(Op::CaptureLatestLengthMatches, &[])?
                            && self.flag(Op::CaptureContentsMatch, &[])?
                    } else {
                        false
                    };
                    if same {
                        self.action(Op::CaptureCopyLatest, &[])?;
                        self.action(Op::CaptureSetCopiedStamp, &[])?;
                        self.action(Op::CaptureMoveLatest, &[])?;
                        self.action(Op::CaptureIncrementUnchanged, &[])?;
                        self.action(Op::CaptureUnlock, &[])?;
                        return Ok([0.; 3]);
                    }
                    self.action(Op::CaptureUnlock, &[])?;
                }
                self.action(Op::CaptureLock, &[])?;
                let n = self.scalar(Op::CapturePoolSize, &[])? as usize;
                for i in 0..n {
                    let slot = [i as f64];
                    if !self.flag(Op::CapturePoolPresent, &slot)?
                        || self.scalar(Op::CapturePoolReferences, &slot)? == 1.
                    {
                        if !self.flag(Op::CapturePoolPresent, &slot)? {
                            self.action(Op::CaptureCreatePoolFrame, &slot)?;
                        }
                        self.action(Op::CaptureUsePoolFrame, &slot)?;
                        break;
                    }
                }
                self.action(Op::CaptureUnlock, &[])?;
                if !self.flag(Op::CaptureHasPublishFrame, &[])? {
                    return Ok([0.; 3]);
                }
                self.action(Op::CaptureResizeBytes, &[])?;
                self.action(Op::CaptureCopyBytes, &[])?;
                self.action(Op::CaptureAssignFrameFields, &[])?;
                self.action(Op::CaptureLock, &[])?;
                self.action(Op::CaptureIncrementSequence, &[])?;
                self.action(Op::CapturePublishFrame, &[])?;
                if self.flag(Op::CaptureShortErrorActive, &[])? {
                    self.action(Op::CaptureClearError, &[])?;
                }
                self.action(Op::CaptureAppendArrival, &[])?;
                if self.scalar(Op::CaptureArrivalCount, &[])? > 240. {
                    self.action(Op::CapturePopArrival, &[])?;
                }
                self.action(Op::CaptureSetReady, &[])?;
                self.action(Op::CaptureUnlock, &[])?;
                self.action(Op::CaptureNotifyAll, &[])?;
            }
            Method::CaptureVerifyUnchanged => {
                self.action(Op::CaptureLock, &[])?;
                if !self.flag(Op::CaptureStopped, &[])?
                    && self.flag(Op::CaptureHasLatest, &[])?
                    && self.flag(Op::CaptureSourceDesktop, &[])?
                {
                    self.action(Op::CaptureCopyLatest, &[])?;
                    self.action(Op::CaptureSetCopiedStamp, &[])?;
                    self.action(Op::CaptureMoveLatest, &[])?;
                    self.action(Op::CaptureIncrementUnchanged, &[])?;
                }
                self.action(Op::CaptureUnlock, &[])?;
            }
            Method::CaptureStart => {
                if self.flag(Op::CaptureThreadJoinable, &[])? {
                    return self.capture_flow_fail(0.);
                }
                self.action(Op::CaptureValidate, &[])?;
                self.action(Op::CaptureCreateThread, &[])?;
                self.action(Op::CaptureLock, &[])?;
                if !self.flag(Op::CaptureWaitStart, &[8.])? {
                    return self.capture_flow_fail(1.);
                }
                if self.flag(Op::CaptureHasError, &[])? {
                    return self.capture_flow_fail(2.);
                }
                self.action(Op::CaptureUnlock, &[])?;
            }
            Method::CaptureStop => {
                self.action(Op::CaptureMarkStopped, &[])?;
                self.action(Op::CaptureNotifyAll, &[])?;
                if self.flag(Op::CaptureThreadJoinable, &[])? {
                    self.action(Op::CaptureJoinThread, &[])?;
                }
            }
            Method::CaptureError => {
                self.action(Op::CaptureLock, &[])?;
                self.action(Op::CaptureCopyError, &[])?;
                self.action(Op::CaptureUnlock, &[])?;
            }
            Method::CaptureDesktopRecovery => {
                self.action(Op::CaptureLock, &[])?;
                self.action(Op::CaptureWriteRecovery, &[])?;
                self.action(Op::CaptureUnlock, &[])?;
            }
            Method::CaptureRecovering => {
                self.action(Op::CaptureLock, &[])?;
                let recovering = self.flag(Op::CaptureRecoveryActive, &[])?;
                self.action(Op::CaptureUnlock, &[])?;
                return Ok([f64::from(recovering), 0., 0.]);
            }
            Method::CaptureLatest => {
                self.action(Op::CaptureLock, &[])?;
                if !self.flag(Op::CaptureStopped, &[])? {
                    self.action(Op::CaptureCopyLatestOutput, &[])?;
                }
                self.action(Op::CaptureUnlock, &[])?;
            }
            Method::CaptureActual => {
                self.action(Op::CaptureLock, &[])?;
                if !self.flag(Op::CaptureHasLatest, &[])? {
                    self.action(Op::CaptureRecoveryState, &[])?;
                } else {
                    self.action(Op::CaptureBasicState, &[])?;
                    if self.flag(Op::CaptureSourceDesktop, &[])? {
                        self.action(Op::CaptureLocalBackendState, &[])?;
                    } else {
                        self.action(Op::CaptureCardBackendState, &[])?;
                    }
                    if self.flag(Op::CaptureSourceDesktop, &[])? {
                        if self.flag(Op::CaptureIsDxgi, &[])? {
                            self.action(Op::CaptureNegotiatedRequest, &[])?;
                        } else {
                            self.action(Op::CaptureLocalRequest, &[])?;
                        }
                    } else {
                        self.action(Op::CaptureCardRequest, &[])?;
                    }
                    self.action(Op::CaptureRemainingState, &[])?;
                }
                self.action(Op::CaptureUnlock, &[])?;
            }
            Method::CaptureFps => {
                self.action(Op::CaptureLock, &[])?;
                let fps = if self.scalar(Op::CaptureArrivalCount, &[])? > 1.
                    && self.flag(Op::CaptureArrivalRecent, &[])?
                {
                    self.scalar(Op::CaptureFpsValue, &[])?
                } else {
                    0.
                };
                self.action(Op::CaptureUnlock, &[])?;
                return Ok([fps, 0., 0.]);
            }
            Method::CaptureNext => {
                self.action(Op::CaptureLock, &[])?;
                self.action(Op::CaptureWaitNext, &[20.])?;
                if self.flag(Op::CaptureHasError, &[])? {
                    return self.capture_flow_fail(2.);
                }
                if !self.flag(Op::CaptureStopped, &[])?
                    && self.flag(Op::CaptureHasLatest, &[])?
                    && self.flag(Op::CaptureNewSequence, &[])?
                {
                    self.action(Op::CaptureCopyLatestOutput, &[])?;
                }
                self.action(Op::CaptureUnlock, &[])?;
            }
            Method::CaptureNextReady => {
                return Ok([
                    f64::from(
                        self.flag(Op::CaptureStopped, &[])?
                            || self.flag(Op::CaptureHasError, &[])?
                            || (self.flag(Op::CaptureHasLatest, &[])?
                                && self.flag(Op::CaptureNewSequence, &[])?),
                    ),
                    0.,
                    0.,
                ]);
            }
            Method::CaptureRun => {
                let run = (|| {
                    self.action(Op::CaptureCreateCom, &[])?;
                    self.action(Op::CaptureCheckCom, &[])?;
                    self.action(Op::CaptureDescribeThread, &[])?;
                    self.action(
                        if self.flag(Op::CaptureSourceDesktop, &[])? {
                            Op::CaptureRunDesktop
                        } else if self.flag(Op::CaptureIsDshow, &[])? {
                            Op::CaptureRunDs
                        } else {
                            Op::CaptureRunMf
                        },
                        &[],
                    )?;
                    self.action(Op::CaptureDropCom, &[])?;
                    Ok(())
                })();
                if let Err(error) = run {
                    if self.scalar(Op::ExceptionKind, &[])? != 6. {
                        return Err(error);
                    }
                    self.action(Op::ClearException, &[])?;
                    self.action(Op::CaptureDropCom, &[])?;
                    self.action(Op::CaptureLock, &[])?;
                    self.action(Op::CaptureCaughtError, &[])?;
                    self.action(Op::CaptureUnlock, &[])?;
                    self.action(Op::ClearException, &[])?;
                }
                self.action(Op::CaptureMarkStopped, &[])?;
                self.action(Op::CaptureNotifyAll, &[])?;
            }
            Method::CaptureStartReady => {
                return Ok([
                    f64::from(
                        self.flag(Op::CaptureReady, &[])? || self.flag(Op::CaptureHasError, &[])?,
                    ),
                    0.,
                    0.,
                ]);
            }
            Method::CaptureStopReady => return self.call(Op::CaptureStopped, &[]),
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
