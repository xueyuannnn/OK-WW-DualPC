pub fn simulation_sequence(stamp: i64) -> u64 {
    (stamp / 16_666_667) as u64
}
pub fn recovery_timed_out(now: i64, began: i64) -> bool {
    now.wrapping_sub(began) >= 120_000_000_000
}
