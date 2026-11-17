pub fn simulation(width: i32, height: i32, stride: i32, output: &mut [u8]) -> bool {
    let Some(minimum_stride) = width.checked_mul(3) else {
        return false;
    };
    if width <= 0 || height <= 0 || stride < minimum_stride {
        return false;
    }
    let (w, h, s) = (width as usize, height as usize, stride as usize);
    if s.checked_mul(h).is_none_or(|n| n > output.len()) {
        return false;
    }
    for y in 0..h {
        for x in 0..w {
            let i = y * s + x * 3;
            output[i] = (x * 255 / w) as u8;
            output[i + 1] = (y * 255 / h) as u8;
            output[i + 2] = 80;
        }
    }
    true
}
