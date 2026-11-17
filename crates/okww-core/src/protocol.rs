pub fn header(uuid: u32, random: u32, sequence: u32, command: u32) -> [u8; 16] {
    let mut bytes = [0; 16];
    for (i, v) in [uuid, random, sequence, command].iter().enumerate() {
        bytes[i * 4..i * 4 + 4].copy_from_slice(&v.to_le_bytes());
    }
    bytes
}
pub fn matching_reply(reply: &[u8], sequence: u32, command: u32) -> bool {
    reply.len() >= 16
        && reply[8..12] == sequence.to_le_bytes()
        && reply[12..16] == command.to_le_bytes()
}
pub fn mouse(buttons: i32, x: i32, y: i32, wheel: i32) -> [u8; 56] {
    let mut bytes = [0; 56];
    for (i, v) in [buttons, x, y, wheel].iter().enumerate() {
        bytes[i * 4..i * 4 + 4].copy_from_slice(&v.to_le_bytes());
    }
    bytes
}
pub fn keyboard(modifiers: i32, keys: &[i32]) -> Option<[u8; 12]> {
    if keys.len() > 10 {
        return None;
    }
    let mut bytes = [0; 12];
    bytes[0] = modifiers as u8;
    for (i, key) in keys.iter().enumerate() {
        bytes[2 + i] = *key as u8;
    }
    Some(bytes)
}

#[repr(C)]
pub struct Transport {
    pub context: *mut std::ffi::c_void,
    pub now: unsafe extern "C" fn(*mut std::ffi::c_void) -> i64,
    pub send: unsafe extern "C" fn(*mut std::ffi::c_void, *const u8, usize) -> i32,
    pub readable: unsafe extern "C" fn(*mut std::ffi::c_void, i64) -> i32,
    pub receive: unsafe extern "C" fn(*mut std::ffi::c_void, *mut u8, usize) -> i32,
}
impl Transport {
    // One send only: a lost response must never replay a delivered mouse move.
    pub fn exchange(&self, packet: &[u8], timeout: i64, sequence: u32, command: u32) -> i32 {
        let sent = unsafe { (self.send)(self.context, packet.as_ptr(), packet.len()) };
        if sent < 0 || sent as usize != packet.len() {
            return -1;
        }
        let deadline = unsafe { (self.now)(self.context) }.saturating_add(timeout);
        while unsafe { (self.now)(self.context) } < deadline {
            let remaining = (deadline as i128 - unsafe { (self.now)(self.context) } as i128)
                .clamp(1, i64::MAX as i128) as i64;
            if unsafe { (self.readable)(self.context, remaining) } <= 0 {
                break;
            }
            let mut buffer = [0u8; 2048];
            let received =
                unsafe { (self.receive)(self.context, buffer.as_mut_ptr(), buffer.len()) };
            if received < 16 {
                continue;
            }
            if received as usize > buffer.len() {
                return 0;
            }
            if matching_reply(&buffer[..received as usize], sequence, command) {
                return 1;
            }
        }
        0
    }
}
pub fn latency_percentiles(samples: &mut [f64]) -> Option<(f64, f64)> {
    if samples.is_empty() {
        return None;
    }
    samples.sort_unstable_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));
    let count = samples.len() as f64;
    Some((
        samples[(0.5 * count).ceil() as usize - 1],
        samples[(0.95 * count).ceil() as usize - 1],
    ))
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn reports_match_wire_format() {
        let h = header(0x12345678, 1, 2, 3);
        assert_eq!(&h[..4], &[0x78, 0x56, 0x34, 0x12]);
        assert!(matching_reply(&h, 2, 3));
        assert!(!matching_reply(&h, 1, 3));
        let m = mouse(1, -40, 40, 0);
        assert_eq!(&m[4..8], &(-40i32).to_le_bytes());
        assert!(m[16..].iter().all(|v| *v == 0));
        assert_eq!(
            keyboard(2, &[4, 8]).unwrap(),
            [2, 0, 4, 8, 0, 0, 0, 0, 0, 0, 0, 0]
        );
    }
}
