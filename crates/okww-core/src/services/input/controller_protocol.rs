//! Controller packet/report sequencing, history limits and latency accounting.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    pub(in crate::services) fn run_controller_protocol(&self, method: Method) -> Result<[f64; 3]> {
        match method {
            Method::ControllerLatency => {
                self.action(Op::ControllerLatencyAppend, &[])?;
                if self.scalar(Op::ControllerLatencyCount, &[])? > 128. {
                    self.action(Op::ControllerLatencyPop, &[])?;
                }
                self.action(Op::ControllerLatencyCopy, &[])?;
                if !self.flag(Op::ControllerLatencyPercentiles, &[])? {
                    self.action(Op::ControllerProtocolFailure, &[0.])?;
                    return Err(HOST_ERROR);
                }
                self.action(Op::ControllerLatencyStore, &[])?;
            }
            Method::ControllerRecord => {
                self.action(Op::ControllerHistoryAppend, &[])?;
                while self.scalar(Op::ControllerHistoryCount, &[])? > 256. {
                    self.action(Op::ControllerHistoryPop, &[])?;
                }
            }
            Method::ControllerExchange => {
                self.action(Op::ControllerExchangeClock, &[])?;
                self.action(Op::ControllerExchangeSequence, &[])?;
                self.action(Op::ControllerExchangeAllocate, &[])?;
                self.action(Op::ControllerExchangeHeader, &[])?;
                self.action(Op::ControllerExchangePayload, &[])?;
                if self.flag(Op::ControllerProtocolDryRun, &[])? {
                    self.action(Op::ControllerExchangeRecord, &[])?;
                    return Ok([0.; 3]);
                }
                let status = self.scalar(Op::ControllerExchangeTransport, &[])?;
                if status == 1. {
                    self.action(Op::ControllerExchangeLatency, &[])?;
                    return Ok([0.; 3]);
                }
                self.action(Op::ControllerExchangeDisconnected, &[])?;
                self.action(
                    Op::ControllerProtocolFailure,
                    &[if status == -1. { 1. } else { 2. }],
                )?;
                return Err(HOST_ERROR);
            }
            Method::ControllerMouseReport => {
                self.action(Op::ControllerMouseAllocate, &[])?;
                self.action(Op::ControllerMouseEncode, &[])?;
                self.action(Op::ControllerMouseExchange, &[])?;
            }
            Method::ControllerKeyboardReport => {
                self.action(Op::ControllerProtocolBeginKeys, &[])?;
                let mut count = 0;
                while self.flag(Op::ControllerProtocolHasKey, &[])? {
                    if count >= 10 {
                        self.action(Op::ControllerProtocolFailure, &[3.])?;
                        return Err(HOST_ERROR);
                    }
                    self.action(Op::ControllerKeyboardCopyKey, &[count as f64])?;
                    count += 1;
                    self.action(Op::ControllerProtocolNextKey, &[])?;
                }
                self.action(Op::ControllerKeyboardAllocate, &[])?;
                if !self.flag(Op::ControllerKeyboardEncode, &[count as f64])? {
                    self.action(Op::ControllerProtocolFailure, &[4.])?;
                    return Err(HOST_ERROR);
                }
                self.action(Op::ControllerKeyboardExchange, &[])?;
            }
            Method::ControllerState => {
                self.action(Op::ControllerProtocolBeginKeys, &[])?;
                while self.flag(Op::ControllerProtocolHasKey, &[])? {
                    self.action(Op::ControllerStateAppendKey, &[])?;
                    self.action(Op::ControllerProtocolNextKey, &[])?;
                }
                self.action(Op::ControllerStateResult, &[])?;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
