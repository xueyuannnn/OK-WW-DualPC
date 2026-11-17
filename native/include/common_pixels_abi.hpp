#pragma once
#include <cstddef>
#include <cstdint>
namespace okww::rust {
extern "C" int32_t okww_rs_frame_rgb32(const uint8_t *, size_t length, int32_t width, int32_t height,
                                       int32_t stride, int32_t format, int32_t bottomUp, uint8_t *,
                                       size_t outputLength, size_t outputStride);
}
