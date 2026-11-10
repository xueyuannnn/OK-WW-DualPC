#include "gauge_analysis.hpp"
#include <opencv2/imgproc.hpp>
namespace okww::runtime {
double gaugeColorRatio(const cv::Mat &image, ColorRange color) {
    if (image.empty())
        return 0;
    cv::Mat mask;
    cv::inRange(image, color.lower, color.upper, mask);
    return double(cv::countNonZero(mask)) / double(mask.total());
}
double gaugeRingRatio(const cv::Mat &image, ColorRange color, double inner, double outer, bool nonzero) {
    if (image.empty())
        return 0;
    const int r1 = int(std::floor(image.rows * inner)), r2 = int(std::ceil(image.rows * outer));
    if (!nonzero && r2 <= r1)
        return 0;
    cv::Mat ring = cv::Mat::zeros(image.rows, image.cols, CV_8U);
    const cv::Point center(image.cols / 2, image.rows / 2);
    if (r2 > 0)
        cv::circle(ring, center, r2, 255, -1);
    if (r1 > 0)
        cv::circle(ring, center, r1, 0, -1);
    cv::Mat colored, valid;
    int denominator;
    if (nonzero) {
        cv::Mat masked;
        cv::bitwise_and(image, image, masked, ring);
        cv::inRange(masked, cv::Scalar(1, 1, 1), cv::Scalar(255, 255, 255), valid);
        denominator = cv::countNonZero(valid);
        cv::inRange(masked, color.lower, color.upper, colored);
    } else {
        denominator = cv::countNonZero(ring);
        cv::inRange(image, color.lower, color.upper, colored);
        colored &= ring;
    }
    return denominator ? double(cv::countNonZero(colored)) / denominator : 0;
}
static QVector<double> spectrum(const cv::Mat &mask) {
    cv::Mat profile(1, mask.cols, CV_32F);
    float sum = 0;
    for (int x = 0; x < mask.cols; ++x) {
        const float n = float(cv::countNonZero(mask.col(x)));
        profile.at<float>(0, x) = n;
        sum += n;
    }
    profile -= sum / float(mask.cols);
    cv::Mat values, transformed;
    profile.convertTo(values, CV_64F);
    cv::dft(values, transformed, cv::DFT_COMPLEX_OUTPUT);
    QVector<double> result(mask.cols);
    for (int x = 0; x < mask.cols; ++x) {
        const auto v = transformed.at<cv::Vec2d>(0, x);
        result[x] = std::hypot(v[0], v[1]);
    }
    return result;
}
int phoebeGaugeValue(const cv::Mat &image, ColorRange color, int segments, int minFrequency, int maxFrequency,
                     double minAmplitude) {
    if (image.empty() || segments < 1)
        return 0;
    cv::Mat mask;
    cv::inRange(image, color.lower, color.upper, mask);
    const int step = mask.cols / segments;
    if (step < 64)
        return 0;
    int count = 0;
    bool failed = false;
    for (int left = 0; left + step < mask.cols; left += step) {
        const auto part = mask(cv::Rect(left, 0, step, mask.rows));
        const int white = cv::countNonZero(part);
        bool score = false;
        if (white > 0 && white < part.total()) {
            const auto amplitudes = spectrum(part);
            const double amplitude = *std::max_element(amplitudes.begin() + 1, amplitudes.end());
            score = (minFrequency <= step && step <= maxFrequency) || amplitude >= minAmplitude;
        }
        if (!failed && score)
            ++count;
        if (!score)
            failed = true;
    }
    return count;
}
std::pair<int, int> stripeRegion(const cv::Mat &gray) {
    if (gray.empty() || gray.cols < 64 || gray.type() != CV_8UC1)
        return {-1, -1};
    cv::Mat white, black;
    cv::compare(gray, 255, white, cv::CMP_EQ);
    cv::compare(gray, 0, black, cv::CMP_EQ);
    if (!cv::countNonZero(white | black))
        return {-1, -1};
    if (!cv::countNonZero(white))
        return {0, 0};
    cv::Mat clean = gray.clone();
    const int width = gray.cols, threshold = int(width * .009);
    if (threshold >= 1)
        for (int y = 0; y < gray.rows; ++y) {
            const auto row = gray.ptr<uchar>(y);
            auto target = clean.ptr<uchar>(y);
            for (int x = 0; x < width;) {
                if (row[x] != 255) {
                    ++x;
                    continue;
                }
                const int start = x;
                while (x < width && row[x] == 255)
                    ++x;
                if (x - start < threshold)
                    std::fill(target + start, target + x, 0);
            }
        }
    const int window = std::max(8, int(width * .045)), step = std::max(1, int(width * .012));
    QVector<double> scores, ratios;
    QVector<int> positions;
    for (int x = 0; x + window <= width; x += step) {
        cv::Mat part = clean(cv::Rect(x, 0, window, clean.rows)), binary;
        cv::compare(part, 255, binary, cv::CMP_EQ);
        const double ratio = double(cv::countNonZero(binary)) / part.total();
        double score = 0;
        if (ratio >= .05 && ratio <= .75) {
            const auto amplitudes = spectrum(binary);
            score = *std::max_element(amplitudes.begin() + 1, amplitudes.begin() + window / 2);
        }
        scores.append(score);
        ratios.append(ratio);
        positions.append(x);
    }
    if (scores.empty())
        return {-1, -1};
    const double maximum = *std::max_element(scores.begin(), scores.end()),
                 cutoff = std::max(1.5, maximum * .45);
    if (maximum < .001)
        return {0, 0};
    int endIndex = 0;
    for (int i = 0; i < scores.size(); ++i) {
        if (scores[i] >= cutoff)
            endIndex = i;
        else if (ratios[i] < .375) {
            if (std::any_of(scores.begin() + i + 1, scores.end(), [&](double v) { return v > cutoff; }))
                return {-2, -2};
            break;
        }
    }
    return {0, endIndex == 0                                  ? 0
               : positions[endIndex] + window + step >= width ? width
                                                              : positions[endIndex]};
}
double stripedGaugeRatio(const cv::Mat &image, ColorRange color) {
    if (image.empty())
        return 0;
    cv::Mat mask;
    cv::inRange(image, color.lower, color.upper, mask);
    const auto [start, end] = stripeRegion(mask);
    if (start == -2)
        return -1;
    if (start == -1)
        return std::min(1., gaugeColorRatio(image, color) * 2);
    return double(end - start) / image.cols;
}
double gaugeRoundHalfUp(double value, int decimals) {
    // Inputs are ratios, bounded to [-1,1]; decimal ties follow the source's
    // Decimal(str(value)) ROUND_HALF_UP rather than banker's rounding.
    const double scale = std::pow(10., decimals);
    return std::copysign(std::floor(std::abs(value) * scale + .5) / scale, value);
}
} // namespace okww::runtime
