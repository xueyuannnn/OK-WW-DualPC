#pragma once
#include <cstdint>
namespace okww::rust {
extern "C" uint64_t okww_rs_backend_simulation_sequence(int64_t);
extern "C" int32_t okww_rs_backend_recovery_timed_out(int64_t, int64_t);
} // namespace okww::rust
