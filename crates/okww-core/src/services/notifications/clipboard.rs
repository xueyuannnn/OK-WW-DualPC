//! Clipboard image ownership and restoration, including original failure cleanup order.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn notice_clipboard_fail<T>(&self, code: f64) -> Result<T> {
        self.action(Op::NoticeClipboardFailure, &[code])?;
        Err(HOST_ERROR)
    }
    pub(in crate::services) fn run_notification_clipboard(
        &self,
        method: Method,
    ) -> Result<[f64; 3]> {
        match method {
            Method::NoticeClipboardCreate => {
                if !self.flag(Op::NoticeClipboardCreateOwner, &[])? {
                    return self.notice_clipboard_fail(0.);
                }
                self.action(Op::NoticeClipboardDecode, &[])?;
                if self.flag(Op::NoticeClipboardImageEmpty, &[])? {
                    self.action(Op::NoticeClipboardDestroyOwner, &[])?;
                    return self.notice_clipboard_fail(1.);
                }
                let cols = self.scalar(Op::NoticeClipboardImageCols, &[])? as i32;
                let stride = cols.wrapping_mul(3).wrapping_add(3) & !3;
                self.action(Op::NoticeClipboardStride, &[f64::from(stride)])?;
                if !self.flag(Op::NoticeClipboardAllocateImage, &[])? {
                    self.action(Op::NoticeClipboardDestroyOwner, &[])?;
                    return self.notice_clipboard_fail(2.);
                }
                if !self.flag(Op::NoticeClipboardLockImage, &[])? {
                    self.action(Op::NoticeClipboardFreeMemory, &[])?;
                    self.action(Op::NoticeClipboardDestroyOwner, &[])?;
                    return self.notice_clipboard_fail(3.);
                }
                self.action(Op::NoticeClipboardImageHeader, &[])?;
                let rows = self.scalar(Op::NoticeClipboardImageRows, &[])? as usize;
                for y in 0..rows {
                    self.action(Op::NoticeClipboardCopyRow, &[y as f64])?;
                }
                self.action(Op::NoticeClipboardUnlockMemory, &[])?;
                if !self.flag(Op::NoticeClipboardOpen, &[])? {
                    self.action(Op::NoticeClipboardFreeMemory, &[])?;
                    self.action(Op::NoticeClipboardDestroyOwner, &[])?;
                    return self.notice_clipboard_fail(4.);
                }
                if self.flag(Op::NoticeClipboardOldText, &[])?
                    && self.flag(Op::NoticeClipboardLockOld, &[])?
                {
                    self.action(Op::NoticeClipboardSaveOld, &[])?;
                    self.action(Op::NoticeClipboardUnlockOld, &[])?;
                }
                self.action(Op::NoticeClipboardEmpty, &[])?;
                let set = self.flag(Op::NoticeClipboardSetImage, &[])?;
                self.action(Op::NoticeClipboardSaveSequence, &[])?;
                self.action(Op::NoticeClipboardClose, &[])?;
                if !set {
                    self.action(Op::NoticeClipboardFreeMemory, &[])?;
                    self.action(Op::NoticeClipboardDestroyOwner, &[])?;
                    return self.notice_clipboard_fail(5.);
                }
            }
            Method::NoticeClipboardRestore => {
                if self.flag(Op::NoticeClipboardSequenceMatches, &[])?
                    && self.flag(Op::NoticeClipboardOpen, &[])?
                {
                    self.action(Op::NoticeClipboardEmpty, &[])?;
                    if !self.flag(Op::NoticeClipboardSavedEmpty, &[])?
                        && self.flag(Op::NoticeClipboardAllocateSaved, &[])?
                    {
                        if self.flag(Op::NoticeClipboardLockSaved, &[])? {
                            self.action(Op::NoticeClipboardCopySaved, &[])?;
                            self.action(Op::NoticeClipboardUnlockMemory, &[])?;
                            if !self.flag(Op::NoticeClipboardSetText, &[])? {
                                self.action(Op::NoticeClipboardFreeMemory, &[])?;
                            }
                        } else {
                            self.action(Op::NoticeClipboardFreeMemory, &[])?;
                        }
                    }
                    self.action(Op::NoticeClipboardClose, &[])?;
                }
                self.action(Op::NoticeClipboardDestroyOwner, &[])?;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
