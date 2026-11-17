//! Desktop notification capture bounds and unique contact selection.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn desktop_notice_fail<T>(&self, code: f64) -> Result<T> {
        self.action(Op::NoticeDesktopFailure, &[code])?;
        Err(HOST_ERROR)
    }
    fn desktop_notice_checkpoint(&self) -> Result {
        if self.flag(Op::NoticeDesktopHasCancellation, &[])?
            && self.flag(Op::NoticeDesktopCancelled, &[])?
        {
            return self.desktop_notice_fail(1.);
        }
        Ok(())
    }
    pub(in crate::services) fn run_notification_desktop(
        &self,
        method: Method,
        args: &[f64],
    ) -> Result<[f64; 3]> {
        match method {
            Method::NoticeDesktopBoundedCapture => {
                if !self.flag(Op::NoticeDesktopHasCapture, &[])? || args[0] < 1. || args[0] > 30000.
                {
                    return self.desktop_notice_fail(0.);
                }
                self.desktop_notice_checkpoint()?;
                if self.flag(Op::NoticeDesktopClaimCapture, &[])? {
                    return self.desktop_notice_fail(2.);
                }
                self.action(Op::NoticeDesktopCreatePromise, &[])?;
                self.action(Op::NoticeDesktopGetFuture, &[])?;
                if let Err(error) = self.action(Op::NoticeDesktopSpawnCapture, &[]) {
                    self.action(Op::ClearException, &[])?;
                    self.action(Op::NoticeDesktopReleaseCapture, &[])?;
                    self.action(Op::RethrowHandled, &[])?;
                    return Err(error);
                }
                self.action(Op::NoticeDesktopTimerStart, &[])?;
                loop {
                    self.desktop_notice_checkpoint()?;
                    if self.flag(Op::NoticeDesktopFutureReady, &[20.])? {
                        self.action(Op::NoticeDesktopFutureGet, &[])?;
                        break;
                    }
                    if self.scalar(Op::NoticeDesktopTimerElapsed, &[])? >= args[0] {
                        return self.desktop_notice_fail(3.);
                    }
                }
            }
            Method::NoticeDesktopCaptureWorker => {
                if self.action(Op::NoticeDesktopExecuteCapture, &[]).is_err() {
                    self.action(Op::NoticeDesktopRememberException, &[])?;
                    self.action(Op::ClearException, &[])?;
                }
                self.action(Op::NoticeDesktopReleaseCapture, &[])?;
                if self.flag(Op::NoticeDesktopCaptureFailed, &[])? {
                    self.action(Op::NoticeDesktopPromiseException, &[])?;
                } else {
                    self.action(Op::NoticeDesktopPromiseValue, &[])?;
                }
            }
            Method::NoticeDesktopContact => {
                self.action(Op::NoticeDesktopFoldNickname, &[])?;
                if self.flag(Op::NoticeDesktopNicknameEmpty, &[])? {
                    return Ok([0.; 3]);
                }
                self.action(Op::NoticeDesktopContactHeadings, &[])?;
                if args[0] == 0. {
                    self.action(Op::NoticeDesktopContactSection, &[0., f64::from(i32::MAX)])?;
                } else {
                    let n = self.scalar(Op::NoticeDesktopLineCount, &[])? as usize;
                    for i in 0..n {
                        if self.flag(Op::NoticeDesktopAllowedHeading, &[i as f64])? {
                            let start = self.scalar(Op::NoticeDesktopLineBottom, &[i as f64])?;
                            let mut end = f64::from(i32::MAX);
                            for j in 0..n {
                                if self.flag(Op::NoticeDesktopAnyHeading, &[j as f64])? {
                                    let y = self.scalar(Op::NoticeDesktopLineY, &[j as f64])?;
                                    if y >= start {
                                        end = end.min(y);
                                    }
                                }
                            }
                            self.action(Op::NoticeDesktopContactSection, &[start, end])?;
                        }
                    }
                }
                let sections = self.scalar(Op::NoticeDesktopSectionCount, &[])? as usize;
                for s in 0..sections {
                    let section = self.call(Op::NoticeDesktopSectionAt, &[s as f64])?;
                    let n = self.scalar(Op::NoticeDesktopLineCount, &[])? as usize;
                    for i in 0..n {
                        self.action(Op::NoticeDesktopLineCenter, &[i as f64])?;
                        if self.flag(Op::NoticeDesktopNicknameMatches, &[i as f64])? {
                            let y = self.scalar(Op::NoticeDesktopCenterY, &[])?;
                            if y >= section[0] && y < section[1] {
                                if self.flag(Op::NoticeDesktopContactFound, &[])?
                                    && !self.flag(Op::NoticeDesktopSameContactPoint, &[])?
                                {
                                    self.action(Op::NoticeDesktopClearContact, &[])?;
                                    return Ok([0.; 3]);
                                }
                                self.action(Op::NoticeDesktopUseContactPoint, &[])?;
                            }
                        }
                    }
                }
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
