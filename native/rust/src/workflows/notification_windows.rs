//! Window selection and GDI capture policy for desktop notifications.
use super::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn notice_window_fail<T>(&self, code: f64) -> Result<T> {
        self.action(Op::NoticeWindowFailure, &[code])?;
        Err(HOST_ERROR)
    }
    pub(super) fn run_notification_windows(
        &self,
        method: Method,
        args: &[f64],
    ) -> Result<[f64; 3]> {
        match method {
            Method::NoticeWindowFindMessenger => {
                self.action(Op::NoticeWindowEnumerate, &[])?;
                let n = self.scalar(Op::NoticeWindowCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::NoticeWindowCandidate, &[i as f64])?;
                    if !self.flag(Op::NoticeWindowVisible, &[])?
                        || self.flag(Op::NoticeWindowHasParent, &[])?
                    {
                        continue;
                    }
                    if !self.flag(Op::NoticeWindowOpenProcess, &[])? {
                        continue;
                    }
                    let ok = self.flag(Op::NoticeWindowQueryPath, &[])?;
                    self.action(Op::NoticeWindowCloseProcess, &[])?;
                    if !ok {
                        continue;
                    }
                    self.action(Op::NoticeWindowProcessName, &[])?;
                    if !(if args[0] != 0. {
                        self.flag(Op::NoticeWindowWechatName, &[])?
                    } else {
                        self.flag(Op::NoticeWindowQqName, &[])?
                    }) {
                        continue;
                    }
                    self.action(Op::NoticeWindowTitle, &[])?;
                    let exact = if args[0] != 0. {
                        self.flag(Op::NoticeWindowWechatTitle, &[])?
                    } else {
                        self.flag(Op::NoticeWindowQqTitle, &[])?
                    };
                    self.action(Op::NoticeWindowScore, &[f64::from(exact)])?;
                    if self.flag(Op::NoticeWindowBetterScore, &[])? {
                        self.action(Op::NoticeWindowChooseBest, &[])?;
                    }
                }
                if !self.flag(Op::NoticeWindowHasBest, &[])? {
                    return self.notice_window_fail(0.);
                }
            }
            Method::NoticeWindowPopups => {
                self.action(Op::NoticeWindowEnumerate, &[])?;
                let n = self.scalar(Op::NoticeWindowCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::NoticeWindowCandidate, &[i as f64])?;
                    if self.flag(Op::NoticeWindowCandidateIsRoot, &[])?
                        || !self.flag(Op::NoticeWindowVisible, &[])?
                    {
                        continue;
                    }
                    if args[0] != 0. {
                        if !self.flag(Op::NoticeWindowSameProcess, &[])?
                            || !self.flag(Op::NoticeWindowContactClass, &[])?
                        {
                            continue;
                        }
                    } else if (!self.flag(Op::NoticeWindowSameProcess, &[])?
                        && !self.flag(Op::NoticeWindowOwnerIsRoot, &[])?)
                        || self.flag(Op::NoticeWindowAreaTooLarge, &[])?
                    {
                        continue;
                    }
                    if self.flag(Op::NoticeWindowPositiveArea, &[])? {
                        self.action(Op::NoticeWindowAppendPopup, &[])?;
                    }
                }
                self.action(Op::NoticeWindowSortPopups, args)?;
            }
            Method::NoticeWindowPopupCompare => {
                return self.call(
                    if args[0] != 0. {
                        Op::NoticeWindowAreaGreater
                    } else {
                        Op::NoticeWindowAreaLess
                    },
                    &[],
                );
            }
            Method::NoticeWindowRender => {
                self.action(Op::NoticeWindowEnumerateChildren, &[])?;
                if !self.flag(Op::NoticeWindowHasRender, &[])? {
                    self.action(Op::NoticeWindowUseRootRender, &[])?;
                }
            }
            Method::NoticeWindowRenderChild => {
                if self.flag(Op::NoticeWindowChromeClass, &[])? {
                    self.action(Op::NoticeWindowUseChildRender, &[])?;
                    return Ok([0.; 3]);
                }
                return Ok([1., 0., 0.]);
            }
            Method::NoticeWindowCapture => {
                if self.flag(Op::NoticeWindowIconic, &[])? {
                    return self.notice_window_fail(1.);
                }
                if !self.flag(Op::NoticeWindowClientRect, &[])?
                    || self.scalar(Op::NoticeWindowClientWidth, &[])? <= 0.
                    || self.scalar(Op::NoticeWindowClientHeight, &[])? <= 0.
                {
                    return self.notice_window_fail(2.);
                }
                if !self.flag(Op::NoticeWindowResponsive, &[1000.])? {
                    return self.notice_window_fail(3.);
                }
                if !self.flag(Op::NoticeWindowGetDc, &[])? {
                    return self.notice_window_fail(4.);
                }
                self.action(Op::NoticeWindowCompatibleDc, &[])?;
                self.action(Op::NoticeWindowBitmapInfo, &[])?;
                self.action(Op::NoticeWindowCreateBitmap, &[])?;
                if !self.flag(Op::NoticeWindowHasDc, &[])?
                    || !self.flag(Op::NoticeWindowHasBitmap, &[])?
                {
                    if self.flag(Op::NoticeWindowHasBitmap, &[])? {
                        self.action(Op::NoticeWindowDeleteBitmap, &[])?;
                    }
                    if self.flag(Op::NoticeWindowHasDc, &[])? {
                        self.action(Op::NoticeWindowDeleteDc, &[])?;
                    }
                    self.action(Op::NoticeWindowReleaseDc, &[])?;
                    return self.notice_window_fail(5.);
                }
                self.action(Op::NoticeWindowSelectBitmap, &[])?;
                let ok = self.flag(Op::NoticeWindowPrint, &[3.])?;
                if ok {
                    self.action(Op::NoticeWindowConvertImage, &[])?;
                }
                self.action(Op::NoticeWindowRestoreBitmap, &[])?;
                self.action(Op::NoticeWindowDeleteBitmap, &[])?;
                self.action(Op::NoticeWindowDeleteDc, &[])?;
                self.action(Op::NoticeWindowReleaseDc, &[])?;
                if !ok || self.flag(Op::NoticeWindowFrameEmpty, &[])? {
                    return self.notice_window_fail(6.);
                }
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
