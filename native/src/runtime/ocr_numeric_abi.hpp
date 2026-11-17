#pragma once
#include "rust_core.hpp"
namespace okww::rust {
extern "C" {
int32_t okww_rs_ocr_normalized(const uint8_t *, size_t width, size_t height, size_t stride,
                               size_t outputWidth, int32_t detector, float *out, size_t capacity);
}
} // namespace okww::rust
