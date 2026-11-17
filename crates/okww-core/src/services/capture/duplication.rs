//! DXGI output choice, frame reuse, staging layout and rotation policy.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    pub(in crate::services) fn run_capture_duplication(&self, method: Method) -> Result<[f64; 3]> {
        match method {
            Method::DesktopDuplicationCreate => {
                self.action(Op::DesktopDupCreateFactory, &[])?;
                let mut adapter = 0_u32;
                while self.flag(Op::DesktopDupNextAdapter, &[adapter as f64])? {
                    let mut output = 0_u32;
                    while self.flag(Op::DesktopDupNextOutput, &[output as f64])? {
                        self.action(Op::DesktopDupOutputDesc, &[])?;
                        self.action(Op::DesktopDupScreenRect, &[])?;
                        if self.flag(Op::DesktopDupAttached, &[])?
                            && self.flag(Op::DesktopDupContainsTarget, &[])?
                        {
                            self.action(Op::DesktopDupSelectOutput, &[])?;
                            self.action(Op::DesktopDupStoreBounds, &[])?;
                            self.action(Op::DesktopDupStoreRotation, &[])?;
                            break;
                        }
                        self.action(Op::DesktopDupResetCandidate, &[])?;
                        output = output.wrapping_add(1);
                    }
                    if self.flag(Op::DesktopDupHasSelected, &[])? {
                        self.action(Op::DesktopDupResetCandidate, &[])?;
                        break;
                    }
                    self.action(Op::DesktopDupResetAdapter, &[])?;
                    self.action(Op::DesktopDupResetCandidate, &[])?;
                    adapter = adapter.wrapping_add(1);
                }
                if !self.flag(Op::DesktopDupHasSelected, &[])? {
                    self.action(Op::DesktopDupFailure, &[0.])?;
                    return Err(HOST_ERROR);
                }
                self.action(Op::DesktopDupCreateDevice, &[])?;
                self.action(Op::DesktopDupOutputInterface, &[])?;
                self.action(Op::DesktopDupDuplicateOutput, &[])?;
            }
            Method::DesktopDuplicationFrame => {
                self.action(Op::DesktopDupClearChanged, &[])?;
                if !self.flag(Op::DesktopDupBoundsContain, &[])? {
                    self.action(Op::DesktopDupFailure, &[1.])?;
                    return Err(HOST_ERROR);
                }
                self.action(Op::DesktopDupAcquire, &[])?;
                if self.flag(Op::DesktopDupAcquireTimeout, &[])? {
                    return Ok([0.; 3]);
                }
                self.action(Op::DesktopDupCheckAcquire, &[])?;
                self.action(Op::DesktopDupReleaseGuard, &[])?;
                if !self.flag(Op::DesktopDupLastNull, &[])?
                    && self.flag(Op::DesktopDupPointerOnly, &[])?
                {
                    return Ok([0.; 3]);
                }
                self.action(Op::DesktopDupTextureInterface, &[])?;
                self.action(Op::DesktopDupTextureDesc, &[])?;
                let rotation = self.scalar(Op::DesktopDupRotation, &[])? as u32;
                let rotated = !matches!(rotation, 0 | 1);
                self.action(Op::DesktopDupCrop, &[])?;
                self.action(Op::DesktopDupWantedDesc, &[f64::from(rotated)])?;
                if self.flag(Op::DesktopDupHasStaging, &[])? {
                    self.action(Op::DesktopDupCurrentDesc, &[])?;
                }
                if !self.flag(Op::DesktopDupHasStaging, &[])?
                    || !self.flag(Op::DesktopDupSameWidth, &[])?
                    || !self.flag(Op::DesktopDupSameHeight, &[])?
                    || !self.flag(Op::DesktopDupSameFormat, &[])?
                {
                    self.action(Op::DesktopDupResetStaging, &[])?;
                    self.action(Op::DesktopDupCreateStaging, &[])?;
                }
                self.action(
                    if rotated {
                        Op::DesktopDupCopyResource
                    } else {
                        Op::DesktopDupCopyRegion
                    },
                    &[],
                )?;
                self.action(Op::DesktopDupMap, &[])?;
                self.action(Op::DesktopDupUnmapGuard, &[])?;
                self.action(Op::DesktopDupBorrowImage, &[])?;
                if rotated {
                    let angle = match rotation {
                        2 => 90.,
                        3 => 180.,
                        _ => 270.,
                    };
                    self.action(Op::DesktopDupCopyRotated, &[angle])?;
                } else {
                    self.action(Op::DesktopDupCopyImage, &[])?;
                }
                self.action(Op::DesktopDupSetChanged, &[])?;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
