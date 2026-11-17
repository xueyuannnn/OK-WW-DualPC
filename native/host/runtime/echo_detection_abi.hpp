#pragma once
#include "rust_core.hpp"
namespace okww::rust {
struct EchoDetection {
    int32_t x, y, width, height, category, reserved;
    double confidence;
};
struct EchoPrediction {
    const float *scores;
    size_t length, channels, rows;
    double gain, threshold;
    int32_t left, top, label, checkpoints;
};
struct EchoCheckpoint {
    void *context;
    int32_t (*invoke)(void *);
};
struct EchoDetections;
static_assert(sizeof(EchoDetection) == 32 && sizeof(EchoPrediction) == 64 && sizeof(EchoCheckpoint) == 16);
extern "C" {
int32_t okww_rs_echo_layout(int32_t width, int32_t height, int32_t targetWidth, int32_t targetHeight,
                            double *out5);
int32_t okww_rs_echo_tensor(const uint8_t *data, size_t width, size_t height, size_t stride, float *out,
                            size_t capacity);
EchoDetections *okww_rs_echo_postprocess(const EchoPrediction *, const EchoCheckpoint *, int32_t *status);
size_t okww_rs_echo_detection_count(const EchoDetections *);
int32_t okww_rs_echo_detections(const EchoDetections *, EchoDetection *out, size_t capacity);
void okww_rs_echo_detection_free(EchoDetections *);
}
} // namespace okww::rust
