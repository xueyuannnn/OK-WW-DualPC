//! DirectShow graph setup and acquisition lifetime, matching the original call order.
use crate::services::{Host, Op, Result, HOST_ERROR};
impl Host {
    pub(in crate::services) fn run_capture_dshow(&self) -> Result<[f64; 3]> {
        self.action(Op::CaptureDsRunSource, &[])?;
        self.action(Op::CaptureDsCreateGraph, &[])?;
        self.action(Op::CaptureDsAddSource, &[])?;
        self.action(Op::CaptureDsRunPins, &[])?;
        let count = self.scalar(Op::CaptureDsRunPinCount, &[])? as usize;
        for pin in 0..count {
            self.action(Op::CaptureDsRunSelectPin, &[pin as f64])?;
            if self.flag(Op::CaptureDsRunStreamConfig, &[])? {
                let caps = self.call(Op::CaptureDsRunCapabilityCounts, &[])?;
                // Unlike capability discovery, the original graph builder does
                // not reject a negative capability count; its loop is empty.
                if caps[0] != 0. && caps[2] > 0. && caps[2] <= 65536. && caps[1] <= 4096. {
                    self.action(Op::CaptureDsRunAllocateCaps, &[])?;
                    for index in 0..caps[1] as i32 {
                        if !self.flag(Op::CaptureDsRunStreamCaps, &[index as f64])? {
                            continue;
                        }
                        self.action(Op::CaptureDsRunReadBitmap, &[])?;
                        self.action(Op::CaptureDsRunReadPeriod, &[])?;
                        if self.flag(Op::CaptureDsRunBitmapExists, &[])?
                            && self.flag(Op::CaptureDsRunPeriodExists, &[])?
                            && self.flag(Op::CaptureDsRunWidthMatches, &[])?
                            && self.flag(Op::CaptureDsRunHeightMatches, &[])?
                            && self.flag(Op::CaptureDsRunFormatMatches, &[])?
                        {
                            let fps = self.scalar(Op::CaptureDsRunRequestedRate, &[])?;
                            self.action(Op::CaptureDsRunSetPeriod, &[1e7 / fps])?;
                            if self.flag(Op::CaptureDsRunSetFormat, &[])? {
                                self.action(Op::CaptureDsRunChoose, &[])?;
                                break;
                            }
                        }
                        self.action(Op::CaptureDsRunFreeType, &[])?;
                    }
                }
            }
            self.action(Op::CaptureDsRunDropPinScope, &[])?;
            if self.flag(Op::CaptureDsRunSelected, &[])? {
                break;
            }
        }
        self.action(Op::CaptureDsRunDropPins, &[])?;
        if !self.flag(Op::CaptureDsRunSelected, &[])? {
            self.action(Op::CaptureDsRunFailure, &[0.])?;
            return Err(HOST_ERROR);
        }
        for op in [
            Op::CaptureDsCreateGrabber,
            Op::CaptureDsCreateNull,
            Op::CaptureDsGrabInterface,
            Op::CaptureDsGrabSetType,
            Op::CaptureDsFreeChosen,
            Op::CaptureDsAddGrabber,
            Op::CaptureDsAddNull,
            Op::CaptureDsConnectSource,
            Op::CaptureDsConnectOutput,
            Op::CaptureDsConnectedType,
            Op::CaptureDsActualBitmap,
        ] {
            self.action(op, &[])?;
        }
        if !self.flag(Op::CaptureDsActualBitmapExists, &[])? {
            self.action(Op::CaptureDsFreeActual, &[])?;
            self.action(Op::CaptureDsRunFailure, &[1.])?;
            return Err(HOST_ERROR);
        }
        self.action(Op::CaptureDsActualDimensions, &[])?;
        self.action(Op::CaptureDsActualPeriod, &[])?;
        if self.flag(Op::CaptureDsActualPeriodExists, &[])?
            && self.flag(Op::CaptureDsActualPositivePeriod, &[])?
        {
            self.action(Op::CaptureDsNegotiatedRate, &[])?;
        }
        for op in [
            Op::CaptureDsActualPixelFormat,
            Op::CaptureDsActualLayout,
            Op::CaptureDsFreeActual,
            Op::CaptureDsCreateCallback,
            Op::CaptureDsSetOneShot,
            Op::CaptureDsSetBufferSamples,
            Op::CaptureDsSetCallback,
            Op::CaptureDsControlInterface,
            Op::CaptureDsMediaFilterInterface,
            Op::CaptureDsUnsetSyncSource,
            Op::CaptureDsControlRun,
        ] {
            self.action(op, &[])?;
        }
        while !self.flag(Op::CaptureDsRunStopped, &[])? {
            self.action(Op::CaptureDsRunWait, &[])?;
        }
        self.action(Op::CaptureDsControlStop, &[])?;
        self.action(Op::CaptureDsClearCallback, &[])?;
        Ok([0.; 3])
    }
}
