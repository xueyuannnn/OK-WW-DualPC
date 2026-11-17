//! Device enumeration/identity selection; native SDK adapters own every COM value.
use super::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    pub(super) fn run_capture_devices(&self, method: Method, args: &[f64]) -> Result<[f64; 3]> {
        match method {
            Method::CaptureDsDevices => {
                self.action(Op::CaptureDsCreateEnumerator, &[])?;
                if !self.flag(Op::CaptureDsClassEnumerator, &[])? {
                    return Ok([0.; 3]);
                }
                while self.flag(Op::CaptureDsNextMoniker, &[])? {
                    self.action(Op::CaptureDsBindBag, &[])?;
                    self.action(Op::CaptureDsFriendlyName, &[])?;
                    self.action(Op::CaptureDsDevicePath, &[])?;
                    self.action(Op::CaptureDsAppendDevice, &[])?;
                    self.action(Op::CaptureDsResetMoniker, &[])?;
                    self.action(Op::CaptureDsResetBag, &[])?;
                }
            }
            Method::CaptureDsDevice => {
                self.action(Op::CaptureDsDeviceList, &[])?;
                let n = self.scalar(Op::CaptureDeviceListCount, &[])? as usize;
                for i in 0..n {
                    let index = [i as f64];
                    let matched = if !self.flag(Op::CaptureDevicePathEmpty, &[])? {
                        self.flag(Op::CaptureDevicePathMatches, &index)?
                    } else if !self.flag(Op::CaptureDeviceNameEmpty, &[])? {
                        self.flag(Op::CaptureDeviceNameMatches, &index)?
                    } else {
                        self.flag(Op::CaptureDeviceIndexMatches, &index)?
                    };
                    if matched {
                        self.action(Op::CaptureDsAppendMatch, &index)?;
                    }
                }
                if self.scalar(Op::CaptureDsMatchCount, &[])? != 1. {
                    self.action(Op::CaptureDeviceFailure, &[0.])?;
                    return Err(HOST_ERROR);
                }
                self.action(Op::CaptureDsBindDevice, &[])?;
            }
            Method::CaptureDsPins => {
                self.action(Op::CaptureDsEnumeratePins, &[])?;
                while self.flag(Op::CaptureDsNextPin, &[])? {
                    if self.scalar(Op::CaptureDsPinDirection, &[])? == args[0] {
                        self.action(Op::CaptureDsAppendPin, &[])?;
                    }
                    self.action(Op::CaptureDsResetPin, &[])?;
                }
            }
            Method::CaptureMfDevices => {
                self.action(Op::CaptureMfCreateAttributes, &[])?;
                self.action(Op::CaptureMfSetVideoSource, &[])?;
                self.action(Op::CaptureMfEnumerateSources, &[])?;
                let n = self.scalar(Op::CaptureMfSourceCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::CaptureMfAttachSource, &[i as f64])?;
                    self.action(Op::CaptureMfFriendlyName, &[])?;
                    self.action(Op::CaptureMfDevicePath, &[])?;
                    self.action(Op::CaptureMfAppendDevice, &[i as f64])?;
                    self.action(Op::CaptureMfResetActivation, &[])?;
                }
                self.action(Op::CaptureMfFreeSourceArray, &[])?;
            }
            Method::CaptureMfSource => {
                self.action(Op::CaptureMfDeviceList, &[])?;
                let n = self.scalar(Op::CaptureDeviceListCount, &[])? as usize;
                for i in 0..n {
                    let index = [i as f64];
                    let matched = (!self.flag(Op::CaptureDevicePathEmpty, &[])?
                        && self.flag(Op::CaptureDevicePathMatches, &index)?)
                        || (self.flag(Op::CaptureDevicePathEmpty, &[])?
                            && if !self.flag(Op::CaptureDeviceNameEmpty, &[])? {
                                self.flag(Op::CaptureDeviceNameMatches, &index)?
                            } else {
                                self.flag(Op::CaptureDeviceIndexMatches, &index)?
                            });
                    if matched {
                        self.action(Op::CaptureMfActivateSource, &index)?;
                        return Ok([0.; 3]);
                    }
                }
                self.action(Op::CaptureDeviceFailure, &[1.])?;
                return Err(HOST_ERROR);
            }
            Method::CaptureMfMode => {
                self.action(Op::CaptureMfReadSize, &[])?;
                self.action(Op::CaptureMfReadRate, &[])?;
                self.action(Op::CaptureMfReadSubtype, &[])?;
                let kind = self.scalar(Op::CaptureMfSubtypeKind, &[])?;
                self.action(
                    if kind == 0. {
                        Op::CaptureMfRgb32Format
                    } else if kind == 1. {
                        Op::CaptureMfRgb24Format
                    } else {
                        Op::CaptureMfFourccFormat
                    },
                    &[],
                )?;
                self.action(Op::CaptureMfCopyDimensions, &[])?;
                let ratio = self.call(Op::CaptureMfRateRatio, &[])?;
                self.action(
                    Op::CaptureMfSetModeRate,
                    &[if ratio[1] != 0. {
                        ratio[0] / ratio[1]
                    } else {
                        0.
                    }],
                )?;
            }
            Method::CaptureDevices => {
                if self.flag(Op::CaptureDevicesDshow, &[])? {
                    self.action(Op::CaptureDevicesDsList, &[])?;
                    let n = self.scalar(Op::CaptureDeviceListCount, &[])? as usize;
                    for i in 0..n {
                        self.action(Op::CaptureDevicesAppendDs, &[i as f64])?;
                    }
                    self.action(Op::CaptureDevicesDropDs, &[])?;
                } else {
                    self.action(Op::CaptureDevicesStartMf, &[])?;
                    self.action(Op::CaptureDevicesMfList, &[])?;
                    let n = self.scalar(Op::CaptureDeviceListCount, &[])? as usize;
                    for i in 0..n {
                        self.action(Op::CaptureDevicesAppendMf, &[i as f64])?;
                    }
                    self.action(Op::CaptureDevicesDropMf, &[])?;
                    self.action(Op::CaptureDevicesStopMf, &[])?;
                }
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
