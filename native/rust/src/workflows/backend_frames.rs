//! Backend frame validity, first-fault evidence and bounded recovery.
use super::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    pub(super) fn run_backend_frames(&self, method: Method) -> Result<[f64; 3]> {
        match method {
            Method::BackendFrame => {
                if self.flag(Op::BackendFrameHasCapture, &[])?
                    && self.flag(Op::BackendFrameCaptureRecovering, &[])?
                {
                    return Ok([0.; 3]);
                }
                self.action(Op::BackendFrameReadRaw, &[])?;
                if !self.flag(Op::BackendFramePresent, &[])?
                    || !self.flag(Op::BackendFrameFresh, &[])?
                {
                    self.action(Op::BackendFrameClear, &[])?;
                    return Ok([0.; 3]);
                }
                self.action(Op::BackendFrameResetRecovery, &[])?;
            }
            Method::BackendRawFrame => {
                if !self.flag(Op::BackendFrameRunning, &[])? {
                    return Ok([0.; 3]);
                }
                if self.flag(Op::BackendFrameSimulation, &[])?
                    && self.flag(Op::BackendFrameHasSimulated, &[])?
                {
                    self.action(Op::BackendFrameCloneSimulated, &[])?;
                    self.action(Op::BackendFrameStampSimulated, &[])?;
                    self.action(Op::BackendFrameSequenceSimulated, &[])?;
                    self.action(Op::BackendFrameStoreSimulated, &[])?;
                } else if self.flag(Op::BackendFrameHasCapture, &[])? {
                    self.action(Op::BackendFrameCaptureLatest, &[])?;
                }
            }
            Method::BackendRecoverCapture => {
                if !self.flag(Op::BackendFrameHasCapture, &[])?
                    || self.flag(Op::BackendFrameCaptureStopped, &[])?
                {
                    if self.flag(Op::BackendFrameHasCapture, &[])? {
                        self.action(Op::BackendRecoveryCaptureError, &[])?;
                    }
                    self.action(Op::BackendRecoveryDisarmFailed, &[])?;
                    return Ok([0.; 3]);
                }
                // The first attempt records one clock reading, then takes a
                // second reading for the >= 120-second deadline comparison.
                if !self.flag(Op::BackendRecoveryStarted, &[])? {
                    self.action(Op::BackendRecoveryBegin, &[])?;
                }
                if self.flag(Op::BackendRecoveryExpired, &[])? {
                    self.action(Op::BackendRecoveryDisarmTimeout, &[])?;
                    return Ok([0.; 3]);
                }
                let attempt: Result = (|| {
                    self.action(Op::BackendFrameBoxState, &[])?;
                    if !self.flag(Op::BackendRecoveryKeysEmpty, &[])?
                        || self.scalar(Op::BackendRecoveryModifiers, &[])? != 0.
                        || self.scalar(Op::BackendRecoveryButtons, &[])? != 0.
                    {
                        self.action(Op::BackendFrameBoxRelease, &[])?;
                    }
                    Ok(())
                })();
                if attempt.is_err() {
                    if self.scalar(Op::ExceptionKind, &[])? != 6. {
                        return Err(HOST_ERROR);
                    }
                    self.action(Op::ClearException, &[])?;
                    self.action(Op::BackendFrameDropBoxState, &[])?;
                    self.action(Op::BackendRecoveryDisarmInputError, &[])?;
                    self.action(Op::ClearException, &[])?;
                    return Ok([0.; 3]);
                }
                self.action(Op::BackendFrameDropBoxState, &[])?;
                return Ok([1., 0., 0.]);
            }
            Method::BackendDisarmFault => {
                self.action(Op::BackendFrameReadRaw, &[])?;
                if self.flag(Op::BackendFrameHasBox, &[])? {
                    self.action(Op::BackendFrameBoxState, &[])?;
                }
                if self.flag(Op::BackendFaultEmpty, &[])? {
                    self.action(Op::BackendFaultRecord, &[])?;
                    self.action(Op::BackendFaultSetError, &[])?;
                }
                self.action(Op::BackendFaultDisarm, &[])?;
                if self.flag(Op::BackendFrameHasBox, &[])?
                    && self.action(Op::BackendFrameBoxRelease, &[]).is_err()
                {
                    if self.scalar(Op::ExceptionKind, &[])? != 6. {
                        return Err(HOST_ERROR);
                    }
                    self.action(Op::ClearException, &[])?;
                    self.action(Op::BackendFaultReleaseError, &[])?;
                    self.action(Op::ClearException, &[])?;
                }
            }
            Method::BackendPreview => {
                self.action(Op::BackendFrameLock, &[])?;
                self.action(Op::BackendFrameReadFresh, &[])?;
                self.action(Op::BackendFrameUnlock, &[])?;
                if !self.flag(Op::BackendFramePresent, &[])?
                    || self.flag(Op::BackendPreviewSameSequence, &[])?
                {
                    return Ok([0.; 3]);
                }
                self.action(Op::BackendPreviewStoreSequence, &[])?;
                self.action(Op::BackendPreviewConvert, &[])?;
            }
            Method::BackendSnapshot => {
                self.action(Op::BackendFrameReadFresh, &[])?;
                if self.flag(Op::BackendFrameHasCapture, &[])? {
                    self.action(Op::BackendSnapshotActual, &[])?;
                }
                if self.flag(Op::BackendFrameHasCapture, &[])?
                    && !self.flag(Op::BackendFramePresent, &[])?
                {
                    let recovering = self.flag(Op::BackendFrameRunning, &[])?
                        && !self.flag(Op::BackendFrameCaptureStopped, &[])?
                        && !self.flag(Op::BackendSnapshotRecoveryTimedOut, &[])?;
                    self.action(Op::BackendSnapshotSetRecovering, &[f64::from(recovering)])?;
                    if recovering && self.flag(Op::BackendSnapshotRecoveryReasonEmpty, &[])? {
                        self.action(Op::BackendSnapshotSetRecoveryReason, &[])?;
                    }
                }
                self.action(Op::BackendSnapshotRecoveryElapsed, &[])?;
                if self.flag(Op::BackendFrameSimulation, &[])?
                    && self.flag(Op::BackendFramePresent, &[])?
                {
                    self.action(Op::BackendSnapshotSimulated, &[])?;
                }
                if self.flag(Op::BackendFrameHasCapture, &[])? {
                    self.action(Op::BackendSnapshotCaptureFps, &[])?;
                } else {
                    self.action(Op::BackendSnapshotDefaultFps, &[])?;
                }
                if self.flag(Op::BackendFrameHasBox, &[])? {
                    self.action(Op::BackendFrameBoxState, &[])?;
                } else {
                    self.action(Op::BackendSnapshotDisconnectedBox, &[])?;
                }
                self.action(Op::BackendSnapshotResult, &[])?;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
