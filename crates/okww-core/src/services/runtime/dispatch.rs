//! Compatibility command routing, cleanup precedence and native exception types.
use crate::services::{Host, Op, Result, HOST_ERROR};
impl Host {
    pub(in crate::services) fn run_backend_dispatch(&self) -> Result<[f64; 3]> {
        if !self.flag(Op::BackendRpcAuthenticated, &[])? {
            self.action(Op::BackendRpcFailure, &[0.])?;
            return Err(HOST_ERROR);
        }
        let command = self.scalar(Op::BackendRpcCommand, &[])? as i32;
        match command {
            0 => {
                self.action(Op::BackendRpcHeartbeat, &[])?;
                self.action(Op::BackendRpcSnapshot, &[])?;
                return Ok([0.; 3]);
            }
            1 | 2 => {
                self.action(Op::BackendRpcSnapshot, &[])?;
                return Ok([0.; 3]);
            }
            3 => {
                self.action(Op::BackendRpcReadFrame, &[])?;
                if !self.flag(Op::BackendRpcFramePresent, &[])? {
                    self.action(Op::BackendRpcFrameUnavailable, &[])?;
                    return Ok([0.; 3]);
                }
                if !self.flag(Op::BackendRpcFrameFitsMapping, &[])? {
                    self.action(Op::BackendRpcFailure, &[1.])?;
                    return Err(HOST_ERROR);
                }
                self.action(Op::BackendRpcCopyFrame, &[])?;
                self.action(Op::BackendRpcFrameResult, &[])?;
                return Ok([0.; 3]);
            }
            4 => {
                if self.flag(Op::BackendRpcHasBox, &[])? {
                    self.action(Op::BackendRpcRelease, &[])?;
                }
                return Ok([0.; 3]);
            }
            5 => {
                if !self.flag(Op::BackendRpcHasBox, &[])? {
                    self.action(Op::BackendRpcFailure, &[2.])?;
                    return Err(HOST_ERROR);
                }
                self.action(Op::BackendRpcProbe, &[])?;
                self.action(Op::BackendRpcSnapshot, &[])?;
                return Ok([0.; 3]);
            }
            _ => {}
        }
        // Key-up/button-up remains valid while paused or without a fresh frame.
        if matches!(command, 6 | 7) && !self.flag(Op::BackendRpcDown, &[])? {
            if self.flag(Op::BackendRpcHasBox, &[])?
                && self.flag(Op::BackendRpcBoxConnected, &[])?
            {
                self.action(
                    if command == 6 {
                        Op::BackendRpcKeyUp
                    } else {
                        Op::BackendRpcButtonUp
                    },
                    &[],
                )?;
            }
            if self.flag(Op::BackendRpcHasBox, &[])? {
                self.action(Op::BackendRpcBoxState, &[])?;
            }
            return Ok([0.; 3]);
        }
        if self.flag(Op::BackendRpcArmed, &[])?
            && (!self.flag(Op::BackendRpcHasBox, &[])?
                || !self.flag(Op::BackendRpcBoxConnected, &[])?)
        {
            self.action(Op::BackendRpcDisarmBox, &[])?;
        } else if self.flag(Op::BackendRpcArmed, &[])?
            && !self.flag(Op::BackendRpcFreshFrame, &[])?
            && self.flag(Op::BackendRpcRecoverFrame, &[])?
        {
            self.action(Op::BackendRpcWaitFrame, &[])?;
            return Err(HOST_ERROR);
        }
        if !self.flag(Op::BackendRpcArmed, &[])? {
            self.action(Op::BackendRpcPausedError, &[])?;
            return Err(HOST_ERROR);
        }
        let result = self.action(
            match command {
                8 => Op::BackendRpcMove,
                9 => Op::BackendRpcAbsolute,
                7 => Op::BackendRpcButton,
                6 => Op::BackendRpcKey,
                10 => Op::BackendRpcWheel,
                _ => Op::BackendRpcUnknownCommand,
            },
            &[],
        );
        if result.is_err() {
            if self.flag(Op::BackendRpcErrorIsLocalBlocked, &[])? {
                self.action(Op::ClearException, &[])?;
                // A release error replaces the caught error, as in the native catch.
                self.action(Op::BackendRpcRelease, &[])?;
                self.action(Op::RethrowHandled, &[])?;
                return Err(HOST_ERROR);
            }
            if self.scalar(Op::ExceptionKind, &[])? != 6. {
                return Err(HOST_ERROR);
            }
            self.action(Op::ClearException, &[])?;
            if self.flag(Op::BackendRpcLocalProtocol, &[])? {
                self.action(Op::BackendRpcDisarmLocalError, &[])?;
            } else if !self.flag(Op::BackendRpcBoxConnected, &[])? {
                self.action(Op::BackendRpcDisarmTransportError, &[])?;
            }
            self.action(Op::RethrowHandled, &[])?;
            return Err(HOST_ERROR);
        }
        self.action(Op::BackendRpcBoxState, &[])?;
        Ok([0.; 3])
    }
}
