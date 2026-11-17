//! Device backend construction/start/stop and unchanged exception boundaries.
use super::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn backend_start_inner(&self) -> Result {
        if self.flag(Op::BackendStartSimulation, &[])? {
            for op in [
                Op::BackendSimCreate,
                Op::BackendSimDescribe,
                Op::BackendSimAllocate,
                Op::BackendSimFormat,
                Op::BackendSimFill,
                Op::BackendSimStore,
                Op::BackendDropTempFrame,
            ] {
                self.action(op, &[])?;
            }
        } else {
            self.action(Op::BackendStartCreateCapture, &[])?;
            self.action(Op::BackendStartCapture, &[])?;
        }
        self.action(Op::BackendStartCreateBox, &[])?;
        self.action(Op::BackendStartBox, &[])?;
        if self.flag(Op::BackendStartLocalProtocol, &[])?
            && !self.flag(Op::BackendStartDryRun, &[])?
        {
            self.action(Op::BackendStartCreatePhysicalKeys, &[])?;
        }
        self.action(Op::BackendStartRunning, &[])?;
        self.action(Op::BackendStartClearError, &[])?;
        self.action(Op::BackendStartClearInputFault, &[])?;
        Ok(())
    }
    pub(super) fn run_backend_lifecycle(&self, method: Method) -> Result<[f64; 3]> {
        match method {
            Method::BackendCreate => {
                if self.flag(Op::BackendCreateWsa, &[])? {
                    self.action(Op::BackendLifecycleFailure, &[0.])?;
                    return Err(HOST_ERROR);
                }
                self.action(Op::BackendCreateToken, &[])?;
                if self.flag(Op::BackendCreateCompatibility, &[])? {
                    self.action(Op::BackendCreateMappingName, &[])?;
                    self.action(Op::BackendCreateMapping, &[])?;
                    if !self.flag(Op::BackendLifecycleHasMapping, &[])? {
                        self.action(Op::BackendLifecycleFailure, &[1.])?;
                        return Err(HOST_ERROR);
                    }
                    self.action(Op::BackendCreateMapView, &[])?;
                    if !self.flag(Op::BackendLifecycleHasMapped, &[])? {
                        self.action(Op::BackendLifecycleFailure, &[2.])?;
                        return Err(HOST_ERROR);
                    }
                    self.action(Op::BackendCreateSocket, &[])?;
                    self.action(Op::BackendCreateAddress, &[])?;
                    if !self.flag(Op::BackendLifecycleHasSocket, &[])?
                        || !self.flag(Op::BackendCreateBind, &[])?
                        || !self.flag(Op::BackendCreateListen, &[])?
                    {
                        self.action(Op::BackendLifecycleFailure, &[3.])?;
                        return Err(HOST_ERROR);
                    }
                    self.action(Op::BackendCreateSocketName, &[])?;
                    self.action(Op::BackendCreatePort, &[])?;
                    self.action(Op::BackendCreateServerThread, &[])?;
                }
                self.action(Op::BackendCreateMonitorThread, &[])?;
            }
            Method::BackendDestroy => {
                self.action(Op::BackendDestroySetExit, &[])?;
                if self.flag(Op::BackendLifecycleHasSocket, &[])? {
                    self.action(Op::BackendDestroyShutdown, &[])?;
                }
                if self.flag(Op::BackendDestroyServerJoinable, &[])? {
                    self.action(Op::BackendDestroyJoinServer, &[])?;
                }
                if self.flag(Op::BackendDestroyMonitorJoinable, &[])? {
                    self.action(Op::BackendDestroyJoinMonitor, &[])?;
                }
                self.action(Op::BackendLifecycleStop, &[])?;
                if self.flag(Op::BackendLifecycleHasSocket, &[])? {
                    self.action(Op::BackendDestroyCloseSocket, &[])?;
                }
                if self.flag(Op::BackendLifecycleHasMapped, &[])? {
                    self.action(Op::BackendDestroyUnmap, &[])?;
                }
                if self.flag(Op::BackendLifecycleHasMapping, &[])? {
                    self.action(Op::BackendDestroyCloseMapping, &[])?;
                }
                self.action(Op::BackendDestroyWsa, &[])?;
            }
            Method::BackendStart => {
                self.action(Op::BackendLifecycleStop, &[])?;
                self.action(Op::BackendStartCopyConfig, &[])?;
                if self.flag(Op::BackendStartSimulation, &[])? {
                    self.action(Op::BackendStartForceDryRun, &[])?;
                }
                self.action(Op::BackendStartValidateCapture, &[])?;
                self.action(Op::BackendStartValidateBox, &[])?;
                self.action(Op::BackendStartStoreSimulation, &[])?;
                if self.backend_start_inner().is_err() {
                    if self.scalar(Op::ExceptionKind, &[])? != 6. {
                        return Err(HOST_ERROR);
                    }
                    self.action(Op::ClearException, &[])?;
                    self.action(Op::BackendDropTempFrame, &[])?;
                    self.action(Op::BackendStartStoreError, &[])?;
                    self.action(Op::BackendLifecycleStop, &[])?;
                    self.action(Op::RethrowHandled, &[])?;
                    return Err(HOST_ERROR);
                }
            }
            Method::BackendConfigMatches => {
                self.action(Op::BackendMatchCopyConfig, &[])?;
                if self.flag(Op::BackendMatchSimulation, &[])? {
                    self.action(Op::BackendMatchForceDryRun, &[])?;
                }
                return Ok([
                    f64::from(
                        self.flag(Op::BackendMatchRunning, &[])?
                            && self.flag(Op::BackendMatchEqualConfig, &[])?,
                    ),
                    0.,
                    0.,
                ]);
            }
            Method::BackendStop => {
                self.action(Op::BackendStopDisarm, &[])?;
                self.action(Op::BackendStopRunning, &[])?;
                self.action(Op::BackendStopResetRecovery, &[])?;
                if self.flag(Op::BackendStopHasBox, &[])? {
                    self.action(Op::BackendStopCloseBox, &[])?;
                }
                self.action(Op::BackendStopDropBox, &[])?;
                self.action(Op::BackendStopDropPhysicalKeys, &[])?;
                if self.flag(Op::BackendStopHasCapture, &[])? {
                    self.action(Op::BackendStopCapture, &[])?;
                }
                self.action(Op::BackendStopDropCapture, &[])?;
                self.action(Op::BackendStopDropSimulated, &[])?;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
