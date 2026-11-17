//! Gauge segmentation policies. The native host supplies only OpenCV's DFT;
//! scan order, float32 profile arithmetic and strict segment bounds stay here.
use std::ffi::c_void;
#[derive(Debug)]
pub enum Error {
    Allocation,
    Transform,
}

#[repr(C)]
pub struct SpectrumHost {
    pub context: *mut c_void,
    pub transform: unsafe extern "C" fn(*mut c_void, *const f64, usize, *mut f64) -> i32,
}
pub struct Mask<'a> {
    pub bytes: &'a [u8],
    pub width: usize,
    pub height: usize,
    pub stride: usize,
}
impl Mask<'_> {
    fn at(&self, x: usize, y: usize) -> u8 {
        self.bytes[y * self.stride + x]
    }
    fn count(&self, left: usize, width: usize, value: u8) -> usize {
        (0..self.height)
            .map(|y| {
                self.bytes[y * self.stride + left..y * self.stride + left + width]
                    .iter()
                    .filter(|&&v| v == value)
                    .count()
            })
            .sum()
    }
}
fn buffer<T: Clone>(size: usize, value: T) -> Result<Vec<T>, Error> {
    let mut result = Vec::new();
    result
        .try_reserve_exact(size)
        .map_err(|_| Error::Allocation)?;
    result.resize(size, value);
    Ok(result)
}
fn spectrum(
    mask: &Mask<'_>,
    left: usize,
    width: usize,
    host: &SpectrumHost,
) -> Result<Vec<f64>, Error> {
    let mut profile = buffer(width, 0_f32)?;
    let mut sum = 0_f32;
    for (x, value) in profile.iter_mut().enumerate() {
        *value = (0..mask.height)
            .filter(|&y| mask.at(left + x, y) != 0)
            .count() as f32;
        sum += *value;
    }
    let mean = sum / (width as f32);
    let mut input = buffer(width, 0_f64)?;
    for (value, output) in profile.iter().zip(input.iter_mut()) {
        *output = (*value - mean) as f64;
    }
    let mut result = buffer(width, 0_f64)?;
    // SAFETY: the host borrows contiguous live input/output for this synchronous
    // primitive transform and catches exceptions before returning status.
    if unsafe { (host.transform)(host.context, input.as_ptr(), width, result.as_mut_ptr()) } != 1 {
        return Err(Error::Transform);
    }
    Ok(result)
}
pub fn phoebe(
    mask: &Mask<'_>,
    segments: i32,
    min: i32,
    max: i32,
    amplitude: f64,
    host: &SpectrumHost,
) -> Result<i32, Error> {
    if mask.width == 0 || mask.height == 0 || segments < 1 {
        return Ok(0);
    }
    let step = mask.width / (segments as usize);
    if step < 64 {
        return Ok(0);
    }
    let (mut count, mut failed) = (0, false);
    for left in (0..mask.width - step).step_by(step) {
        let white = mask.count(left, step, 255);
        let mut score = false;
        if white > 0 && white < step * mask.height {
            let amplitudes = spectrum(mask, left, step, host)?;
            let peak = amplitudes[1..]
                .iter()
                .copied()
                .fold(f64::NEG_INFINITY, f64::max);
            score = (min <= step as i32 && step as i32 <= max) || peak >= amplitude;
        }
        if !failed && score {
            count += 1;
        }
        if !score {
            failed = true;
        }
    }
    Ok(count)
}
// The native host's general gauge scans from the final segment backwards and
// tests the actual spectral peak. Phoebe's script-specific policy above has
// different bounds and a width test; preserve the two original algorithms.
pub fn native_segments(
    mask: &Mask<'_>,
    segments: i32,
    min: i32,
    max: i32,
    amplitude: f64,
    host: &SpectrumHost,
) -> Result<i32, Error> {
    if mask.width == 0 || mask.height == 0 || segments < 1 {
        return Ok(0);
    }
    let step = mask.width / segments as usize;
    if step < 64 {
        return Ok(0);
    }
    for count in (1..=segments).rev() {
        let left = step * (count as usize - 1);
        let white = mask.count(left, step, 255);
        if white == 0 || white == mask.height * step {
            continue;
        }
        let values = spectrum(mask, left, step, host)?;
        let mut peak = 0.;
        let mut frequency = 0;
        for (index, value) in values.iter().enumerate().skip(1) {
            if *value > peak {
                peak = *value;
                frequency = index as i32;
            }
        }
        if (frequency >= min && frequency <= max) || peak >= amplitude {
            return Ok(count);
        }
    }
    Ok(0)
}
pub fn stripes(mask: &Mask<'_>, host: &SpectrumHost) -> Result<(i32, i32), Error> {
    if mask.width < 64 || mask.height == 0 {
        return Ok((-1, -1));
    }
    let white = mask.count(0, mask.width, 255);
    let black = mask.count(0, mask.width, 0);
    if white + black == 0 {
        return Ok((-1, -1));
    }
    if white == 0 {
        return Ok((0, 0));
    }
    let width = mask.width;
    let threshold = (width as f64 * 0.009) as usize;
    let mut clean = buffer(width * mask.height, 0_u8)?;
    for y in 0..mask.height {
        let row = &mask.bytes[y * mask.stride..y * mask.stride + width];
        let target = &mut clean[y * width..(y + 1) * width];
        target.copy_from_slice(row);
        if threshold >= 1 {
            let mut x = 0;
            while x < width {
                if row[x] != 255 {
                    x += 1;
                    continue;
                }
                let start = x;
                while x < width && row[x] == 255 {
                    x += 1;
                }
                if x - start < threshold {
                    target[start..x].fill(0);
                }
            }
        }
    }
    let window = 8.max((width as f64 * 0.045) as usize);
    let step = 1.max((width as f64 * 0.012) as usize);
    let clean = Mask {
        bytes: &clean,
        width,
        height: mask.height,
        stride: width,
    };
    let slots = if window <= width {
        (width - window) / step + 1
    } else {
        0
    };
    let mut scores = buffer(slots, 0_f64)?;
    let mut ratios = buffer(slots, 0_f64)?;
    let mut positions = buffer(slots, 0_usize)?;
    for (i, x) in (0..=width - window).step_by(step).enumerate() {
        let ratio = clean.count(x, window, 255) as f64 / (window * mask.height) as f64;
        let score = if (0.05..=0.75).contains(&ratio) {
            // OpenCV compare(part,255) produces binary bytes, even for input
            // gray values other than 0/255; preserve that conversion.
            let mut binary = buffer(window * mask.height, 0_u8)?;
            for y in 0..mask.height {
                for column in 0..window {
                    binary[y * window + column] = if clean.at(x + column, y) == 255 {
                        255
                    } else {
                        0
                    };
                }
            }
            let part = Mask {
                bytes: &binary,
                width: window,
                height: mask.height,
                stride: window,
            };
            spectrum(&part, 0, window, host)?[1..window / 2]
                .iter()
                .copied()
                .fold(f64::NEG_INFINITY, f64::max)
        } else {
            0.
        };
        scores[i] = score;
        ratios[i] = ratio;
        positions[i] = x;
    }
    if scores.is_empty() {
        return Ok((-1, -1));
    }
    let maximum = scores.iter().copied().fold(f64::NEG_INFINITY, f64::max);
    let cutoff = 1.5_f64.max(maximum * 0.45);
    if maximum < 0.001 {
        return Ok((0, 0));
    }
    let mut end = 0;
    for i in 0..scores.len() {
        if scores[i] >= cutoff {
            end = i;
        } else if ratios[i] < 0.375 {
            if scores[i + 1..].iter().any(|&value| value > cutoff) {
                return Ok((-2, -2));
            }
            break;
        }
    }
    Ok((
        0,
        if end == 0 {
            0
        } else if positions[end] + window + step >= width {
            width as i32
        } else {
            positions[end] as i32
        },
    ))
}
