//! Exact version grammar and ordering used by the Qt update client.
use std::{cmp::Ordering, slice};
#[repr(C)]
#[derive(Default)]
pub struct ParsedVersion {
    pub numbers: [u64; 3],
    pub pre_start: usize,
    pub pre_len: usize,
}
#[repr(C)]
pub struct WideView {
    pub data: *const u16,
    pub len: usize,
}
#[repr(C)]
pub struct VersionView {
    pub numbers: [u64; 3],
    pub pre: *const WideView,
    pub len: usize,
}
fn digit(value: u16) -> bool {
    (u16::from(b'0')..=u16::from(b'9')).contains(&value)
}
fn identifier(value: u16) -> bool {
    digit(value)
        || (u16::from(b'A')..=u16::from(b'Z')).contains(&value)
        || (u16::from(b'a')..=u16::from(b'z')).contains(&value)
        || value == u16::from(b'-')
}
fn identifiers(text: &[u16], pre: bool) -> bool {
    text.split(|&c| c == u16::from(b'.')).all(|part| {
        !part.is_empty()
            && part.iter().all(|&c| identifier(c))
            && !(pre
                && part.len() > 1
                && part[0] == u16::from(b'0')
                && part.iter().all(|&c| digit(c)))
    })
}
fn parse(text: &[u16]) -> Option<ParsedVersion> {
    // Qt/PCRE's final $ accepts a single terminal LF; the input is not trimmed.
    let end = text.len() - usize::from(text.last() == Some(&u16::from(b'\n')));
    let mut at = usize::from(text.first() == Some(&u16::from(b'v')));
    let mut result = ParsedVersion::default();
    for (i, slot) in result.numbers.iter_mut().enumerate() {
        let start = at;
        let mut number = 0u64;
        while at < end && digit(text[at]) {
            number = number
                .checked_mul(10)?
                .checked_add(u64::from(text[at] - u16::from(b'0')))?;
            at += 1;
        }
        if at == start || (at - start > 1 && text[start] == u16::from(b'0')) {
            return None;
        }
        *slot = number;
        if i < 2 {
            if at >= end || text[at] != u16::from(b'.') {
                return None;
            }
            at += 1;
        }
    }
    if at < end && text[at] == u16::from(b'-') {
        at += 1;
        result.pre_start = at;
        while at < end && text[at] != u16::from(b'+') {
            at += 1;
        }
        result.pre_len = at - result.pre_start;
        if !identifiers(&text[result.pre_start..at], true) {
            return None;
        }
    }
    if at < end && text[at] == u16::from(b'+') {
        at += 1;
        if !identifiers(&text[at..end], false) {
            return None;
        }
        at = end;
    }
    (at == end).then_some(result)
}
unsafe fn borrowed<'a, T>(data: *const T, len: usize) -> Option<&'a [T]> {
    if len == 0 {
        return Some(&[]);
    }
    if data.is_null() || len > isize::MAX as usize / std::mem::size_of::<T>() {
        return None;
    }
    // SAFETY: the synchronous ABI caller owns aligned, live contiguous storage.
    Some(unsafe { slice::from_raw_parts(data, len) })
}
#[no_mangle]
/// # Safety
/// The caller owns the readable UTF-16 slice and writable aligned output.
pub unsafe extern "C" fn okww_rs_version_parse(
    text: *const u16,
    len: usize,
    out: *mut ParsedVersion,
) -> i32 {
    if out.is_null() {
        return 0;
    }
    // SAFETY: strings are borrowed only for this invocation.
    let Some(text) = (unsafe { borrowed(text, len) }) else {
        return 0;
    };
    let Some(value) = parse(text) else {
        return 0;
    };
    // SAFETY: out is a writable ParsedVersion supplied by the caller.
    unsafe {
        out.write(value);
    }
    1
}
#[no_mangle]
/// # Safety
/// Both version views, their spans and strings remain readable during the call;
/// `out` is a writable aligned scalar.
pub unsafe extern "C" fn okww_rs_version_compare(
    a: *const VersionView,
    b: *const VersionView,
    out: *mut i32,
) -> i32 {
    if a.is_null() || b.is_null() || out.is_null() {
        return 0;
    }
    // SAFETY: callers keep both views and all referenced strings alive.
    let (a, b) = unsafe { (&*a, &*b) };
    let Some(ap) = (unsafe { borrowed(a.pre, a.len) }) else {
        return 0;
    };
    let Some(bp) = (unsafe { borrowed(b.pre, b.len) }) else {
        return 0;
    };
    let order = (|| {
        let core = a.numbers.cmp(&b.numbers);
        if core != Ordering::Equal {
            return Some(core);
        }
        if ap.is_empty() != bp.is_empty() {
            return Some(ap.is_empty().cmp(&bp.is_empty()));
        }
        for (a, b) in ap.iter().zip(bp) {
            // SAFETY: each foreign string uses the same borrowed-view contract.
            let (a, b) = unsafe { (borrowed(a.data, a.len)?, borrowed(b.data, b.len)?) };
            if a == b {
                continue;
            }
            let an = a.iter().all(|&c| digit(c));
            let bn = b.iter().all(|&c| digit(c));
            if an != bn {
                return Some(bn.cmp(&an));
            }
            if an && a.len() != b.len() {
                return Some(a.len().cmp(&b.len()));
            }
            return Some(a.cmp(b));
        }
        Some(ap.len().cmp(&bp.len()))
    })();
    let Some(order) = order else {
        return 0;
    };
    let value = match order {
        Ordering::Less => -1,
        Ordering::Equal => 0,
        Ordering::Greater => 1,
    };
    // SAFETY: out is the caller's writable scalar result.
    unsafe {
        out.write(value);
    }
    1
}
#[cfg(test)]
mod tests {
    use super::*;
    fn parse_string(s: &str) -> Option<ParsedVersion> {
        parse(&s.encode_utf16().collect::<Vec<_>>())
    }
    #[test]
    fn grammar_boundaries() {
        assert!(parse_string("v18446744073709551615.0.0-999999999999999999999999+001").is_some());
        assert!(parse_string("18446744073709551616.0.0").is_none());
        assert!(parse_string("1.2.3\n").is_some());
        for invalid in [
            "",
            "v",
            "1.2.3\r\n",
            "1.2.3\n\n",
            "1.2.3-01",
            "1.2.3+",
            "1.2.3-",
            "01.2.3",
            " 1.2.3",
            "1.2.3-α",
        ] {
            assert!(parse_string(invalid).is_none(), "{invalid:?}");
        }
        let parsed = parse_string("v1.2.3-beta.8+build").unwrap();
        assert_eq!((parsed.pre_start, parsed.pre_len), (7, 6));
    }
}
