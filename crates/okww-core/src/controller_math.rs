pub fn elapsed_ms(now: i64, began: i64) -> f64 {
    now.wrapping_sub(began) as f64 / 1e6
}
pub fn map_point(point: [i32; 2], image: [i32; 2], rect: [i32; 4]) -> Option<[i32; 2]> {
    if image[0] == 0 || image[1] == 0 {
        return None;
    }
    Some([
        rect[0]
            .wrapping_add((i64::from(point[0]) * i64::from(rect[2]) / i64::from(image[0])) as i32),
        rect[1]
            .wrapping_add((i64::from(point[1]) * i64::from(rect[3]) / i64::from(image[1])) as i32),
    ])
}
