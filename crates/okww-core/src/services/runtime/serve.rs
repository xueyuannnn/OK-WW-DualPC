//! Local IPC stream framing and reply handling, including original nested break behavior.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn backend_parse_line(&self) -> Result {
        let result: Result = (|| {
            self.action(Op::BackendServeParseDocument, &[])?;
            if !self.flag(Op::BackendServeParseValid, &[])?
                || !self.flag(Op::BackendServeDocumentObject, &[])?
            {
                self.action(Op::BackendServeInvalidMessage, &[])?;
                return Err(HOST_ERROR);
            }
            self.action(Op::BackendServeSuccessReply, &[])?;
            Ok(())
        })();
        if result.is_err() {
            if self.scalar(Op::ExceptionKind, &[])? != 6. {
                return Err(HOST_ERROR);
            }
            self.action(Op::ClearException, &[])?;
            self.action(Op::BackendServeDropDocument, &[])?;
            self.action(Op::BackendServeErrorReply, &[])?;
            self.action(Op::ClearException, &[])?;
        } else {
            self.action(Op::BackendServeDropDocument, &[])?;
        }
        Ok(())
    }
    pub(in crate::services) fn run_backend_serve(&self, method: Method) -> Result<[f64; 3]> {
        if matches!(method, Method::BackendServeParseLine) {
            self.backend_parse_line()?;
            return Ok([0.; 3]);
        }
        if !matches!(method, Method::BackendServe) {
            return Err(INVALID_ACTION);
        }
        while !self.flag(Op::BackendServeExit, &[])? {
            if !self.flag(Op::BackendServeReadable, &[])? {
                continue;
            }
            self.action(Op::BackendServeAccept, &[])?;
            if !self.flag(Op::BackendServeClientValid, &[])? {
                continue;
            }
            self.action(Op::BackendServeReceiveTimeout, &[])?;
            self.action(Op::BackendServeSendTimeout, &[])?;
            self.action(Op::BackendServeNoDelay, &[])?;
            while !self.flag(Op::BackendServeExit, &[])? {
                let received = self.scalar(Op::BackendServeReceive, &[])? as i32;
                if received == 0 {
                    break;
                }
                if received < 0 {
                    if self.flag(Op::BackendServeTimedOut, &[])? {
                        continue;
                    }
                    break;
                }
                self.action(Op::BackendServeAppendBuffer, &[])?;
                if self.scalar(Op::BackendServeBufferSize, &[])? > 65536. {
                    break;
                }
                loop {
                    let newline = self.scalar(Op::BackendServeNewline, &[])?;
                    if newline < 0. {
                        break;
                    }
                    self.action(Op::BackendServeReadLine, &[newline])?;
                    self.action(Op::BackendServeRemoveLine, &[newline])?;
                    self.backend_parse_line()?;
                    self.action(Op::BackendServeEncodeReply, &[])?;
                    self.action(Op::BackendServeResetSent, &[])?;
                    while self.flag(Op::BackendServeHasUnsent, &[])? {
                        let count = self.scalar(Op::BackendServeSend, &[])?;
                        if count <= 0. {
                            break;
                        }
                        self.action(Op::BackendServeAdvanceSent, &[count])?;
                    }
                    if !self.flag(Op::BackendServeSendComplete, &[])? {
                        self.action(Op::BackendServeShutdownClient, &[])?;
                        self.action(Op::BackendServeDropLineScope, &[])?;
                        // Only the newline loop ends here. The original next
                        // recv observes the shut-down connection.
                        break;
                    }
                    self.action(Op::BackendServeDropLineScope, &[])?;
                }
            }
            self.action(Op::BackendServeCloseClient, &[])?;
            if self.action(Op::BackendServeDisarm, &[]).is_err() {
                self.action(Op::ClearException, &[])?;
                self.action(Op::ClearException, &[])?;
            }
            self.action(Op::BackendServeDropBuffer, &[])?;
        }
        Ok([0.; 3])
    }
}
