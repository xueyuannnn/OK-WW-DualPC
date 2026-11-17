//! OCR policy around the unchanged OpenCV/OpenVINO SDK primitives.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn ocr_fail<T>(&self, reason: f64) -> Result<T> {
        self.action(Op::OcrFailure, &[reason])?;
        Err(HOST_ERROR)
    }
    fn ocr_checkpoint(&self) -> Result {
        if self.flag(Op::OcrHasCheckpoint, &[])? {
            self.action(Op::Checkpoint, &[])?;
        }
        Ok(())
    }
    pub(in crate::services) fn run_ocr(&self, method: Method, args: &[f64]) -> Result<[f64; 3]> {
        match method {
            Method::OcrInitialize => {
                if !self.flag(Op::OcrOpenDictionary, &[])? {
                    return self.ocr_fail(0.);
                }
                self.action(Op::OcrAlphabetBlank, &[])?;
                while !self.flag(Op::OcrDictionaryEnd, &[])? {
                    self.action(Op::OcrReadDictionaryLine, &[])?;
                    while self.flag(Op::OcrDictionaryLineEnding, &[])? {
                        self.action(Op::OcrChopDictionaryLine, &[])?;
                    }
                    self.action(Op::OcrAppendDictionaryLine, &[])?;
                }
                self.action(Op::OcrAlphabetSpace, &[])?;
                self.action(Op::OcrCompileDetector, &[])?;
                self.action(Op::OcrCompileRecognizer, &[])?;
                self.action(Op::OcrCreateDetectorRequest, &[])?;
                self.action(Op::OcrCreateRecognizerRequest, &[])?;
            }
            Method::OcrCompile => {
                if !self.flag(Op::OcrModelExists, &[])? {
                    return self.ocr_fail(1.);
                }
                self.action(Op::OcrCompileModel, &[])?;
            }
            Method::OcrNormalize => {
                self.action(Op::OcrAllocateNormalized, args)?;
                self.action(Op::OcrZeroNormalized, &[])?;
                let size = self.call(Op::OcrImageSize, &[])?;
                let resized_width = if args[2] != 0. {
                    args[1]
                } else {
                    args[1].min((args[0] * size[0] / size[1]).ceil())
                };
                self.action(Op::OcrResizeNormalized, &[resized_width, args[0]])?;
                self.action(Op::OcrFillNormalized, &[args[2]])?;
            }
            Method::OcrDetect => {
                let image = self.call(Op::OcrImageSize, &[])?;
                let ratio = 1_f64.min(960. / image[0].max(image[1]));
                let h = self.scalar(
                    Op::PixelRound,
                    &[f64::from((image[1] * ratio) as i32) / 32.],
                )? * 32.;
                let w = self.scalar(
                    Op::PixelRound,
                    &[f64::from((image[0] * ratio) as i32) / 32.],
                )? * 32.;
                self.action(Op::OcrSetDetectorInput, &[h.max(32.), w.max(32.)])?;
                self.action(Op::OcrInferDetector, &[])?;
                self.action(Op::OcrReadDetectionTensor, &[])?;
                let shape = self.call(Op::OcrDetectionShape, &[])?;
                if shape[0] != 4.
                    || shape[1] != 1.
                    || shape[2] != 1.
                    || !self.flag(Op::OcrDetectionFloat, &[])?
                {
                    return self.ocr_fail(2.);
                }
                self.action(Op::OcrDetectionMask, &[])?;
                self.action(Op::OcrFindContours, &[])?;
                let count = (self.scalar(Op::OcrContourCount, &[])? as usize).min(1000);
                for i in 0..count {
                    if i % 16 == 0 {
                        self.ocr_checkpoint()?;
                    }
                    self.action(Op::OcrLoadContour, &[i as f64])?;
                    let side = self.scalar(Op::OcrContourMiniBox, &[])?;
                    if side < 3. || self.scalar(Op::OcrContourScore, &[])? < 0.5 {
                        continue;
                    }
                    self.action(Op::OcrExpandContour, &[])?;
                    if self.scalar(Op::OcrExpandedCount, &[])? < 3. {
                        continue;
                    }
                    if self.scalar(Op::OcrExpandedMiniBox, &[])? < 5. {
                        continue;
                    }
                    let dimensions = self.call(Op::OcrDetectionSize, &[])?;
                    for j in 0..4 {
                        let point = self.call(Op::OcrQuadPoint, &[f64::from(j)])?;
                        // Preserve the intermediate float32 arithmetic.
                        let x = (point[0] as f32 / dimensions[0] as f32 * image[0] as f32) as f64;
                        let y = (point[1] as f32 / dimensions[1] as f32 * image[1] as f32) as f64;
                        let x = self.scalar(Op::PixelRound, &[x])?.clamp(0., image[0] - 1.);
                        let y = self.scalar(Op::PixelRound, &[y])?.clamp(0., image[1] - 1.);
                        self.action(Op::OcrStoreQuadPoint, &[f64::from(j), x, y])?;
                    }
                    let norms = self.call(Op::OcrQuadNorms, &[])?;
                    if norms[0] <= 3. || norms[1] <= 3. {
                        continue;
                    }
                    self.action(Op::OcrAppendQuad, &[])?;
                }
                self.action(Op::OcrSortQuads, &[])?;
                let count = self.scalar(Op::OcrQuadCount, &[])? as i32;
                for i in 0..count - 1 {
                    for j in (0..=i).rev() {
                        let pair = self.call(Op::OcrAdjacentQuads, &[f64::from(j)])?;
                        if pair[0].abs() < 10. && pair[1] < pair[2] {
                            self.action(Op::OcrSwapQuads, &[f64::from(j)])?;
                        } else {
                            break;
                        }
                    }
                }
            }
            Method::OcrOrient => {
                if !self.flag(Op::OcrClassifierReady, &[])? {
                    self.action(Op::OcrCompileClassifier, &[])?;
                    self.action(Op::OcrCreateClassifierRequest, &[])?;
                }
                self.action(Op::OcrSetClassifierInput, &[])?;
                self.action(Op::OcrInferClassifier, &[])?;
                self.action(Op::OcrReadClassifierTensor, &[])?;
                if self.scalar(Op::OcrClassifierSize, &[])? != 2.
                    || !self.flag(Op::OcrClassifierFloat, &[])?
                {
                    return self.ocr_fail(3.);
                }
                let scores = self.call(Op::OcrClassifierScores, &[])?;
                if scores[1] > f64::from(0.9_f32) && scores[1] > scores[0] {
                    self.action(Op::OcrRotateImage, &[])?;
                }
            }
            Method::OcrRecognize => {
                let count = self.scalar(Op::OcrImageCount, &[])? as usize;
                let mut indices = Vec::<(usize, f64)>::new();
                if indices.try_reserve(count).is_err() {
                    self.action(Op::OcrAllocationFailure, &[])?;
                    return Err(HOST_ERROR);
                }
                for i in 0..count {
                    let size = self.call(Op::OcrIndexedImageSize, &[i as f64])?;
                    indices.push((i, size[0] / size[1]));
                }
                indices.sort_unstable_by(|(ai, a), (bi, b)| {
                    a.partial_cmp(b)
                        .unwrap_or(std::cmp::Ordering::Equal)
                        .then(ai.cmp(bi))
                });
                for (i, _) in indices {
                    self.action(Op::OcrAppendImageIndex, &[i as f64])?;
                }
                self.action(Op::OcrAllocateRecognitionResults, &[count as f64])?;
                for begin in (0..count).step_by(6) {
                    self.ocr_checkpoint()?;
                    let end = (begin + 6).min(count);
                    let mut ratio = 320. / 48.;
                    for i in begin..end {
                        let size = self.call(Op::OcrSortedImageSize, &[i as f64])?;
                        let value = size[0] / size[1];
                        if ratio < value {
                            ratio = value;
                        }
                    }
                    let width = (48. * ratio) as i32;
                    if width > 8192 {
                        return self.ocr_fail(4.);
                    }
                    self.action(
                        Op::OcrAllocateBatch,
                        &[(end - begin) as f64, f64::from(width)],
                    )?;
                    for i in begin..end {
                        self.action(
                            Op::OcrNormalizeBatchItem,
                            &[i as f64, begin as f64, f64::from(width)],
                        )?;
                    }
                    self.action(Op::OcrSetRecognizerInput, &[])?;
                    self.action(Op::OcrInferRecognizer, &[])?;
                    self.action(Op::OcrReadRecognitionTensor, &[])?;
                    let shape = self.call(Op::OcrRecognitionShape, &[])?;
                    if shape[0] != 3.
                        || shape[1] != (end - begin) as f64
                        || shape[2] != self.scalar(Op::OcrAlphabetSize, &[])?
                        || !self.flag(Op::OcrRecognitionFloat, &[])?
                    {
                        return self.ocr_fail(5.);
                    }
                    let steps = self.scalar(Op::OcrRecognitionSteps, &[])? as usize;
                    for i in 0..end - begin {
                        self.action(Op::OcrClearRecognizedText, &[])?;
                        let mut score = 0.;
                        let mut selected = 0;
                        let mut last = -1.;
                        for t in 0..steps {
                            if t % 32 == 0 {
                                self.ocr_checkpoint()?;
                            }
                            let token = self.call(Op::OcrBestToken, &[i as f64, t as f64])?;
                            let index = token[0];
                            if index != 0. && index != last {
                                self.action(Op::OcrAppendToken, &[index])?;
                                score += token[1];
                                selected += 1;
                            }
                            last = index;
                        }
                        self.action(
                            Op::OcrStoreRecognizedText,
                            &[
                                (begin + i) as f64,
                                if selected != 0 {
                                    score / f64::from(selected)
                                } else {
                                    0.
                                },
                            ],
                        )?;
                    }
                }
            }
            Method::OcrRead => {
                self.ocr_checkpoint()?;
                if self.flag(Op::OcrImageEmpty, &[])? {
                    return Ok([0.; 3]);
                }
                if !self.flag(Op::OcrImageBgr, &[])? || args[0] < 0. || args[0] > 1. {
                    return self.ocr_fail(6.);
                }
                self.action(Op::OcrLock, &[])?;
                self.action(Op::OcrSetBounds, &[])?;
                if self.flag(Op::OcrBoundsEmpty, &[])? {
                    return Ok([0.; 3]);
                }
                self.action(Op::OcrSetSource, &[])?;
                if args[1] == 0. {
                    self.action(Op::OcrCloneSource, &[])?;
                    if args[2] != 0. {
                        self.action(Op::OcrOrientCrop, &[])?;
                    }
                    self.action(Op::OcrRecognizeSingle, &[])?;
                    if self.scalar(Op::OcrSingleScore, &[])? >= args[0]
                        && !self.flag(Op::OcrSingleEmpty, &[])?
                    {
                        self.action(Op::OcrAppendSingleResult, &[])?;
                    }
                } else {
                    self.action(Op::OcrDetectSource, &[])?;
                    let count = self.scalar(Op::OcrDetectedQuadCount, &[])? as usize;
                    for i in 0..count {
                        self.ocr_checkpoint()?;
                        self.action(Op::OcrCropDetectedQuad, &[i as f64])?;
                        if self.flag(Op::OcrCropEmpty, &[])? {
                            continue;
                        }
                        if args[2] != 0. {
                            self.action(Op::OcrOrientCrop, &[])?;
                        }
                        self.action(Op::OcrStoreCrop, &[i as f64])?;
                    }
                    self.action(Op::OcrRecognizeCrops, &[])?;
                    let count = self.scalar(Op::OcrRecognizedCropCount, &[])? as usize;
                    for i in 0..count {
                        let score = self.scalar(Op::OcrCropScore, &[i as f64])?;
                        if score >= args[0] && !self.flag(Op::OcrCropTextEmpty, &[i as f64])? {
                            self.action(Op::OcrAppendCropResult, &[i as f64])?;
                        }
                    }
                }
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
