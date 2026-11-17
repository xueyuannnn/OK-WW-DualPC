#pragma once
#include "vision.hpp"
namespace okww::runtime {
using OcrQuad = std::array<cv::Point2f, 4>;
std::pair<OcrQuad, float> miniBox(const std::vector<cv::Point2f> &);
double boxScore(const cv::Mat &, const OcrQuad &);
std::vector<cv::Point2f> unclip(const OcrQuad &);
cv::Mat textCrop(const cv::Mat &, const OcrQuad &);
} // namespace okww::runtime
