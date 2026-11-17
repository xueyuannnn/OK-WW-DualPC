#pragma once
#include "runtime/gauge_analysis.hpp"
#include <opencv2/imgproc.hpp>
namespace okww::runtime::combat_images_reference {
class OriginalConcerto {
    std::array<double,6> fullAreas_{};
  public:
    explicit OriginalConcerto(std::array<double,6> values={}):fullAreas_(values){}
    double detect(const cv::Mat&,CharacterState&);
    const std::array<double,6>&fullAreas()const{return fullAreas_;}
    static QRect region(QSize);
};
const std::array<ColorRange, 6> &concertoColors() {
    static const std::array<ColorRange, 6> colors{{{{90, 190, 205}, {130, 222, 235}},
                                                   {{210, 95, 150}, {249, 140, 190}},
                                                   {{75, 100, 200}, {105, 130, 230}},
                                                   {{210, 150, 60}, {245, 180, 95}},
                                                   {{155, 215, 70}, {190, 250, 110}},
                                                   {{145, 65, 190}, {175, 105, 220}}}};
    return colors;
}
RingResult countRing(const cv::Mat &bgr, ColorRange color, double minBoundingArea) {
    if (bgr.empty() || bgr.type() != CV_8UC3 || bgr.rows < 4 || bgr.cols < 4)
        fail("协奏环图像无效");
    const cv::Point center(bgr.cols / 2, bgr.rows / 2);
    cv::Mat mask = cv::Mat::zeros(bgr.size(), CV_8UC1), masked, raw, closed, labels, stats, centroids;
    cv::circle(mask, center, int(std::ceil(bgr.rows * .42261)), 255, -1);
    cv::circle(mask, center, int(std::floor(bgr.rows * .35119)), 0, -1);
    cv::bitwise_and(bgr, bgr, masked, mask);
    cv::inRange(masked, color.lower, color.upper, raw);
    cv::morphologyEx(raw, closed, cv::MORPH_CLOSE, cv::getStructuringElement(cv::MORPH_RECT, {3, 3}));
    const cv::Rect stripe(center.x + 1, center.y - 1, bgr.cols - center.x - 1, 3);
    raw(stripe).copyTo(closed(stripe));
    const int count = cv::connectedComponentsWithStats(closed, labels, stats, centroids, 8);
    RingResult result;
    for (int label = 1; label < count; ++label) {
        if (double(stats.at<int>(label, cv::CC_STAT_WIDTH)) * stats.at<int>(label, cv::CC_STAT_HEIGHT) <
            minBoundingArea)
            continue;
        cv::Mat component = labels == label;
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(component, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        if (contours.size() == 1) {
            std::vector<cv::Point> polygon;
            cv::approxPolyDP(contours[0], polygon, .05 * cv::arcLength(contours[0], true), true);
            if (polygon.size() >= 4 && cv::isContourConvex(polygon))
                result.full = true;
        }
        result.area = stats.at<int>(label, cv::CC_STAT_AREA);
        ++result.components;
    }
    if (result.components > 1) {
        result.full = false;
        result.area = 0;
    }
    return result;
}
QRect OriginalConcerto::region(QSize screen) {
    return Templates::anchored({1431, 1942, 126, 126}, {3840, 2160}, screen, true, false);
}
double OriginalConcerto::detect(const cv::Mat &frame, CharacterState &character) {
    QRect box = region({frame.cols, frame.rows});
    if (!QRect(0, 0, frame.cols, frame.rows).contains(box))
        fail("协奏环区域超出画面");
    if (character.ringIndex < 0) {
        double best = 0;
        character.ringIndex = 0;
        for (int i = 0; i < 6; ++i) {
            auto color = concertoColors()[i];
            const double p = colorPercentage(frame, box, color.lower, color.upper);
            if (p > best) {
                best = p;
                character.ringIndex = i;
            }
        }
    }
    if (character.ringIndex >= 6)
        fail("角色属性索引无效");
    const auto ring =
        combat_images_reference::countRing(frame(cv::Rect(box.x(), box.y(), box.width(), box.height())),
                  concertoColors()[character.ringIndex], 1500. / 3840 / 2160 * frame.cols * frame.rows);
    double &full = fullAreas_[character.ringIndex];
    if (ring.full)
        full = ring.area;
    const double percent = ring.full ? 1 : full > 0 ? std::min(.99, ring.area / full) : 0;
    character.currentCon = percent;
    return percent;
}

}


namespace okww::runtime::combat_images_reference {
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
        return std::min(1., combat_images_reference::gaugeColorRatio(image, color) * 2);
    return double(end - start) / image.cols;
}
double gaugeRoundHalfUp(double value, int decimals) {
    // Inputs are ratios, bounded to [-1,1]; decimal ties follow the source's
    // Decimal(str(value)) ROUND_HALF_UP rather than banker's rounding.
    const double scale = std::pow(10., decimals);
    return std::copysign(std::floor(std::abs(value) * scale + .5) / scale, value);
}
} // namespace okww::runtime
