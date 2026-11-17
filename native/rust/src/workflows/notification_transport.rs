//! HTTP cancellation and SMTP wire sequencing, preserving accepted-delivery semantics.
use super::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn notice_transport_fail<T>(&self, code: f64, detail: f64) -> Result<T> {
        self.action(Op::NoticeTransportFailure, &[code, detail])?;
        Err(HOST_ERROR)
    }
    pub(super) fn run_notification_transport(
        &self,
        method: Method,
        args: &[f64],
    ) -> Result<[f64; 3]> {
        match method {
            Method::NoticeHttp => {
                self.action(Op::NoticeTransportCheckpoint, &[])?;
                self.action(Op::NoticeHttpCreateManager, &[])?;
                self.action(Op::NoticeHttpCreateRequest, &[])?;
                self.action(Op::NoticeHttpContentType, &[])?;
                self.action(Op::NoticeHttpRedirectPolicy, &[])?;
                self.action(Op::NoticeHttpTransferTimeout, &[30000.])?;
                self.action(Op::NoticeHttpHeaderKeys, &[])?;
                let n = self.scalar(Op::NoticeHttpHeaderCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::NoticeHttpSetHeader, &[i as f64])?;
                }
                self.action(Op::NoticeHttpPost, &[])?;
                self.action(Op::NoticeHttpCreateLoop, &[])?;
                self.action(Op::NoticeHttpCreateTimer, &[])?;
                self.action(Op::NoticeHttpTimerInterval, &[50.])?;
                self.action(Op::NoticeHttpStartElapsed, &[])?;
                self.action(Op::NoticeHttpBindPoll, &[])?;
                self.action(Op::NoticeHttpBindFinished, &[])?;
                self.action(Op::NoticeHttpStartTimer, &[])?;
                if !self.flag(Op::NoticeHttpFinished, &[])? {
                    self.action(Op::NoticeHttpExecLoop, &[])?;
                }
                self.action(Op::NoticeTransportCheckpoint, &[])?;
                let status = self.scalar(Op::NoticeHttpStatus, &[])?;
                if self.scalar(Op::NoticeHttpError, &[])? != 0. || !(200. ..300.).contains(&status)
                {
                    return self.notice_transport_fail(0., status);
                }
                if self.scalar(Op::NoticeHttpAvailable, &[])? > 1024. * 1024. {
                    return self.notice_transport_fail(1., 0.);
                }
                self.action(Op::NoticeHttpReadAll, &[])?;
            }
            Method::NoticeHttpPoll => {
                if (self.flag(Op::NoticeTransportHasCancellation, &[])?
                    && self.flag(Op::NoticeTransportCancelled, &[])?)
                    || self.scalar(Op::NoticeHttpElapsed, &[])? > 30000.
                {
                    self.action(Op::NoticeHttpAbort, &[])?;
                }
            }
            Method::NoticeSmtp => {
                self.action(Op::NoticeSmtpReadHost, &[])?;
                self.action(Op::NoticeSmtpReadSender, &[])?;
                self.action(Op::NoticeSmtpReadRecipient, &[])?;
                let port = self.scalar(Op::NoticeSmtpReadPort, &[])?;
                if !(1. ..=65535.).contains(&port) {
                    return self.notice_transport_fail(2., 0.);
                }
                self.action(Op::NoticeSmtpCreateSocket, &[])?;
                self.action(Op::NoticeSmtpVerifyPeer, &[])?;
                self.action(Op::NoticeSmtpTlsProtocol, &[])?;
                let tls = self.flag(Op::NoticeSmtpUseTls, &[])?;
                if tls && port == 465. {
                    self.action(Op::NoticeSmtpConnectEncrypted, &[])?;
                } else {
                    self.action(Op::NoticeSmtpConnectPlain, &[])?;
                }
                self.action(Op::NoticeSmtpUntilNested, &[0.])?;
                if tls && port == 465. {
                    self.action(Op::NoticeSmtpUntilNested, &[1.])?;
                }
                self.action(Op::NoticeSmtpWelcome, &[])?;
                self.action(Op::NoticeSmtpEhlo, &[])?;
                if tls && port != 465. {
                    self.action(Op::NoticeSmtpStartTlsCommand, &[])?;
                    self.action(Op::NoticeSmtpStartEncryption, &[])?;
                    self.action(Op::NoticeSmtpUntilNested, &[1.])?;
                    self.action(Op::NoticeSmtpEhlo, &[])?;
                }
                self.action(Op::NoticeSmtpReadUser, &[])?;
                if !self.flag(Op::NoticeSmtpUserEmpty, &[])? {
                    self.action(Op::NoticeSmtpAuthLogin, &[])?;
                    self.action(Op::NoticeSmtpAuthUser, &[])?;
                    self.action(Op::NoticeSmtpAuthPassword, &[])?;
                }
                self.action(Op::NoticeSmtpMailFrom, &[])?;
                self.action(Op::NoticeSmtpRecipient, &[])?;
                self.action(Op::NoticeSmtpData, &[])?;
                self.action(Op::NoticeSmtpWriteMime, &[])?;
                self.action(Op::NoticeSmtpAccepted, &[])?;
                self.action(Op::NoticeSmtpWriteQuit, &[])?;
                self.action(Op::NoticeSmtpWaitQuitWritten, &[100.])?;
                self.action(Op::NoticeSmtpDisconnect, &[])?;
            }
            Method::NoticeSmtpUntil => {
                self.action(Op::NoticeSmtpStartElapsed, &[])?;
                while !self.flag(Op::NoticeSmtpWaitPredicate, args)? {
                    self.action(Op::NoticeTransportCheckpoint, &[])?;
                    if self.scalar(Op::NoticeSmtpElapsed, &[])? > 30000. {
                        return self.notice_transport_fail(3., 0.);
                    }
                    self.action(Op::NoticeSmtpWaitPoll, &[args[0], 100.])?;
                    if self.flag(Op::NoticeSmtpDisconnected, &[])? {
                        return self.notice_transport_fail(4., 0.);
                    }
                }
            }
            Method::NoticeSmtpResponse => {
                let (mut code, mut total) = (0i32, 0i32);
                loop {
                    self.action(Op::NoticeSmtpUntilNested, &[2.])?;
                    self.action(Op::NoticeSmtpReadLine, &[8192.])?;
                    let size = self.scalar(Op::NoticeSmtpLineSize, &[])? as i32;
                    total += size;
                    if size < 4 || !self.flag(Op::NoticeSmtpLineEndsLf, &[])? || total > 65536 {
                        return self.notice_transport_fail(5., 0.);
                    }
                    let next = self.call(Op::NoticeSmtpParseCode, &[])?;
                    if next[1] == 0. || (code != 0 && next[0] != f64::from(code)) {
                        return self.notice_transport_fail(6., 0.);
                    }
                    code = next[0] as i32;
                    if !self.flag(Op::NoticeSmtpLineContinues, &[])? {
                        break;
                    }
                }
                let mut expected = false;
                if self.flag(Op::NoticeSmtpLineSpace, &[])? {
                    let n = self.scalar(Op::NoticeSmtpExpectedCount, &[])? as usize;
                    for i in 0..n {
                        if self.scalar(Op::NoticeSmtpExpectedAt, &[i as f64])? == f64::from(code) {
                            expected = true;
                            break;
                        }
                    }
                }
                if !expected {
                    return self.notice_transport_fail(7., f64::from(code));
                }
            }
            Method::NoticeSmtpWrite => {
                self.action(Op::NoticeTransportCheckpoint, &[])?;
                if self.scalar(Op::NoticeSmtpWritePayload, &[])? < 0. {
                    return self.notice_transport_fail(8., 0.);
                }
                self.action(Op::NoticeSmtpUntilNested, &[3.])?;
            }
            Method::NoticeSmtpCommand => {
                self.action(Op::NoticeSmtpWriteCommand, &[])?;
                self.action(Op::NoticeSmtpCommandResponse, &[])?;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
