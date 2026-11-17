#ifndef Q_MOC_RUN
#include "reference_combat_images.hpp"
#include "reference_task_text.hpp"
#endif
#include <QtTest>
#include <bit>
#include <limits>
using namespace okww;
using namespace okww::runtime;
namespace ref = okww::runtime::combat_images_reference;
namespace {
template <class F> QString failure(F action) {
    try {
        action();
        return {};
    } catch (const std::exception &e) {
        return errorText(e);
    }
}
bool same(double a, double b) {
    return std::isnan(a) && std::isnan(b) || std::bit_cast<uint64_t>(a) == std::bit_cast<uint64_t>(b);
}
cv::Mat pattern(int scene, int w, int h, cv::Scalar ink) {
    cv::Mat parent(h + 5, w + 7, CV_8UC3, cv::Scalar(0, 0, 0));
    auto image = parent(cv::Rect(2, 3, w, h));
    const cv::Point center(w / 2, h / 2);
    const int radius = std::max(1, int(h * .39));
    switch (scene % 7) {
    case 1:
        image.setTo(ink);
        break;
    case 2:
        cv::circle(image, center, radius, ink, 2);
        break;
    case 3:
        cv::ellipse(image, center, {radius, radius}, 0, 15, 170, ink, 3);
        break;
    case 4:
        cv::rectangle(image, {std::max(0, w / 2 - 2), 1, std::min(4, w), std::max(1, h - 2)}, ink, -1);
        break;
    case 5:
        for (int x = 0; x < w; ++x)
            if ((x + scene) % 7 < 3)
                image.col(x).setTo(ink);
        break;
    case 6:
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if ((x * 17 + y * 31 + scene) % 11 < 4)
                    image.at<cv::Vec3b>(y, x) = cv::Vec3b(uchar(ink[0]), uchar(ink[1]), uchar(ink[2]));
        break;
    }
    return image;
}
} // namespace
class CombatImageContracts : public QObject {
    Q_OBJECT
  private slots:
    void magnitudes_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 512; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void magnitudes() {
        QFETCH(int, scene);
        std::array<double, 128> values{};
        std::array<double, 64> out{};
        uint64_t rng = uint64_t(scene + 1) * 0x9e3779b97f4a7c15ULL;
        for (auto &value : values) {
            rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
            const auto exponent = uint64_t(50 + (rng >> 54) % 1900);
            value = std::bit_cast<double>((rng & 0x800fffffffffffffULL) | (exponent << 52));
        }
        if (scene % 7 == 0) {
            values[0] = 0.;
            values[1] = -0.;
        }
        if (scene % 11 == 0) {
            values[2] = std::numeric_limits<double>::infinity();
            values[3] = std::numeric_limits<double>::quiet_NaN();
        }
        QCOMPARE(rust::okww_rs_spectrum_magnitudes(values.data(), 64, out.data(), out.size()), 1);
        for (size_t i = 0; i < out.size(); ++i)
            QVERIFY(same(out[i], std::hypot(values[i * 2], values[i * 2 + 1])));
        QCOMPARE(rust::okww_rs_spectrum_magnitudes(nullptr, 0, nullptr, 0), 1);
        QCOMPARE(rust::okww_rs_spectrum_magnitudes(values.data(), 64, out.data(), 63), 0);
    }
    void rings_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 2048; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void rings() {
        QFETCH(int, scene);
        const auto color = concertoColors()[scene % 6];
        const auto ink = (color.lower + color.upper) * .5;
        auto image = pattern(scene, 4 + scene % 97, 4 + (scene / 7) % 91, ink);
        if (scene % 29 == 0)
            image = cv::Mat(3, 4, CV_8UC3);
        if (scene % 31 == 0)
            image = {};
        if (scene % 37 == 0)
            image = cv::Mat(12, 12, CV_8U, cv::Scalar(0));
        const double area = scene % 13 == 0   ? std::numeric_limits<double>::quiet_NaN()
                            : scene % 17 == 0 ? std::numeric_limits<double>::infinity()
                                              : (scene % 11) * 20.;
        RingResult a, b;
        auto expected = failure([&] { a = ref::countRing(image, color, area); });
        auto actual = failure([&] { b = countRing(image, color, area); });
        QCOMPARE(actual, expected);
        if (!actual.isEmpty())
            return;
        QCOMPARE(b.area, a.area);
        QCOMPARE(b.full, a.full);
        QCOMPARE(b.components, a.components);
    }
    void calibration_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 384; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void calibration() {
        QFETCH(int, scene);
        std::array<double, 6> areas{};
        for (int i = 0; i < 6; ++i)
            areas[i] = scene % 7 == 0    ? std::numeric_limits<double>::quiet_NaN()
                       : scene % 11 == 0 ? std::numeric_limits<double>::infinity()
                                         : double(i * 53 + (scene % 9) * 31);
        ref::OriginalConcerto old(areas);
        Concerto candidate(areas);
        CharacterState a, b;
        a.ringIndex = b.ringIndex = scene % 10 - 2;
        a.currentCon = b.currentCon = .4;
        const int width = std::array<int, 3>{640, 1280, 1920}[scene % 3], height = width * 9 / 16;
        const auto color = concertoColors()[(scene / 3) % 6];
        const auto ink = (color.lower + color.upper) * .5;
        for (int step = 0; step < 3; ++step) {
            cv::Mat frame = cv::Mat::zeros(height, width, CV_8UC3);
            auto box = Concerto::region({width, height});
            pattern(scene + step, box.width(), box.height(), ink)
                .copyTo(frame(cv::Rect(box.x(), box.y(), box.width(), box.height())));
            double x = -7, y = -7;
            const auto expected = failure([&] { x = old.detect(frame, a); });
            const auto actual = failure([&] { y = candidate.detect(frame, b); });
            QCOMPARE(actual, expected);
            QCOMPARE(a.ringIndex, b.ringIndex);
            QVERIFY(same(a.currentCon, b.currentCon));
            QVERIFY(same(x, y));
            for (int i = 0; i < 6; ++i)
                QVERIFY(same(old.fullAreas()[i], candidate.fullAreas()[i]));
        }
    }
    void gauges_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 512; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void gauges() {
        QFETCH(int, scene);
        const auto color = concertoColors()[scene % 6];
        const auto ink = (color.lower + color.upper) * .5;
        auto image = pattern(scene, 8 + (scene % 19) * 7, 5 + (scene / 7) % 31, ink);
        if (scene % 41 == 0)
            image = {};
        const double inner = std::array<double, 5>{-.1, 0., .2, .35, .8}[scene % 5],
                     outer = std::array<double, 4>{.3, .5, .85, 0.}[(scene / 5) % 4];
        QVERIFY(same(gaugeColorRatio(image, color), ref::gaugeColorRatio(image, color)));
        QVERIFY(same(gaugeRingRatio(image, color, inner, outer, scene % 2),
                     ref::gaugeRingRatio(image, color, inner, outer, scene % 2)));
        cv::Mat gray;
        if (!image.empty())
            cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
        QCOMPARE(stripeRegion(gray), ref::stripeRegion(gray));
        QCOMPARE(
            phoebeGaugeValue(image, color, 1 + scene % 5, 1, std::max(2, image.cols / 2), scene % 3 * 20.),
            ref::phoebeGaugeValue(image, color, 1 + scene % 5, 1, std::max(2, image.cols / 2),
                                  scene % 3 * 20.));
        QVERIFY(same(stripedGaugeRatio(image, color), ref::stripedGaugeRatio(image, color)));
    }
    void dates_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 2048; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void dates() {
        QFETCH(int, scene);
        const std::array<QByteArray, 4> zones{"UTC", "Asia/Shanghai", "America/New_York", "Europe/Berlin"};
        const int hour = scene % 28 - 1;
        QDateTime now(QDate(2026, 1 + (scene / 28) % 12, 1 + (scene / 336) % 27),
                      QTime((scene * 7) % 24, (scene * 11) % 60, (scene * 13) % 60),
                      QTimeZone(zones[scene % 4]));
        if (scene % 17 == 0)
            now = QDateTime(QDate(2026, 11, 1), QTime(23, 59, 29 + (scene % 3)), QTimeZone(zones[scene % 4]));
        if (scene % 29 == 0)
            now = {};
        QString a, b;
        auto expected = failure([&] { a = reference_task_text::monthlyWindowKey(now, hour); });
        auto actual = failure([&] { b = monthlyWindowKey(now, hour); });
        QCOMPARE(actual, expected);
        QCOMPARE(b, a);
        QCOMPARE(b.isNull(), a.isNull());
    }
    void markers_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 128; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void markers() {
        QFETCH(int, scene);
        const int width = std::array<int, 3>{640, 960, 1920}[scene % 3];
        const int height = width * 9 / 16;
        cv::Mat frame = cv::Mat::zeros(height, width, CV_8UC3);
        const auto box = relativeBox(frame, .475, .925, .525, .98);
        if (!box.isEmpty())
            for (int i = 0; i < 1 + scene % 4; ++i) {
                const int x = box.x() + i * std::max(1, box.width() / 5), y = box.y() + 2;
                cv::ellipse(
                    frame, {x + 3, y + height / 100}, {std::max(1, width / 150), std::max(1, height / 80)},
                    scene % 90, 0, scene % 2 ? 360 : 200,
                    cv::Scalar(200 + scene % 56, 200 + scene % 56, 200 + scene % 56), scene % 3 ? 1 : -1);
            }
        QCOMPARE(hasContinueMarker(frame), reference_task_text::hasContinueMarker(frame));
    }
};
QTEST_GUILESS_MAIN(CombatImageContracts)
#include "test_combat_image_contract.moc"
