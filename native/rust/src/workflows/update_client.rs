//! Qt network callbacks retain transport objects; sequencing and cleanup live here.
use super::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn update_client_throw<T>(&self, code: f64) -> Result<T> {
        self.action(Op::UpdateClientThrow, &[code])?;
        Err(HOST_ERROR)
    }
    pub(super) fn run_update_client(&self, method: Method, args: &[f64]) -> Result<[f64; 3]> {
        match method {
            Method::UpdateClientInitialize => {
                self.action(Op::UpdateClientSingleShot, &[])?;
                self.action(Op::UpdateClientBindTimeout, &[])?;
            }
            Method::UpdateClientFail => {
                self.action(Op::UpdateClientCancelNested, &[])?;
                self.action(Op::UpdateClientEmitFailure, &[])?;
            }
            Method::UpdateClientCancel => {
                self.action(Op::UpdateClientSetBusy, &[0.])?;
                self.action(Op::UpdateClientStopDeadline, &[])?;
                if self.flag(Op::UpdateClientReplyPresent, &[])? {
                    self.action(Op::UpdateClientSaveReply, &[])?;
                    self.action(Op::UpdateClientClearReply, &[])?;
                    self.action(Op::UpdateClientDisconnectReply, &[])?;
                    self.action(Op::UpdateClientAbortReply, &[])?;
                    self.action(Op::UpdateClientDeleteReplyLater, &[])?;
                }
                if self.flag(Op::UpdateClientOutputPresent, &[])? {
                    self.action(Op::UpdateClientCancelOutput, &[])?;
                    self.action(Op::UpdateClientDeleteOutput, &[])?;
                    self.action(Op::UpdateClientResetOutput, &[])?;
                }
            }
            Method::UpdateClientGet => {
                self.action(Op::UpdateClientClearBuffer, &[])?;
                self.action(Op::UpdateClientResetReceived, &[])?;
                self.action(Op::UpdateClientCreateRequest, &[])?;
                self.action(Op::UpdateClientUserAgent, &[])?;
                self.action(Op::UpdateClientAcceptHeader, &[])?;
                self.action(Op::UpdateClientRedirectPolicy, &[])?;
                self.action(Op::UpdateClientTransferTimeout, &[30000.])?;
                if self.flag(Op::UpdateClientHasTransport, &[])? {
                    self.action(Op::UpdateClientStartTransport, &[])?;
                } else {
                    self.action(Op::UpdateClientStartNetwork, &[])?;
                }
                self.action(Op::UpdateClientReadBufferLimit, &[1024. * 1024.])?;
                self.action(
                    Op::UpdateClientStartDeadline,
                    &[if args[1] != 0. {
                        30. * 60. * 1000.
                    } else {
                        30000.
                    }],
                )?;
                self.action(Op::UpdateClientBindReadyRead, args)?;
                self.action(Op::UpdateClientBindFinished, &[])?;
            }
            Method::UpdateClientReadyRead => {
                self.action(Op::UpdateClientReadChunk, &[])?;
                self.action(Op::UpdateClientAccumulateReceived, &[])?;
                if self.scalar(Op::UpdateClientReceived, &[])? > args[0] {
                    self.action(Op::UpdateClientFailLargeFile, &[])?;
                    return Ok([0.; 3]);
                }
                if args[1] != 0. {
                    if !self.flag(Op::UpdateClientOutputPresent, &[])?
                        || !self.flag(Op::UpdateClientWriteChunk, &[])?
                    {
                        self.action(Op::UpdateClientFailWrite, &[])?;
                        return Ok([0.; 3]);
                    }
                    self.action(Op::UpdateClientHashChunk, &[])?;
                    self.action(Op::UpdateClientEmitProgress, &[])?;
                } else {
                    self.action(Op::UpdateClientAppendChunk, &[])?;
                }
            }
            Method::UpdateClientFinished => {
                self.action(Op::UpdateClientSaveReply, &[])?;
                if !self.flag(Op::UpdateClientSavedReplyPresent, &[])? {
                    return Ok([0.; 3]);
                }
                self.action(Op::UpdateClientReadHttpStatus, &[])?;
                self.action(Op::UpdateClientReadNetworkError, &[])?;
                self.action(Op::UpdateClientReadNetworkMessage, &[])?;
                self.action(Op::UpdateClientReadFinalUrl, &[])?;
                self.action(Op::UpdateClientClearReply, &[])?;
                self.action(Op::UpdateClientDeleteReplyLater, &[])?;
                self.action(Op::UpdateClientStopDeadline, &[])?;
                let status = self.scalar(Op::UpdateClientHttpStatus, &[])?;
                if status != 200. || self.scalar(Op::UpdateClientNetworkError, &[])? != 0. {
                    if status == 403. || status == 429. {
                        self.action(Op::UpdateClientFailRateLimit, &[])?;
                    } else {
                        self.action(Op::UpdateClientFailNetwork, &[])?;
                    }
                    return Ok([0.; 3]);
                }
                if !self.flag(Op::UpdateClientFinalApiHost, &[])?
                    && !self.flag(Op::UpdateClientFinalTrusted, &[])?
                {
                    self.action(Op::UpdateClientFailRedirect, &[])?;
                    return Ok([0.; 3]);
                }
                if let Err(error) = self.action(Op::UpdateClientInvokeDone, &[]) {
                    if self.scalar(Op::ExceptionKind, &[])? == 7. {
                        return Err(error);
                    }
                    self.action(Op::ClearException, &[])?;
                    self.action(Op::UpdateClientFailDoneException, &[])?;
                }
            }
            Method::UpdateClientCheck => {
                if self.flag(Op::UpdateClientBusy, &[])? {
                    return Ok([0.; 3]);
                }
                self.action(Op::UpdateClientSetBusy, &[1.])?;
                self.action(Op::UpdateClientAssignCurrent, &[])?;
                self.action(Op::UpdateClientAssignPreviews, args)?;
                self.action(Op::UpdateClientClearReleases, &[])?;
                self.action(Op::UpdateClientFirstPage, &[])?;
                self.action(Op::UpdateClientGetPageNested, &[])?;
            }
            Method::UpdateClientGetPage => self.action(Op::UpdateClientRequestPage, &[])?,
            Method::UpdateClientPageReply => {
                self.action(Op::UpdateClientParseReleasePage, &[])?;
                if !self.flag(Op::UpdateClientPageParseOk, &[])?
                    || !self.flag(Op::UpdateClientPageArray, &[])?
                {
                    return self.update_client_throw(0.);
                }
                self.action(Op::UpdateClientReadPageEntries, &[])?;
                let n = self.scalar(Op::UpdateClientPageEntryCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::UpdateClientAppendPageEntry, &[i as f64])?;
                }
                if n == 100 && self.scalar(Op::UpdateClientPageNumber, &[])? < 10. {
                    self.action(Op::UpdateClientNextPage, &[])?;
                    self.action(Op::UpdateClientGetPageNested, &[])?;
                    return Ok([0.; 3]);
                }
                self.action(Op::UpdateClientFilterReleases, &[])?;
                self.action(Op::UpdateClientSetBusy, &[0.])?;
                self.action(Op::UpdateClientEmitChecked, &[])?;
            }
            Method::UpdateClientDownload => {
                if self.flag(Op::UpdateClientBusy, &[])? {
                    return Ok([0.; 3]);
                }
                if !self.flag(Op::UpdateClientReleaseInstallable, &[])?
                    || !self.flag(Op::UpdateClientReleasePackageTrusted, &[])?
                    || !self.flag(Op::UpdateClientReleaseChecksumsTrusted, &[])?
                    || self.scalar(Op::UpdateClientReleaseSize, &[])? > 4. * 1024. * 1024. * 1024.
                {
                    self.action(Op::UpdateClientEmitUnavailable, &[])?;
                    return Ok([0.; 3]);
                }
                self.action(Op::UpdateClientAssignRelease, &[])?;
                self.action(Op::UpdateClientAssignJob, &[])?;
                self.action(Op::UpdateClientSetBusy, &[1.])?;
                self.action(Op::UpdateClientRequestChecksums, &[])?;
            }
            Method::UpdateClientChecksumReply => {
                self.action(Op::UpdateClientReadExpectedChecksum, &[])?;
                if !self.flag(Op::UpdateClientSelectedDigestEmpty, &[])?
                    && !self.flag(Op::UpdateClientDigestMatches, &[])?
                {
                    return self.update_client_throw(1.);
                }
                self.action(Op::UpdateClientCreateOutput, &[])?;
                if !self.flag(Op::UpdateClientOpenOutput, &[])? {
                    return self.update_client_throw(2.);
                }
                self.action(Op::UpdateClientResetHash, &[])?;
                self.action(Op::UpdateClientRequestPackage, &[])?;
            }
            Method::UpdateClientPackageReply => {
                if !self.flag(Op::UpdateClientReceivedSizeMatches, &[])?
                    || !self.flag(Op::UpdateClientPackageHashMatches, &[])?
                {
                    return self.update_client_throw(3.);
                }
                if !self.flag(Op::UpdateClientCommitOutput, &[])? {
                    return self.update_client_throw(4.);
                }
                self.action(Op::UpdateClientDeleteOutput, &[])?;
                self.action(Op::UpdateClientResetOutput, &[])?;
                self.action(Op::UpdateClientSetBusy, &[0.])?;
                self.action(Op::UpdateClientEmitDownloaded, &[])?;
            }
            Method::UpdateClientTimeout => self.action(Op::UpdateClientFailTimeout, &[])?,
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
