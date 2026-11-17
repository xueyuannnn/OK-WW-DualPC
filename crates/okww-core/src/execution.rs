use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
#[derive(Default)]
pub struct Gate {
    epoch: AtomicU64,
    running: AtomicBool,
}
impl Gate {
    pub fn start(&self) -> u64 {
        self.running.store(false, Ordering::SeqCst);
        let epoch = self.epoch.fetch_add(1, Ordering::SeqCst).wrapping_add(1);
        self.running.store(true, Ordering::SeqCst);
        epoch
    }
    pub fn stop(&self) {
        self.running.store(false, Ordering::SeqCst);
        self.epoch.fetch_add(1, Ordering::SeqCst);
    }
    pub fn active(&self, epoch: u64) -> bool {
        self.running.load(Ordering::SeqCst) && self.epoch.load(Ordering::SeqCst) == epoch
    }
}
pub fn pixel_round(value: f64) -> Option<i32> {
    if !value.is_finite() || value < i32::MIN as f64 || value > i32::MAX as f64 {
        return None;
    }
    let floor = value.floor();
    let fraction = value - floor;
    Some(if fraction < 0.5 {
        floor
    } else if fraction > 0.5 {
        floor + 1.0
    } else if floor % 2.0 == 0.0 {
        floor
    } else {
        floor + 1.0
    } as i32)
}

pub const HOST_ERROR: i32 = -1;
pub const INVALID_WAIT: i32 = -2;
pub const INVALID_CONDITION: i32 = -3;
pub const CANCELLED: i32 = -4;
#[repr(C)]
#[derive(Clone, Copy)]
pub struct ExecutionHost {
    pub context: *mut std::ffi::c_void,
    pub now: unsafe extern "C" fn(*mut std::ffi::c_void, *mut f64) -> i32,
    pub sleep: unsafe extern "C" fn(*mut std::ffi::c_void, f64) -> i32,
    pub checkpoint: unsafe extern "C" fn(*mut std::ffi::c_void) -> i32,
    pub condition: unsafe extern "C" fn(*mut std::ffi::c_void, *mut i32) -> i32,
}
impl ExecutionHost {
    fn now(&self) -> Result<f64, i32> {
        let mut out = 0.0;
        if unsafe { (self.now)(self.context, &mut out) } == 0 {
            Err(HOST_ERROR)
        } else {
            Ok(out)
        }
    }
    fn checkpoint(&self) -> Result<(), i32> {
        if unsafe { (self.checkpoint)(self.context) } == 0 {
            Err(HOST_ERROR)
        } else {
            Ok(())
        }
    }
    pub fn wait(&self, seconds: f64) -> Result<(), i32> {
        if !seconds.is_finite() || !(0.0..=86400.0).contains(&seconds) {
            return Err(INVALID_WAIT);
        }
        let end = self.now()? + seconds;
        self.checkpoint()?;
        while self.now()? < end {
            let remaining = end - self.now()?;
            let sleep = if remaining < 0.01 { remaining } else { 0.01 };
            if unsafe { (self.sleep)(self.context, sleep) } == 0 {
                return Err(HOST_ERROR);
            }
            self.checkpoint()?;
        }
        Ok(())
    }
    pub fn until(&self, timeout: f64, interval: f64) -> Result<bool, i32> {
        until_sequence(
            || self.now(),
            || self.checkpoint(),
            |seconds| self.wait(seconds),
            || {
                let mut condition = 0;
                if unsafe { (self.condition)(self.context, &mut condition) } == 0 {
                    Err(HOST_ERROR)
                } else {
                    Ok(condition != 0)
                }
            },
            timeout,
            interval,
        )
    }
}
pub(crate) fn until_sequence(
    now: impl Fn() -> Result<f64, i32>,
    checkpoint: impl Fn() -> Result<(), i32>,
    wait: impl Fn(f64) -> Result<(), i32>,
    condition: impl Fn() -> Result<bool, i32>,
    timeout: f64,
    interval: f64,
) -> Result<bool, i32> {
    if !timeout.is_finite() || timeout < 0.0 || interval <= 0.0 {
        return Err(INVALID_CONDITION);
    }
    let end = now()? + timeout;
    loop {
        checkpoint()?;
        if condition()? {
            return Ok(true);
        }
        if now()? >= end {
            return Ok(false);
        }
        let remaining = end - now()?;
        // std::min(interval,remaining) keeps a NaN first argument.
        wait(if remaining < interval {
            remaining
        } else {
            interval
        })?;
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn old_epoch_cannot_resume() {
        let g = Gate::default();
        let old = g.start();
        assert!(g.active(old));
        g.stop();
        let new = g.start();
        assert!(!g.active(old));
        assert!(g.active(new));
    }
    #[test]
    fn rounding_keeps_python_ties() {
        for (n, expected) in [
            (-2.5, -2),
            (-1.5, -2),
            (-0.5, 0),
            (0.5, 0),
            (1.5, 2),
            (2.5, 2),
        ] {
            assert_eq!(pixel_round(n), Some(expected));
        }
        assert_eq!(pixel_round(f64::NAN), None);
    }
}
