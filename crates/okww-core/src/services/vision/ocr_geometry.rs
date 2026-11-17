use crate::services::{Host, Method, Op, Result, INVALID_ACTION};
fn minimum(a: f64, b: f64) -> f64 {
    if b < a {
        b
    } else {
        a
    }
}
fn maximum(a: f64, b: f64) -> f64 {
    if a < b {
        b
    } else {
        a
    }
}
impl Host {
    pub(in crate::services) fn run_ocr_geometry(&self, method: Method) -> Result<[f64; 3]> {
        let mut out = [0.; 3];
        match method {
            Method::OcrMiniBox => {
                self.action(Op::OcrMinimumAreaRect, &[])?;
                self.action(Op::OcrSortRectPoints, &[])?;
                let y = self.call(Op::OcrRectPointYs, &[])?;
                let a = if y[1] > y[0] { 0 } else { 1 };
                let d = 1 - a;
                let y = self.call(Op::OcrOtherRectPointYs, &[])?;
                let b = if y[1] > y[0] { 2 } else { 3 };
                let c = 5 - b;
                self.action(
                    Op::OcrOrderRectPoints,
                    &[f64::from(a), f64::from(b), f64::from(c)],
                )?;
                self.action(Op::OcrOrderLastRectPoint, &[f64::from(d)])?;
                let dimensions = self.call(Op::OcrRotatedRectSize, &[])?;
                out[0] = minimum(dimensions[0], dimensions[1]);
            }
            Method::OcrBoxScore => {
                let first = self.call(Op::OcrGeometryPoint, &[0.])?;
                let (mut min_x, mut max_x, mut min_y, mut max_y) =
                    (first[0], first[0], first[1], first[1]);
                for i in 0..4 {
                    let p = self.call(Op::OcrGeometryPoint, &[f64::from(i)])?;
                    min_x = minimum(min_x, p[0]);
                    max_x = maximum(max_x, p[0]);
                    min_y = minimum(min_y, p[1]);
                    max_y = maximum(max_y, p[1]);
                }
                let size = self.call(Op::OcrGeometryScoreSize, &[])?;
                let x = min_x.floor().clamp(0., size[0] - 1.);
                let y = min_y.floor().clamp(0., size[1] - 1.);
                let r = max_x.ceil().clamp(0., size[0] - 1.);
                let b = max_y.ceil().clamp(0., size[1] - 1.);
                self.action(Op::OcrSetScoreArea, &[x, y, r - x + 1.])?;
                self.action(Op::OcrSetScoreAreaHeight, &[b - y + 1.])?;
                self.action(Op::OcrCreateScoreMask, &[])?;
                for i in 0..4 {
                    let p = self.call(Op::OcrGeometryPoint, &[f64::from(i)])?;
                    let px = (p[0] as f32 - x as f32) as i32;
                    let py = (p[1] as f32 - y as f32) as i32;
                    self.action(Op::OcrAppendScorePoint, &[f64::from(px), f64::from(py)])?;
                }
                self.action(Op::OcrFillScoreMask, &[])?;
                out[0] = self.scalar(Op::OcrMeanScore, &[])?;
            }
            Method::OcrUnclip => {
                self.action(Op::OcrGeometryPolygon, &[])?;
                let perimeter = self.scalar(Op::OcrPolygonPerimeter, &[])?;
                let area = self.scalar(Op::OcrPolygonArea, &[])?.abs();
                if perimeter <= 0. {
                    return Ok(out);
                }
                for i in 0..4 {
                    let p = self.call(Op::OcrGeometryPoint, &[f64::from(i)])?;
                    self.action(Op::OcrAppendClipperPoint, &p[..2])?;
                }
                self.action(Op::OcrAddClipperPath, &[])?;
                self.action(Op::OcrExpandClipperPath, &[area * 1.6 / perimeter])?;
                self.action(Op::OcrReadExpandedPath, &[])?;
            }
            Method::OcrTextCrop => {
                let norms = self.call(Op::OcrCropWidthNorms, &[])?;
                let w = maximum(norms[0], norms[1]) as i32;
                let norms = self.call(Op::OcrCropHeightNorms, &[])?;
                let h = maximum(norms[0], norms[1]) as i32;
                if w <= 0 || h <= 0 {
                    return Ok(out);
                }
                self.action(Op::OcrWarpCrop, &[f64::from(w), f64::from(h)])?;
                if f64::from(h) / f64::from(w) >= 1.5 {
                    self.action(Op::OcrRotateTallCrop, &[])?;
                }
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok(out)
    }
}
