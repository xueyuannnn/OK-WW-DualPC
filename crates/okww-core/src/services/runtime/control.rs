//! Backend arming, heartbeat, input probing and physical key routing.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    pub(in crate::services) fn run_backend_control(&self, method: Method) -> Result<[f64; 3]> {
        match method {
            Method::BackendArm => {
                let enabled = self.flag(Op::BackendControlEnabled, &[])?;
                if enabled {
                    self.action(Op::BackendControlReadFrame, &[])?;
                    let ready = self.flag(Op::BackendControlFramePresent, &[])?
                        && self.flag(Op::BackendControlHasBox, &[])?
                        && self.flag(Op::BackendControlBoxConnected, &[])?;
                    self.action(Op::BackendControlDropFrame, &[])?;
                    if !ready {
                        self.action(Op::BackendControlFailure, &[0.])?;
                        return Err(HOST_ERROR);
                    }
                    if !self.flag(Op::BackendControlBoxKeyboard, &[])? {
                        self.action(Op::BackendControlCapabilityError, &[])?;
                        return Err(HOST_ERROR);
                    }
                    self.action(Op::BackendControlHeartbeatNow, &[])?;
                    self.action(Op::BackendControlResetRecovery, &[])?;
                    self.action(Op::BackendControlClearError, &[])?;
                    self.action(Op::BackendControlClearFault, &[])?;
                }
                self.action(Op::BackendControlSetArmed, &[f64::from(enabled)])?;
                if !enabled && self.flag(Op::BackendControlHasBox, &[])? {
                    self.action(Op::BackendControlRelease, &[])?;
                }
            }
            Method::BackendHeartbeat => {
                if !self.flag(Op::BackendControlArmed, &[])? {
                    return Ok([0.; 3]);
                }
                self.action(Op::BackendControlHeartbeatNow, &[])?;
                return Ok([1., 0., 0.]);
            }
            Method::BackendTestMove => {
                if !self.flag(Op::BackendControlHasBox, &[])?
                    || !self.flag(Op::BackendControlBoxConnected, &[])?
                {
                    self.action(Op::BackendControlFailure, &[1.])?;
                    return Err(HOST_ERROR);
                }
                self.action(
                    if self.flag(Op::BackendControlLocal, &[])? {
                        Op::BackendControlLocalTestCounts
                    } else {
                        Op::BackendControlNetTestCounts
                    },
                    &[],
                )?;
                if self.action(Op::BackendControlMove, &[]).is_err() {
                    if self.scalar(Op::ExceptionKind, &[])? != 6. {
                        return Err(HOST_ERROR);
                    }
                    self.action(Op::ClearException, &[])?;
                    self.action(Op::BackendControlSaveMoveError, &[])?;
                    self.action(Op::BackendControlSetArmed, &[0.])?;
                    self.action(Op::BackendControlMoveError, &[])?;
                    if self.action(Op::BackendControlRelease, &[]).is_err() {
                        self.action(Op::ClearException, &[])?;
                        self.action(Op::ClearException, &[])?;
                    }
                    self.action(Op::BackendControlRethrowMoveError, &[])?;
                    return Err(HOST_ERROR);
                }
                self.action(Op::BackendControlMoveResult, &[])?;
            }
            Method::BackendPhysicalDown => {
                return self.call(
                    if self.flag(Op::BackendControlHasPhysicalKeys, &[])? {
                        Op::BackendControlPhysicalDown
                    } else {
                        Op::BackendControlAsyncDown
                    },
                    &[],
                );
            }
            Method::BackendPhysicalHotkey => {
                if !self.flag(Op::BackendControlHasPhysicalKeys, &[])? {
                    return Ok([1., 0., 0.]);
                }
                return self.call(Op::BackendControlPhysicalHotkey, &[]);
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
