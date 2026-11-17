//! Pixel statistics used by the original weekly card selector.
fn median(histogram: &[usize; 256], total: usize) -> f64 {
    let left = (total - 1) / 2;
    let right = total / 2;
    let mut count = 0;
    let mut first = None;
    for (value, &frequency) in histogram.iter().enumerate() {
        count += frequency;
        if first.is_none() && left < count {
            first = Some(value);
        }
        if right < count {
            return (first.unwrap_or(value) + value) as f64 * 0.5;
        }
    }
    0.
}
pub fn pixels(data: &[u8], width: usize, height: usize, stride: usize, kind: i32) -> i32 {
    if width == 0 || height == 0 {
        return 0;
    }
    let Some(row) = width.checked_mul(3) else {
        return 0;
    };
    let Some(length) = (height - 1)
        .checked_mul(stride)
        .and_then(|offset| offset.checked_add(row))
    else {
        return 0;
    };
    let Some(total) = width.checked_mul(height) else {
        return 0;
    };
    if stride < row || length > data.len() {
        return 0;
    }
    if kind == 1 {
        let mut selected = 0_i32;
        for y in 0..height {
            for p in data[y * stride..y * stride + row].as_chunks::<3>().0 {
                selected = selected.wrapping_add(
                    (p[1] > 150
                        && p[1] as f64 > p[2] as f64 * 1.15
                        && p[1] as f64 > p[0] as f64 * 1.25) as i32,
                );
            }
        }
        return selected;
    }
    let mut hues = [0_usize; 256];
    let mut saturations = [0_usize; 256];
    for y in 0..height {
        for p in data[y * stride..y * stride + row].as_chunks::<3>().0 {
            hues[p[0] as usize] += 1;
            saturations[p[1] as usize] += 1;
        }
    }
    let hue = median(&hues, total);
    let saturation = median(&saturations, total);
    if (12. ..=45.).contains(&hue) && saturation >= 100. {
        3
    } else if (118. ..=165.).contains(&hue) && saturation >= 45. {
        2
    } else if (75. ..118.).contains(&hue) && saturation >= 85. {
        1
    } else {
        0
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn two_middle_pixels_keep_the_original_half_step() {
        assert_eq!(pixels(&[11, 100, 0, 12, 100, 0], 2, 1, 6, 0), 0);
        assert_eq!(pixels(&[12, 99, 0, 12, 100, 0], 2, 1, 6, 0), 0);
        assert_eq!(pixels(&[12, 100, 0, 45, 100, 0], 2, 1, 6, 0), 3);
        assert_eq!(pixels(&[117, 100, 0, 118, 100, 0], 2, 1, 6, 0), 1);
    }
    #[test]
    fn green_selection_uses_strict_thresholds_and_bgr_order() {
        assert_eq!(
            pixels(
                &[0, 150, 0, 0, 151, 0, 255, 200, 0, 0, 200, 255],
                4,
                1,
                12,
                1
            ),
            1
        );
        assert_eq!(
            pixels(&[0, 151, 0, 255, 255, 255, 0, 151, 0], 1, 2, 6, 1),
            2
        );
        assert_eq!(pixels(&[], 1, 1, 3, 0), 0);
    }
}
