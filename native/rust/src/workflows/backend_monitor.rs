//! Watchdog loop: the original idle/connection checks precede clock reads.
use super::{Host, Op, Result};
impl Host {
    pub(super) fn run_backend_monitor(&self) -> Result<[f64; 3]> {
        while !self.flag(Op::BackendMonitorExit, &[])? {
            self.action(Op::BackendMonitorSleep, &[])?;
            self.action(Op::BackendMonitorLock, &[])?;
            if !self.flag(Op::BackendMonitorArmed, &[])? {
                self.action(Op::BackendMonitorUnlock, &[])?;
                continue;
            }
            if !self.flag(Op::BackendMonitorHasBox, &[])?
                || !self.flag(Op::BackendMonitorBoxConnected, &[])?
            {
                self.action(Op::BackendMonitorDisarmBox, &[])?;
            } else if self.flag(Op::BackendMonitorHeartbeatExpired, &[])? {
                self.action(Op::BackendMonitorDisarmHeartbeat, &[])?;
            } else if !self.flag(Op::BackendMonitorFreshFrame, &[])? {
                self.action(Op::BackendMonitorRecover, &[])?;
            }
            self.action(Op::BackendMonitorUnlock, &[])?;
        }
        Ok([0.; 3])
    }
}
