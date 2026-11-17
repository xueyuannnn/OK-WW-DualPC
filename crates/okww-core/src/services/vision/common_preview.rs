//! Qt image/preview selection; conversion buffers stay in the native SDK host.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn common_preview_fail<T>(&self, code: f64) -> Result<T> {
        self.action(Op::CommonPreviewFailure, &[code])?;
        Err(HOST_ERROR)
    }
    pub(in crate::services) fn run_common_preview(&self, method: Method) -> Result<[f64; 3]> {
        match method {
            Method::CommonFrameImage => {
                if self.scalar(Op::CommonPreviewFormat, &[])? == 7. {
                    self.action(Op::CommonFrameDecodeJpeg, &[])?;
                    if self.flag(Op::CommonFrameImageNull, &[])? {
                        return self.common_preview_fail(0.);
                    }
                    if !self.flag(Op::CommonFrameSizeMatches, &[])? {
                        return self.common_preview_fail(1.);
                    }
                    self.action(Op::CommonFrameConvertRgb32, &[])?;
                } else {
                    self.action(Op::CommonFrameCreateRgb32, &[])?;
                    let dimensions = self.call(Op::CommonPreviewDimensions, &[])?;
                    if dimensions[0] > 0. && dimensions[1] > 0. {
                        self.action(Op::CommonFramePixels, &[])?;
                    }
                }
            }
            Method::CommonPreviewImage => {
                let dimensions = self.call(Op::CommonPreviewDimensions, &[])?;
                if dimensions[0] <= 0.
                    || dimensions[1] <= 0.
                    || self.flag(Op::CommonPreviewTargetEmpty, &[])?
                {
                    return Ok([0.; 3]);
                }
                self.action(Op::CommonPreviewSourceSize, &[])?;
                let target = self.call(Op::CommonPreviewTargetSize, &[])?;
                if dimensions[0] > target[0] || dimensions[1] > target[1] {
                    self.action(Op::CommonPreviewScaleSize, &[])?;
                }
                let format = self.scalar(Op::CommonPreviewFormat, &[])? as i32;
                if format == 7 {
                    self.action(Op::CommonPreviewScaledFrame, &[])?;
                    return Ok([0.; 3]);
                }
                if self.flag(Op::CommonPreviewFastCompiled, &[])?
                    && dimensions[2] > 0.
                    && matches!(format, 0 | 1 | 2 | 4 | 5)
                {
                    let planar = format == 2;
                    let channels = match format {
                        1 => 4,
                        0 => 3,
                        2 => 1,
                        _ => 2,
                    };
                    if !self.flag(
                        Op::CommonPreviewLengthValid,
                        &[if planar { 3. } else { 2. }],
                    )? || dimensions[2] < dimensions[0] * f64::from(channels)
                        || ((planar || channels == 2) && (dimensions[0] as i32) % 2 != 0)
                        || (planar && (dimensions[1] as i32) % 2 != 0)
                    {
                        return self.common_preview_fail(2.);
                    }
                    self.action(
                        match channels {
                            1 => Op::CommonPreviewNv12,
                            2 => {
                                if format == 4 {
                                    Op::CommonPreviewYuy2
                                } else {
                                    Op::CommonPreviewUyvy
                                }
                            }
                            4 => Op::CommonPreviewBgra,
                            _ => Op::CommonPreviewBgr,
                        },
                        &[],
                    )?;
                    if self.flag(Op::CommonPreviewBottomUp, &[])? && channels >= 3 {
                        self.action(Op::CommonPreviewFlip, &[])?;
                    }
                    if self.flag(Op::CommonPreviewConvertedSizeMatches, &[])? {
                        self.action(Op::CommonPreviewReuseConverted, &[])?;
                    } else {
                        self.action(Op::CommonPreviewResize, &[])?;
                    }
                    if channels == 3
                        && !self.flag(Op::CommonPreviewBottomUp, &[])?
                        && self.flag(Op::CommonPreviewOriginalSizeMatches, &[])?
                    {
                        self.action(Op::CommonPreviewCopyBorrowed, &[])?;
                    } else {
                        self.action(Op::CommonPreviewOwnConverted, &[])?;
                    }
                } else {
                    self.action(Op::CommonPreviewScaledFrame, &[])?;
                }
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
