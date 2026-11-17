//! Python compatibility execution remains external; its lifecycle, input
//! routing, recording and wire-message ordering are owned here.
use super::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn script_failure<T>(&self, reason: f64) -> Result<T> {
        self.action(Op::ScriptFailure, &[reason])?;
        Err(HOST_ERROR)
    }
    fn script_rpc(&self) -> Result {
        match self.scalar(Op::ScriptRpcKind, &[])? as i32 {
            0 => {
                self.action(Op::ScriptRefreshFrame, &[])?;
                if self.flag(Op::ScriptFrameEmpty, &[])? {
                    self.action(Op::ScriptReturnUnavailableFrame, &[])?;
                    return Ok(());
                }
                if !self.flag(Op::ScriptFrameBgr, &[])?
                    || self.scalar(Op::ScriptFrameBytes, &[])? > 64. * 1024. * 1024.
                {
                    return self.script_failure(6.);
                }
                if !self.flag(Op::ScriptFrameContinuous, &[])? {
                    self.action(Op::ScriptCloneFrame, &[])?;
                }
                self.action(Op::ScriptCopyMappedFrame, &[])?;
                self.action(Op::ScriptReturnFrame, &[])?;
            }
            1 => self.action(Op::ScriptReportProgress, &[])?,
            2 => {
                if self.flag(Op::ScriptHasInfo, &[])? {
                    self.action(Op::ScriptReportInfo, &[])?;
                }
            }
            3 => {
                self.action(Op::ScriptReadInputAction, &[])?;
                if self.flag(Op::ScriptHasProgress, &[])? {
                    self.action(Op::ScriptReportInputAction, &[])?;
                }
                self.action(Op::ScriptReadInputPoint, &[])?;
                let point = self.call(Op::ScriptInputPoint, &[])?;
                let positioned = point[0] >= 0. && point[1] >= 0.;
                self.action(Op::ScriptReadInputKey, &[])?;
                match self.scalar(Op::ScriptInputKind, &[])? as i32 {
                    0 => self.action(Op::ScriptInputMove, &[])?,
                    1 => self.action(Op::ScriptInputClick, &[f64::from(positioned)])?,
                    2 => {
                        if positioned {
                            self.action(Op::ScriptInputMove, &[])?;
                        }
                        self.action(Op::ScriptInputMouseDown, &[])?;
                    }
                    3 => self.action(Op::ScriptInputMouseUp, &[])?,
                    4 => self.action(Op::ScriptInputKeyDown, &[])?,
                    5 => self.action(Op::ScriptInputKeyUp, &[])?,
                    6 => self.action(Op::ScriptInputKey, &[])?,
                    7 => self.action(Op::ScriptInputScroll, &[f64::from(positioned)])?,
                    8 => self.action(Op::ScriptInputSwipe, &[])?,
                    9 => self.action(Op::ScriptInputText, &[])?,
                    _ => return self.script_failure(7.),
                }
                self.action(Op::ScriptReadInputState, &[])?;
                if self.flag(Op::ScriptInputBoxHasKeys, &[])? {
                    self.action(Op::ScriptReturnBoxState, &[])?;
                } else {
                    self.action(Op::ScriptReturnInputState, &[])?;
                }
            }
            4 => {
                if !self.flag(Op::ScriptHasBuiltin, &[])? {
                    return self.script_failure(8.);
                }
                self.action(Op::ScriptReadBuiltinTask, &[])?;
                self.action(Op::ScriptCreateBuiltinScope, &[])?;
                self.action(Op::ScriptInvokeBuiltin, &[])?;
            }
            _ => self.action(Op::ScriptForwardInputRequest, &[])?,
        }
        Ok(())
    }
    fn script_receive(&self) -> Result {
        loop {
            self.action(Op::Checkpoint, &[])?;
            if self.flag(Op::ScriptHasEnabledPredicate, &[])?
                && !self.flag(Op::ScriptEnabled, &[])?
            {
                return self.script_failure(9.);
            }
            self.action(Op::ScriptReadStderr, &[])?;
            self.action(Op::ScriptTrimStderr, &[65536.])?;
            self.action(Op::ScriptReadStdout, &[])?;
            if self.scalar(Op::ScriptBufferBytes, &[])? > 16. * 1024. * 1024. {
                return self.script_failure(10.);
            }
            while self.flag(Op::ScriptBufferHasLine, &[])? {
                self.action(Op::ScriptParseLine, &[])?;
                self.action(Op::ScriptConsumeLine, &[])?;
                if self.flag(Op::ScriptMessageRpc, &[])? {
                    let reply = (|| {
                        self.action(Op::ScriptInvokeRpc, &[])?;
                        self.action(Op::ScriptWriteRpcSuccess, &[])
                    })();
                    if let Err(error) = reply {
                        let kind = self.scalar(Op::ExceptionKind, &[])? as i32;
                        if matches!(kind, 1 | 7) {
                            return Err(error);
                        }
                        self.action(Op::ClearException, &[])?;
                        self.action(Op::ScriptWriteRpcError, &[])?;
                    }
                } else if !self.flag(Op::ScriptMessageEmpty, &[])? {
                    self.action(Op::ScriptReturnMessage, &[])?;
                    return Ok(());
                }
            }
            if !self.flag(Op::ScriptProcessRunning, &[])? {
                return self.script_failure(11.);
            }
            self.action(Op::ScriptWaitReadyRead, &[15.])?;
        }
    }
    fn script_start(&self) -> Result {
        if self.flag(Op::ScriptProcessRunning, &[])? {
            return Ok(());
        }
        self.action(Op::ScriptResolveRuntime, &[])?;
        if self.flag(Op::ScriptPythonEmpty, &[])? {
            return self.script_failure(12.);
        }
        self.action(Op::ScriptCreateMappingName, &[])?;
        self.action(Op::ScriptCreateMapping, &[])?;
        if !self.flag(Op::ScriptMappingPresent, &[])? {
            return self.script_failure(13.);
        }
        self.action(Op::ScriptMapImage, &[])?;
        if !self.flag(Op::ScriptMappedPresent, &[])? {
            return self.script_failure(14.);
        }
        self.action(Op::ScriptSetEnvironment, &[])?;
        self.action(Op::ScriptSetWorkingDirectory, &[])?;
        self.action(Op::ScriptStartWorker, &[])?;
        if !self.flag(Op::ScriptWaitStarted, &[10000.])? {
            return self.script_failure(15.);
        }
        self.action(Op::ScriptReceiveNested, &[])?;
        if !self.flag(Op::ScriptReplyReady, &[])? {
            return self.script_failure(16.);
        }
        Ok(())
    }
    pub(super) fn run_scripts(&self, method: Method) -> Result<[f64; 3]> {
        match method {
            Method::ScriptEnvironment => {
                for i in 0..4 {
                    self.action(Op::ScriptRemoveEnvironment, &[f64::from(i)])?;
                }
                for i in 0..3 {
                    self.action(Op::ScriptInsertEnvironment, &[f64::from(i)])?;
                }
            }
            Method::ScriptManage => {
                self.action(Op::ScriptResolveRuntime, &[])?;
                if self.flag(Op::ScriptPythonEmpty, &[])? {
                    return self.script_failure(0.);
                }
                self.action(Op::ScriptSetEnvironment, &[])?;
                self.action(Op::ScriptSetWorkingDirectory, &[])?;
                self.action(Op::ScriptStartManager, &[])?;
                if !self.flag(Op::ScriptWaitStarted, &[10000.])? {
                    return self.script_failure(1.);
                }
                self.action(Op::ScriptWriteManagerRequest, &[])?;
                self.action(Op::ScriptCloseWriteChannel, &[])?;
                if !self.flag(Op::ScriptWaitFinished, &[60000.])? {
                    self.action(Op::ScriptKillProcess, &[])?;
                    self.action(Op::ScriptWaitFinishedIgnored, &[2000.])?;
                    return self.script_failure(2.);
                }
                self.action(Op::ScriptReadManagerReply, &[])?;
                if self.scalar(Op::ScriptExitCode, &[])? != 0.
                    || !self.flag(Op::ScriptManagerReplyOk, &[])?
                {
                    return self.script_failure(3.);
                }
                self.action(Op::ScriptReturnManagerData, &[])?;
            }
            Method::ScriptCatalogue => {
                if self.flag(Op::ScriptCatalogueExists, &[])? {
                    self.action(Op::ScriptReadCatalogue, &[])?;
                }
            }
            Method::RecordedScript => {
                if !self.flag(Op::RecordedClassNameValid, &[])? {
                    return self.script_failure(4.);
                }
                self.action(Op::RecordedScriptHeader, &[])?;
                let count = self.scalar(Op::RecordedEventCount, &[])? as usize;
                let mut previous = 0.;
                for i in 0..count {
                    self.action(Op::LoadRecordedEvent, &[i as f64])?;
                    let time = self.scalar(Op::RecordedEventTime, &[])?;
                    if !time.is_finite() || time < previous {
                        return self.script_failure(5.);
                    }
                    if time > previous + 0.001 {
                        self.action(Op::RecordedAppendWait, &[time - previous])?;
                    }
                    previous = time;
                    self.action(Op::RecordedPrepareFields, &[])?;
                    match self.scalar(Op::RecordedEventKind, &[])? as i32 {
                        0 => self.action(Op::RecordedAppendKey, &[])?,
                        1 => self.action(Op::RecordedAppendMouseDown, &[])?,
                        2 => self.action(Op::RecordedAppendMouseUp, &[])?,
                        3 => self.action(Op::RecordedAppendMove, &[])?,
                        4 => self.action(Op::RecordedAppendWheel, &[])?,
                        _ => return self.script_failure(17.),
                    }
                }
                if count == 0 {
                    self.action(Op::RecordedAppendPass, &[])?;
                }
            }
            Method::ScriptStop => {
                if self.flag(Op::ScriptProcessRunning, &[])? {
                    self.action(Op::ScriptKillProcess, &[])?;
                    self.action(Op::ScriptWaitFinishedIgnored, &[2000.])?;
                }
                self.action(Op::ScriptReleaseInput, &[])?;
                if self.flag(Op::ScriptMappedPresent, &[])? {
                    self.action(Op::ScriptUnmapImage, &[])?;
                }
                if self.flag(Op::ScriptMappingPresent, &[])? {
                    self.action(Op::ScriptCloseMapping, &[])?;
                }
                self.action(Op::ScriptResetMappedPointer, &[])?;
                self.action(Op::ScriptResetMappingHandle, &[])?;
                self.action(Op::ScriptClearBuffer, &[])?;
                self.action(Op::ScriptClearErrors, &[])?;
            }
            Method::ScriptWrite => {
                self.action(Op::ScriptEncodeMessage, &[])?;
                if !self.flag(Op::ScriptWriteMessage, &[])? {
                    return self.script_failure(18.);
                }
                self.action(Op::ScriptWaitWritten, &[1000.])?;
            }
            Method::ScriptRpc => self.script_rpc()?,
            Method::ScriptReceive => self.script_receive()?,
            Method::ScriptStart => self.script_start()?,
            Method::ScriptRun => {
                let run = (|| {
                    self.action(Op::ScriptStartNested, &[])?;
                    let fields = self.scalar(Op::ScriptFieldCount, &[])? as usize;
                    for i in 0..fields {
                        self.action(Op::ScriptCopyFieldDefault, &[i as f64])?;
                    }
                    if self.flag(Op::ScriptDefinitionTrigger, &[])?
                        && self.flag(Op::ScriptSavedValuesExist, &[])?
                    {
                        self.action(Op::ScriptReadSavedValues, &[])?;
                        let count = self.scalar(Op::ScriptSavedValueCount, &[])? as usize;
                        for i in 0..count {
                            self.action(Op::ScriptSelectSavedValue, &[i as f64])?;
                            if self.flag(Op::ScriptDefaultContainsSavedKey, &[])? {
                                self.action(Op::ScriptOverlaySavedValue, &[])?;
                            }
                        }
                    }
                    self.action(Op::ScriptWriteRunRequest, &[])?;
                    self.action(Op::ScriptReceiveNested, &[])?;
                    if !self.flag(Op::ScriptReplyOk, &[])? {
                        return self.script_failure(19.);
                    }
                    self.action(Op::ScriptReturnReplyData, &[])
                })();
                if run.is_err() {
                    self.action(Op::ScriptStopNested, &[])?;
                }
                run?;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
