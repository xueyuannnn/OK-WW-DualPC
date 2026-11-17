#pragma once
#include <cstdint>
namespace okww::rust {
extern "C" int32_t okww_rs_desktop_hid(int32_t, int32_t, uint32_t *);
extern "C" int32_t okww_rs_desktop_absolute(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
                                            int32_t *);
} // namespace okww::rust
