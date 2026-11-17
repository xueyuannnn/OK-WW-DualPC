// Preserve the original C++ treatment of an unordered floating-point threshold.
#![allow(clippy::neg_cmp_op_on_partial_ord)]
pub fn frame_fresh(now: i64, captured: i64, present: bool, stale_after: f64) -> bool {
    present && !((now as i128 - captured as i128) as f64 > stale_after * 1e9)
}
// 0: no work, 1: missing input device, 2: missed task heartbeat, 3: inspect frame.
pub fn watchdog(now: i64, armed: bool, box_connected: bool, heartbeat: i64) -> i32 {
    if !armed {
        0
    } else if !box_connected {
        1
    } else if now as i128 - heartbeat as i128 > 1_500_000_000 {
        2
    } else {
        3
    }
}
// 0: wait, 1: capture thread terminated, 2: recovery deadline exceeded.
pub fn capture_recovery(now: i64, present: bool, stopped: bool, began: &mut i64) -> i32 {
    if !present || stopped {
        return 1;
    }
    if *began == 0 {
        *began = now;
    }
    if now as i128 - *began as i128 >= 120_000_000_000 {
        2
    } else {
        0
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn exact_freshness_and_watchdog_boundaries() {
        assert!(frame_fresh(150_000_000, 0, true, 0.15));
        assert!(!frame_fresh(174_194_100, 0, true, 0.15));
        assert_eq!(watchdog(1_500_000_000, true, true, 0), 3);
        assert_eq!(watchdog(1_500_000_001, true, true, 0), 2);
        assert_eq!(watchdog(i64::MAX, true, false, 0), 1);
        assert_eq!(watchdog(i64::MAX, false, false, 0), 0);
    }
    #[test]
    fn temporary_capture_outage_does_not_disarm() {
        let mut began = 0;
        assert_eq!(capture_recovery(1, true, false, &mut began), 0);
        assert_eq!(
            capture_recovery(120_000_000_000, true, false, &mut began),
            0
        );
        assert_eq!(
            capture_recovery(120_000_000_001, true, false, &mut began),
            2
        );
        assert_eq!(capture_recovery(2, true, true, &mut began), 1);
    }
}
