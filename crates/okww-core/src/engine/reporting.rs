use super::*;
impl Host {
    pub(super) fn record_result(&self) -> EngineResult {
        if let Err(error) = self.action(Op::WriteRunResult, &[]) {
            if self.call(Op::ExceptionKind, &[])? == 5. {
                return Err(error);
            }
            self.action(Op::ClearException, &[])?;
            self.action(Op::LogResultFailure, &[])?;
        }
        Ok(())
    }
    fn step_diagnostic_body(&self) -> EngineResult {
        self.action(Op::BuildStepPaths, &[])?;
        let capture = (|| {
            if !self.flag(Op::StepFrameEmpty, &[])?
                && self.flag(Op::CreateScreenshotDirectory, &[])?
                && self.flag(Op::EncodeStepImage, &[])?
                && self.flag(Op::OpenScreenshot, &[])?
                && self.flag(Op::WriteScreenshot, &[])?
                && self.flag(Op::CommitScreenshot, &[])?
            {
                self.action(Op::SetScreenshotPath, &[])?;
            }
            Ok::<_, i32>(())
        })();
        let dispose = self.action(Op::DisposeScreenshot, &[]);
        capture.and(dispose)?;
        self.action(Op::SaveStepDiagnostic, &[])?;
        if self.call(Op::StepDiagnosticCount, &[])? >= 3. {
            self.action(Op::RemoveFirstStepDiagnostic, &[])?;
        }
        self.action(Op::AppendStepDiagnostic, &[])?;
        self.action(Op::LogStepDiagnostic, &[])
    }
    pub(super) fn step_diagnostic(&self) -> EngineResult {
        if let Err(error) = self.step_diagnostic_body() {
            if self.call(Op::ExceptionKind, &[])? == 5. {
                return Err(error);
            }
            self.action(Op::ClearException, &[])?;
            self.action(Op::LogStepFailure, &[])?;
        }
        Ok(())
    }
    pub(super) fn error_screenshot(&self) -> EngineResult {
        if self.flag(Op::LowMemory, &[])? {
            return Ok(());
        }
        self.action(Op::ReadErrorFrame, &[])?;
        if !self.flag(Op::ErrorFrameAvailable, &[])? {
            return Ok(());
        }
        self.action(Op::PrepareErrorImage, &[])?;
        if !self.flag(Op::EncodeErrorImage, &[])? {
            return Ok(());
        }
        self.action(Op::BuildErrorScreenshot, &[])?;
        if self.flag(Op::OpenScreenshot, &[])?
            && self.flag(Op::WriteScreenshot, &[])?
            && self.flag(Op::CommitScreenshot, &[])?
        {
            self.action(Op::LogScreenshot, &[])?;
            self.action(Op::SetScreenshotPath, &[])?;
        }
        self.action(Op::DisposeScreenshot, &[])?;
        self.action(Op::AppendNoticeImage, &[])
    }
    pub(super) fn disarm(&self) -> EngineResult {
        self.action(Op::LockLifecycle, &[])?;
        let result = (|| {
            self.action(Op::StopGate, &[])?;
            self.action(Op::SetPaused, &[1.])?;
            self.action(Op::SetWarming, &[0.])?;
            if let Err(error) = self.action(Op::BackendArm, &[0.]) {
                if self.call(Op::ExceptionKind, &[])? == 5. {
                    return Err(error);
                }
                self.action(Op::ClearException, &[])?;
                self.action(Op::LogException, &[])?;
            }
            Ok(())
        })();
        let unlock = self.action(Op::UnlockLifecycle, &[]);
        result.and(unlock)
    }
    pub(super) fn session_lifetime(&self) -> EngineResult {
        if let Err(error) = self.action(Op::ExecuteSession, &[]) {
            let kind = self.call(Op::ExceptionKind, &[])?;
            if kind == 5. {
                return Err(error);
            }
            self.action(Op::ClearException, &[])?;
            self.action(Op::ReleaseEmergency, &[])?;
            if kind == 1. {
                self.action(Op::SetCancelledFailure, &[])?;
            } else {
                let low_memory = self.flag(Op::ClassifyFailure, &[])?;
                self.action(Op::SetTaskFailure, &[])?;
                if low_memory {
                    self.action(Op::LogLowMemoryError, &[])?;
                }
                self.disarm()?;
                self.action(Op::BuildErrorEvent, &[])?;
                if self.flag(Op::IsExecutionFailure, &[])? {
                    self.action(Op::SetErrorTraceback, &[])?;
                } else {
                    self.action(Op::SetErrorType, &[])?;
                }
                self.action(Op::CreateErrorNotice, &[])?;
                if let Err(capture_error) = self.action(Op::CaptureErrorScreenshot, &[]) {
                    if self.call(Op::ExceptionKind, &[])? == 5. {
                        return Err(capture_error);
                    }
                    self.action(Op::ClearException, &[])?;
                    self.action(Op::LogScreenshotFailure, &[])?;
                    self.action(Op::SetScreenshotError, &[])?;
                }
                self.action(Op::SaveErrorDiagnostic, &[])?;
                self.action(Op::AssignDiagnosticReport, &[])?;
                self.action(Op::LogDiagnosticReport, &[])?;
                self.action(Op::EmitErrorEvent, &[])?;
                self.action(Op::NotifyError, &[])?;
                self.action(Op::SetExitAfterFalse, &[])?;
            }
        }
        self.disarm()?;
        self.action(Op::PostStatus, &[])?;
        if !self.flag(Op::ResultRecorded, &[])? {
            self.action(Op::RecordSelectedResult, &[])?;
        }
        if self.flag(Op::ExitAfter, &[])? {
            self.action(Op::QueueExit, &[])?;
        }
        self.action(Op::SetWorkerFinished, &[1.])
    }
    pub(super) fn schedule_exit(&self) -> EngineResult {
        if !self.flag(Op::GenerationMatches, &[])? || self.flag(Op::HasPending, &[])? {
            return Ok(());
        }
        self.action(Op::LogWaitingExit, &[])?;
        self.action(Op::StartExitWait, &[])?;
        self.action(Op::StartExitTimer, &[])
    }
}
