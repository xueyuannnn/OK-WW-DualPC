//! Original YOLO letterbox and class-agnostic NMS. Tensor memory is borrowed.
use std::cmp::Ordering;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Detection {
    pub x: i32,
    pub y: i32,
    pub width: i32,
    pub height: i32,
    pub category: i32,
    pub reserved: i32,
    pub confidence: f64,
}
#[repr(C)]
pub struct Prediction {
    pub scores: *const f32,
    pub length: usize,
    pub channels: usize,
    pub rows: usize,
    pub gain: f64,
    pub threshold: f64,
    pub left: i32,
    pub top: i32,
    pub label: i32,
    pub checkpoints: i32,
}
#[repr(C)]
pub struct Checkpoint {
    pub context: *mut std::ffi::c_void,
    pub invoke: unsafe extern "C" fn(*mut std::ffi::c_void) -> i32,
}
impl Checkpoint {
    fn check(&self, active: bool) -> Result<(), i32> {
        if active && unsafe { (self.invoke)(self.context) } != 1 {
            Err(-2)
        } else {
            Ok(())
        }
    }
}
pub struct Detections(pub Vec<Detection>);
pub fn layout(width: i32, height: i32, target_width: i32, target_height: i32) -> Option<[f64; 5]> {
    let gain = (f64::from(target_width) / f64::from(width))
        .min(f64::from(target_height) / f64::from(height));
    let w = crate::execution::pixel_round(f64::from(width) * gain)?;
    let h = crate::execution::pixel_round(f64::from(height) * gain)?;
    let left = crate::execution::pixel_round(f64::from(target_width - w) / 2. - 0.1)?;
    let top = crate::execution::pixel_round(f64::from(target_height - h) / 2. - 0.1)?;
    Some([
        gain,
        f64::from(w),
        f64::from(h),
        f64::from(left),
        f64::from(top),
    ])
}
pub fn tensor(data: &[u8], width: usize, height: usize, stride: usize, out: &mut [f32]) {
    for y in 0..height {
        for x in 0..width {
            for c in 0..3 {
                out[c * height * width + y * width + x] =
                    f32::from(data[y * stride + x * 3 + 2 - c]) / 255.;
            }
        }
    }
}
fn screen_less(a: Detection, b: Detection) -> bool {
    let row = !(a.y > b.y + b.height || a.y + a.height < b.y);
    let mut delta = if row { a.x - b.x } else { a.y - b.y };
    if delta == 0 {
        delta = if row { a.y - b.y } else { a.x - b.x };
    }
    if delta == 0 {
        delta = (a.confidence * 1000. - b.confidence * 1000.) as i32;
    }
    if delta == 0 {
        delta = (a.category != 0) as i32 - (b.category != 0) as i32;
    }
    delta < 0
}
pub fn postprocess(
    p: &Prediction,
    scores: &[f32],
    checkpoint: &Checkpoint,
) -> Result<Detections, i32> {
    let mut candidates = Vec::<(Detection, usize)>::new();
    for row in 0..p.rows {
        if row % 256 == 0 {
            checkpoint.check(p.checkpoints != 0)?;
        }
        let mut confidence = scores[4 * p.rows + row];
        let mut category = 0;
        for c in 5..p.channels {
            if scores[c * p.rows + row] > confidence {
                confidence = scores[c * p.rows + row];
                category = c - 4;
            }
        }
        if !confidence.is_finite()
            || f64::from(confidence) <= p.threshold
            || (p.label != -1 && category as i32 != p.label)
        {
            continue;
        }
        let x = scores[row] - p.left as f32;
        let y = scores[p.rows + row] - p.top as f32;
        let bw = scores[2 * p.rows + row];
        let bh = scores[3 * p.rows + row];
        let ratio = p.gain as f32;
        let values = [
            (x - bw / 2.) / ratio,
            (y - bh / 2.) / ratio,
            bw / ratio,
            bh / ratio,
        ];
        if values.iter().any(|v| !v.is_finite() || v.abs() > 10000000.) {
            return Err(-1);
        }
        let [x, y, width, height] = values.map(|v| v as i32);
        if width > 0 && height > 0 {
            candidates.try_reserve(1).map_err(|_| -3)?;
            candidates.push((
                Detection {
                    x,
                    y,
                    width,
                    height,
                    category: category as i32,
                    reserved: 0,
                    confidence: f64::from(confidence),
                },
                row,
            ));
        }
    }
    // Original score sort is stable. A unique row tie-breaker preserves that
    // order using an allocation-free sort and a strict total comparator.
    candidates.sort_unstable_by(|(a, ai), (b, bi)| {
        b.confidence
            .partial_cmp(&a.confidence)
            .unwrap_or(Ordering::Equal)
            .then(ai.cmp(bi))
    });
    let mut kept = Vec::<Detection>::new();
    for (candidate, _) in candidates {
        checkpoint.check(p.checkpoints != 0)?;
        let mut suppressed = false;
        for prior in &kept {
            let width = (candidate.x + candidate.width).min(prior.x + prior.width)
                - candidate.x.max(prior.x);
            let height = (candidate.y + candidate.height).min(prior.y + prior.height)
                - candidate.y.max(prior.y);
            let area = if width > 0 && height > 0 {
                f64::from(width) * f64::from(height)
            } else {
                0.
            };
            let total = f64::from(candidate.width) * f64::from(candidate.height)
                + f64::from(prior.width) * f64::from(prior.height)
                - area;
            if total > 0. && area / total > 0.45 {
                suppressed = true;
                break;
            }
        }
        if !suppressed {
            kept.try_reserve(1).map_err(|_| -3)?;
            kept.push(candidate);
        }
    }
    // Preserve the original natural initial run and stable binary insertion.
    // Its overlapping-row relation is not a strict weak ordering.
    let mut run = 2.min(kept.len());
    if kept.len() > 1 {
        let descending = screen_less(kept[1], kept[0]);
        while run < kept.len() && screen_less(kept[run], kept[run - 1]) == descending {
            run += 1;
        }
        if descending {
            kept[..run].reverse();
        }
    }
    for i in run..kept.len() {
        let item = kept[i];
        let (mut low, mut high) = (0, i);
        while low < high {
            let mid = (low + high) / 2;
            if screen_less(item, kept[mid]) {
                high = mid;
            } else {
                low = mid + 1;
            }
        }
        for j in (low + 1..=i).rev() {
            kept[j] = kept[j - 1];
        }
        kept[low] = item;
    }
    Ok(Detections(kept))
}
