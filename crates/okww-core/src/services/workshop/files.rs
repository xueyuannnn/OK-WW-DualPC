//! Persistent workshop file-manager lifecycle and request identity.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn workshop_file_fail<T>(&self, code: f64) -> Result<T> {
        self.action(Op::WorkshopFileFailure, &[code])?;
        Err(HOST_ERROR)
    }
    pub(in crate::services) fn run_workshop_files(&self, method: Method) -> Result<[f64; 3]> {
        match method {
            Method::WorkshopRoot => {
                if self.flag(Op::WorkshopBundledEngineExists, &[])? {
                    self.action(Op::WorkshopReturnBundledRoot, &[])?;
                } else {
                    self.action(Op::WorkshopReturnOriginalRoot, &[])?;
                }
            }
            Method::WorkshopPython => {
                self.action(Op::WorkshopResolveEngineRoot, &[])?;
                for i in 0..2 {
                    if self.flag(Op::WorkshopPythonPathExists, &[f64::from(i)])? {
                        self.action(Op::WorkshopReturnPythonPath, &[f64::from(i)])?;
                        break;
                    }
                }
            }
            Method::WorkshopEnvironment => {
                for i in 0..3 {
                    self.action(Op::WorkshopInsertEnvironment, &[f64::from(i)])?;
                }
                for i in 0..4 {
                    self.action(Op::WorkshopRemoveEnvironment, &[f64::from(i)])?;
                }
            }
            Method::WorkshopManage => {
                self.action(Op::WorkshopFileResolveRuntime, &[])?;
                if self.flag(Op::WorkshopFilePythonEmpty, &[])? {
                    return self.workshop_file_fail(0.);
                }
                self.action(Op::WorkshopFileSetEnvironment, &[])?;
                self.action(Op::WorkshopFileSetWorkingDirectory, &[])?;
                self.action(Op::WorkshopFileStartManager, &[])?;
                if !self.flag(Op::WorkshopFileWaitStarted, &[10000.])? {
                    return self.workshop_file_fail(1.);
                }
                self.action(Op::WorkshopFileWriteManagerRequest, &[])?;
                self.action(Op::WorkshopFileCloseWriteChannel, &[])?;
                let timeout = if self.flag(Op::WorkshopFileLibraryRequest, &[])? {
                    180000.
                } else {
                    60000.
                };
                if !self.flag(Op::WorkshopFileWaitFinished, &[timeout])? {
                    self.action(Op::WorkshopFileKillProcess, &[])?;
                    self.action(Op::WorkshopFileWaitFinishedIgnored, &[3000.])?;
                    return self.workshop_file_fail(2.);
                }
                self.action(Op::WorkshopFileReadResponse, &[])?;
                if self.scalar(Op::WorkshopFileExitCode, &[])? != 0.
                    || !self.flag(Op::WorkshopFileReplyOk, &[])?
                {
                    return self.workshop_file_fail(3.);
                }
                self.action(Op::WorkshopFileReturnData, &[])?;
            }
            Method::WorkshopFileStop => {
                if self.flag(Op::WorkshopFileHasProcess, &[])?
                    && self.flag(Op::WorkshopFileProcessRunning, &[])?
                {
                    self.action(Op::WorkshopFileKillProcess, &[])?;
                    self.action(Op::WorkshopFileWaitFinishedIgnored, &[3000.])?;
                }
                self.action(Op::WorkshopFileResetPid, &[])?;
                self.action(Op::WorkshopFileClearBuffer, &[])?;
                self.action(Op::WorkshopFileClearErrors, &[])?;
            }
            Method::WorkshopFileExchange => {
                if !self.flag(Op::WorkshopFileHasProcess, &[])? {
                    self.action(Op::WorkshopFileCreateProcess, &[])?;
                }
                if !self.flag(Op::WorkshopFileProcessRunning, &[])? {
                    self.action(Op::WorkshopFileClearBuffer, &[])?;
                    self.action(Op::WorkshopFileClearErrors, &[])?;
                    self.action(Op::WorkshopFileResolveRuntime, &[])?;
                    if self.flag(Op::WorkshopFilePythonEmpty, &[])? {
                        return self.workshop_file_fail(0.);
                    }
                    self.action(Op::WorkshopFileSetEnvironment, &[])?;
                    self.action(Op::WorkshopFileSetWorkingDirectory, &[])?;
                    self.action(Op::WorkshopFileStartPersistentManager, &[])?;
                    if !self.flag(Op::WorkshopFileWaitStarted, &[10000.])? {
                        return self.workshop_file_fail(1.);
                    }
                    self.action(Op::WorkshopFileRememberPid, &[])?;
                }
                self.action(Op::WorkshopFileAssignRequestIdentity, &[])?;
                self.action(Op::WorkshopFileEncodePayload, &[])?;
                if !self.flag(Op::WorkshopFileWritePayload, &[])? {
                    return self.workshop_file_fail(4.);
                }
                self.action(Op::WorkshopFileWaitWritten, &[1000.])?;
                self.action(Op::WorkshopFileStartTimer, &[])?;
                while self.scalar(Op::WorkshopFileElapsed, &[])? < 60000. {
                    self.action(Op::WorkshopFileReadStderr, &[])?;
                    self.action(Op::WorkshopFileTrimStderr, &[65536.])?;
                    self.action(Op::WorkshopFileReadStdout, &[])?;
                    if self.scalar(Op::WorkshopFileBufferSize, &[])? > 16. * 1024. * 1024. {
                        return self.workshop_file_fail(5.);
                    }
                    if self.scalar(Op::WorkshopFileNewline, &[])? >= 0. {
                        self.action(Op::WorkshopFileParseReply, &[])?;
                        self.action(Op::WorkshopFileConsumeLine, &[])?;
                        if !self.flag(Op::WorkshopFileReplyIdentityMatches, &[])?
                            || self.scalar(Op::WorkshopFileManagerProtocol, &[])? != 1.
                        {
                            return self.workshop_file_fail(6.);
                        }
                        if !self.flag(Op::WorkshopFileReplyOk, &[])? {
                            return self.workshop_file_fail(7.);
                        }
                        self.action(Op::WorkshopFileReturnData, &[])?;
                        return Ok([0.; 3]);
                    }
                    if !self.flag(Op::WorkshopFileProcessRunning, &[])? {
                        return self.workshop_file_fail(8.);
                    }
                    self.action(Op::WorkshopFileWaitReadyRead, &[15.])?;
                }
                return self.workshop_file_fail(2.);
            }
            Method::WorkshopFileCommand => {
                if let Err(error) = self.action(Op::WorkshopFileExchangeNested, &[]) {
                    self.action(Op::WorkshopFileStopNested, &[])?;
                    return Err(error);
                }
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
