//! Desktop recipient search, geometry and message/image delivery.
use super::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
fn int_cast(value: f64) -> i32 {
    let value = value.trunc();
    if value.is_finite() && value >= f64::from(i32::MIN) && value <= f64::from(i32::MAX) {
        value as i32
    } else {
        i32::MIN
    }
}
impl Host {
    fn notice_delivery_fail<T>(&self, code: f64) -> Result<T> {
        self.action(Op::NoticeDeliveryFailure, &[code])?;
        Err(HOST_ERROR)
    }
    pub(super) fn run_notification_delivery(
        &self,
        method: Method,
        args: &[f64],
    ) -> Result<[f64; 3]> {
        match method {
            Method::NoticeDeliveryImage => {
                self.action(Op::NoticeDeliveryConfirm, &[])?;
                self.action(Op::NoticeDeliveryClipboard, &[])?;
                self.action(Op::NoticeDeliveryComposerSurface, &[0.])?;
                self.action(Op::NoticeDeliveryWait, &[1.])?;
                self.action(Op::NoticeDeliveryComposerSurface, &[1.])?;
                self.action(Op::NoticeDeliveryWait, &[1.])?;
                self.action(Op::NoticeDeliveryTimerStart, &[])?;
                loop {
                    self.action(Op::NoticeDeliveryPopupCandidates, &[0.])?;
                    self.action(Op::NoticeDeliveryRenderSurface, &[])?;
                    if !self.flag(Op::NoticeDeliveryRenderIsRoot, &[])? {
                        self.action(Op::NoticeDeliveryPrependRender, &[])?;
                    }
                    self.action(Op::NoticeDeliveryAppendRoot, &[])?;
                    let n = self.scalar(Op::NoticeDeliveryCandidateCount, &[])? as usize;
                    for i in 0..n {
                        self.action(Op::NoticeDeliveryCandidate, &[i as f64])?;
                        if self.flag(Op::NoticeDeliveryFindPaste, &[])? {
                            self.action(Op::NoticeDeliveryUseMenu, &[])?;
                            break;
                        }
                    }
                    if self.flag(Op::NoticeDeliveryHasPaste, &[])? {
                        self.action(Op::NoticeDeliveryDropCandidates, &[])?;
                        break;
                    }
                    self.action(Op::NoticeDeliveryWait, &[0.15])?;
                    self.action(Op::NoticeDeliveryDropCandidates, &[])?;
                    if self.scalar(Op::NoticeDeliveryElapsed, &[])? >= 8000. {
                        break;
                    }
                }
                if !self.flag(Op::NoticeDeliveryHasPaste, &[])? {
                    return self.notice_delivery_fail(0.);
                }
                self.action(Op::NoticeDeliveryClickPaste, &[])?;
                self.action(Op::NoticeDeliveryWait, &[1.])?;
                self.action(Op::NoticeDeliveryConfirm, &[])?;
                self.action(Op::NoticeDeliveryClickSend, &[])?;
                self.action(Op::NoticeDeliveryWait, &[1.])?;
            }
            Method::NoticeDeliveryInitialize => {
                let size = self.call(Op::NoticeDeliveryClientSize, &[])?;
                self.action(Op::NoticeDeliveryQueryDpi, &[])?;
                let mut dpi = self.scalar(Op::NoticeDeliveryDpi, &[])?;
                if dpi <= 0. {
                    dpi = 1.;
                    self.action(Op::NoticeDeliverySetDpi, &[dpi])?;
                }
                let panel = int_cast(if args[0] != 0. { 295. } else { 377. } * dpi);
                let left = int_cast(55. * dpi);
                let height = int_cast(120. * dpi);
                let top = int_cast(75. * dpi);
                let w = size[0] as i32;
                let h = size[1] as i32;
                self.action(
                    Op::NoticeDeliverySetSearchRect,
                    &[
                        f64::from(left),
                        0.,
                        f64::from(panel.wrapping_sub(left).max(1)),
                        f64::from(height),
                    ],
                )?;
                self.action(
                    Op::NoticeDeliverySetContactsRect,
                    &[
                        f64::from(left),
                        f64::from(top),
                        f64::from(panel.wrapping_sub(left).max(1)),
                        f64::from(h.wrapping_sub(top).max(1)),
                    ],
                )?;
                self.action(
                    Op::NoticeDeliverySetHeaderRect,
                    &[
                        f64::from(panel),
                        0.,
                        f64::from(w.wrapping_sub(panel).max(1)),
                        f64::from(height),
                    ],
                )?;
                let send_left = panel.max(w.wrapping_sub(int_cast(260. * dpi)));
                self.action(
                    Op::NoticeDeliverySetSendRect,
                    &[
                        f64::from(send_left),
                        f64::from(h.wrapping_sub(int_cast(150. * dpi)).max(0)),
                        f64::from(w.wrapping_sub(send_left).max(1)),
                        f64::from(int_cast(150. * dpi)),
                    ],
                )?;
            }
            Method::NoticeDeliverySend => {
                self.action(Op::NoticeDeliveryBackground, &[])?;
                self.action(Op::NoticeDeliveryWait, &[5.])?;
                if self.flag(Op::NoticeDeliveryInitialContact, &[])? {
                    self.action(Op::NoticeDeliveryClickInitialContact, &[])?;
                } else if !(self.flag(Op::NoticeDeliveryFindReadySend, &[0.6])?
                    && self.flag(Op::NoticeDeliveryFindReadyHeader, &[0.6])?)
                {
                    self.action(Op::NoticeDeliveryFindSearch, &[1.5])?;
                    self.action(Op::NoticeDeliveryClickSearch, &[])?;
                    self.action(Op::NoticeDeliveryWait, &[1.])?;
                    self.action(Op::NoticeDeliveryClear, &[1.])?;
                    self.action(Op::NoticeDeliveryWait, &[1.])?;
                    self.action(
                        if self.flag(Op::NoticeDeliveryIsWechat, &[])? {
                            Op::NoticeDeliveryTypeWechatNickname
                        } else {
                            Op::NoticeDeliveryTypeFullNickname
                        },
                        &[],
                    )?;
                    self.action(Op::NoticeDeliveryWait, &[1.])?;
                    self.action(Op::NoticeDeliveryTimerStart, &[])?;
                    loop {
                        self.action(Op::NoticeDeliveryPopupCandidates, &[1.])?;
                        self.action(Op::NoticeDeliveryAppendRoot, &[])?;
                        let n = self.scalar(Op::NoticeDeliveryCandidateCount, &[])? as usize;
                        for i in 0..n {
                            self.action(Op::NoticeDeliveryCandidate, &[i as f64])?;
                            let root = self.flag(Op::NoticeDeliveryCandidateIsRoot, &[])?;
                            if self.flag(Op::NoticeDeliveryFindContact, &[f64::from(root)])? {
                                self.action(Op::NoticeDeliveryUseSelected, &[])?;
                                break;
                            }
                        }
                        if self.flag(Op::NoticeDeliveryHasSelected, &[])? {
                            self.action(Op::NoticeDeliveryDropCandidates, &[])?;
                            break;
                        }
                        self.action(Op::NoticeDeliveryWait, &[0.3])?;
                        self.action(Op::NoticeDeliveryDropCandidates, &[])?;
                        if self.scalar(Op::NoticeDeliveryElapsed, &[])? >= 8000. {
                            break;
                        }
                    }
                    if !self.flag(Op::NoticeDeliveryHasSelected, &[])? {
                        return self.notice_delivery_fail(1.);
                    }
                    self.action(Op::NoticeDeliveryClickSelected, &[])?;
                    self.action(Op::NoticeDeliveryWait, &[1.])?;
                    if !self.flag(Op::NoticeDeliveryIsWechat, &[])?
                        && !self.flag(Op::NoticeDeliverySelectedIsRoot, &[])?
                        && self.flag(Op::NoticeDeliverySelectedVisible, &[])?
                    {
                        self.action(Op::NoticeDeliveryEscape, &[])?;
                    }
                }
                if !self.flag(Op::NoticeDeliveryFindSend, &[8.])? {
                    return self.notice_delivery_fail(2.);
                }
                self.action(Op::NoticeDeliveryConfirm, &[])?;
                let point = self.call(Op::NoticeDeliverySendPoint, &[])?;
                self.action(
                    Op::NoticeDeliverySetComposer,
                    &[
                        f64::from((point[0] as i32).wrapping_sub(80).max(20)),
                        f64::from((point[1] as i32).wrapping_sub(45).max(20)),
                    ],
                )?;
                self.action(Op::NoticeDeliveryClickComposer, &[])?;
                self.action(Op::NoticeDeliveryWait, &[1.])?;
                self.action(Op::NoticeDeliveryClear, &[0.])?;
                self.action(Op::NoticeDeliveryWait, &[1.])?;
                if self.flag(Op::NoticeDeliveryTitleEmpty, &[])? {
                    self.action(Op::NoticeDeliveryBodyText, &[])?;
                } else {
                    self.action(Op::NoticeDeliveryTitleText, &[])?;
                }
                self.action(Op::NoticeDeliveryTypeText, &[])?;
                self.action(Op::NoticeDeliveryWait, &[1.])?;
                self.action(Op::NoticeDeliveryConfirm, &[])?;
                self.action(Op::NoticeDeliveryClickSend, &[])?;
                self.action(Op::NoticeDeliveryWait, &[1.])?;
                let n = self.scalar(Op::NoticeDeliveryImageCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::NoticeDeliverySendImage, &[i as f64])?;
                }
            }
            Method::NoticeDeliveryEntry => {
                self.action(
                    if args[0] != 0. {
                        Op::NoticeDeliveryReadWechatNickname
                    } else {
                        Op::NoticeDeliveryReadQqNickname
                    },
                    &[],
                )?;
                if self.flag(Op::NoticeDeliveryNicknameEmpty, &[])? {
                    return self.notice_delivery_fail(3.);
                }
                self.action(Op::NoticeDeliveryConstructAndSend, &[])?;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
