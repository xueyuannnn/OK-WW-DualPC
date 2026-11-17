#ifndef Q_MOC_RUN
#include "reference_common_preview.hpp"
#endif
#include <QtTest>
#include <random>
#ifdef OKWW_FAST_PREVIEW
#include <opencv2/imgcodecs.hpp>
#endif
using namespace okww;
namespace {
Frame fixture(int seed, int format) {
    Frame f;
    f.width = 2 * (seed % 11 + 1);
    f.height = 2 * (seed % 13 + 1);
    f.format = PixelFormat(format);
    f.bottomUp = seed % 2;
    const int channels = format == 0                                 ? 3
                         : format == 1                               ? 4
                         : format == 3 || format == 4 || format == 5 ? 2
                                                                     : 1;
    f.stride = f.width * channels + (seed % 3) * 2;
    f.bytes.resize(qsizetype(f.stride) * f.height * ((format == 2 || format == 3 || format == 6) ? 3 : 2) /
                   2);
    std::mt19937 rng(seed);
    for (auto &byte : f.bytes)
        byte = char(rng());
#ifdef OKWW_FAST_PREVIEW
    if (format == 7) {
        cv::Mat image(f.height, f.width, CV_8UC3, cv::Scalar(13, seed % 256, 227));
        std::vector<uchar> encoded;
        cv::imencode(".jpg", image, encoded);
        f.bytes = QByteArray(reinterpret_cast<const char *>(encoded.data()), qsizetype(encoded.size()));
        if (seed % 7 == 0)
            f.bytes = "invalid JPEG";
        else if (seed % 11 == 0)
            f.width += 2;
    }
#endif
    return f;
}
QJsonObject imageSignature(const QImage &image) {
    const auto rgb = image.convertToFormat(QImage::Format_RGB32);
    QByteArray pixels;
    for (int y = 0; y < rgb.height(); ++y)
        pixels.append(reinterpret_cast<const char *>(rgb.constScanLine(y)), rgb.width() * 4);
    return {{"width", image.width()},
            {"height", image.height()},
            {"format", int(image.format())},
            {"pixels", QString::fromLatin1(pixels.toHex())}};
}
QJsonObject outcome(Frame frame, QSize target, bool preview, bool rust) {
    QJsonObject result;
    frame.bytes.detach();
    try {
        auto image = preview ? rust ? previewImage(frame, target)
                                    : common_preview_reference::previewImage(frame, target)
                     : rust ? frameImage(frame)
                             : common_preview_reference::frameImage(frame);
        result["image"] = imageSignature(image);
        std::fill(frame.bytes.begin(), frame.bytes.end(), char(0x45));
        result["after-source-reuse"] = imageSignature(image);
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    }
    return result;
}
} // namespace
class CommonPreviewContracts final : public QObject {
    Q_OBJECT
  private slots:
    void pixels_data() {
        QTest::addColumn<int>("seed");
        QTest::addColumn<int>("format");
        for (int seed = 0; seed < 256; ++seed)
            for (int format = 0; format < 7; ++format)
                QTest::newRow(qPrintable(QString("%1-%2").arg(seed).arg(format))) << seed << format;
    }
    void pixels() {
        QFETCH(int, seed);
        QFETCH(int, format);
        const auto f = fixture(seed, format);
        QCOMPARE(outcome(f, {}, false, true), outcome(f, {}, false, false));
    }
    void preview_data() {
        QTest::addColumn<int>("seed");
        QTest::addColumn<int>("format");
        for (int seed = 0; seed < 256; ++seed)
            for (int format = 0; format < 8; ++format)
                QTest::newRow(qPrintable(QString("%1-%2").arg(seed).arg(format))) << seed << format;
    }
    void preview() {
        QFETCH(int, seed);
        QFETCH(int, format);
        const auto f = fixture(seed, format);
        const QList<QSize> targets{{f.width, f.height},
                                   {f.width / 2, f.height / 2},
                                   {7, 5},
                                   {f.width * 2, f.height * 2},
                                   {0, 40},
                                   {1, 100},
                                   {-1, 64}};
        const auto target = targets[seed % targets.size()];
        QCOMPARE(outcome(f, target, true, true), outcome(f, target, true, false));
    }
    void malformedFastPreview_data() {
        QTest::addColumn<int>("seed");
        for (int seed = 0; seed < 128; ++seed)
            QTest::newRow(qPrintable(QString::number(seed))) << seed;
    }
    void malformedFastPreview() {
#ifndef OKWW_FAST_PREVIEW
        QSKIP("Invalid raw-frame guard is only defined in the OpenCV fast-preview path");
#else
        QFETCH(int, seed);
        constexpr int formats[]{0, 1, 2, 4, 5};
        auto f = fixture(seed, formats[seed % 5]);
        f.bytes.chop(f.bytes.size() / 3 + 1);
        QCOMPARE(outcome(f, {100, 100}, true, true), outcome(f, {100, 100}, true, false));
#endif
    }
};
QTEST_GUILESS_MAIN(CommonPreviewContracts)
#include "test_common_preview_contract.moc"
