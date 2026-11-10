#pragma once
#include "combat_rules.hpp"
namespace okww::runtime {
double gaugeColorRatio(const cv::Mat &image, ColorRange color);
double gaugeRingRatio(const cv::Mat &image, ColorRange color, double inner, double outer,
                      bool nonzeroDenominator = false);
// Preserve Phoebe's original width test and strict final-segment boundary.
int phoebeGaugeValue(const cv::Mat &image, ColorRange color, int segments, int minFrequency, int maxFrequency,
                     double minAmplitude);
std::pair<int, int> stripeRegion(const cv::Mat &gray);
double stripedGaugeRatio(const cv::Mat &image, ColorRange color);
double gaugeRoundHalfUp(double value, int decimals);
} // namespace okww::runtime
