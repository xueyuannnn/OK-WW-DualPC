pub fn fourcc(value: u32, bits: i32) -> ([u8; 4], usize) {
    let bytes = value.to_ne_bytes();
    if bytes.iter().all(|&b| (32..127).contains(&b)) {
        return (bytes, 4);
    }
    match bits {
        32 => (*b"RGB4", 4),
        24 => (*b"RGB3", 4),
        _ => ([0; 4], 0),
    }
}
pub fn format(name: &[u16]) -> i32 {
    let names = [
        ("NV12", 2),
        ("P010", 3),
        ("YUY2", 4),
        ("UYVY", 5),
        ("I420", 6),
        ("IYUV", 6),
        ("RGB4", 1),
        ("MJPG", 7),
        ("RGB3", 0),
    ];
    names
        .iter()
        .find(|(s, _)| name.iter().copied().eq(s.bytes().map(u16::from)))
        .map(|(_, code)| *code)
        .unwrap_or(-1)
}
pub fn rate(period: i64) -> f64 {
    let period = period as f64;
    let v = 1e7 / period;
    let rounded = v.round();
    if rounded > 0. && (period - 1e7 / rounded).abs() <= 1. {
        rounded
    } else {
        (v * 1000.).round() / 1000.
    }
}
pub fn payload(length: usize, stride: i32, height: i32, format: i32) -> i32 {
    let mut expected = (stride as usize).wrapping_mul(height as usize);
    if matches!(format, 2 | 3 | 6) {
        expected = expected.wrapping_mul(3) / 2;
    }
    if format != 7 && length < expected {
        1
    } else if length > 256 * 1024 * 1024 {
        2
    } else {
        0
    }
}
pub fn fps(count: usize, back: i64, front: i64) -> f64 {
    count.wrapping_sub(1) as f64 * 1e9 / back.wrapping_sub(front) as f64
}
pub fn stride(width: i32, format: i32) -> i32 {
    match format {
        0 => width.wrapping_mul(3).wrapping_add(3) & !3,
        1 => width.wrapping_mul(4),
        3..=5 => width.wrapping_mul(2),
        _ => width,
    }
}
pub fn bottom(height: i32, format: i32) -> bool {
    height > 0 && matches!(format, 0 | 1)
}
pub fn timer_due(remaining: i64) -> i64 {
    -(remaining / 100).max(1)
}
pub fn recovery_expired(now: i64, began: i64) -> bool {
    now.wrapping_sub(began) > 120_000_000_000
}
