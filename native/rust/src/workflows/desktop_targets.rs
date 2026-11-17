//! Monitor/window picker enumeration and filtering policy.
use super::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    pub(super) fn run_desktop_targets(&self, method: Method, args: &[f64]) -> Result<[f64; 3]> {
        match method {
            Method::DesktopTargets => {
                if args[0] == 0. {
                    self.action(Op::DesktopTargetsEnumerateMonitors, &[])?;
                }
                self.action(Op::DesktopTargetsEnumerateWindows, &[])?;
            }
            Method::DesktopTargetsMonitor => {
                if !self.flag(Op::DesktopTargetsMonitorInfo, &[])? {
                    return Ok([0.; 3]);
                }
                self.action(Op::DesktopTargetsMonitorName, &[])?;
                if self.action(Op::DesktopTargetsMonitorRefresh, &[]).is_err() {
                    if self.scalar(Op::ExceptionKind, &[])? != 6. {
                        return Err(HOST_ERROR);
                    }
                    self.action(Op::ClearException, &[])?;
                    self.action(Op::ClearException, &[])?;
                }
                self.action(Op::DesktopTargetsAppendMonitor, &[])?;
            }
            Method::DesktopTargetsWindow => {
                self.action(Op::DesktopTargetsWindowProcess, &[])?;
                if !self.flag(Op::DesktopTargetsWindowVisible, &[])?
                    || self.flag(Op::DesktopTargetsWindowOwned, &[])?
                    || self.flag(Op::DesktopTargetsWindowOwnProcess, &[])?
                    || self.flag(Op::DesktopTargetsWindowShell, &[])?
                    || self.flag(Op::DesktopTargetsWindowExcludedStyle, &[])?
                {
                    return Ok([0.; 3]);
                }
                self.action(Op::DesktopTargetsWindowTitle, &[])?;
                if self.flag(Op::DesktopTargetsWindowTitleEmpty, &[])? {
                    return Ok([0.; 3]);
                }
                self.action(Op::DesktopTargetsWindowExe, &[])?;
                if self.flag(Op::DesktopTargetsWindowExeEmpty, &[])? {
                    return Ok([0.; 3]);
                }
                self.action(Op::DesktopTargetsAppendWindow, &[])?;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
