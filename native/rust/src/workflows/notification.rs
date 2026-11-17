//! Notification payload, UTF-16 chunking and response validation policies.
use super::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn notice_fail<T>(&self, code: f64) -> Result<T> {
        self.action(Op::NoticeFailure, &[code])?;
        Err(HOST_ERROR)
    }
    pub(super) fn run_notification(&self, method: Method, args: &[f64]) -> Result<[f64; 3]> {
        match method {
            Method::NoticeRequired => {
                self.action(Op::NoticeTrimConfig, &[])?;
                if self.flag(Op::NoticeConfigEmpty, &[])? {
                    return self.notice_fail(0.);
                }
                for i in 0..3 {
                    if self.flag(Op::NoticeConfigControl, &[f64::from(i)])? {
                        return self.notice_fail(1.);
                    }
                }
            }
            Method::NoticeContent => {
                if self.flag(Op::NoticeTitleEmpty, &[])? {
                    self.action(Op::NoticeReturnText, &[])?;
                } else {
                    self.action(Op::NoticeReturnTitleAndText, &[])?;
                }
            }
            Method::NoticeChunks => {
                while !self.flag(Op::NoticeChunkTextEmpty, &[])? {
                    let size = self.scalar(Op::NoticeChunkTextSize, &[])?;
                    let mut count = args[0].min(size);
                    if count < size && self.flag(Op::NoticeChunkEndsHighSurrogate, &[count])? {
                        count -= 1.;
                    }
                    self.action(Op::NoticeAppendChunk, &[count])?;
                    self.action(Op::NoticeConsumeChunk, &[count])?;
                }
            }
            Method::NoticeWebhook => {
                self.action(Op::NoticeParseWebhook, &[])?;
                if !self.flag(Op::NoticeWebhookValid, &[])?
                    || !self.flag(Op::NoticeWebhookHttps, &[])?
                    || self.flag(Op::NoticeWebhookHostEmpty, &[])?
                    || !self.flag(Op::NoticeWebhookUserEmpty, &[])?
                {
                    return self.notice_fail(2.);
                }
            }
            Method::NoticeMultipart => {
                self.action(Op::NoticeNewBoundary, &[])?;
                self.action(Op::NoticeLoadFieldKeys, &[])?;
                let n = self.scalar(Op::NoticeFieldCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::NoticeAppendFieldPart, &[i as f64])?;
                }
                let n = self.scalar(Op::NoticeFileCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::NoticeAppendFilePart, &[i as f64, (i + 1) as f64])?;
                }
                self.action(Op::NoticeCloseMultipart, &[])?;
                self.action(Op::NoticeReturnMultipart, &[])?;
            }
            Method::NoticeWrappedBase64 => {
                self.action(Op::NoticeEncodeBase64, &[])?;
                let n = self.scalar(Op::NoticeEncodedSize, &[])? as usize;
                for at in (0..n).step_by(76) {
                    self.action(Op::NoticeAppendBase64Line, &[at as f64, 76.])?;
                }
            }
            Method::NoticeAddress => {
                if !self.flag(Op::NoticeMailboxValid, &[])? {
                    return self.notice_fail(3.);
                }
            }
            Method::NoticeCheckpoint => {
                if self.flag(Op::NoticeHasCancellation, &[])?
                    && self.flag(Op::NoticeCancelled, &[])?
                {
                    return self.notice_fail(4.);
                }
            }
            Method::NoticeRequests => {
                let kind = self.scalar(Op::NoticeProviderKind, &[])? as i32;
                match kind {
                    0 => {
                        self.action(Op::NoticeDiscordWebhook, &[])?;
                        self.action(Op::NoticeContentChunks, &[1900.])?;
                        if self.scalar(Op::NoticePartCount, &[])? == 0. {
                            self.action(Op::NoticeAppendEmptyPart, &[])?;
                        }
                        let n = self.scalar(Op::NoticePartCount, &[])? as usize;
                        for i in 0..n {
                            self.action(Op::NoticeDiscordPayload, &[i as f64])?;
                            self.action(Op::NoticeClearFiles, &[])?;
                            if i == 0 {
                                let n = self.scalar(Op::NoticeImageCount, &[])? as usize;
                                for j in 0..n {
                                    self.action(Op::NoticeAppendDiscordImage, &[j as f64])?;
                                }
                            }
                            self.action(Op::NoticeAppendDiscordRequest, &[])?;
                        }
                    }
                    1 => {
                        self.action(Op::NoticeTelegramToken, &[])?;
                        self.action(Op::NoticeTelegramChat, &[])?;
                        if !self.flag(Op::NoticeTelegramTokenValid, &[])? {
                            return self.notice_fail(5.);
                        }
                        self.action(Op::NoticeTelegramBase, &[])?;
                        self.action(Op::NoticeContentChunks, &[4000.])?;
                        let n = self.scalar(Op::NoticePartCount, &[])? as usize;
                        for i in 0..n {
                            self.action(Op::NoticeAppendTelegramText, &[i as f64])?;
                        }
                        let n = self.scalar(Op::NoticeImageCount, &[])? as usize;
                        for i in 0..n {
                            self.action(Op::NoticeAppendTelegramImage, &[i as f64])?;
                        }
                    }
                    2 => {
                        self.action(Op::NoticeWecomWebhook, &[])?;
                        self.action(Op::NoticeContentChunks, &[950.])?;
                        let n = self.scalar(Op::NoticePartCount, &[])? as usize;
                        for i in 0..n {
                            self.action(Op::NoticeAppendWecomText, &[i as f64])?;
                        }
                        let n = self.scalar(Op::NoticeImageCount, &[])? as usize;
                        for i in 0..n {
                            if self.scalar(Op::NoticeImageSize, &[i as f64])? > 2. * 1024. * 1024. {
                                return self.notice_fail(6.);
                            }
                            self.action(Op::NoticeAppendWecomImage, &[i as f64])?;
                        }
                    }
                    3 => {
                        for i in 0..3 {
                            self.action(Op::NoticeQqRequired, &[f64::from(i)])?;
                        }
                        if !self.flag(Op::NoticeQqAppNumeric, &[])?
                            || !self.flag(Op::NoticeQqChannelNumeric, &[])?
                        {
                            return self.notice_fail(7.);
                        }
                        self.action(Op::NoticeQqContent, &[])?;
                        if self.scalar(Op::NoticeImageCount, &[])? > 0. {
                            self.action(Op::NoticeQqImageSummary, &[])?;
                        }
                        self.action(Op::NoticeQqChunks, &[1800.])?;
                        let n = self.scalar(Op::NoticePartCount, &[])? as usize;
                        for i in 0..n {
                            self.action(Op::NoticeCreateQqRequest, &[i as f64])?;
                            self.action(Op::NoticeQqAuthorization, &[])?;
                            self.action(Op::NoticeAppendQqRequest, &[])?;
                        }
                    }
                    _ => return self.notice_fail(8.),
                }
            }
            Method::NoticeValidateResponse => {
                let kind = self.scalar(Op::NoticeResponseProvider, &[])? as i32;
                if kind == 0 || kind == 1 {
                    return Ok([0.; 3]);
                }
                self.action(Op::NoticeParseResponse, &[])?;
                if !self.flag(Op::NoticeResponseParseOk, &[])?
                    || !self.flag(Op::NoticeResponseObject, &[])?
                {
                    return self.notice_fail(9.);
                }
                self.action(Op::NoticeResponseFields, &[])?;
                if kind == 2 && !self.flag(Op::NoticeResponseOk, &[])? {
                    return self.notice_fail(10.);
                }
                if kind == 3
                    && (!self.flag(Op::NoticeResponseCodeNumber, &[])?
                        || self.scalar(Op::NoticeResponseCode, &[])? != 0.)
                {
                    return self.notice_fail(11.);
                }
            }
            Method::NoticeMime => {
                if !self.flag(Op::NoticeBoundaryValid, &[])? {
                    return self.notice_fail(12.);
                }
                self.action(Op::NoticeMimeSender, &[])?;
                self.action(Op::NoticeMimeRecipient, &[])?;
                self.action(Op::NoticeMimeHeader, &[])?;
                if self.flag(Op::NoticeTitleEmpty, &[])? {
                    self.action(Op::NoticeDefaultSubject, &[])?;
                } else {
                    self.action(Op::NoticeUseSubject, &[])?;
                }
                self.action(Op::NoticeSubjectChunks, &[12.])?;
                let n = self.scalar(Op::NoticePartCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::NoticeAppendSubjectWord, &[i as f64])?;
                }
                self.action(Op::NoticeMimeTypeHeader, &[])?;
                self.action(Op::NoticeMimePlainPart, &[])?;
                let n = self.scalar(Op::NoticeImageCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::NoticeMimeImagePart, &[i as f64, (i + 1) as f64])?;
                }
                self.action(Op::NoticeMimeClosingBoundary, &[])?;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
