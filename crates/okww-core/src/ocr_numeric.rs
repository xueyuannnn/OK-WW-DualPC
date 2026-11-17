pub fn normalized(
    data: &[u8],
    width: usize,
    height: usize,
    stride: usize,
    output_width: usize,
    detector: bool,
    out: &mut [f32],
) {
    let mean = [0.485_f32, 0.456, 0.406];
    let dev = [0.229_f32, 0.224, 0.225];
    for y in 0..height {
        for x in 0..width {
            for c in 0..3 {
                let value = f32::from(data[y * stride + x * 3 + c]);
                out[c * height * output_width + y * output_width + x] = if detector {
                    (value / 255. - mean[c]) / dev[c]
                } else {
                    value / 127.5 - 1.
                };
            }
        }
    }
}
