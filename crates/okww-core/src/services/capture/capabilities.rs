//! One device scan collects each device's supported parameters and isolated errors.
use crate::services::{Host, Method, Op, Result, INVALID_ACTION};
impl Host {
    pub(in crate::services) fn run_capture_capabilities(&self, method: Method) -> Result<[f64; 3]> {
        if !matches!(method, Method::CaptureCapabilities) {
            return Err(INVALID_ACTION);
        }
        self.action(Op::CaptureCapabilitiesDevices, &[])?;
        let n = self.scalar(Op::CaptureCapabilitiesDeviceCount, &[])? as usize;
        for i in 0..n {
            self.action(Op::CaptureCapabilitiesEntry, &[i as f64])?;
            self.action(Op::CaptureCapabilitiesConfig, &[])?;
            self.action(Op::CaptureCapabilitiesSetName, &[i as f64])?;
            self.action(Op::CaptureCapabilitiesSetPath, &[i as f64])?;
            self.action(Op::CaptureCapabilitiesSetIndex, &[i as f64])?;
            let scan = (|| {
                self.action(Op::CaptureCapabilitiesModes, &[])?;
                let count = self.scalar(Op::CaptureCapabilitiesModeCount, &[])? as usize;
                for j in 0..count {
                    if self.flag(Op::CaptureCapabilitiesSupported, &[j as f64])? {
                        self.action(Op::CaptureCapabilitiesAppendMode, &[j as f64])?;
                    }
                }
                self.action(Op::CaptureCapabilitiesDropModes, &[])?;
                Ok(())
            })();
            if let Err(error) = scan {
                if self.scalar(Op::ExceptionKind, &[])? != 6. {
                    return Err(error);
                }
                self.action(Op::ClearException, &[])?;
                self.action(Op::CaptureCapabilitiesDropModes, &[])?;
                self.action(Op::CaptureCapabilitiesError, &[])?;
                self.action(Op::ClearException, &[])?;
            }
            self.action(Op::CaptureCapabilitiesSetModes, &[])?;
            self.action(Op::CaptureCapabilitiesAppendEntry, &[])?;
            self.action(Op::CaptureCapabilitiesDropEntry, &[])?;
        }
        self.action(Op::CaptureCapabilitiesDropDevices, &[])?;
        Ok([0.; 3])
    }
}
