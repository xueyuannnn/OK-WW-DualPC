#pragma once
#include <cstddef>
#include <cstdint>
namespace okww::rust {
extern "C" int32_t okww_rs_capture_fourcc(uint32_t, int32_t, uint8_t *);
extern "C" int32_t okww_rs_capture_format(const uint16_t *, size_t);
extern "C" double okww_rs_capture_rate(int64_t);
extern "C" int32_t okww_rs_capture_payload(size_t, int32_t, int32_t, int32_t);
extern "C" int32_t okww_rs_capture_recent(int64_t, int64_t);
extern "C" double okww_rs_capture_fps(size_t, int64_t, int64_t);
extern "C" int32_t okww_rs_capture_stride(int32_t, int32_t);
extern "C" int32_t okww_rs_capture_bottom(int32_t, int32_t);
extern "C" int64_t okww_rs_capture_timer_due(int64_t);
extern "C" int32_t okww_rs_capture_recovery_expired(int64_t, int64_t);
} // namespace okww::rust
