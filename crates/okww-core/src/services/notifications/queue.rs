//! Notification worker lifecycle; native objects retain mutex and payload ownership.
use crate::services::{Host, Method, Op, Result, INVALID_ACTION};
impl Host {
    pub(in crate::services) fn run_notification_queue(&self, method: Method) -> Result<[f64; 3]> {
        match method {
            Method::NoticeQueueSelectTransport => {
                if self.flag(Op::NoticeQueueHasOverride, &[])? {
                    self.action(Op::NoticeQueueMoveOverride, &[])?;
                } else {
                    self.action(Op::NoticeQueueDefaultTransport, &[])?;
                }
            }
            Method::NoticeQueueStop => {
                self.action(Op::NoticeQueueSetStopping, &[])?;
                self.action(Op::NoticeQueueNotifyAll, &[])?;
                if self.flag(Op::NoticeQueueJoinable, &[])? {
                    self.action(Op::NoticeQueueJoin, &[])?;
                }
            }
            Method::NoticeQueueIdle => {
                self.action(Op::NoticeQueueLock, &[])?;
                let idle = self.flag(Op::NoticeQueueEmpty, &[])?
                    && !self.flag(Op::NoticeQueueBusy, &[])?;
                self.action(Op::NoticeQueueUnlock, &[])?;
                return Ok([f64::from(idle), 0., 0.]);
            }
            Method::NoticeQueueSubmit => {
                self.action(Op::NoticeQueueFlags, &[])?;
                let mut enabled = false;
                for i in 0..7 {
                    if self.flag(Op::NoticeQueueSubmittedFlag, &[f64::from(i)])? {
                        enabled = true;
                        break;
                    }
                }
                if !enabled {
                    return Ok([0.; 3]);
                }
                self.action(Op::NoticeQueueLock, &[])?;
                if !self.flag(Op::NoticeQueueStopping, &[])? {
                    if self.scalar(Op::NoticeQueueSize, &[])? >= 32. {
                        if self.flag(Op::NoticeQueueHasReporter, &[])? {
                            self.action(Op::NoticeQueueReportFull, &[])?;
                        }
                    } else {
                        self.action(Op::NoticeQueuePush, &[])?;
                        self.action(Op::NoticeQueueNotifyOne, &[])?;
                    }
                }
                self.action(Op::NoticeQueueUnlock, &[])?;
            }
            Method::NoticeQueueRun => {
                self.action(Op::NoticeQueueCancellation, &[])?;
                while !self.flag(Op::NoticeQueueStopping, &[])? {
                    self.action(Op::NoticeQueueNewItem, &[])?;
                    self.action(Op::NoticeQueueLock, &[])?;
                    self.action(Op::NoticeQueueWaitReady, &[])?;
                    if self.flag(Op::NoticeQueueStopping, &[])? {
                        self.action(Op::NoticeQueueUnlock, &[])?;
                        self.action(Op::NoticeQueueDropItem, &[])?;
                        break;
                    }
                    self.action(Op::NoticeQueueTakeFront, &[])?;
                    self.action(Op::NoticeQueuePopFront, &[])?;
                    self.action(Op::NoticeQueueSetBusy, &[1.])?;
                    self.action(Op::NoticeQueueUnlock, &[])?;
                    self.action(Op::NoticeQueueChannels, &[])?;
                    for i in 0..7 {
                        if self.flag(Op::NoticeQueueStopping, &[])? {
                            break;
                        }
                        if !self.flag(Op::NoticeQueueItemFlag, &[f64::from(i)])? {
                            continue;
                        }
                        let delivery = (|| {
                            match i {
                                4 => self.action(Op::NoticeQueueSmtp, &[])?,
                                5 | 6 => self.action(Op::NoticeQueueDesktop, &[f64::from(i)])?,
                                _ => {
                                    self.action(Op::NoticeQueueRequests, &[f64::from(i)])?;
                                    let n = self.scalar(Op::NoticeQueueRequestCount, &[])? as usize;
                                    for j in 0..n {
                                        self.action(Op::NoticeQueueSend, &[j as f64])?;
                                        self.action(Op::NoticeQueueValidate, &[f64::from(i)])?;
                                    }
                                    self.action(Op::NoticeQueueClearRequests, &[])?;
                                }
                            }
                            if self.flag(Op::NoticeQueueHasReporter, &[])? {
                                self.action(Op::NoticeQueueReportSuccess, &[f64::from(i)])?;
                            }
                            Ok(())
                        })();
                        if let Err(error) = delivery {
                            if self.scalar(Op::ExceptionKind, &[])? == 7. {
                                return Err(error);
                            }
                            self.action(Op::ClearException, &[])?;
                            self.action(Op::NoticeQueueClearRequests, &[])?;
                            if !self.flag(Op::NoticeQueueStopping, &[])?
                                && self.flag(Op::NoticeQueueHasReporter, &[])?
                            {
                                self.action(Op::NoticeQueueReportError, &[f64::from(i)])?;
                            }
                            self.action(Op::ClearException, &[])?;
                        }
                    }
                    self.action(Op::NoticeQueueSetBusy, &[0.])?;
                    self.action(Op::NoticeQueueLock, &[])?;
                    self.action(Op::NoticeQueueDelay, &[5.])?;
                    self.action(Op::NoticeQueueUnlock, &[])?;
                    self.action(Op::NoticeQueueDropChannels, &[])?;
                    self.action(Op::NoticeQueueDropItem, &[])?;
                }
                self.action(Op::NoticeQueueSetBusy, &[0.])?;
            }
            Method::NoticeQueueReadyPredicate => {
                return Ok([
                    f64::from(
                        self.flag(Op::NoticeQueueStopping, &[])?
                            || !self.flag(Op::NoticeQueueEmpty, &[])?,
                    ),
                    0.,
                    0.,
                ]);
            }
            Method::NoticeQueueDelayPredicate => {
                return self.call(Op::NoticeQueueStopping, &[]);
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
