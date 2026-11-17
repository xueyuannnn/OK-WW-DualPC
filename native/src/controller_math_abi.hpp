#pragma once
#include <cstdint>
namespace okww::rust {
extern "C" double okww_rs_controller_elapsed_ms(int64_t, int64_t);
extern "C" int32_t okww_rs_controller_map_point(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
                                                int32_t, int32_t *);
} // namespace okww::rust
