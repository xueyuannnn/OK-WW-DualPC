#include "reference_imaging.hpp"
#include "template_context_replay.hpp"
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
class ImagingContracts final : public QObject {
    Q_OBJECT
    template_test::Fixture fixture;
  private slots:
    void invalidCoordinates_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 128; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void invalidCoordinates() {
        QFETCH(int, scene);
        const QSize size = std::array<QSize, 4>{{{}, {0, 10}, {-1, 100}, {640, 480}}}[scene % 4];
        const double x = std::array{0., std::numeric_limits<double>::quiet_NaN(),
                                    std::numeric_limits<double>::infinity(), 1.e10}[size_t(scene / 4) % 4];
        const auto run = [&](bool original) {
            QJsonObject result;
            try {
                const auto p = original
                                   ? image_reference::reference_gamePoint(size, x, .5, scene % 2, scene % 3)
                                   : gamePoint(size, x, .5, scene % 2, scene % 3);
                result["point"] = QJsonArray{p.x(), p.y()};
            } catch (const std::exception &e) {
                result["error"] = errorText(e);
            }
            return result;
        };
        QCOMPARE(run(false), run(true));
    }
    void templates_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 2048; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void templates() {
        QFETCH(int, scene);
        QCOMPARE(template_test::search<Templates>(fixture, scene),
                 template_test::search<image_reference::OriginalTemplates>(fixture, scene));
    }
    void catalogs_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 128; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void catalogs() {
        QFETCH(int, scene);
        template_test::extra(fixture, scene);
        QCOMPARE(template_test::catalog<Templates>(fixture),
                 template_test::catalog<image_reference::OriginalTemplates>(fixture));
    }
    void coordinates_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 4096; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void coordinates() {
        QFETCH(int, scene);
        const QSize size = std::array<QSize, 8>{{{1920, 1080},
                                                 {1280, 720},
                                                 {800, 600},
                                                 {2560, 1080},
                                                 {1080, 1920},
                                                 {3840, 2160},
                                                 {2560, 1440},
                                                 {1, 1}}}[scene % 8];
        const double x = (scene % 19 - 2) / 17., y = (scene % 23 - 2) / 21., right = x + (scene % 7) / 20.,
                     bottom = y + (scene % 5) / 20.;
        const bool hc = scene % 2, vc = scene % 3 == 0;
        const cv::Mat frame(size.height(), size.width(), CV_8UC3);
        QCOMPARE(gamePoint(size, x, y, hc, vc), image_reference::reference_gamePoint(size, x, y, hc, vc));
        QCOMPARE(gameBox(frame, x, y, right, bottom, hc, vc),
                 image_reference::reference_gameBox(frame, x, y, right, bottom, hc, vc));
        QCOMPARE(relativeBox(frame, x, y, right, bottom),
                 image_reference::reference_relativeBox(frame, x, y, right, bottom));
        const QRect r(scene % 2000 - 200, scene % 1000 - 100, scene % 97 - 2, scene % 79 - 2);
        QCOMPARE(Templates::anchored(r, {1920, 1080}, size, hc, vc),
                 image_reference::OriginalTemplates::anchored(r, {1920, 1080}, size, hc, vc));
    }
    void conversions_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 1024; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void conversions() {
        QFETCH(int, scene);
        Frame frame;
        frame.width = 16 + (scene % 3) * 2;
        frame.height = 8 + (scene % 3) * 2;
        frame.format = PixelFormat(scene % 9);
        frame.bottomUp = scene % 2;
        const int bpp = frame.format == PixelFormat::BGR24                                       ? 3
                        : frame.format == PixelFormat::BGRA32                                    ? 4
                        : frame.format == PixelFormat::NV12 || frame.format == PixelFormat::I420 ? 1
                                                                                                 : 2;
        frame.stride = frame.width * bpp + (scene % 3) * 2;
        frame.bytes = QByteArray(frame.stride * frame.height * 2, char(0));
        for (qsizetype i = 0; i < frame.bytes.size(); ++i)
            frame.bytes[i] = char((i * 37 + scene) % 256);
        if (frame.format == PixelFormat::MJPG) {
            cv::Mat image(frame.height, frame.width, CV_8UC3, cv::Scalar(17, 35, 109));
            std::vector<uint8_t> bytes;
            cv::imencode(".jpg", image, bytes);
            frame.bytes = QByteArray(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size()));
        }
        if (scene % 13 == 0)
            frame.width += 1;
        if (scene % 17 == 0)
            frame.bytes.truncate(4);
        if (scene % 19 == 0)
            frame.stride = 1;
        if (scene % 23 == 0)
            frame.height = 0;
        const auto run = [&](bool original) {
            QJsonObject result;
            try {
                const auto image = original ? image_reference::reference_decode(frame) : decode(frame);
                const auto data = image.clone();
                result["rows"] = data.rows;
                result["cols"] = data.cols;
                result["type"] = data.type();
                result["pixels"] = QString::fromLatin1(
                    QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(data.data),
                                                            qsizetype(data.total() * data.elemSize())),
                                             QCryptographicHash::Sha256)
                        .toHex());
            } catch (const std::exception &e) {
                result["error"] = errorText(e);
                result["type"] = typeid(e).name();
            }
            return result;
        };
        QCOMPARE(run(false), run(true));
    }
    void filters_data() { conversions_data(); }
    void filters() {
        QFETCH(int, scene);
        const int channels = scene % 2 ? 1 : 3;
        cv::Mat image(5 + scene % 7, 9 + scene % 11, CV_MAKETYPE(CV_8U, channels));
        for (int y = 0; y < image.rows; ++y)
            for (int x = 0; x < image.cols * channels; ++x)
                image.ptr(y)[x] = uint8_t(x * 37 + y * 13 + scene);
        auto oldImage = image.clone();
        const auto actual = filtered(image, Filter(scene % 9)),
                   expected = image_reference::reference_filtered(oldImage, Filter(scene % 9));
        QCOMPARE(actual.rows, expected.rows);
        QCOMPARE(actual.cols, expected.cols);
        QCOMPARE(actual.type(), expected.type());
        QCOMPARE(cv::norm(actual, expected, cv::NORM_INF), 0.);
        QCOMPARE(cv::norm(image, oldImage, cv::NORM_INF), 0.);
        QCOMPARE(actual.data == image.data, expected.data == oldImage.data);
        if (channels == 3) {
            const QRect area(scene % 5 - 1, scene % 3 - 1, 3, 3);
            QCOMPARE(
                colorPercentage(image, area, {20, 30, 40}, {220, 230, 240}),
                image_reference::reference_colorPercentage(oldImage, area, {20, 30, 40}, {220, 230, 240}));
        }
    }
    void cache_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 64; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void cache() {
        QFETCH(int, scene);
        Config config = Config::defaults();
        config.set("capture.output_width", 16);
        config.set("capture.output_height", 8);
        if (scene % 3 == 0)
            config.set("capture.crop", QJsonArray{1, 1, 10, 6});
        InferenceFrames cache(config);
        image_reference::OriginalInferenceFrames oldCache(config);
        for (int i = 0; i < 24; ++i) {
            auto frame = std::make_shared<Frame>();
            frame->sequence = (uint64_t(1) << 63) + uint64_t(i / 3);
            frame->width = i % 7 == 0 ? 18 : 16;
            frame->height = i % 5 == 0 ? 10 : 8;
            frame->format = i % 2 ? PixelFormat::BGRA32 : PixelFormat::BGR24;
            frame->stride = frame->width * (i % 2 ? 4 : 3);
            frame->bytes = QByteArray(frame->stride * frame->height, char(13 + i));
            const FramePtr source = i % 11 == 0 ? FramePtr{} : frame;
            const auto actual = cache.get(source), expected = oldCache.get(source);
            QCOMPARE(cache.conversions(), oldCache.conversions());
            QCOMPARE(actual.rows, expected.rows);
            QCOMPARE(actual.cols, expected.cols);
            QCOMPARE(actual.type(), expected.type());
            if (!actual.empty())
                QCOMPARE(cv::norm(actual, expected, cv::NORM_INF), 0.);
        }
    }
};
QTEST_GUILESS_MAIN(ImagingContracts)
#include "test_imaging_contract.moc"
