pub fn p010(
    data: &[u8],
    width: usize,
    height: usize,
    stride: usize,
    out: &mut [u8],
    output_stride: usize,
) {
    for y in 0..height {
        for x in 0..width {
            let i = y * stride + x * 2;
            out[y * output_stride + x] = (u16::from_ne_bytes([data[i], data[i + 1]]) >> 8) as u8;
        }
    }
}
