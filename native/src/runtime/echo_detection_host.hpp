#pragma once
#include "vision.hpp"
namespace okww::runtime {
QVector<Match> echoPrediction(const float *scores, size_t length, size_t channels, size_t rows, double gain,
                              int left, int top, double threshold, int label,
                              const std::function<void()> &checkpoint);
}
