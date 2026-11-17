use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn template_fail<T>(&self, reason: f64) -> Result<T> {
        self.action(Op::TemplateFailure, &[reason])?;
        Err(HOST_ERROR)
    }
    fn template_checkpoint(&self) -> Result {
        if self.flag(Op::TemplateHasCheckpoint, &[])? {
            self.action(Op::Checkpoint, &[])?;
        }
        Ok(())
    }
    pub(in crate::services) fn run_templates(&self, method: Method, a: &[f64]) -> Result<[f64; 3]> {
        match method {
            Method::TemplateAdd => {
                self.action(Op::ReadTemplateDocument, &[])?;
                let count = self.scalar(Op::TemplateImageCount, &[])? as usize;
                for i in 0..count {
                    self.action(Op::LoadTemplateImageRecord, &[i as f64])?;
                }
                let count = self.scalar(Op::TemplateCategoryCount, &[])? as usize;
                for i in 0..count {
                    self.action(Op::LoadTemplateCategoryRecord, &[i as f64])?;
                }
                let count = self.scalar(Op::TemplateAnnotationCount, &[])? as usize;
                for i in 0..count {
                    self.action(Op::LoadTemplateAnnotation, &[i as f64])?;
                    if self.flag(Op::TemplateAnnotationNameEmpty, &[])?
                        || self.scalar(Op::TemplateAnnotationBoxCount, &[])? != 4.
                    {
                        return self.template_fail(0.);
                    }
                    self.action(Op::TemplateAnnotationPath, &[])?;
                    self.action(Op::NormalizeTemplatePath, &[])?;
                    if self.flag(Op::TemplatePathAbsolute, &[])?
                        && self.flag(Op::TemplatePathHasImages, &[])?
                    {
                        self.action(Op::TemplatePathRelative, &[])?;
                    }
                    self.action(Op::ResolveTemplatePath, &[])?;
                    let origin = self.call(Op::TemplateAnnotationOrigin, &[])?;
                    let size = self.call(Op::TemplateAnnotationSize, &[])?;
                    let x = self.scalar(Op::PixelRound, &[origin[0]])?;
                    let y = self.scalar(Op::PixelRound, &[origin[1]])?;
                    let right = self.scalar(Op::PixelRound, &[origin[0] + size[0]])?;
                    let bottom = self.scalar(Op::PixelRound, &[origin[1] + size[1]])?;
                    self.action(Op::SetTemplateSourceBox, &[x, y, right - x, bottom - y])?;
                    let hc = self.flag(Op::TemplateHasHorizontalName, &[])?
                        || self.flag(Op::TemplateHorizontalAnchor, &[])?;
                    let vc = self.flag(Op::TemplateHasVerticalName, &[])?
                        || self.flag(Op::TemplateVerticalAnchor, &[])?;
                    self.action(
                        Op::SetTemplateSourceAnchors,
                        &[f64::from(hc), f64::from(vc)],
                    )?;
                    let kind = self.scalar(Op::TemplateNameKind, &[])?;
                    if kind != 0. {
                        self.action(Op::SetTemplateSourceFilter, &[kind])?;
                    }
                    if self.flag(Op::TemplateSourceExists, &[])? {
                        continue;
                    }
                    self.action(Op::StoreTemplateSource, &[])?;
                }
                self.action(Op::ClearTemplateCache, &[])?;
            }
            Method::TemplateScaled => {
                if !self.flag(Op::TemplateSizeMatches, &[])? {
                    self.action(Op::ClearTemplateCache, &[])?;
                    self.action(Op::SetTemplateSize, &[])?;
                }
                if self.flag(Op::TemplateCached, &[])? {
                    self.action(Op::ReturnCachedTemplate, &[])?;
                    return Ok([0.; 3]);
                }
                if !self.flag(Op::TemplateSourceExists, &[])? {
                    return self.template_fail(1.);
                }
                self.action(Op::ReadTemplateSource, &[])?;
                if !self.flag(Op::TemplateImageCached, &[])? {
                    self.action(Op::LoadTemplateImage, &[])?;
                }
                self.action(Op::ReadCachedTemplateImage, &[])?;
                if !self.flag(Op::TemplateSourceBoxContained, &[])?
                    || self.flag(Op::TemplateSourceBoxEmpty, &[])?
                {
                    return self.template_fail(2.);
                }
                self.action(Op::AnchorTemplateSource, &[])?;
                self.action(Op::ResizeTemplateSource, &[])?;
                self.action(Op::StoreScaledTemplate, &[])?;
            }
            Method::TemplateFind => {
                // threshold, horizontal, vertical, top, searchHeight, scale,
                // targetHeight, rotation, sigma, grayscale, limit, explicitArea.
                self.template_checkpoint()?;
                if self.flag(Op::TemplateFrameEmpty, &[])? {
                    return Ok([0.; 3]);
                }
                self.action(Op::LoadScaledFeature, &[])?;
                self.action(Op::CopyTemplatePattern, &[])?;
                if a[7] != 0. {
                    if !a[7].is_finite() {
                        return self.template_fail(3.);
                    }
                    self.action(Op::RotateTemplatePattern, &[a[7]])?;
                }
                if a[3] <= 0. || a[3] > 1. || a[4] <= 0. || a[4] > 1. {
                    return self.template_fail(4.);
                }
                if a[3] != 1. {
                    let size = self.call(Op::TemplatePatternSize, &[])?;
                    self.action(
                        Op::TrimTemplatePattern,
                        &[f64::from(((size[1] * a[3]) as i32).max(1))],
                    )?;
                }
                if a[5] != 1. {
                    if !a[5].is_finite() || a[5] <= 0. || a[5] > 8. {
                        return self.template_fail(5.);
                    }
                    self.action(Op::ScaleTemplatePattern, &[a[5]])?;
                }
                if a[11] != 0. {
                    self.action(Op::UseSpecifiedTemplateArea, &[])?;
                } else {
                    self.action(Op::CopyFeatureSearchBox, &[])?;
                    if a[4] != 1. {
                        let rows = self.scalar(Op::TemplateFeatureRows, &[])?;
                        self.action(
                            Op::SetFeatureSearchHeight,
                            &[f64::from(((rows * a[4]) as i32).max(1))],
                        )?;
                    }
                    let origin = self.call(Op::TemplateSearchOrigin, &[])?;
                    let size = self.call(Op::TemplateSearchSize, &[])?;
                    let frame = self.call(Op::TemplateFrameSize, &[])?;
                    let x = self.scalar(Op::PixelRound, &[origin[0] - frame[0] * a[1]])?;
                    let y = self.scalar(Op::PixelRound, &[origin[1] - frame[1] * a[2]])?;
                    let right =
                        self.scalar(Op::PixelRound, &[origin[0] + size[0] + frame[0] * a[1]])?;
                    let bottom =
                        self.scalar(Op::PixelRound, &[origin[1] + size[1] + frame[1] * a[2]])?;
                    self.action(Op::SetTemplateSearchArea, &[x, y, right - x, bottom - y])?;
                }
                self.action(Op::ClipTemplateSearchArea, &[])?;
                let area = self.call(Op::TemplateAreaSize, &[])?;
                let pattern = self.call(Op::TemplatePatternSize, &[])?;
                if area[0] < pattern[0] || area[1] < pattern[1] {
                    return Ok([0.; 3]);
                }
                self.action(Op::FilterTemplateSearch, &[])?;
                if a[9] != 0. {
                    if self.scalar(Op::TemplateSearchChannels, &[])? > 1. {
                        self.action(Op::GrayTemplateSearch, &[])?;
                    }
                    if self.scalar(Op::TemplatePatternChannels, &[])? > 1. {
                        self.action(Op::GrayTemplatePattern, &[])?;
                    }
                }
                if !self.flag(Op::TemplateTypesMatch, &[])? {
                    return self.template_fail(6.);
                }
                if a[8] != 0. {
                    if !a[8].is_finite() || a[8] < 0. || a[8] > 3. {
                        return self.template_fail(7.);
                    }
                    self.action(Op::BlurTemplateSearch, &[a[8]])?;
                    self.action(Op::BlurTemplatePattern, &[a[8]])?;
                    self.action(Op::UseSoftTemplateImages, &[])?;
                }
                self.action(Op::StoreOriginalTemplateSize, &[])?;
                let mut scale = 1.;
                let frame = self.call(Op::TemplateFrameSize, &[])?;
                if a[6] > 0. && frame[1] >= 1.5 * a[6] {
                    scale = a[6] / frame[1];
                    let search = self.call(Op::TemplateSearchImageSize, &[])?;
                    let width = self.scalar(Op::PixelRound, &[search[0] * scale])?.max(1.);
                    let height = self.scalar(Op::PixelRound, &[search[1] * scale])?.max(1.);
                    self.action(Op::ResizeTemplateSearch, &[width, height])?;
                    self.action(Op::ScaleTemplatePattern, &[scale])?;
                    let search = self.call(Op::TemplateSearchImageSize, &[])?;
                    let pattern = self.call(Op::TemplatePatternSize, &[])?;
                    if search[0] < pattern[0] || search[1] < pattern[1] {
                        return Ok([0.; 3]);
                    }
                }
                self.template_checkpoint()?;
                self.action(Op::MatchTemplateImages, &[])?;
                self.template_checkpoint()?;
                let limit = if a[10] <= 0. {
                    1000
                } else {
                    (a[10] as usize).min(1000)
                };
                for i in 0..limit {
                    if i % 16 == 0 {
                        self.template_checkpoint()?;
                    }
                    let value = self.call(Op::BestTemplateMatch, &[])?;
                    if !value[0].is_finite() || value[0] < a[0] {
                        break;
                    }
                    let x = self.scalar(Op::PixelRound, &[value[1] / scale])?;
                    let y = self.scalar(Op::PixelRound, &[value[2] / scale])?;
                    self.action(Op::AppendTemplateMatch, &[x, y, value[0]])?;
                    let pattern = self.call(Op::TemplatePatternSize, &[])?;
                    self.action(
                        Op::SuppressTemplateMatch,
                        &[
                            value[1] - pattern[0] + 1.,
                            value[2] - pattern[1] + 1.,
                            pattern[0] * 2. - 1.,
                            pattern[1] * 2. - 1.,
                        ],
                    )?;
                }
            }
            Method::TemplateOne => {
                self.action(Op::SetSingleTemplateLimit, &[])?;
                self.action(Op::FindSingleTemplate, &[])?;
                if !self.flag(Op::SingleTemplateEmpty, &[])? {
                    self.action(Op::ReturnSingleTemplate, &[])?;
                }
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
