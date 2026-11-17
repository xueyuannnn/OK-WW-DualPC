//! Messenger OCR and background-input sequencing; window handles never enter Rust.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn messenger_fail<T>(&self, code: f64) -> Result<T> {
        self.action(Op::NoticeMessengerFailure, &[code])?;
        Err(HOST_ERROR)
    }
    pub(in crate::services) fn run_notification_messenger(
        &self,
        method: Method,
        args: &[f64],
    ) -> Result<[f64; 3]> {
        match method {
            Method::NoticeMessengerCheck => {
                if self.flag(Op::NoticeMessengerHasCancellation, &[])?
                    && self.flag(Op::NoticeMessengerCancelled, &[])?
                {
                    return self.messenger_fail(0.);
                }
                if !self.flag(Op::NoticeMessengerWindowExists, &[])? {
                    return self.messenger_fail(1.);
                }
            }
            Method::NoticeMessengerWait => {
                self.action(Op::NoticeMessengerStartTimer, &[])?;
                while self.scalar(Op::NoticeMessengerElapsed, &[])? < args[0] * 1000. {
                    self.action(Op::NoticeMessengerCheck, &[])?;
                    self.action(Op::NoticeMessengerSleep, &[50.])?;
                }
            }
            Method::NoticeMessengerBackground => {
                self.action(Op::NoticeMessengerStartTimer, &[])?;
                while self.flag(Op::NoticeMessengerForegroundRoot, &[])?
                    || self.flag(Op::NoticeMessengerForegroundProcess, &[])?
                {
                    self.action(Op::NoticeMessengerCheck, &[])?;
                    if self.scalar(Op::NoticeMessengerElapsed, &[])? > 120000. {
                        return self.messenger_fail(2.);
                    }
                    self.action(Op::NoticeMessengerWait, &[0.25])?;
                }
                self.action(Op::NoticeMessengerCheck, &[])?;
            }
            Method::NoticeMessengerMessage => {
                self.action(Op::NoticeMessengerBackground, &[])?;
                if !self.flag(Op::NoticeMessengerSendMessage, &[1000.])? {
                    return self.messenger_fail(3.);
                }
            }
            Method::NoticeMessengerKey => {
                let mut flags = self.scalar(Op::NoticeMessengerKeyScanFlags, &[])? as u32;
                if args[0] == 0. {
                    flags |= (1 << 30) | (1 << 31);
                }
                self.action(Op::NoticeMessengerKeyFlags, &[f64::from(flags)])?;
                self.action(
                    if args[0] != 0. {
                        Op::NoticeMessengerKeyDown
                    } else {
                        Op::NoticeMessengerKeyUp
                    },
                    &[],
                )?;
            }
            Method::NoticeMessengerTap => {
                self.action(Op::NoticeMessengerKey, &[1.])?;
                self.action(Op::NoticeMessengerKey, &[0.])?;
            }
            Method::NoticeMessengerClick => {
                self.action(
                    if args[1] != 0. {
                        Op::NoticeMessengerRenderTarget
                    } else {
                        Op::NoticeMessengerOriginalTarget
                    },
                    &[],
                )?;
                if !self.flag(Op::NoticeMessengerTargetIsOriginal, &[])? {
                    self.action(Op::NoticeMessengerMapPoint, &[])?;
                }
                self.action(Op::NoticeMessengerPointParameter, &[])?;
                self.action(Op::NoticeMessengerMouseMove, &[])?;
                self.action(Op::NoticeMessengerMouseDown, &[args[0]])?;
                self.action(Op::NoticeMessengerMouseUp, &[args[0]])?;
            }
            Method::NoticeMessengerType => {
                let n = self.scalar(Op::NoticeMessengerTextSize, &[])? as usize;
                for i in 0..n {
                    self.action(Op::NoticeMessengerCharacter, &[i as f64])?;
                }
            }
            Method::NoticeMessengerRead => {
                self.action(Op::NoticeMessengerBackground, &[])?;
                self.action(Op::NoticeMessengerCapture, &[])?;
                self.action(Op::NoticeMessengerCheck, &[])?;
                self.action(Op::NoticeMessengerOcr, &[])?;
                self.action(Op::NoticeMessengerCheck, &[])?;
            }
            Method::NoticeMessengerFind => {
                let n = self.scalar(Op::NoticeMessengerNameCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::NoticeMessengerFoldName, &[i as f64])?;
                }
                self.action(Op::NoticeMessengerStartTimer, &[])?;
                loop {
                    self.action(Op::NoticeMessengerRead, &[])?;
                    let lines = self.scalar(Op::NoticeMessengerLineCount, &[])? as usize;
                    for line in 0..lines {
                        self.action(Op::NoticeMessengerFoldLine, &[line as f64])?;
                        for i in 0..n {
                            let operation = if args[2] != 0. {
                                Op::NoticeMessengerTextSuffix
                            } else if args[1] != 0. {
                                Op::NoticeMessengerTextEqual
                            } else {
                                Op::NoticeMessengerTextContains
                            };
                            if self.flag(operation, &[i as f64])? {
                                self.action(Op::NoticeMessengerFoundLine, &[line as f64])?;
                                return Ok([0.; 3]);
                            }
                        }
                    }
                    self.action(Op::NoticeMessengerDropLines, &[])?;
                    self.action(Op::NoticeMessengerWait, &[0.15])?;
                    if self
                        .scalar(Op::NoticeMessengerElapsed, &[])?
                        .partial_cmp(&(args[0] * 1000.))
                        != Some(std::cmp::Ordering::Less)
                    {
                        break;
                    }
                }
            }
            Method::NoticeMessengerConfirmRecipient => {
                if !self.flag(Op::NoticeMessengerFindRecipient, &[2.])? {
                    return self.messenger_fail(4.);
                }
            }
            Method::NoticeMessengerClear => {
                if args[0] != 0. {
                    self.action(Op::NoticeMessengerControlDown, &[])?;
                    if let Err(error) = self.action(Op::NoticeMessengerTapA, &[]) {
                        let _ = self.action(Op::NoticeMessengerControlUp, &[]);
                        return Err(error);
                    }
                    self.action(Op::NoticeMessengerControlUp, &[])?;
                    self.action(Op::NoticeMessengerTapBackspace, &[])?;
                    self.action(Op::NoticeMessengerTapDelete, &[])?;
                } else {
                    self.action(Op::NoticeMessengerTapEnd, &[])?;
                    for _ in 0..256 {
                        self.action(Op::NoticeMessengerTapBackspace, &[])?;
                    }
                    self.action(Op::NoticeMessengerTapHome, &[])?;
                    for _ in 0..256 {
                        self.action(Op::NoticeMessengerTapDelete, &[])?;
                    }
                }
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
