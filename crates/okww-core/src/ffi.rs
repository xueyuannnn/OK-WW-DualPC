//! C ABI shared with rust_core.hpp. Ownership never crosses allocators.
//! Borrowed arrays are valid only during a call. All fallible allocations use
//! try_reserve / a checked allocator, so C++ can retain its bad_alloc recovery.
//!
//! # Pointer contract
//! Non-null pointers must be aligned, live values of the declared ABI type.
//! Input arrays contain at least their stated length, output buffers contain
//! at least their stated capacity and cannot alias any borrowed input. Timeline
//! mutation and destruction are exclusive; Gate operations are atomic and may
//! be concurrent, but destruction requires all users to have finished. Opaque
//! pointers originate from this module and must be freed here exactly once.
use crate::{combat::*, execution::Gate, farming::*, input, protocol};
use std::alloc::{alloc, dealloc, Layout};

static EMPTY_TIMELINE: FreezeTimeline = FreezeTimeline {
    entries: Vec::new(),
};
/// # Safety
/// The host and numeric buffers remain live for this synchronous call; output
/// contains three doubles and the callback catches all foreign exceptions.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_workflow_run(
    host: *const crate::services::Host,
    method: i32,
    args: *const f64,
    count: usize,
    out: *mut f64,
) -> i32 {
    let (Some(host), Some(args), Some(out)) = (
        unsafe { host.as_ref() },
        unsafe { slice(args, count) },
        unsafe { output(out, 3) },
    ) else {
        return -1;
    };
    match host.run(method, args) {
        Ok(value) => {
            out.copy_from_slice(&value);
            1
        }
        Err(code) => code,
    }
}
/// # Safety
/// The host and numeric buffers remain live for this synchronous call; output
/// contains three doubles and the callback catches all foreign exceptions.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_vision_run(
    host: *const crate::visionflow::Host,
    method: i32,
    args: *const f64,
    count: usize,
    out: *mut f64,
) -> i32 {
    let (Some(host), Some(args), Some(out)) = (
        unsafe { host.as_ref() },
        unsafe { slice(args, count) },
        unsafe { output(out, 3) },
    ) else {
        return -1;
    };
    match host.run(method, args) {
        Ok(value) => {
            out.copy_from_slice(&value);
            1
        }
        Err(code) => code,
    }
}
/// # Safety
/// Host, arguments and output remain live for this synchronous call; the host
/// catches all exceptions. Output points to three writable doubles.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_session_run(
    host: *const crate::session::Host,
    method: i32,
    args: *const f64,
    count: usize,
    out: *mut f64,
) -> i32 {
    let (Some(host), Some(args), Some(out)) = (
        unsafe { host.as_ref() },
        unsafe { slice(args, count) },
        unsafe { output(out, 3) },
    ) else {
        return -1;
    };
    match host.run(method, args) {
        Ok(value) => {
            out.copy_from_slice(&[value, 0., 0.]);
            1
        }
        Err(code) => code,
    }
}
/// # Safety
/// `value` borrows `length` bytes for this call, or is null for an empty string.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_engine_command_kind(value: *const u8, length: usize) -> i32 {
    let Some(value) = (unsafe { slice(value, length) }) else {
        return -1;
    };
    crate::engine::commands::parse(value)
}
/// # Safety
/// `value` borrows `length` bytes for this call, or is null for an empty string.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_engine_script_key(value: *const u8, length: usize) -> i32 {
    let Some(value) = (unsafe { slice(value, length) }) else {
        return -1;
    };
    crate::engine::commands::script_key(value)
}
/// # Safety
/// The host and argument array remain live throughout this synchronous call;
/// the host catches exceptions and retains ownership of all task objects.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_engine_run(
    host: *const crate::engine::Host,
    method: i32,
    args: *const f64,
    count: usize,
    out: *mut f64,
) -> i32 {
    let (Some(host), Some(args), Some(out)) = (
        unsafe { host.as_ref() },
        unsafe { slice(args, count) },
        unsafe { output(out, 3) },
    ) else {
        return -1;
    };
    match host.run(method, args) {
        Ok(value) => {
            out[0] = value;
            out[1] = 0.;
            out[2] = 0.;
            1
        }
        Err(code) => code,
    }
}
/// # Safety
/// Host/context and numeric buffers remain live for this synchronous call;
/// the host catches exceptions and output contains three writable doubles.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_taskflow_run(
    host: *const crate::taskflow::Host,
    method: i32,
    args: *const f64,
    count: usize,
    out: *mut f64,
) -> i32 {
    let (Some(host), Some(args), Some(out)) = (
        unsafe { host.as_ref() },
        unsafe { slice(args, count) },
        unsafe { output(out, 3) },
    ) else {
        return -1;
    };
    match host.run(method, args) {
        Ok(value) => {
            out.copy_from_slice(&value);
            1
        }
        Err(code) => code,
    }
}
#[no_mangle]
pub extern "C" fn okww_rs_monthly_day(hour: i32, valid: i32, milliseconds: i32) -> i32 {
    crate::taskflow::monthly_day(hour, valid != 0, milliseconds)
}
#[no_mangle]
pub extern "C" fn okww_rs_monthly_offset(seconds: i64) -> i32 {
    crate::taskflow::monthly_offset(seconds) as i32
}
/// # Safety
/// Image and mask rows are live for the specified strides and dimensions;
/// mask output cannot alias the borrowed BGR image.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_continue_mask(
    image: *const u8,
    width: usize,
    height: usize,
    stride: usize,
    out: *mut u8,
    out_stride: usize,
) -> i32 {
    let Some(row) = width.checked_mul(3) else {
        return 0;
    };
    if stride < row || out_stride < width {
        return 0;
    }
    let length = |stride: usize, row: usize| {
        if height == 0 {
            Some(0)
        } else {
            (height - 1).checked_mul(stride)?.checked_add(row)
        }
    };
    let (Some(input_len), Some(output_len)) = (length(stride, row), length(out_stride, width))
    else {
        return 0;
    };
    let (Some(input), Some(out)) = (unsafe { slice(image, input_len) }, unsafe {
        output(out, output_len)
    }) else {
        return 0;
    };
    crate::taskflow::marker_mask(input, width, height, stride, out, out_stride);
    1
}

#[no_mangle]
pub extern "C" fn okww_rs_toolbox_intro(intro: i32, previous: i32) -> i32 {
    (intro != 0 || previous != 0) as i32
}

/// # Safety
/// The host/context remain live synchronously. Its invoke callback catches
/// exceptions and writes at most three doubles; args/out follow the module
/// pointer contract. No character state is borrowed across a host callback.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_rotation_run(
    host: *const crate::rotation::Host,
    method: i32,
    args: *const f64,
    count: usize,
    out: *mut f64,
) -> i32 {
    let (Some(host), Some(args), Some(out)) = (
        unsafe { host.as_ref() },
        unsafe { slice(args, count) },
        unsafe { output(out, 3) },
    ) else {
        return crate::rotation::HOST_ERROR;
    };
    match host.run(method, args) {
        Ok(value) => {
            out.copy_from_slice(&value);
            1
        }
        Err(code) => code,
    }
}

/// # Safety
/// Gate ownership and the callback context follow the module's pointer contract.
/// The health callback catches host exceptions and returns zero on failure.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_checkpoint(
    gate: *const Gate,
    epoch: u64,
    context: *mut std::ffi::c_void,
    health: unsafe extern "C" fn(*mut std::ffi::c_void) -> i32,
) -> i32 {
    let Some(gate) = (unsafe { gate.as_ref() }) else {
        return crate::execution::CANCELLED;
    };
    if !gate.active(epoch) {
        return crate::execution::CANCELLED;
    }
    if unsafe { health(context) } == 0 {
        return crate::execution::HOST_ERROR;
    }
    if !gate.active(epoch) {
        return crate::execution::CANCELLED;
    }
    1
}
/// # Safety
/// The host and all four callbacks remain live for this synchronous call. Host
/// callbacks must catch exceptions; no exception may unwind across Rust.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_execution_wait(
    host: *const crate::execution::ExecutionHost,
    seconds: f64,
) -> i32 {
    let Some(host) = (unsafe { host.as_ref() }) else {
        return crate::execution::HOST_ERROR;
    };
    match host.wait(seconds) {
        Ok(()) => 1,
        Err(code) => code,
    }
}
/// # Safety
/// The host and callback context follow the same contract as execution_wait.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_execution_until(
    host: *const crate::execution::ExecutionHost,
    timeout: f64,
    interval: f64,
) -> i32 {
    let Some(host) = (unsafe { host.as_ref() }) else {
        return crate::execution::HOST_ERROR;
    };
    match host.until(timeout, interval) {
        Ok(value) => value as i32,
        Err(code) => code,
    }
}
#[no_mangle]
pub extern "C" fn okww_rs_frame_fresh(
    now: i64,
    captured: i64,
    present: i32,
    stale_after: f64,
) -> i32 {
    crate::lifecycle::frame_fresh(now, captured, present != 0, stale_after) as i32
}
#[no_mangle]
pub extern "C" fn okww_rs_watchdog(
    now: i64,
    armed: i32,
    box_connected: i32,
    heartbeat: i64,
) -> i32 {
    crate::lifecycle::watchdog(now, armed != 0, box_connected != 0, heartbeat)
}
/// # Safety
/// began points to an exclusively borrowed 64-bit monotonic timestamp.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_capture_recovery(
    now: i64,
    present: i32,
    stopped: i32,
    began: *mut i64,
) -> i32 {
    let Some(began) = (unsafe { began.as_mut() }) else {
        return 1;
    };
    crate::lifecycle::capture_recovery(now, present != 0, stopped != 0, began)
}

/// # Safety
/// All socket callbacks and their context remain live for this synchronous call.
/// Send/receive callbacks obey capacities and cannot unwind across Rust.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_transport_exchange(
    host: *const protocol::Transport,
    packet: *const u8,
    len: usize,
    timeout: i64,
    sequence: u32,
    command: u32,
) -> i32 {
    let (Some(host), Some(packet)) = (unsafe { host.as_ref() }, unsafe { slice(packet, len) })
    else {
        return -1;
    };
    host.exchange(packet, timeout, sequence, command)
}
/// # Safety
/// samples is an exclusively borrowed array of length count, and out has two
/// writable doubles that do not overlap samples.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_latency_percentiles(
    samples: *mut f64,
    count: usize,
    out: *mut f64,
) -> i32 {
    let (Some(samples), Some(out)) = (unsafe { output(samples, count) }, unsafe { output(out, 2) })
    else {
        return 0;
    };
    let Some((p50, p95)) = protocol::latency_percentiles(samples) else {
        return 0;
    };
    out[0] = p50;
    out[1] = p95;
    1
}

fn owned<T>(value: T) -> *mut T {
    let ptr = unsafe { alloc(Layout::new::<T>()) as *mut T };
    if !ptr.is_null() {
        unsafe {
            ptr.write(value);
        }
    }
    ptr
}
unsafe fn dispose<T>(ptr: *mut T) {
    if !ptr.is_null() {
        unsafe {
            ptr.drop_in_place();
            dealloc(ptr.cast(), Layout::new::<T>());
        }
    }
}
unsafe fn slice<'a, T>(ptr: *const T, len: usize) -> Option<&'a [T]> {
    if len == 0 {
        Some(&[])
    } else if ptr.is_null() {
        None
    } else {
        Some(unsafe { std::slice::from_raw_parts(ptr, len) })
    }
}
unsafe fn output<'a, T>(ptr: *mut T, len: usize) -> Option<&'a mut [T]> {
    if len == 0 {
        Some(&mut [])
    } else if ptr.is_null() {
        None
    } else {
        Some(unsafe { std::slice::from_raw_parts_mut(ptr, len) })
    }
}
#[no_mangle]
pub extern "C" fn okww_rs_abi_version() -> u32 {
    2
}
#[no_mangle]
pub extern "C" fn okww_rs_freeze_new() -> *mut FreezeTimeline {
    owned(FreezeTimeline::default())
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_freeze_free(ptr: *mut FreezeTimeline) {
    unsafe {
        dispose(ptr);
    }
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_freeze_clone(ptr: *const FreezeTimeline) -> *mut FreezeTimeline {
    let value = unsafe { ptr.as_ref() }.unwrap_or(&EMPTY_TIMELINE);
    let mut copy = FreezeTimeline::default();
    if copy.entries.try_reserve(value.entries.len()).is_err() {
        return std::ptr::null_mut();
    }
    copy.entries.extend_from_slice(&value.entries);
    owned(copy)
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_freeze_add(
    ptr: *mut FreezeTimeline,
    now: f64,
    start: f64,
    duration: f64,
    threshold: f64,
) -> i32 {
    let Some(value) = (unsafe { ptr.as_mut() }) else {
        return 0;
    };
    if value.add(now, start, duration, threshold).is_ok() {
        1
    } else {
        2
    }
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_freeze_elapsed(
    ptr: *const FreezeTimeline,
    now: f64,
    start: f64,
    intro: i32,
) -> f64 {
    unsafe { ptr.as_ref() }
        .unwrap_or(&EMPTY_TIMELINE)
        .elapsed(now, start, intro != 0)
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_freeze_clear(ptr: *mut FreezeTimeline) {
    if let Some(t) = unsafe { ptr.as_mut() } {
        t.entries.clear();
    }
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_freeze_count(ptr: *const FreezeTimeline) -> usize {
    unsafe { ptr.as_ref() }
        .map(|t| t.entries.len())
        .unwrap_or(0)
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_freeze_entries(
    ptr: *const FreezeTimeline,
    out: *mut Freeze,
    capacity: usize,
) -> i32 {
    let t = unsafe { ptr.as_ref() }.unwrap_or(&EMPTY_TIMELINE);
    if capacity < t.entries.len() {
        return 0;
    }
    let Some(out) = (unsafe { output(out, t.entries.len()) }) else {
        return 0;
    };
    out.copy_from_slice(&t.entries);
    1
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_character_status(
    time: *const FreezeTimeline,
    state: *const CharacterState,
    now: f64,
    kind: i32,
) -> f64 {
    let t = unsafe { time.as_ref() }.unwrap_or(&EMPTY_TIMELINE);
    let Some(c) = (unsafe { state.as_ref() }) else {
        return f64::NAN;
    };
    match kind {
        0 => c.has_buff(t, now) as i32 as f64,
        1 => c.buff_remaining(t, now),
        2 => c.healer_locked(t, now) as i32 as f64,
        3 => c.switch_cooling_down(t, now) as i32 as f64,
        _ => f64::NAN,
    }
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_switch_out(state: *mut CharacterState, full: i32, now: f64) {
    if let Some(c) = unsafe { state.as_mut() } {
        c.switch_out(full != 0, now);
    }
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_apply_intro(
    previous_role: i32,
    next: *mut CharacterState,
    intro: i32,
) {
    if let Some(c) = unsafe { next.as_mut() } {
        c.flags = (c.flags & !6)
            | if intro != 0 {
                2 | if previous_role == 1 { 4 } else { 0 }
            } else {
                0
            };
    }
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_choose_switch(
    current: *const CharacterState,
    candidates: *const Candidate,
    count: usize,
    intro: i32,
    time: *const FreezeTimeline,
    now: f64,
    out: *mut Decision,
) -> i32 {
    let t = unsafe { time.as_ref() }.unwrap_or(&EMPTY_TIMELINE);
    let (Some(c), Some(input), Some(out)) = (
        unsafe { current.as_ref() },
        unsafe { slice(candidates, count) },
        unsafe { out.as_mut() },
    ) else {
        return 0;
    };
    *out = choose_switch(c, input, intro != 0, t, now);
    1
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_plan_stamina(
    current: i32,
    reserve: i32,
    total: i32,
    once: i32,
    remaining: i32,
    exact: i32,
    twice: i32,
    out: *mut Spend,
) -> i32 {
    let (Some(value), Some(out)) = (
        stamina_spend(
            current,
            reserve,
            total,
            once,
            remaining,
            exact != 0,
            twice != 0,
        ),
        unsafe { out.as_mut() },
    ) else {
        return 0;
    };
    *out = value;
    1
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_daily_batch(
    progress: i32,
    cost: i32,
    available: i32,
    twice: i32,
    out: *mut i32,
) -> i32 {
    let (Some(value), Some(out)) = (daily_batch(progress, cost, available, twice != 0), unsafe {
        out.as_mut()
    }) else {
        return 0;
    };
    *out = value;
    1
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_select_card(cards: *const Card, count: usize) -> i32 {
    unsafe { slice(cards, count) }
        .map(select_card)
        .unwrap_or(-1)
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_order_items(
    items: *const Item,
    count: usize,
    money: i32,
    out: *mut usize,
    capacity: usize,
) -> i32 {
    if capacity < count || count > i32::MAX as usize {
        return -1;
    }
    let (Some(items), Some(out)) = (unsafe { slice(items, count) }, unsafe {
        output(out, count)
    }) else {
        return -1;
    };
    order_items(items, money, out) as i32
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_restart_step(
    state: *mut RestartState,
    kind: i32,
    hud: i32,
    realm: i32,
) -> i32 {
    unsafe { state.as_mut() }
        .map(|s| s.step(kind, hud != 0, realm != 0))
        .unwrap_or(5)
}
#[no_mangle]
pub extern "C" fn okww_rs_gate_new() -> *mut Gate {
    owned(Gate::default())
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_gate_free(ptr: *mut Gate) {
    unsafe {
        dispose(ptr);
    }
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_gate_start(ptr: *const Gate) -> u64 {
    unsafe { ptr.as_ref() }.map(|g| g.start()).unwrap_or(0)
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_gate_stop(ptr: *const Gate) {
    if let Some(g) = unsafe { ptr.as_ref() } {
        g.stop();
    }
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_gate_active(ptr: *const Gate, epoch: u64) -> i32 {
    unsafe { ptr.as_ref() }.is_some_and(|g| g.active(epoch)) as i32
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_pixel_round(value: f64, out: *mut i32) -> i32 {
    let (Some(value), Some(out)) = (crate::execution::pixel_round(value), unsafe {
        out.as_mut()
    }) else {
        return 0;
    };
    *out = value;
    1
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_key_code(bytes: *const u8, len: usize) -> i32 {
    let Some(bytes) = (unsafe { slice(bytes, len) }) else {
        return -1;
    };
    std::str::from_utf8(bytes)
        .ok()
        .and_then(input::key_code)
        .unwrap_or(-1)
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_packet_header(
    uuid: u32,
    random: u32,
    seq: u32,
    command: u32,
    out: *mut u8,
) {
    if let Some(out) = unsafe { output(out, 16) } {
        out.copy_from_slice(&protocol::header(uuid, random, seq, command));
    }
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_matching_reply(
    bytes: *const u8,
    len: usize,
    seq: u32,
    command: u32,
) -> i32 {
    unsafe { slice(bytes, len) }.is_some_and(|p| protocol::matching_reply(p, seq, command)) as i32
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_mouse_report(
    buttons: i32,
    x: i32,
    y: i32,
    wheel: i32,
    out: *mut u8,
) {
    if let Some(out) = unsafe { output(out, 56) } {
        out.copy_from_slice(&protocol::mouse(buttons, x, y, wheel));
    }
}
/// # Safety
/// The caller must satisfy this module's pointer, capacity and ownership contract.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_keyboard_report(
    modifiers: i32,
    keys: *const i32,
    count: usize,
    out: *mut u8,
) -> i32 {
    let (Some(keys), Some(out)) = (unsafe { slice(keys, count) }, unsafe { output(out, 12) })
    else {
        return 0;
    };
    let Some(report) = protocol::keyboard(modifiers, keys) else {
        return 0;
    };
    out.copy_from_slice(&report);
    1
}

/// # Safety
/// The host and output must be live, and image rows must contain `width` bytes
/// at each `stride` offset for `height` rows for the synchronous call.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_gauge_segments(
    host: *const crate::gauge::SpectrumHost,
    bytes: *const u8,
    width: usize,
    height: usize,
    stride: usize,
    kind: i32,
    segments: i32,
    min_frequency: i32,
    max_frequency: i32,
    min_amplitude: f64,
    out: *mut i32,
) -> i32 {
    let (Some(host), Some(out)) = (unsafe { host.as_ref() }, unsafe { output(out, 2) }) else {
        return 0;
    };
    if width > i32::MAX as usize || height > i32::MAX as usize || stride < width {
        return 0;
    }
    let Some(length) = height
        .checked_sub(1)
        .and_then(|rows| rows.checked_mul(stride))
        .and_then(|n| n.checked_add(width))
        .or(if height == 0 { Some(0) } else { None })
    else {
        return 0;
    };
    if width.checked_mul(height).is_none() {
        return 0;
    }
    let Some(bytes) = (unsafe { slice(bytes, length) }) else {
        return 0;
    };
    let mask = crate::gauge::Mask {
        bytes,
        width,
        height,
        stride,
    };
    let result = match kind {
        0 => crate::gauge::stripes(&mask, host),
        1 => crate::gauge::phoebe(
            &mask,
            segments,
            min_frequency,
            max_frequency,
            min_amplitude,
            host,
        )
        .map(|n| (n, 0)),
        2 => crate::gauge::native_segments(
            &mask,
            segments,
            min_frequency,
            max_frequency,
            min_amplitude,
            host,
        )
        .map(|n| (n, 0)),
        _ => return 0,
    };
    match result {
        Ok((first, second)) => {
            out[0] = first;
            out[1] = second;
            1
        }
        Err(_) => 0,
    }
}
/// Convert borrowed OpenCV complex pairs to magnitudes without copying the transform.
///
/// # Safety
/// The input must contain `count` pairs of doubles; the output must contain
/// `capacity` writable doubles. Nonempty buffers must not overlap.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_spectrum_magnitudes(
    complex: *const f64,
    count: usize,
    out: *mut f64,
    capacity: usize,
) -> i32 {
    let Some(length) = count.checked_mul(2) else {
        return 0;
    };
    if capacity < count {
        return 0;
    }
    let (Some(values), Some(out)) = (unsafe { slice(complex, length) }, unsafe {
        output(out, count)
    }) else {
        return 0;
    };
    for (result, pair) in out.iter_mut().zip(values.as_chunks::<2>().0) {
        *result = pair[0].hypot(pair[1]);
    }
    1
}
#[no_mangle]
pub extern "C" fn okww_rs_gauge_ratio(colored: u64, total: u64) -> f64 {
    if total == 0 {
        0.
    } else {
        colored as f64 / total as f64
    }
}
#[no_mangle]
pub extern "C" fn okww_rs_stripe_ratio(
    start: i32,
    end: i32,
    width: i32,
    colored: u64,
    total: u64,
) -> f64 {
    if start == -2 {
        -1.
    } else if start == -1 {
        (okww_rs_gauge_ratio(colored, total) * 2.).min(1.)
    } else {
        (end - start) as f64 / width as f64
    }
}
#[no_mangle]
pub extern "C" fn okww_rs_gauge_round(value: f64, decimals: i32) -> f64 {
    let scale = 10_f64.powf(decimals as f64);
    ((value.abs() * scale + 0.5).floor() / scale).copysign(value)
}
/// # Safety
/// `out` must point to two writable integers for the synchronous call.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_ring_bounds(
    height: i32,
    inner: f64,
    outer: f64,
    nonzero: i32,
    out: *mut i32,
) -> i32 {
    let Some(out) = (unsafe { output(out, 2) }) else {
        return 0;
    };
    let first = (height as f64 * inner).floor() as i32;
    let second = (height as f64 * outer).ceil() as i32;
    out[0] = first;
    out[1] = second;
    (nonzero != 0 || second > first) as i32
}

/// # Safety
/// The buffer must contain `height` rows of three-channel bytes, with the
/// supplied stride, for the duration of this synchronous borrowed read.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_garden_pixels(
    data: *const u8,
    width: i32,
    height: i32,
    stride: usize,
    kind: i32,
) -> i32 {
    if width <= 0 || height <= 0 {
        return 0;
    }
    let (w, h) = (width as usize, height as usize);
    let Some(row) = w.checked_mul(3) else {
        return 0;
    };
    if stride < row {
        return 0;
    }
    let Some(length) = (h - 1)
        .checked_mul(stride)
        .and_then(|offset| offset.checked_add(row))
    else {
        return 0;
    };
    let Some(data) = (unsafe { slice(data, length) }) else {
        return 0;
    };
    crate::garden::pixels(data, w, h, stride, kind)
}

/// Simplifies the existing QString buffer without changing its length.
///
/// # Safety
/// For a nonzero length, `words` must point to that many aligned, writable,
/// exclusively borrowed UTF-16 code units for the duration of this call.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_echo_simplify(words: *mut u16, length: usize) -> i32 {
    if length == 0 {
        return 1;
    }
    if words.is_null()
        || !words.is_aligned()
        || length > isize::MAX as usize / std::mem::size_of::<u16>()
    {
        return 0;
    }
    // SAFETY: the caller supplies the writable QString storage described above.
    crate::echo_text::simplify(unsafe { std::slice::from_raw_parts_mut(words, length) });
    1
}

/// # Safety
/// `out` points to five writable doubles for this call.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_echo_layout(
    width: i32,
    height: i32,
    target_width: i32,
    target_height: i32,
    out: *mut f64,
) -> i32 {
    let Some(out) = (unsafe { output(out, 5) }) else {
        return 0;
    };
    let Some(value) = crate::echo_detection::layout(width, height, target_width, target_height)
    else {
        return 0;
    };
    out.copy_from_slice(&value);
    1
}
/// # Safety
/// Input has `height` rows of three bytes per pixel, at the given stride.
/// Output has at least `capacity` writable floats and cannot alias the input.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_echo_tensor(
    data: *const u8,
    width: usize,
    height: usize,
    stride: usize,
    out: *mut f32,
    capacity: usize,
) -> i32 {
    let Some(row) = width.checked_mul(3) else {
        return 0;
    };
    let Some(count) = row.checked_mul(height) else {
        return 0;
    };
    if width == 0 || height == 0 || stride < row || capacity < count {
        return 0;
    }
    let Some(length) = (height - 1)
        .checked_mul(stride)
        .and_then(|n| n.checked_add(row))
    else {
        return 0;
    };
    let (Some(data), Some(out)) = (unsafe { slice(data, length) }, unsafe {
        output(out, count)
    }) else {
        return 0;
    };
    crate::echo_detection::tensor(data, width, height, stride, out);
    1
}
/// # Safety
/// The prediction, scores and callback are live for this synchronous call.
/// The callback catches every foreign exception. Status is writable. Returned
/// storage must be released by `okww_rs_echo_detection_free` exactly once.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_echo_postprocess(
    prediction: *const crate::echo_detection::Prediction,
    checkpoint: *const crate::echo_detection::Checkpoint,
    status: *mut i32,
) -> *mut crate::echo_detection::Detections {
    let Some(status) = (unsafe { status.as_mut() }) else {
        return std::ptr::null_mut();
    };
    *status = -4;
    let (Some(p), Some(checkpoint)) = (unsafe { prediction.as_ref() }, unsafe {
        checkpoint.as_ref()
    }) else {
        return std::ptr::null_mut();
    };
    if !(5..=1000).contains(&p.channels) || p.channels.checked_mul(p.rows) != Some(p.length) {
        return std::ptr::null_mut();
    }
    let Some(scores) = (unsafe { slice(p.scores, p.length) }) else {
        return std::ptr::null_mut();
    };
    match crate::echo_detection::postprocess(p, scores, checkpoint) {
        Ok(value) => {
            let result = owned(value);
            *status = if result.is_null() { -3 } else { 1 };
            result
        }
        Err(error) => {
            *status = error;
            std::ptr::null_mut()
        }
    }
}
/// # Safety
/// The pointer is null or a live result returned by this module.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_echo_detection_count(
    value: *const crate::echo_detection::Detections,
) -> usize {
    unsafe { value.as_ref() }.map(|v| v.0.len()).unwrap_or(0)
}
/// # Safety
/// The result is live and output contains `capacity` nonaliasing writable
/// detection records. Empty output accepts null.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_echo_detections(
    value: *const crate::echo_detection::Detections,
    out: *mut crate::echo_detection::Detection,
    capacity: usize,
) -> i32 {
    let Some(value) = (unsafe { value.as_ref() }) else {
        return 0;
    };
    if capacity < value.0.len() {
        return 0;
    }
    let Some(out) = (unsafe { output(out, value.0.len()) }) else {
        return 0;
    };
    out.copy_from_slice(&value.0);
    1
}
/// # Safety
/// The pointer is null or a uniquely owned result returned by this module and
/// must not be used again after this call.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_echo_detection_free(
    value: *mut crate::echo_detection::Detections,
) {
    unsafe {
        dispose(value);
    }
}

/// # Safety
/// Input borrows `height` byte rows, with three channels, at the given stride.
/// Output holds `capacity` writable floats, is already zero initialized and
/// cannot alias input. Padding beyond `width` remains unchanged.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_ocr_normalized(
    data: *const u8,
    width: usize,
    height: usize,
    stride: usize,
    output_width: usize,
    detector: i32,
    out: *mut f32,
    capacity: usize,
) -> i32 {
    let Some(row) = width.checked_mul(3) else {
        return 0;
    };
    let Some(count) = output_width
        .checked_mul(3)
        .and_then(|v| v.checked_mul(height))
    else {
        return 0;
    };
    if width == 0 || height == 0 || width > output_width || stride < row || capacity < count {
        return 0;
    }
    let Some(length) = (height - 1)
        .checked_mul(stride)
        .and_then(|v| v.checked_add(row))
    else {
        return 0;
    };
    let (Some(data), Some(out)) = (unsafe { slice(data, length) }, unsafe {
        output(out, count)
    }) else {
        return 0;
    };
    crate::ocr_numeric::normalized(
        data,
        width,
        height,
        stride,
        output_width,
        detector != 0,
        out,
    );
    1
}

/// # Safety
/// Arguments borrow `count` doubles for this call. Output has four writable
/// i32 values and cannot alias the input.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_image_geometry(
    kind: i32,
    args: *const f64,
    count: usize,
    out: *mut i32,
) -> i32 {
    let (Some(args), Some(out)) = (unsafe { slice(args, count) }, unsafe { output(out, 4) }) else {
        return -5;
    };
    match crate::image_geometry::geometry(kind, args) {
        Ok(value) => {
            out.copy_from_slice(&value);
            1
        }
        Err(code) => -code - 1,
    }
}

/// # Safety
/// Input borrows `height` rows of native-endian 16-bit words at `stride`.
/// Output borrows that many writable byte rows at `output_stride`, without
/// aliasing the input. The caller owns both buffers for this entire call.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_p010_reduce(
    data: *const u8,
    width: usize,
    height: usize,
    stride: usize,
    out: *mut u8,
    output_stride: usize,
) -> i32 {
    let Some(row) = width.checked_mul(2) else {
        return 0;
    };
    if width == 0 || height == 0 || stride < row || output_stride < width {
        return 0;
    }
    let Some(length) = (height - 1)
        .checked_mul(stride)
        .and_then(|v| v.checked_add(row))
    else {
        return 0;
    };
    let Some(out_length) = (height - 1)
        .checked_mul(output_stride)
        .and_then(|v| v.checked_add(width))
    else {
        return 0;
    };
    let (Some(data), Some(out)) = (unsafe { slice(data, length) }, unsafe {
        output(out, out_length)
    }) else {
        return 0;
    };
    crate::image_pixels::p010(data, width, height, stride, out, output_stride);
    1
}

/// # Safety
/// Read/write buffers remain live, aligned for bytes and disjoint for this
/// synchronous call; their full lengths are supplied by the owning Qt images.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_frame_rgb32(
    data: *const u8,
    length: usize,
    width: i32,
    height: i32,
    stride: i32,
    format: i32,
    bottom_up: i32,
    out: *mut u8,
    output_length: usize,
    output_stride: usize,
) -> i32 {
    if width <= 0 || height <= 0 || stride < 0 || !(0..=6).contains(&format) {
        return 0;
    }
    let (Some(data), Some(out)) = (unsafe { slice(data, length) }, unsafe {
        output(out, output_length)
    }) else {
        return 0;
    };
    i32::from(
        crate::frame_pixels::rgb32(
            data,
            width as usize,
            height as usize,
            stride as usize,
            format,
            bottom_up != 0,
            out,
            output_stride,
        )
        .is_some(),
    )
}

#[no_mangle]
pub extern "C" fn okww_rs_diagnostic_tail_start(size: i64) -> i64 {
    size.wrapping_sub(65536).max(0)
}

/// # Safety
/// Output borrows four writable bytes for this synchronous call.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_capture_fourcc(value: u32, bits: i32, out: *mut u8) -> i32 {
    let Some(out) = (unsafe { output(out, 4) }) else {
        return 0;
    };
    let (bytes, length) = crate::capture_math::fourcc(value, bits);
    out.copy_from_slice(&bytes);
    length as i32
}
/// # Safety
/// Name borrows the supplied count of UTF-16 units; no pointer is retained.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_capture_format(name: *const u16, length: usize) -> i32 {
    let Some(name) = (unsafe { slice(name, length) }) else {
        return -1;
    };
    crate::capture_math::format(name)
}
#[no_mangle]
pub extern "C" fn okww_rs_capture_rate(period: i64) -> f64 {
    crate::capture_math::rate(period)
}
#[no_mangle]
pub extern "C" fn okww_rs_capture_payload(
    length: usize,
    stride: i32,
    height: i32,
    format: i32,
) -> i32 {
    crate::capture_math::payload(length, stride, height, format)
}
#[no_mangle]
pub extern "C" fn okww_rs_capture_recent(now: i64, back: i64) -> i32 {
    i32::from(now.wrapping_sub(back) < 1_000_000_000)
}
#[no_mangle]
pub extern "C" fn okww_rs_capture_fps(count: usize, back: i64, front: i64) -> f64 {
    crate::capture_math::fps(count, back, front)
}
#[unsafe(no_mangle)]
pub extern "C" fn okww_rs_capture_stride(width: i32, format: i32) -> i32 {
    crate::capture_math::stride(width, format)
}
#[unsafe(no_mangle)]
pub extern "C" fn okww_rs_capture_bottom(height: i32, format: i32) -> i32 {
    i32::from(crate::capture_math::bottom(height, format))
}
#[unsafe(no_mangle)]
pub extern "C" fn okww_rs_capture_timer_due(remaining: i64) -> i64 {
    crate::capture_math::timer_due(remaining)
}
#[unsafe(no_mangle)]
pub extern "C" fn okww_rs_capture_recovery_expired(now: i64, began: i64) -> i32 {
    i32::from(crate::capture_math::recovery_expired(now, began))
}

/// # Safety
/// `id` borrows `length` readable UTF-8 bytes for this call, or is null for an
/// empty ID. No pointer is retained.
#[no_mangle]
pub unsafe extern "C" fn okww_rs_business_task_kind(id: *const u8, length: usize) -> i32 {
    let Some(id) = (unsafe { slice(id, length) }) else {
        return -1;
    };
    crate::task_ids::kind(id)
}

#[unsafe(no_mangle)]
/// # Safety
/// A non-null output must point to two writable u32 values.
pub unsafe extern "C" fn okww_rs_desktop_hid(usage: i32, down: i32, out: *mut u32) -> i32 {
    let Some(value) = crate::desktop_input::hid_input(usage, down != 0) else {
        return 0;
    };
    if out.is_null() {
        return 0;
    }
    // SAFETY: the native adapter provides two writable u32 values.
    unsafe {
        std::ptr::copy_nonoverlapping(value.as_ptr(), out, 2);
    }
    1
}
#[unsafe(no_mangle)]
/// # Safety
/// A non-null output must point to two writable i32 values.
pub unsafe extern "C" fn okww_rs_desktop_absolute(
    x: i32,
    y: i32,
    left: i32,
    top: i32,
    width: i32,
    height: i32,
    contains: i32,
    out: *mut i32,
) -> i32 {
    let Some(value) =
        crate::desktop_input::absolute([x, y], [left, top, width, height], contains != 0)
    else {
        return 0;
    };
    if out.is_null() {
        return 0;
    }
    // SAFETY: the native adapter provides two writable i32 values.
    unsafe {
        std::ptr::copy_nonoverlapping(value.as_ptr(), out, 2);
    }
    1
}

#[unsafe(no_mangle)]
pub extern "C" fn okww_rs_controller_elapsed_ms(now: i64, began: i64) -> f64 {
    crate::controller_math::elapsed_ms(now, began)
}
/// # Safety
/// A non-null output must point to two writable i32 values.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn okww_rs_controller_map_point(
    x: i32,
    y: i32,
    width: i32,
    height: i32,
    left: i32,
    top: i32,
    rect_width: i32,
    rect_height: i32,
    out: *mut i32,
) -> i32 {
    let Some(value) = crate::controller_math::map_point(
        [x, y],
        [width, height],
        [left, top, rect_width, rect_height],
    ) else {
        return 0;
    };
    if out.is_null() {
        return 0;
    }
    // SAFETY: native adapter provides two writable i32 values.
    unsafe {
        std::ptr::copy_nonoverlapping(value.as_ptr(), out, 2);
    }
    1
}
/// # Safety
/// Non-null output must identify length writable bytes, borrowed exclusively for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn okww_rs_backend_simulation(
    width: i32,
    height: i32,
    stride: i32,
    output: *mut u8,
    length: usize,
) -> i32 {
    if output.is_null() {
        return 0;
    }
    // SAFETY: native adapter allocates and exclusively borrows the indicated buffer.
    i32::from(crate::backend_pixels::simulation(
        width,
        height,
        stride,
        unsafe { std::slice::from_raw_parts_mut(output, length) },
    ))
}
#[unsafe(no_mangle)]
pub extern "C" fn okww_rs_backend_simulation_sequence(stamp: i64) -> u64 {
    crate::backend_state::simulation_sequence(stamp)
}
#[unsafe(no_mangle)]
pub extern "C" fn okww_rs_backend_recovery_timed_out(now: i64, began: i64) -> i32 {
    i32::from(crate::backend_state::recovery_timed_out(now, began))
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn abi_sizes_match_cpp() {
        assert_eq!(std::mem::size_of::<crate::echo_detection::Detection>(), 32);
        assert_eq!(std::mem::size_of::<crate::echo_detection::Prediction>(), 64);
        assert_eq!(std::mem::size_of::<crate::echo_detection::Checkpoint>(), 16);
        assert_eq!(std::mem::size_of::<crate::engine::Host>(), 16);
        assert_eq!(std::mem::size_of::<crate::session::Host>(), 16);
        assert_eq!(std::mem::size_of::<crate::visionflow::Host>(), 16);
        assert_eq!(std::mem::size_of::<crate::services::Host>(), 16);
        assert_eq!(std::mem::size_of::<crate::taskflow::Host>(), 24);
        assert_eq!(std::mem::size_of::<crate::rotation::Host>(), 24);
        assert_eq!(std::mem::size_of::<Freeze>(), 24);
        assert_eq!(std::mem::size_of::<CharacterState>(), 64);
        assert_eq!(std::mem::size_of::<Candidate>(), 72);
        assert_eq!(std::mem::size_of::<RestartState>(), 28);
        assert_eq!(std::mem::size_of::<Decision>(), 16);
    }
    #[test]
    fn opaque_objects_clone_and_release() {
        unsafe {
            let time = okww_rs_freeze_new();
            assert!(!time.is_null());
            assert_eq!(okww_rs_freeze_add(time, 10.0, 2.0, 2.0, 0.1), 1);
            let copy = okww_rs_freeze_clone(time);
            okww_rs_freeze_clear(time);
            assert_eq!(okww_rs_freeze_count(copy), 1);
            okww_rs_freeze_free(time);
            okww_rs_freeze_free(copy);
            okww_rs_freeze_free(std::ptr::null_mut());
        }
    }
    #[test]
    fn empty_ffi_arrays_accept_null() {
        unsafe {
            assert_eq!(okww_rs_select_card(std::ptr::null(), 0), -1);
            assert_eq!(
                okww_rs_order_items(std::ptr::null(), 0, 10, std::ptr::null_mut(), 0),
                0
            );
        }
    }
}
