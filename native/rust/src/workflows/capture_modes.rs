//! Capture capability traversal. COM values and standard sort/unique stay in the SDK.
use super::{Host, Method, Op, Result, INVALID_ACTION};
impl Host {
    pub(super) fn run_capture_modes(&self, method: Method) -> Result<[f64; 3]> {
        match method {
            Method::CaptureModes => {
                if self.flag(Op::CaptureModesDshow, &[])? {
                    self.action(Op::CaptureModesDsSource, &[])?;
                    self.action(Op::CaptureModesVideoControl, &[])?;
                    self.action(Op::CaptureModesPins, &[])?;
                    let pins = self.scalar(Op::CaptureModesPinCount, &[])? as usize;
                    for pin in 0..pins {
                        self.action(Op::CaptureModesSelectPin, &[pin as f64])?;
                        if self.flag(Op::CaptureModesStreamConfig, &[])? {
                            let caps = self.call(Op::CaptureModesCapabilityCounts, &[])?;
                            if caps[0] != 0.
                                && caps[1] >= 0.
                                && caps[1] <= 4096.
                                && caps[2] >= 1.
                                && caps[2] <= 65536.
                            {
                                self.action(Op::CaptureModesAllocateCaps, &[])?;
                                for index in 0..caps[1] as i32 {
                                    if !self.flag(Op::CaptureModesStreamCaps, &[index as f64])? {
                                        continue;
                                    }
                                    self.action(Op::CaptureModesReadBitmap, &[])?;
                                    self.action(Op::CaptureModesReadPeriod, &[])?;
                                    if self.flag(Op::CaptureModesBitmapExists, &[])?
                                        && self.flag(Op::CaptureModesPeriodExists, &[])?
                                        && self.flag(Op::CaptureModesPositivePeriod, &[])?
                                    {
                                        self.action(Op::CaptureModesDsMode, &[])?;
                                        if self.flag(Op::CaptureModesSupported, &[])? {
                                            self.action(Op::CaptureModesAppend, &[])?;
                                            if self.flag(Op::CaptureModesHasVideoControl, &[])? {
                                                self.action(Op::CaptureModesInitRates, &[])?;
                                                if self.flag(
                                                    Op::CaptureModesFrameRates,
                                                    &[index as f64],
                                                )? && self
                                                    .flag(Op::CaptureModesRatesExist, &[])?
                                                {
                                                    let count = self
                                                        .scalar(Op::CaptureModesRateCount, &[])?;
                                                    // The original driver contract accepts negative counts:
                                                    // the signed loop is empty, but the allocation is freed.
                                                    if count <= 4096. {
                                                        for rate in 0..count as i32 {
                                                            if self.flag(
                                                                Op::CaptureModesRatePositive,
                                                                &[rate as f64],
                                                            )? {
                                                                self.action(
                                                                    Op::CaptureModesSetRate,
                                                                    &[rate as f64],
                                                                )?;
                                                                self.action(
                                                                    Op::CaptureModesAppend,
                                                                    &[],
                                                                )?;
                                                            }
                                                        }
                                                    }
                                                }
                                                self.action(Op::CaptureModesFreeRates, &[])?;
                                            }
                                        }
                                        self.action(Op::CaptureModesDropMode, &[])?;
                                    }
                                    self.action(Op::CaptureModesFreeType, &[])?;
                                }
                            }
                        }
                        self.action(Op::CaptureModesDropPinScope, &[])?;
                    }
                    self.action(Op::CaptureModesDropDs, &[])?;
                } else {
                    self.action(Op::CaptureModesStartMf, &[])?;
                    self.action(Op::CaptureModesMfSource, &[])?;
                    self.action(Op::CaptureModesMfReader, &[])?;
                    let mut index = 0_u32;
                    loop {
                        if !self.flag(Op::CaptureModesMfType, &[index as f64])? {
                            self.action(Op::CaptureModesDropMfType, &[])?;
                            break;
                        }
                        self.action(Op::CaptureModesMfMode, &[])?;
                        if self.flag(Op::CaptureModesSupported, &[])?
                            && self.scalar(Op::CaptureModesModeRate, &[])? > 0.
                        {
                            self.action(Op::CaptureModesAppend, &[])?;
                        }
                        self.action(Op::CaptureModesDropMode, &[])?;
                        self.action(Op::CaptureModesDropMfType, &[])?;
                        index = index.wrapping_add(1);
                    }
                    self.action(Op::CaptureModesResetReader, &[])?;
                    self.action(Op::CaptureModesShutdownSource, &[])?;
                    self.action(Op::CaptureModesDropMf, &[])?;
                }
                self.action(Op::CaptureModesSort, &[])?;
                self.action(Op::CaptureModesUnique, &[])?;
                Ok([0.; 3])
            }
            Method::CaptureModesSame => {
                let same = self.flag(Op::CaptureModesSameWidth, &[])?
                    && self.flag(Op::CaptureModesSameHeight, &[])?
                    && self.flag(Op::CaptureModesSameFormat, &[])?
                    && self.scalar(Op::CaptureModesRateDifference, &[])?.abs() < 0.001;
                Ok([f64::from(same), 0., 0.])
            }
            _ => Err(INVALID_ACTION),
        }
    }
}
