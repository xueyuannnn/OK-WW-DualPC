//! RGB32 fallback conversion used by the unchanged Qt preview.
// Mirrors the flat borrowed-buffer ABI without allocating an intermediate image.
#[allow(clippy::too_many_arguments)]
pub fn rgb32(
    data: &[u8],
    width: usize,
    height: usize,
    stride: usize,
    format: i32,
    bottom_up: bool,
    out: &mut [u8],
    output_stride: usize,
) -> Option<()> {
    if output_stride < width.checked_mul(4)? || out.len() < height.checked_mul(output_stride)? {
        return None;
    }
    for y in 0..height {
        for x in 0..width {
            let bgr = match format {
                0 | 1 => {
                    let row = if bottom_up { height - 1 - y } else { y };
                    let channels = if format == 0 { 3 } else { 4 };
                    let start = row
                        .checked_mul(stride)?
                        .checked_add(x.checked_mul(channels)?)?;
                    [
                        *data.get(start)?,
                        *data.get(start.checked_add(1)?)?,
                        *data.get(start.checked_add(2)?)?,
                    ]
                }
                3 => {
                    let read = |offset: usize| -> Option<i32> {
                        Some(i32::from(
                            u16::from_ne_bytes([
                                *data.get(offset)?,
                                *data.get(offset.checked_add(1)?)?,
                            ]) >> 6,
                        ))
                    };
                    let row = y.checked_mul(stride)?;
                    let uv = stride
                        .checked_mul(height)?
                        .checked_add((y / 2).checked_mul(stride)?)?;
                    let yy = i64::from((read(row.checked_add(x.checked_mul(2)?)?)? - 64).max(0))
                        * 1220542;
                    let u = i64::from(read(uv.checked_add((x & !1).checked_mul(2)?)?)? - 512);
                    let v = i64::from(read(uv.checked_add(((x & !1) + 1).checked_mul(2)?)?)? - 512);
                    let clip = |v: i64| -> u8 { ((v + (1 << 21)) >> 22).clamp(0, 255) as u8 };
                    [
                        clip(yy + 2116026 * u),
                        clip(yy - 409993 * u - 852492 * v),
                        clip(yy + 1673527 * v),
                    ]
                }
                _ => {
                    let (luma, u, v) = match format {
                        2 => {
                            let uv = stride
                                .checked_mul(height)?
                                .checked_add((y / 2).checked_mul(stride)?)?
                                .checked_add(x & !1)?;
                            (
                                *data.get(y.checked_mul(stride)?.checked_add(x)?)?,
                                *data.get(uv)?,
                                *data.get(uv.checked_add(1)?)?,
                            )
                        }
                        6 => {
                            let uv = stride.checked_mul(height)?;
                            let offset = (y / 2).checked_mul(stride / 2)?.checked_add(x / 2)?;
                            (
                                *data.get(y.checked_mul(stride)?.checked_add(x)?)?,
                                *data.get(uv.checked_add(offset)?)?,
                                *data.get(
                                    uv.checked_add(stride.checked_mul(height)? / 4)?
                                        .checked_add(offset)?,
                                )?,
                            )
                        }
                        _ => {
                            let base = y
                                .checked_mul(stride)?
                                .checked_add((x & !1).checked_mul(2)?)?;
                            let uyvy = format == 5;
                            (
                                *data.get(base.checked_add((x & 1) * 2 + usize::from(uyvy))?)?,
                                *data.get(base.checked_add(usize::from(!uyvy))?)?,
                                *data.get(base.checked_add(if uyvy { 2 } else { 3 })?)?,
                            )
                        }
                    };
                    let yy = (i32::from(luma) - 16).max(0) * 1220542;
                    let u = i32::from(u) - 128;
                    let v = i32::from(v) - 128;
                    let clip = |v: i32| -> u8 { ((v + (1 << 19)) >> 20).clamp(0, 255) as u8 };
                    [
                        clip(yy + 2116026 * u),
                        clip(yy - 409993 * u - 852492 * v),
                        clip(yy + 1673527 * v),
                    ]
                }
            };
            let at = y * output_stride + x * 4;
            out[at..at + 3].copy_from_slice(&bgr);
            out[at + 3] = 255;
        }
    }
    Some(())
}
