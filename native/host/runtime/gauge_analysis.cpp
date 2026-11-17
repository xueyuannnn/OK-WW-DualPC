#include "gauge_analysis.hpp"
#include "rust_core.hpp"
#include "workflow_abi.hpp"
#include <opencv2/imgproc.hpp>
namespace okww::runtime {
namespace {
std::array<int32_t, 2> analyze(const cv::Mat &mask, int kind, int segments = 0, int min = 0, int max = 0,
                               double amplitude = 0) {
    std::exception_ptr error;
    const rust::SpectrumHost host{
        &error, [](void *context, const double *values, size_t count, double *out) -> int32_t {
            try {
                cv::Mat input(1, int(count), CV_64F, const_cast<double *>(values)), transformed;
                cv::dft(input, transformed, cv::DFT_COMPLEX_OUTPUT);
                if (!rust::okww_rs_spectrum_magnitudes(reinterpret_cast<const double *>(transformed.data),
                                                       count, out, count))
                    fail("Rust 频谱缓冲区无效");
                return 1;
            } catch (...) {
                *static_cast<std::exception_ptr *>(context) = std::current_exception();
                return 0;
            }
        }};
    std::array<int32_t, 2> out{};
    const auto status = rust::okww_rs_gauge_segments(&host, mask.data, mask.cols, mask.rows, mask.step[0],
                                                     kind, segments, min, max, amplitude, out.data());
    if (error)
        std::rethrow_exception(error);
    if (status != 1)
        throw std::bad_alloc();
    return out;
}
} // namespace
namespace {
std::array<double, 3> gaugePipeline(rust::WorkflowMethod method, const cv::Mat &image, ColorRange color,
                                    std::initializer_list<double> args = {}) {
    cv::Mat mask, ring, colored, valid, masked;
    int denominator = 0;
    return rust::workflowRun(
        method, std::span<const double>(args.begin(), args.size()),
        [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::GaugeImageEmpty:
                out[0] = image.empty();
                break;
            case rust::WorkflowOp::GaugeThreshold:
                cv::inRange(image, color.lower, color.upper, mask);
                break;
            case rust::WorkflowOp::GaugeMaskRatio:
                out[0] = rust::okww_rs_gauge_ratio(cv::countNonZero(mask), mask.total());
                break;
            case rust::WorkflowOp::GaugePhoebeAnalyze:
                out[0] = analyze(mask, 1, int(a[0]), int(a[1]), int(a[2]), a[3])[0];
                break;
            case rust::WorkflowOp::GaugeFindStripe: {
                const auto [start, end] = stripeRegion(mask);
                out[0] = start;
                out[1] = end;
                break;
            }
            case rust::WorkflowOp::GaugeStripedRatio:
                out[0] = rust::okww_rs_stripe_ratio(int(a[0]), int(a[1]), image.cols, cv::countNonZero(mask),
                                                    mask.total());
                break;
            case rust::WorkflowOp::GaugeRingBounds: {
                int32_t radii[2]{};
                out[0] = rust::okww_rs_ring_bounds(image.rows, a[0], a[1], a[2] != 0, radii);
                out[1] = radii[0];
                out[2] = radii[1];
                break;
            }
            case rust::WorkflowOp::GaugeCreateRing:
                ring = cv::Mat::zeros(image.rows, image.cols, CV_8U);
                break;
            case rust::WorkflowOp::GaugeOuterCircle:
                cv::circle(ring, {image.cols / 2, image.rows / 2}, int(a[0]), 255, -1);
                break;
            case rust::WorkflowOp::GaugeInnerCircle:
                cv::circle(ring, {image.cols / 2, image.rows / 2}, int(a[0]), 0, -1);
                break;
            case rust::WorkflowOp::GaugeMaskRingImage:
                cv::bitwise_and(image, image, masked, ring);
                break;
            case rust::WorkflowOp::GaugeNonzeroMask:
                cv::inRange(masked, cv::Scalar(1, 1, 1), cv::Scalar(255, 255, 255), valid);
                break;
            case rust::WorkflowOp::GaugeCountNonzeroDenominator:
                denominator = cv::countNonZero(valid);
                break;
            case rust::WorkflowOp::GaugeColorMasked:
                cv::inRange(masked, color.lower, color.upper, colored);
                break;
            case rust::WorkflowOp::GaugeDropMasked:
                masked.release();
                break;
            case rust::WorkflowOp::GaugeCountRingDenominator:
                denominator = cv::countNonZero(ring);
                break;
            case rust::WorkflowOp::GaugeColorImage:
                cv::inRange(image, color.lower, color.upper, colored);
                break;
            case rust::WorkflowOp::GaugeApplyRing:
                colored &= ring;
                break;
            case rust::WorkflowOp::GaugeRingRatio:
                out[0] = rust::okww_rs_gauge_ratio(cv::countNonZero(colored), denominator);
                break;
            case rust::WorkflowOp::GaugeGrayType:
                out[0] = image.type() == CV_8UC1;
                break;
            case rust::WorkflowOp::GaugeStripeAnalyze: {
                const auto values = analyze(image, 0);
                out[0] = values[0];
                out[1] = values[1];
                break;
            }
            default:
                fail("Rust 量表图像宿主操作无效");
            }
        });
}
} // namespace
double gaugeColorRatio(const cv::Mat &image, ColorRange color) {
    return gaugePipeline(rust::WorkflowMethod::GaugeColorPipeline, image, color)[0];
}
double gaugeRingRatio(const cv::Mat &image, ColorRange color, double inner, double outer, bool nonzero) {
    return gaugePipeline(rust::WorkflowMethod::GaugeRingPipeline, image, color,
                         {inner, outer, double(nonzero)})[0];
}
int phoebeGaugeValue(const cv::Mat &image, ColorRange color, int segments, int min, int max,
                     double amplitude) {
    return int(gaugePipeline(rust::WorkflowMethod::GaugePhoebePipeline, image, color,
                             {double(segments), double(min), double(max), amplitude})[0]);
}
std::pair<int, int> stripeRegion(const cv::Mat &gray) {
    const auto out = gaugePipeline(rust::WorkflowMethod::GaugeStripePipeline, gray, {});
    return {int(out[0]), int(out[1])};
}
int nativeSegmentedGaugeMask(const cv::Mat &mask, int segments, int minFrequency, int maxFrequency,
                             double minAmplitude) {
    return analyze(mask, 2, segments, minFrequency, maxFrequency, minAmplitude)[0];
}
double stripedGaugeRatio(const cv::Mat &image, ColorRange color) {
    return gaugePipeline(rust::WorkflowMethod::GaugeStripedPipeline, image, color)[0];
}
double gaugeRoundHalfUp(double value, int decimals) { return rust::okww_rs_gauge_round(value, decimals); }
} // namespace okww::runtime
