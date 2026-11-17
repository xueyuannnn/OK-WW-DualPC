//! User template catalogue rules. Qt retains images, JSON and atomic files.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn store_fail<T>(&self, code: f64) -> Result<T> {
        self.action(Op::StoreFailure, &[code])?;
        Err(HOST_ERROR)
    }
    fn store_labels(&self) -> Result {
        let n = self.scalar(Op::StoreCategoryCount, &[])? as usize;
        for i in 0..n {
            self.action(Op::StoreIndexCategory, &[i as f64])?;
        }
        Ok(())
    }
    pub(in crate::services) fn run_template_store(
        &self,
        method: Method,
        args: &[f64],
    ) -> Result<[f64; 3]> {
        match method {
            Method::StoreNextId => {
                let n = self.scalar(Op::StoreItemCount, &[])? as usize;
                let mut next = 1i32;
                for i in 0..n {
                    let id = self.scalar(Op::StoreItemId, &[i as f64])? as i32;
                    next = next.max(id.wrapping_add(1));
                }
                return Ok([f64::from(next), 0., 0.]);
            }
            Method::StoreWithoutImage => {
                let n = self.scalar(Op::StoreItemCount, &[])? as usize;
                for i in 0..n {
                    if !self.flag(Op::StoreItemMatchesImage, &[i as f64])? {
                        self.action(Op::StoreAppendItem, &[i as f64])?;
                    }
                }
            }
            Method::StoreDocument => {
                if self.flag(Op::StoreDocumentExists, &[])? {
                    self.action(Op::StoreReadDocument, &[])?;
                } else {
                    self.action(Op::StoreEmptyDocument, &[])?;
                }
            }
            Method::StoreImagePath => {
                if self.flag(Op::StoreNameEmpty, &[])?
                    || !self.flag(Op::StoreNameIsFilename, &[])?
                    || self.flag(Op::StoreNameHasColon, &[])?
                    || self.flag(Op::StoreNameHasBackslash, &[])?
                {
                    return self.store_fail(0.);
                }
                self.action(Op::StoreReturnImagePath, &[])?;
            }
            Method::StoreImages => {
                self.action(Op::StoreLoadDocument, &[])?;
                self.store_labels()?;
                let n = self.scalar(Op::StoreImageCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::StoreSelectImage, &[i as f64])?;
                    self.action(Op::StoreImageSetPath, &[])?;
                    self.action(Op::StoreClearNames, &[])?;
                    let a = self.scalar(Op::StoreAnnotationCount, &[])? as usize;
                    for j in 0..a {
                        self.action(Op::StoreSelectAnnotation, &[j as f64])?;
                        if self.flag(Op::StoreAnnotationImageEqual, &[])? {
                            self.action(Op::StoreAppendLabel, &[])?;
                        }
                    }
                    self.action(Op::StoreImageSetLabels, &[])?;
                    self.action(Op::StoreAppendImageResult, &[])?;
                }
            }
            Method::StoreAnnotations => {
                self.action(Op::StoreLoadDocument, &[])?;
                self.store_labels()?;
                let n = self.scalar(Op::StoreAnnotationCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::StoreSelectAnnotation, &[i as f64])?;
                    if self.flag(Op::StoreAnnotationMatchesId, &[])? {
                        self.action(Op::StoreRegionSetName, &[])?;
                        self.action(Op::StoreAppendRegionResult, &[])?;
                    }
                }
            }
            Method::StoreAdd => {
                if self.flag(Op::StoreInputImageNull, &[])? {
                    return self.store_fail(1.);
                }
                let size = self.call(Op::StoreInputImageSize, &[])?;
                if size[0] > 16384. || size[1] > 16384. {
                    return self.store_fail(1.);
                }
                self.action(Op::StoreLoadDocument, &[])?;
                self.action(Op::StoreReadImages, &[])?;
                self.action(Op::StoreNextImageId, &[])?;
                self.action(Op::StoreNewImageName, &[])?;
                self.action(Op::StoreCreateDirectory, &[])?;
                self.action(Op::StoreOpenImageFile, &[])?;
                if !self.flag(Op::StoreImageFileOpen, &[])?
                    || !self.flag(Op::StoreSaveInputImage, &[])?
                    || !self.flag(Op::StoreCommitImage, &[])?
                {
                    return self.store_fail(2.);
                }
                self.action(Op::StoreAppendNewImage, &[])?;
                self.action(Op::StoreSetImages, &[])?;
                if let Err(error) = self.action(Op::StoreWriteDocument, &[]) {
                    // Cleanup is best effort and cannot replace the original cause.
                    let _ = self.action(Op::StoreRemoveImageFile, &[]);
                    return Err(error);
                }
                self.action(Op::StoreReturnImageId, &[])?;
            }
            Method::StoreSaveAnnotations => {
                self.action(Op::StoreLoadDocument, &[])?;
                let n = self.scalar(Op::StoreImageCount, &[])? as usize;
                for i in 0..n {
                    if self.flag(Op::StoreImageMatchesId, &[i as f64])? {
                        self.action(Op::StoreSelectSourceImage, &[i as f64])?;
                    }
                }
                if self.flag(Op::StoreSourceEmpty, &[])? {
                    return self.store_fail(3.);
                }
                self.action(Op::StoreExcludeAnnotations, &[])?;
                self.action(Op::StoreReadCategories, &[])?;
                let n = self.scalar(Op::StoreWorkingCategoryCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::StoreIndexLabel, &[i as f64])?;
                }
                let n = self.scalar(Op::StoreWorkingAnnotationCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::StoreIndexUsedLabel, &[i as f64])?;
                }
                let n = self.scalar(Op::StoreInputRegionCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::StoreSelectInputRegion, &[i as f64])?;
                    if self.flag(Op::StoreRegionNameEmpty, &[])?
                        || self.flag(Op::StoreRegionNameUsed, &[])?
                        || self.scalar(Op::StoreRegionBoxCount, &[])? != 4.
                    {
                        return self.store_fail(4.);
                    }
                    for j in 0..4 {
                        if !self.flag(Op::StoreCoordinateDouble, &[f64::from(j)])? {
                            return self.store_fail(5.);
                        }
                        let coordinate = self.call(Op::StoreCoordinate, &[f64::from(j)])?;
                        if !coordinate[0].is_finite() || coordinate[0] != coordinate[1] {
                            return self.store_fail(5.);
                        }
                    }
                    let xy = self.call(Op::StoreRegionXY, &[])?;
                    let wh = self.call(Op::StoreRegionWH, &[])?;
                    let size = self.call(Op::StoreSourceSize, &[])?;
                    let (x, y, w, h) = (xy[0] as i32, xy[1] as i32, wh[0] as i32, wh[1] as i32);
                    if x < 0
                        || y < 0
                        || w <= 0
                        || h <= 0
                        || x.wrapping_add(w) > size[0] as i32
                        || y.wrapping_add(h) > size[1] as i32
                    {
                        return self.store_fail(6.);
                    }
                    self.action(Op::StoreMarkUsedName, &[])?;
                    if !self.flag(Op::StoreLabelExists, &[])? {
                        self.action(Op::StoreAppendCategory, &[])?;
                    }
                    self.action(Op::StoreAppendAnnotation, &[f64::from(w.wrapping_mul(h))])?;
                }
                self.action(Op::StoreSetAnnotations, &[])?;
                self.action(Op::StoreSetCategories, &[])?;
                self.action(Op::StoreWriteDocument, &[])?;
            }
            Method::StoreRemove => {
                self.action(Op::StoreLoadDocument, &[])?;
                let n = self.scalar(Op::StoreImageCount, &[])? as usize;
                for i in 0..n {
                    if self.flag(Op::StoreImageMatchesId, &[i as f64])? {
                        self.action(Op::StoreSelectRemovalPath, &[i as f64])?;
                    }
                }
                if self.flag(Op::StoreRemovalPathEmpty, &[])? {
                    return self.store_fail(3.);
                }
                self.action(Op::StoreCreateTrash, &[])?;
                self.action(Op::StoreNewTrashName, &[])?;
                if self.flag(Op::StoreRemovalFileExists, &[])?
                    && !self.flag(Op::StoreMoveToTrash, &[])?
                {
                    return self.store_fail(7.);
                }
                self.action(Op::StoreExcludeImages, &[])?;
                self.action(Op::StoreSetImages, &[])?;
                self.action(Op::StoreExcludeAnnotations, &[])?;
                self.action(Op::StoreSetAnnotations, &[])?;
                if let Err(error) = self.action(Op::StoreWriteDocument, &[]) {
                    let _ = self.action(Op::StoreRestoreFromTrash, &[]);
                    return Err(error);
                }
            }
            Method::StorePublish => {
                self.action(Op::StoreLoadDocument, &[])?;
                if self.scalar(Op::StoreAnnotationCount, &[])? == 0. {
                    return self.store_fail(8.);
                }
                self.action(Op::StoreCreateAssetDirectory, &[])?;
                let n = self.scalar(Op::StoreImageCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::StoreSelectImage, &[i as f64])?;
                    self.action(Op::StoreLoadSelectedRegions, &[])?;
                    let regions = self.scalar(Op::StoreSelectedRegionCount, &[])? as usize;
                    if regions == 0 {
                        continue;
                    }
                    self.action(Op::StoreLoadSourcePixels, &[])?;
                    if self.flag(Op::StoreSourcePixelsNull, &[])? {
                        return self.store_fail(9.);
                    }
                    self.action(Op::StoreCreateMaskedImage, &[])?;
                    self.action(Op::StoreFillMaskedBlack, &[])?;
                    self.action(Op::StoreCreatePainter, &[])?;
                    for j in 0..regions {
                        self.action(Op::StoreDrawRegion, &[j as f64])?;
                    }
                    self.action(Op::StoreEndPainter, &[])?;
                    self.action(Op::StoreMarkCompressed, &[])?;
                    self.action(Op::StoreNewPublishedName, &[])?;
                    self.action(Op::StoreOpenPublishFile, &[])?;
                    if !self.flag(Op::StorePublishFileOpen, &[])?
                        || !self.flag(Op::StoreSaveMaskedImage, &[])?
                        || !self.flag(Op::StoreCommitPublishImage, &[])?
                    {
                        return self.store_fail(10.);
                    }
                    self.action(Op::StoreImageSetPublishedName, &[])?;
                    self.action(Op::StoreAppendWorkingImage, &[])?;
                }
                self.action(Op::StoreSetImages, &[])?;
                self.action(Op::StoreWritePublishedDocument, &[])?;
                if !self.flag(Op::StoreEnumFileEmpty, &[])? {
                    self.action(Op::StoreEnumHeader, &[])?;
                    let n = self.scalar(Op::StoreCategoryCount, &[])? as usize;
                    for i in 0..n {
                        self.action(Op::StoreEnumSelectLabel, &[i as f64])?;
                        self.action(Op::StoreEnumSanitizeName, &[])?;
                        if self.flag(Op::StoreEnumNameEmpty, &[])?
                            || self.flag(Op::StoreEnumFirstDigit, &[])?
                            || self.flag(Op::StoreEnumKeyword, &[])?
                        {
                            self.action(Op::StoreEnumPrefix, &[])?;
                        }
                        if self.flag(Op::StoreEnumNameUsed, &[])? {
                            self.action(Op::StoreEnumSuffixId, &[])?;
                        }
                        self.action(Op::StoreEnumMarkName, &[])?;
                        self.action(Op::StoreEnumAppendLine, &[])?;
                    }
                    self.action(Op::StoreEnumOpenFile, &[])?;
                    self.action(Op::StoreEnumEncodeUtf8, &[])?;
                    if !self.flag(Op::StoreEnumFileOpen, &[])?
                        || !self.flag(Op::StoreEnumWriteBytes, &[])?
                        || !self.flag(Op::StoreEnumCommit, &[])?
                    {
                        return self.store_fail(11.);
                    }
                }
            }
            _ => return Err(INVALID_ACTION),
        }
        let _ = args;
        Ok([0.; 3])
    }
}
