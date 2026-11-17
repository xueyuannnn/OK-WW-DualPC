pub fn key_code(key: &str) -> Option<i32> {
    if key.len() == 1 {
        let c = key.as_bytes()[0].to_ascii_lowercase();
        if c.is_ascii_lowercase() {
            return Some(4 + (c - b'a') as i32);
        }
        if (b'1'..=b'9').contains(&c) {
            return Some(0x1d + (c - b'0') as i32);
        }
        if c == b'0' {
            return Some(0x27);
        }
        if let Some(i) = b"-=[]\\;'`,./".iter().position(|&v| v == c) {
            return Some(
                [
                    0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
                ][i],
            );
        }
    }
    // The Qt adapter supplies Unicode-lowercased text. Avoid allocating an
    // additional String on every authored key press.
    let code = match key {
        "enter" | "return" => 0x28,
        "esc" | "escape" => 0x29,
        "backspace" => 0x2a,
        "tab" => 0x2b,
        "space" => 0x2c,
        "capslock" => 0x39,
        "insert" => 0x49,
        "home" => 0x4a,
        "pageup" | "page_up" => 0x4b,
        "delete" => 0x4c,
        "end" => 0x4d,
        "pagedown" | "page_down" => 0x4e,
        "right" => 0x4f,
        "left" => 0x50,
        "down" => 0x51,
        "up" => 0x52,
        "ctrl" | "lctrl" | "control" | "lcontrol" | "ctrl_l" => 0xe0,
        "rctrl" | "rcontrol" | "ctrl_r" => 0xe4,
        "shift" | "lshift" | "shift_l" => 0xe1,
        "rshift" | "shift_r" => 0xe5,
        "alt" | "lalt" | "alt_l" => 0xe2,
        "ralt" | "alt_r" => 0xe6,
        "win" | "cmd" => 0xe3,
        _ => {
            if !key.starts_with(['f', 'F']) {
                return None;
            }
            let n = key[1..].trim().parse::<i32>().ok()?;
            if (1..=12).contains(&n) {
                0x39 + n
            } else {
                return None;
            }
        }
    };
    Some(code)
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn authored_keys() {
        assert_eq!(key_code("F12"), Some(0x45));
        assert_eq!(key_code("ctrl_r"), Some(0xe4));
        assert_eq!(key_code(" "), None);
        assert_eq!(key_code("中文"), None);
    }
}
