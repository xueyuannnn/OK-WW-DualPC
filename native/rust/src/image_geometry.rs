//! Coordinate policies used by template anchoring and game UI targeting.
use crate::execution::pixel_round;
type Rect = [i32; 4];
fn round(v: f64) -> Result<i32, i32> {
    pixel_round(v).ok_or(0)
}
fn axis(value: f64, before: f64, after: f64, scale: f64, center: bool) -> Result<i32, i32> {
    if center {
        round(after * 0.5 + (value - before * 0.5) * scale)
    } else if value > before / 2. {
        Ok(after as i32 - round((before - value) * scale)?)
    } else {
        round(value * scale)
    }
}
fn game_point(width: f64, height: f64, x: f64, y: f64, hc: bool, vc: bool) -> Result<Rect, i32> {
    if width <= 0. || height <= 0. || !x.is_finite() || !y.is_finite() {
        return Err(1);
    }
    if (width / height - 16. / 9.).abs() <= 0.01 {
        return Ok([(width * x) as i32, (height * y) as i32, 0, 0]);
    }
    let base = height * 16. / 9.;
    let scale = (width / base).min(1.);
    Ok([
        axis(x * base, base, width, scale, hc)?,
        axis(y * height, height, height, scale, vc)?,
        0,
        0,
    ])
}
pub fn geometry(kind: i32, a: &[f64]) -> Result<Rect, i32> {
    match kind {
        0 if a.len() == 10 => {
            let (sw, sh, tw, th) = (a[4], a[5], a[6], a[7]);
            if sw <= 0. || sh <= 0. || tw <= 0. || th <= 0. {
                return Err(3);
            }
            let scale = (tw / sw).min(th / sh);
            Ok([
                axis(a[0], sw, tw, scale, a[8] != 0.)?,
                axis(a[1], sh, th, scale, a[9] != 0.)?,
                round(a[2] * scale)?.max(1),
                round(a[3] * scale)?.max(1),
            ])
        }
        1 if a.len() == 6 => {
            let x = round(a[0] * a[2])?;
            let y = round(a[1] * a[3])?;
            Ok([x, y, round(a[0] * a[4])? - x, round(a[1] * a[5])? - y])
        }
        2 if a.len() == 6 => game_point(a[0], a[1], a[2], a[3], a[4] != 0., a[5] != 0.),
        3 if a.len() == 9 => {
            let (w, h, x, y, right, bottom, hc, vc, empty) = (
                a[0],
                a[1],
                a[2],
                a[3],
                a[4],
                a[5],
                a[6] != 0.,
                a[7] != 0.,
                a[8] != 0.,
            );
            if empty || right < x || bottom < y {
                return Err(2);
            }
            let resized = (w / h - 16. / 9.).abs() > 0.01;
            let base = if resized { h * 16. / 9. } else { w };
            let scale = (w / base).min(1.);
            let point = if resized {
                game_point(w, h, x, y, hc, vc)?
            } else {
                [round(x * w)?, round(y * h)?, 0, 0]
            };
            let width = if resized {
                (right * base - x * base) * scale
            } else {
                (right - x) * base
            };
            let height = if resized {
                (bottom * h - y * h) * scale
            } else {
                (bottom - y) * h
            };
            Ok([point[0], point[1], round(width)?, round(height)?])
        }
        _ => Err(4),
    }
}
