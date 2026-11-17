//! Connection, emergency release and shutdown retain the original exception ownership.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn controller_connect_inner(&self) -> Result {
        self.action(Op::ControllerConnectResetSequence, &[])?;
        self.action(Op::ControllerConnectResetLatency, &[])?;
        if self.flag(Op::ControllerLifecycleLocal, &[])? {
            if !self.flag(Op::ControllerLifecycleDryRun, &[])? {
                if self.flag(Op::ControllerConnectTargetEnabled, &[])? {
                    self.action(Op::ControllerConnectCreateTarget, &[])?;
                }
                if self.flag(Op::ControllerConnectDesktopSource, &[])? {
                    self.action(Op::ControllerConnectCreateCaptureTarget, &[])?;
                }
            }
            self.action(Op::ControllerConnectLocalCapability, &[])?;
            self.action(Op::ControllerConnectMarkConnected, &[])?;
            return Ok(());
        }
        self.action(Op::ControllerConnectUuid, &[])?;
        if !self.flag(Op::ControllerLifecycleDryRun, &[])? {
            self.action(Op::ControllerConnectSocket, &[])?;
            self.action(Op::ControllerConnectAddress, &[])?;
            self.action(Op::ControllerConnectParseAddress, &[])?;
            if !self.flag(Op::ControllerLifecycleSocketValid, &[])?
                || !self.flag(Op::ControllerConnectPeer, &[])?
            {
                self.action(Op::ControllerLifecycleFailure, &[0.])?;
                return Err(HOST_ERROR);
            }
        }
        self.action(Op::ControllerConnectExchange, &[])?;
        self.action(Op::ControllerConnectNetCapability, &[])?;
        self.action(Op::ControllerConnectMarkConnected, &[])?;
        self.action(Op::ControllerLifecycleRelease, &[])?;
        Ok(())
    }
    fn controller_release_attempt(&self, op: Op, args: &[f64]) -> Result {
        if self.action(op, args).is_err() {
            self.action(Op::ClearException, &[])?;
            if !self.flag(Op::ControllerReleaseHasError, &[])? {
                self.action(Op::ControllerReleaseSaveError, &[])?;
            }
            self.action(Op::ClearException, &[])?;
        }
        Ok(())
    }
    pub(in crate::services) fn run_controller_lifecycle(&self, method: Method) -> Result<[f64; 3]> {
        match method {
            Method::ControllerConnect => {
                if self.flag(Op::ControllerLifecycleConnected, &[])? {
                    return Ok([0.; 3]);
                }
                self.action(Op::ControllerLifecycleClose, &[])?;
                self.action(Op::ControllerConnectValidate, &[])?;
                if self.controller_connect_inner().is_err() {
                    self.action(Op::ClearException, &[])?;
                    self.action(Op::ControllerLifecycleClose, &[])?;
                    self.action(Op::RethrowHandled, &[])?;
                    return Err(HOST_ERROR);
                }
            }
            Method::ControllerRelease => {
                if self.flag(Op::ControllerLifecycleLocal, &[])? {
                    self.action(Op::ControllerReleaseSnapshotKeys, &[])?;
                    let states = self.call(Op::ControllerReleaseSnapshotStates, &[])?;
                    self.action(Op::ControllerReleaseBeginKeys, &[])?;
                    while self.flag(Op::ControllerReleaseHasKey, &[])? {
                        self.controller_release_attempt(Op::ControllerReleaseKey, &[])?;
                        self.action(Op::ControllerReleaseNextKey, &[])?;
                    }
                    for index in 0..8 {
                        if states[0] as i32 & (1 << index) != 0 {
                            self.controller_release_attempt(
                                Op::ControllerReleaseModifier,
                                &[index as f64],
                            )?;
                        }
                    }
                    for index in 0..5 {
                        if states[1] as i32 & (1 << index) != 0 {
                            self.controller_release_attempt(
                                Op::ControllerReleaseButton,
                                &[index as f64],
                            )?;
                        }
                    }
                } else {
                    self.action(Op::ControllerLifecycleClearHeldState, &[])?;
                    self.action(Op::ControllerLifecycleClearKeys, &[])?;
                    self.controller_release_attempt(Op::ControllerReleaseMouse, &[])?;
                    self.controller_release_attempt(Op::ControllerReleaseKeyboard, &[])?;
                }
                if self.flag(Op::ControllerReleaseHasError, &[])? {
                    self.action(Op::ControllerReleaseRethrow, &[])?;
                    return Err(HOST_ERROR);
                }
            }
            Method::ControllerClose => {
                if (self.flag(Op::ControllerLifecycleConnected, &[])?
                    || self.flag(Op::ControllerLifecycleSocketValid, &[])?)
                    && self.action(Op::ControllerLifecycleRelease, &[]).is_err()
                {
                    self.action(Op::ClearException, &[])?;
                    self.action(Op::ClearException, &[])?;
                }
                if self.flag(Op::ControllerLifecycleSocketValid, &[])? {
                    self.action(Op::ControllerCloseSocket, &[])?;
                }
                for op in [
                    Op::ControllerCloseInvalidateSocket,
                    Op::ControllerCloseDisconnected,
                    Op::ControllerCloseCaptureTarget,
                    Op::ControllerCloseTarget,
                    Op::ControllerLifecycleClearHeldState,
                    Op::ControllerLifecycleClearKeys,
                ] {
                    self.action(op, &[])?;
                }
            }
            Method::ControllerProbe => {
                if !self.flag(Op::ControllerLifecycleConnected, &[])? {
                    self.action(Op::ControllerLifecycleFailure, &[1.])?;
                    return Err(HOST_ERROR);
                }
                if !self.flag(Op::ControllerLifecycleLocal, &[])? {
                    self.action(Op::ControllerProbeMove, &[])?;
                }
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
