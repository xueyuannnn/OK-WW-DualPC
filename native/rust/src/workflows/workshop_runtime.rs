//! Workshop segment lifecycle, request ordering and recovery ownership.
use super::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn workshop_runtime_fail<T>(&self, code: f64) -> Result<T> {
        self.action(Op::WorkshopRuntimeFailure, &[code])?;
        Err(HOST_ERROR)
    }
    fn workshop_rpc(&self) -> Result {
        let kind = self.scalar(Op::WorkshopRequestKind, &[])? as i32;
        if kind == 0 {
            self.action(Op::WorkshopCountFrameRequest, &[])?;
            self.action(Op::WorkshopRefreshObservation, &[])?;
            self.action(Op::WorkshopReadFrameImage, &[])?;
            if self.flag(Op::WorkshopFrameEmpty, &[])? {
                self.action(Op::WorkshopReturnUnavailable, &[])?;
                return Ok(());
            }
            if !self.flag(Op::WorkshopFrameBgr, &[])?
                || self.scalar(Op::WorkshopFrameBytes, &[])? > 64. * 1024. * 1024.
            {
                return self.workshop_runtime_fail(1.);
            }
            if !self.flag(Op::WorkshopFrameContinuous, &[])? {
                self.action(Op::WorkshopCloneFrame, &[])?;
            }
            self.action(Op::WorkshopReadObservationId, &[])?;
            if self.flag(Op::WorkshopObservationIdZero, &[])?
                || !self.flag(Op::WorkshopFrameSequenceMatches, &[])?
                || !self.flag(Op::WorkshopFrameSizeMatches, &[])?
            {
                self.action(Op::WorkshopStartCopyTimer, &[])?;
                self.action(Op::WorkshopCopyMappedFrame, &[])?;
                self.action(Op::WorkshopMeasureCopyTime, &[])?;
                self.action(Op::WorkshopCountFrameCopy, &[])?;
                self.action(Op::WorkshopCacheFrameSequence, &[])?;
                self.action(Op::WorkshopCacheFrameSize, &[])?;
            }
            self.action(Op::WorkshopStampFrameTime, &[])?;
            if !self.flag(Op::WorkshopTelemetryTimerValid, &[])?
                || self.scalar(Op::WorkshopTelemetryElapsed, &[])? >= 250.
            {
                self.action(Op::WorkshopReportNested, &[])?;
                self.action(Op::WorkshopRestartTelemetryTimer, &[])?;
            }
            self.action(Op::WorkshopReturnFrame, &[])?;
            return Ok(());
        }
        if self.flag(Op::WorkshopHasLastFrameTime, &[])?
            && matches!(kind, 1 | 2)
            && self.flag(Op::WorkshopRequestDown, &[])?
        {
            self.action(Op::WorkshopAppendFrameInputSample, &[])?;
            if self.scalar(Op::WorkshopFrameInputSampleCount, &[])? > 128. {
                self.action(Op::WorkshopRemoveFirstInputSample, &[])?;
            }
        }
        self.action(Op::WorkshopForwardCommand, &[])
    }
    fn workshop_receive(&self, timeout: f64) -> Result {
        self.action(Op::WorkshopStartReceiveTimer, &[])?;
        while self.scalar(Op::WorkshopReceiveElapsed, &[])? < timeout {
            self.action(Op::WorkshopCheckpoint, &[])?;
            self.action(Op::WorkshopReadStderr, &[])?;
            if self.scalar(Op::WorkshopStderrSize, &[])? > 65536. {
                self.action(Op::WorkshopTrimStderr, &[65536.])?;
            }
            self.action(Op::WorkshopReadStdout, &[])?;
            if self.scalar(Op::WorkshopBufferSize, &[])? > 16. * 1024. * 1024. {
                return self.workshop_runtime_fail(2.);
            }
            while self.flag(Op::WorkshopBufferHasLine, &[])? {
                self.action(Op::WorkshopReadLine, &[])?;
                self.action(Op::WorkshopConsumeLine, &[])?;
                self.action(Op::WorkshopParseLine, &[])?;
                if self.flag(Op::WorkshopMessageRpc, &[])? {
                    if self.flag(Op::WorkshopLoaded, &[])?
                        && (!self.flag(Op::WorkshopMessageSessionMatches, &[])?
                            || !self.flag(Op::WorkshopMessageRequestMatches, &[])?)
                    {
                        return self.workshop_runtime_fail(3.);
                    }
                    if !self.flag(Op::WorkshopLoaded, &[])?
                        && !self.flag(Op::WorkshopPreHandshakeAllowed, &[])?
                    {
                        return self.workshop_runtime_fail(4.);
                    }
                    let rpc = (|| {
                        self.action(Op::WorkshopRpcNested, &[])?;
                        self.action(Op::WorkshopWriteRpcSuccess, &[])
                    })();
                    if let Err(error) = rpc {
                        let kind = self.scalar(Op::ExceptionKind, &[])? as i32;
                        if matches!(kind, 1 | 4 | 5 | 7) {
                            return Err(error);
                        }
                        self.action(Op::ClearException, &[])?;
                        self.action(Op::WorkshopWriteRpcError, &[])?;
                    }
                    continue;
                }
                if self.flag(Op::WorkshopMessageFatal, &[])? {
                    return self.workshop_runtime_fail(5.);
                }
                if !self.flag(Op::WorkshopMessageEmpty, &[])? {
                    self.action(Op::WorkshopReturnMessage, &[])?;
                    return Ok(());
                }
            }
            if !self.flag(Op::WorkshopProcessRunning, &[])? {
                return self.workshop_runtime_fail(6.);
            }
            self.action(Op::WorkshopWaitReadyRead, &[15.])?;
        }
        self.workshop_runtime_fail(7.)
    }
    fn workshop_start(&self) -> Result {
        if self.flag(Op::WorkshopLoaded, &[])? {
            return Ok(());
        }
        let start = (|| {
            self.action(Op::WorkshopStartStartupTimer, &[])?;
            self.action(Op::WorkshopResetFrameCounters, &[])?;
            self.action(Op::WorkshopResetStartupMetrics, &[])?;
            self.action(Op::WorkshopClearFrameInputSamples, &[])?;
            self.action(Op::WorkshopInvalidateTelemetryTimer, &[])?;
            self.action(Op::WorkshopNewSession, &[])?;
            self.action(Op::WorkshopResolvePython, &[])?;
            if self.flag(Op::WorkshopPythonMissing, &[])? {
                return self.workshop_runtime_fail(8.);
            }
            self.action(Op::WorkshopNewMappingName, &[])?;
            self.action(Op::WorkshopCreateMapping, &[])?;
            if !self.flag(Op::WorkshopMappingPresent, &[])? {
                return self.workshop_runtime_fail(9.);
            }
            self.action(Op::WorkshopMapImage, &[])?;
            if !self.flag(Op::WorkshopMappedPresent, &[])? {
                return self.workshop_runtime_fail(10.);
            }
            self.action(Op::WorkshopSetEnvironment, &[])?;
            self.action(Op::WorkshopSetWorkingDirectory, &[])?;
            self.action(Op::WorkshopStartProcess, &[])?;
            if !self.flag(Op::WorkshopWaitStarted, &[10000.])? {
                return self.workshop_runtime_fail(11.);
            }
            self.action(Op::WorkshopReceiveNested, &[90000.])?;
            self.action(Op::WorkshopValidateHello, &[])?;
            self.action(Op::WorkshopSetLoaded, &[1.])?;
            self.action(Op::WorkshopLoadRuntimeParty, &[])?;
            let n = self.scalar(Op::WorkshopRuntimePartyCount, &[])? as usize;
            for i in 0..n {
                self.action(Op::WorkshopSaveOriginalPolicy, &[i as f64])?;
                self.action(Op::WorkshopAppendLoadCharacter, &[i as f64])?;
            }
            self.action(Op::WorkshopCallLoad, &[])?;
            self.action(Op::WorkshopSyncNested, &[])?;
            self.action(Op::WorkshopMeasureStartup, &[])?;
            self.action(Op::WorkshopClearPingSamples, &[])?;
            for _ in 0..15 {
                self.action(Op::WorkshopStartPingTimer, &[])?;
                self.action(Op::WorkshopCallPing, &[])?;
                self.action(Op::WorkshopAppendPingSample, &[])?;
            }
            self.action(Op::WorkshopSetMedianPing, &[])?;
            self.action(Op::WorkshopReportNested, &[])
        })();
        if start.is_err() {
            self.action(Op::WorkshopStopNested, &[])?;
        }
        start
    }
    fn workshop_call(&self) -> Result {
        let busy = self.scalar(Op::WorkshopReadBusy, &[])?;
        self.action(Op::WorkshopSetBusy, &[1.])?;
        let call = (|| {
            for i in 0..6 {
                self.action(Op::WorkshopFillTaskContext, &[f64::from(i)])?;
            }
            self.action(Op::WorkshopAssignRequestId, &[])?;
            self.action(Op::WorkshopFillRequestIdentity, &[])?;
            self.action(Op::WorkshopFillTaskId, &[])?;
            self.action(Op::WorkshopFillTaskConfig, &[])?;
            self.action(Op::WorkshopFillHostState, &[])?;
            self.action(Op::WorkshopWriteRequest, &[])?;
            self.action(Op::WorkshopReceiveNested, &[600000.])?;
            if !self.flag(Op::WorkshopReplyEvent, &[])?
                || !self.flag(Op::WorkshopReplySessionMatches, &[])?
                || !self.flag(Op::WorkshopReplyRequestMatches, &[])?
            {
                return self.workshop_runtime_fail(12.);
            }
            if !self.flag(Op::WorkshopReplyOk, &[])? {
                return self.workshop_runtime_fail(13.);
            }
            self.action(Op::WorkshopSetBusy, &[busy])?;
            self.action(Op::WorkshopReturnReplyData, &[])
        })();
        if let Err(error) = call {
            self.action(Op::WorkshopSetBusy, &[busy])?;
            self.action(Op::WorkshopStopNested, &[])?;
            let _ = self.action(Op::WorkshopReleaseCommand, &[]);
            return Err(error);
        }
        Ok(())
    }
    fn workshop_sync(&self) -> Result {
        let sync = (|| {
            let n = self.scalar(Op::WorkshopSyncCharacterCount, &[])? as usize;
            for i in 0..n {
                self.action(Op::WorkshopSelectPolicyCharacter, &[i as f64])?;
                if !self.flag(Op::WorkshopPolicyHealerBool, &[])?
                    || !self.flag(Op::WorkshopPolicyShortTargetBool, &[])?
                {
                    return self.workshop_runtime_fail(14.);
                }
                self.action(Op::WorkshopCollectHealerPolicy, &[])?;
                self.action(Op::WorkshopCollectShortPolicy, &[])?;
            }
            self.action(Op::WorkshopApplyStateNested, &[])
        })();
        if let Err(error) = sync {
            self.action(Op::WorkshopStopNested, &[])?;
            let _ = self.action(Op::WorkshopReleaseCommand, &[]);
            return Err(error);
        }
        self.action(Op::WorkshopCommitHealerPolicies, &[])?;
        self.action(Op::WorkshopCommitShortPolicies, &[])?;
        self.action(Op::WorkshopReportNested, &[])
    }
    pub(super) fn run_workshop_runtime(&self, method: Method, args: &[f64]) -> Result<[f64; 3]> {
        match method {
            Method::WorkshopStop => {
                if self.flag(Op::WorkshopProcessRunning, &[])? {
                    self.action(Op::WorkshopKillProcess, &[])?;
                    self.action(Op::WorkshopWaitFinishedIgnored, &[2000.])?;
                }
                if self.flag(Op::WorkshopMappedPresent, &[])? {
                    self.action(Op::WorkshopUnmapImage, &[])?;
                }
                if self.flag(Op::WorkshopMappingPresent, &[])? {
                    self.action(Op::WorkshopCloseMapping, &[])?;
                }
                self.action(Op::WorkshopResetMappedPointer, &[])?;
                self.action(Op::WorkshopResetMappingHandle, &[])?;
                self.action(Op::WorkshopSetLoaded, &[0.])?;
                let n = self.scalar(Op::WorkshopOriginalPolicyCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::WorkshopRestoreOriginalPolicy, &[i as f64])?;
                }
                self.action(Op::WorkshopClearOriginalPolicies, &[])?;
                self.action(Op::WorkshopClearHealerOverrides, &[])?;
                self.action(Op::WorkshopClearShortOverrides, &[])?;
                self.action(Op::WorkshopClearBuffer, &[])?;
                self.action(Op::WorkshopClearErrors, &[])?;
                self.action(Op::WorkshopResetFrameSequence, &[])?;
                self.action(Op::WorkshopResetFrameSize, &[])?;
                self.action(Op::WorkshopResetLastFrameTime, &[])?;
            }
            Method::WorkshopWrite => {
                self.action(Op::WorkshopEncodeMessage, &[])?;
                if !self.flag(Op::WorkshopWriteMessage, &[])? {
                    return self.workshop_runtime_fail(0.);
                }
                self.action(Op::WorkshopWaitWritten, &[1000.])?;
            }
            Method::WorkshopRpc => self.workshop_rpc()?,
            Method::WorkshopReceive => self.workshop_receive(args[0])?,
            Method::WorkshopStart => self.workshop_start()?,
            Method::WorkshopCall => self.workshop_call()?,
            Method::WorkshopSync => self.workshop_sync()?,
            Method::WorkshopReport => self.action(Op::WorkshopPublishTelemetry, &[])?,
            Method::WorkshopReset => {
                self.action(Op::WorkshopStartNested, &[])?;
                self.action(Op::WorkshopCallReset, &[])?;
                self.action(Op::WorkshopSyncNested, &[])?;
            }
            Method::WorkshopCombatEnd => {
                if self.flag(Op::WorkshopLoaded, &[])? {
                    self.action(Op::WorkshopCallEnd, &[])?;
                    self.action(Op::WorkshopSyncNested, &[])?;
                }
            }
            Method::WorkshopPerform => {
                self.action(Op::WorkshopStartNested, &[])?;
                self.action(Op::WorkshopCallPerform, &[])?;
                self.action(Op::WorkshopSyncNested, &[])?;
                if self.flag(Op::WorkshopResultInterrupted, &[])? {
                    return self.workshop_runtime_fail(15.);
                }
                return Ok([1., 0., 0.]);
            }
            Method::WorkshopPriority => {
                self.action(Op::WorkshopStartNested, &[])?;
                self.action(Op::WorkshopCallPriority, args)?;
                if self.flag(Op::WorkshopResultInterrupted, &[])? {
                    return self.workshop_runtime_fail(15.);
                }
                if !self.flag(Op::WorkshopPriorityNumber, &[])? {
                    return self.workshop_runtime_fail(16.);
                }
                self.action(Op::WorkshopSyncNested, &[])?;
                return self.call(Op::WorkshopPriorityInteger, &[]);
            }
            Method::WorkshopSkipsCombatCheck => {
                self.action(Op::WorkshopStartNested, &[])?;
                self.action(Op::WorkshopCallPolicy, &[])?;
                if self.flag(Op::WorkshopResultInterrupted, &[])? {
                    return self.workshop_runtime_fail(15.);
                }
                if !self.flag(Op::WorkshopSkipCheckBool, &[])? {
                    return self.workshop_runtime_fail(17.);
                }
                self.action(Op::WorkshopSyncNested, &[])?;
                return self.call(Op::WorkshopSkipCheckValue, &[]);
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
