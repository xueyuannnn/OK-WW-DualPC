//! Media Foundation's single outstanding async read and buffer lifetime.
use super::{Host, Op, Result, HOST_ERROR};
impl Host {
    pub(super) fn run_capture_mf(&self) -> Result<[f64; 3]> {
        for op in [
            Op::CaptureMfRunStart,
            Op::CaptureMfRunSource,
            Op::CaptureMfRunShutdownGuard,
            Op::CaptureMfRunAttributes,
            Op::CaptureMfRunCallback,
            Op::CaptureMfRunSetCallback,
            Op::CaptureMfRunLowLatency,
            Op::CaptureMfRunDisableConverters,
            Op::CaptureMfRunReader,
        ] {
            self.action(op, &[])?;
        }
        let mut found = false;
        let mut index = 0_u32;
        loop {
            if !self.flag(Op::CaptureMfRunNativeType, &[index as f64])? {
                self.action(Op::CaptureMfRunDropNativeType, &[])?;
                break;
            }
            self.action(Op::CaptureMfRunNativeMode, &[])?;
            if self.flag(Op::CaptureMfRunWidthMatches, &[])?
                && self.flag(Op::CaptureMfRunHeightMatches, &[])?
                && self.flag(Op::CaptureMfRunFormatMatches, &[])?
                && self.scalar(Op::CaptureMfRunRateDifference, &[])?.abs() < 0.02
            {
                self.action(Op::CaptureMfRunSetType, &[])?;
                found = true;
                self.action(Op::CaptureMfRunDropNativeMode, &[])?;
                self.action(Op::CaptureMfRunDropNativeType, &[])?;
                break;
            }
            self.action(Op::CaptureMfRunDropNativeMode, &[])?;
            self.action(Op::CaptureMfRunDropNativeType, &[])?;
            index = index.wrapping_add(1);
        }
        if !found {
            self.action(Op::CaptureMfRunFailure, &[0.])?;
            return Err(HOST_ERROR);
        }
        for op in [
            Op::CaptureMfRunCurrentType,
            Op::CaptureMfRunCurrentMode,
            Op::CaptureMfRunNegotiatedRate,
            Op::CaptureMfRunPixelFormat,
        ] {
            self.action(op, &[])?;
        }
        let signed = self.scalar(Op::CaptureMfRunReadStride, &[])? as i32;
        self.action(
            Op::CaptureMfRunSetStride,
            &[signed.wrapping_abs() as f64, f64::from(signed < 0)],
        )?;
        if signed == 0 {
            self.action(Op::CaptureMfRunDefaultStride, &[])?;
        }
        self.action(Op::CaptureMfRunReadSample, &[])?;
        while !self.flag(Op::CaptureMfRunStopped, &[])? {
            self.action(Op::CaptureMfRunLockCallback, &[])?;
            while !self.flag(Op::CaptureMfRunArrived, &[])?
                && !self.flag(Op::CaptureMfRunStopped, &[])?
            {
                self.action(Op::CaptureMfRunWait, &[])?;
            }
            if self.flag(Op::CaptureMfRunStopped, &[])? {
                self.action(Op::CaptureMfRunDropIteration, &[])?;
                break;
            }
            for op in [
                Op::CaptureMfRunCopySample,
                Op::CaptureMfRunCopyStatus,
                Op::CaptureMfRunCopyFlags,
                Op::CaptureMfRunCopyStamp,
                Op::CaptureMfRunResetSample,
                Op::CaptureMfRunResetArrived,
                Op::CaptureMfRunUnlockCallback,
                Op::CaptureMfRunCheckStatus,
            ] {
                self.action(op, &[])?;
            }
            if self.flag(Op::CaptureMfRunBadFlags, &[])? {
                self.action(Op::CaptureMfRunFailure, &[1.])?;
                return Err(HOST_ERROR);
            }
            // Queue the next acquisition before copying the current frame.
            self.action(Op::CaptureMfRunReadSample, &[])?;
            if !self.flag(Op::CaptureMfRunHasSample, &[])? {
                self.action(Op::CaptureMfRunDropIteration, &[])?;
                continue;
            }
            self.action(Op::CaptureMfRunContiguousBuffer, &[])?;
            self.action(Op::CaptureMfRunLockBuffer, &[])?;
            if self.action(Op::CaptureMfRunPublish, &[]).is_err() {
                // Native exceptions never cross Rust. Preserve the caught
                // exception while still allowing cleanup to replace it.
                self.action(Op::ClearException, &[])?;
                self.action(Op::CaptureMfRunUnlockBuffer, &[])?;
                self.action(Op::RethrowHandled, &[])?;
                return Err(HOST_ERROR);
            }
            self.action(Op::CaptureMfRunUnlockBuffer, &[])?;
            self.action(Op::CaptureMfRunDropIteration, &[])?;
        }
        self.action(Op::CaptureMfRunFlush, &[])?;
        self.action(Op::CaptureMfRunResetReader, &[])?;
        Ok([0.; 3])
    }
}
