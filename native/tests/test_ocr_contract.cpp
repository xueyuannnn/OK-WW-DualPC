#include "ocr_recognition_replay.hpp"
#include "reference_ocr.hpp"
#include "runtime/ocr_geometry_host.hpp"
#include "runtime/ocr_numeric_abi.hpp"
#include <QPainter>
#include <QtTest>
#include <bit>
using namespace okww;
using namespace okww::runtime;
class OcrContracts final : public QObject {
    Q_OBJECT
    std::unique_ptr<Ocr> current;
    std::unique_ptr<ocr_reference::OriginalOcr> original;
  private slots:
    void initTestCase() {
        const QString models = QString(OKWW_SOURCE_ROOT) + "/.venv/Lib/site-packages/onnxocr/models/ppocrv5";
        QVERIFY2(QFile::exists(models + "/ppocrv5_dict.txt"), "Original offline OCR models are required");
        original = std::make_unique<ocr_reference::OriginalOcr>(models);
        current = std::make_unique<Ocr>(models);
    }
    void images_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 32; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void images() {
        QFETCH(int, scene);
        QImage picture(640 + (scene % 3) * 160, 360 + (scene % 3) * 90, QImage::Format_RGB888);
        picture.fill(scene % 2 ? QColor(25, 30, 45) : Qt::white);
        QPainter painter(&picture);
        painter.setPen(scene % 2 ? Qt::white : Qt::black);
        QFont font("Microsoft YaHei", scene % 4 * 3 + 15);
        painter.setFont(font);
        painter.drawText(20, 55, QStringLiteral("每日任务 活跃度 0/100"));
        painter.drawText(35, 130, QStringLiteral("确认 领取奖励 20 40 60 80 100"));
        painter.drawText(40, 210, "Echo obtained 100/100 MAX");
        painter.end();
        cv::Mat image(picture.height(), picture.width(), CV_8UC3, picture.bits(),
                      size_t(picture.bytesPerLine()));
        cv::cvtColor(image, image, cv::COLOR_RGB2BGR);
        if (scene % 5 == 0)
            cv::GaussianBlur(image, image, {3, 3}, .7);
        if (scene % 7 == 0)
            cv::rotate(image, image, cv::ROTATE_180);
        const std::optional<QRect> area =
            scene % 3 == 0 ? std::optional<QRect>({10, 10, image.cols - 20, image.rows - 20}) : std::nullopt;
        const bool detection = scene % 4 != 0, angle = scene % 3 == 0;
        const double threshold = scene % 9 == 0 ? 0. : .5;
        const auto replay = [&](auto &ocr) {
            int checkpoints = 0;
            QJsonArray result;
            const auto lines = ocr.read(image, area, threshold, detection, angle, [&] { ++checkpoints; });
            for (const auto &line : lines)
                result.append(line.json());
            if (checkpoints < 1)
                throw std::runtime_error("OCR checkpoint was not invoked");
            // Async wait poll counts depend on inference wall time; the
            // deterministic policy contract checks those separately.
            return QJsonObject{{"lines", result}};
        };
        QCOMPARE(replay(*current), replay(*original));
    }
    void normalized_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 512; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void normalized() {
        QFETCH(int, scene);
        const size_t width = scene % 17 + 1, height = scene % 13 + 1, stride = width * 3 + scene % 7,
                     outputWidth = width + scene % 5;
        std::vector<uint8_t> bytes(stride * height);
        for (size_t i = 0; i < bytes.size(); ++i)
            bytes[i] = uint8_t(i * 37 + scene);
        std::vector<float> out(outputWidth * height * 3), expected(out.size());
        QVERIFY(rust::okww_rs_ocr_normalized(bytes.data(), width, height, stride, outputWidth, scene % 2,
                                             out.data(), out.size()));
        const float mean[]{.485f, .456f, .406f}, dev[]{.229f, .224f, .225f};
        for (size_t y = 0; y < height; ++y)
            for (size_t x = 0; x < width; ++x)
                for (size_t c = 0; c < 3; ++c)
                    expected[c * height * outputWidth + y * outputWidth + x] =
                        scene % 2 ? (bytes[y * stride + x * 3 + c] / 255.f - mean[c]) / dev[c]
                                  : bytes[y * stride + x * 3 + c] / 127.5f - 1.f;
        for (size_t i = 0; i < out.size(); ++i)
            QCOMPARE(std::bit_cast<uint32_t>(out[i]), std::bit_cast<uint32_t>(expected[i]));
    }
    void policies_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 2048; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void policies() {
        QFETCH(int, scene);
        QCOMPARE(ocr_policy_test::replay(scene, false), ocr_policy_test::replay(scene, true));
    }
    void geometry_data() { normalized_data(); }
    void geometry() {
        QFETCH(int, scene);
        const float x = float(scene % 13) - 3.25f, y = float(scene % 17) - 4.5f, w = float(scene % 29),
                    h = float(scene % 23);
        const OcrQuad quad{{{x, y}, {x + w, y + 1.25f}, {x + w - 2.5f, y + h}, {x - 1.5f, y + h - 1.25f}}};
        const std::vector<cv::Point2f> points(quad.begin(), quad.end());
        const auto actual = miniBox(points), expected = ocr_reference::miniBox(points);
        QCOMPARE(actual.second, expected.second);
        for (int i = 0; i < 4; ++i) {
            QCOMPARE(actual.first[i].x, expected.first[i].x);
            QCOMPARE(actual.first[i].y, expected.first[i].y);
        }
        cv::Mat scores(32, 64, CV_32F);
        for (int iy = 0; iy < 32; ++iy)
            for (int ix = 0; ix < 64; ++ix)
                scores.at<float>(iy, ix) = float((ix * 37 + iy + scene) % 31) / 30.f;
        QCOMPARE(boxScore(scores, quad), ocr_reference::boxScore(scores, quad));
        const auto expanded = unclip(quad), oldExpanded = ocr_reference::unclip(quad);
        QCOMPARE(expanded.size(), oldExpanded.size());
        for (size_t i = 0; i < expanded.size(); ++i) {
            QCOMPARE(expanded[i].x, oldExpanded[i].x);
            QCOMPARE(expanded[i].y, oldExpanded[i].y);
        }
        cv::Mat image(48, 80, CV_8UC3);
        for (int iy = 0; iy < image.rows; ++iy)
            for (int ix = 0; ix < image.cols; ++ix)
                image.at<cv::Vec3b>(iy, ix) = {uint8_t(iy * 7 + scene), uint8_t(ix * 13), uint8_t(ix + iy)};
        const auto crop = textCrop(image, quad), oldCrop = ocr_reference::textCrop(image, quad);
        QCOMPARE(crop.rows, oldCrop.rows);
        QCOMPARE(crop.cols, oldCrop.cols);
        QCOMPARE(crop.type(), oldCrop.type());
        if (!crop.empty())
            QCOMPARE(cv::norm(crop, oldCrop, cv::NORM_INF), 0.);
    }
};
QTEST_MAIN(OcrContracts)
#include "test_ocr_contract.moc"
