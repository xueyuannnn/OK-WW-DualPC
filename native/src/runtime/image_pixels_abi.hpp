#pragma once
#include "rust_core.hpp"
namespace okww::rust {
extern "C" int32_t okww_rs_p010_reduce(const uint8_t *, size_t width, size_t height, size_t stride,
                                       uint8_t *out, size_t outputStride);
}
