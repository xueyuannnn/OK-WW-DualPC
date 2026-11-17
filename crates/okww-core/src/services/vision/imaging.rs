//! Frame conversion and preprocessing around OpenCV primitives.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn imaging_fail<T>(&self, reason: f64) -> Result<T> {
        self.action(Op::ImagingFailure, &[reason])?;
        Err(HOST_ERROR)
    }
    pub(in crate::services) fn run_imaging(
        &self,
        method: Method,
        args: &[f64],
    ) -> Result<[f64; 3]> {
        let mut result = [0.; 3];
        match method {
            Method::ReadImage => {
                if !self.flag(Op::OpenImageFile, &[])? {
                    return self.imaging_fail(0.);
                }
                self.action(Op::ReadImageBytes, &[])?;
                self.action(Op::DecodeImageBytes, &[])?;
                if self.flag(Op::DecodedImageEmpty, &[])? {
                    return self.imaging_fail(1.);
                }
            }
            Method::DecodeFrame => {
                let size = self.call(Op::FrameDimensions, &[])?;
                if size[0] <= 0. || size[1] <= 0. || size[2] <= 0. {
                    return self.imaging_fail(2.);
                }
                let format = self.scalar(Op::FrameFormat, &[])? as i32;
                if format == 7 {
                    self.action(Op::DecodeMotionJpeg, &[])?;
                    let decoded = self.call(Op::DecodedFrameSize, &[])?;
                    if decoded[0] != 0. || decoded[1] != size[0] || decoded[2] != size[1] {
                        return self.imaging_fail(3.);
                    }
                    return Ok(result);
                }
                let planar = matches!(format, 2 | 3 | 6);
                if !self.flag(
                    Op::FrameLengthValid,
                    &[f64::from(if planar { 3 } else { 2 })],
                )? {
                    return self.imaging_fail(4.);
                }
                if matches!(format, 0 | 1) {
                    let channels = if format == 0 { 3. } else { 4. };
                    if size[2] < size[0] * channels {
                        return self.imaging_fail(5.);
                    }
                    self.action(Op::CreateRgbFrame, &[channels])?;
                    if channels == 4. {
                        self.action(Op::ConvertBgraFrame, &[])?;
                    } else {
                        self.action(Op::CloneRgbFrame, &[])?;
                    }
                    if self.flag(Op::FrameBottomUp, &[])? {
                        self.action(Op::FlipDecodedFrame, &[])?;
                    }
                    return Ok(result);
                }
                if size[0] as i32 % 2 != 0 || (planar && size[1] as i32 % 2 != 0) {
                    return self.imaging_fail(6.);
                }
                match format {
                    2 => {
                        if size[2] < size[0] {
                            return self.imaging_fail(7.);
                        }
                        self.action(Op::ConvertNv12Frame, &[])?;
                    }
                    6 => {
                        if size[2] < size[0] || size[2] as i32 % 2 != 0 {
                            return self.imaging_fail(8.);
                        }
                        self.action(Op::CreateI420Frame, &[])?;
                        for y in 0..size[1] as i32 {
                            self.action(Op::CopyI420LumaRow, &[f64::from(y)])?;
                        }
                        for plane in 0..2 {
                            for y in 0..size[1] as i32 / 2 {
                                self.action(
                                    Op::CopyI420ChromaRow,
                                    &[f64::from(plane), f64::from(y)],
                                )?;
                            }
                        }
                        self.action(Op::ConvertI420Frame, &[])?;
                    }
                    4 | 5 => {
                        if size[2] < size[0] * 2. {
                            return self.imaging_fail(9.);
                        }
                        self.action(Op::ConvertPackedYuvFrame, &[f64::from(format)])?;
                    }
                    3 => {
                        if size[2] < size[0] * 2. || size[2] as i32 % 2 != 0 {
                            return self.imaging_fail(10.);
                        }
                        self.action(Op::CreateP010Frame, &[])?;
                        self.action(Op::ReduceP010Frame, &[])?;
                        self.action(Op::ConvertP010Frame, &[])?;
                    }
                    _ => return self.imaging_fail(11.),
                }
            }
            Method::InferenceImage => {
                self.action(Op::DecodeInferenceFrame, &[])?;
                let n = self.scalar(Op::InferenceCropCount, &[])?;
                if n != 0. {
                    if n != 4. {
                        return self.imaging_fail(12.);
                    }
                    self.action(Op::ReadInferenceCrop, &[])?;
                    if !self.flag(Op::InferenceCropValid, &[])? {
                        return self.imaging_fail(13.);
                    }
                    self.action(Op::CropInferenceFrame, &[])?;
                }
                self.action(Op::ReadInferenceSize, &[])?;
                let size = self.call(Op::InferenceOutputSize, &[])?;
                if size[0] <= 0. || size[1] <= 0. {
                    return self.imaging_fail(14.);
                }
                if !self.flag(Op::InferenceSizeMatches, &[])? {
                    self.action(Op::ResizeInferenceFrame, &[])?;
                }
            }
            Method::CachedInferenceFrame => {
                if !self.flag(Op::HasInferenceFrame, &[])? {
                    self.action(Op::ClearInferenceSource, &[])?;
                    self.action(Op::ClearInferenceImage, &[])?;
                    return Ok(result);
                }
                let mut convert = !self.flag(Op::HasInferenceSource, &[])?;
                if !convert {
                    for field in 0..4 {
                        if !self.flag(Op::InferenceSourceFieldMatches, &[f64::from(field)])? {
                            convert = true;
                            break;
                        }
                    }
                }
                if convert {
                    self.action(Op::ConvertInferenceFrame, &[])?;
                    self.action(Op::IncrementInferenceConversions, &[])?;
                }
                self.action(Op::StoreInferenceFrame, &[])?;
                self.action(Op::ReturnInferenceImage, &[])?;
            }
            Method::FilterImage => {
                let filter = args[0] as i32;
                if filter == 0 {
                    self.action(Op::ReturnFilterSource, &[])?;
                    return Ok(result);
                }
                if filter == 6 {
                    if self.scalar(Op::FilterSourceChannels, &[])? == 3. {
                        self.action(Op::FilterToGray, &[])?;
                    } else {
                        self.action(Op::ReturnFilterSource, &[])?;
                    }
                    self.action(Op::FilterThreshold, &[80., 1.])?;
                    if self.scalar(Op::FilterSourceChannels, &[])? == 3. {
                        self.action(Op::FilterToBgr, &[])?;
                    }
                    return Ok(result);
                }
                if matches!(filter, 3 | 4) {
                    if self.scalar(Op::FilterSourceChannels, &[])? == 3. {
                        self.action(Op::FilterToGray, &[])?;
                    } else {
                        self.action(Op::ReturnFilterSource, &[])?;
                    }
                    self.action(
                        Op::FilterThreshold,
                        &[if filter == 3 { 244. } else { 220. }, 0.],
                    )?;
                    return Ok(result);
                }
                let lower = match filter {
                    1 => 244.,
                    2 => 210.,
                    _ => 0.,
                };
                let upper = if filter == 5 { 240. } else { 255. };
                self.action(Op::FilterInRange, &[lower, upper])?;
                self.action(Op::FilterToBgr, &[])?;
            }
            Method::ColorPercentage => {
                if !self.flag(Op::ColorBoxValid, &[])? {
                    return Ok(result);
                }
                self.action(Op::ColorRangeMask, &[])?;
                let size = self.call(Op::ColorMaskCounts, &[])?;
                result[0] = size[0] / size[1];
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok(result)
    }
}
