#include "reference_echo_prediction.hpp"
#include "runtime/echo_detection_abi.hpp"
#include "runtime/workflow_abi.hpp"
#include <QtTest>
#include <bit>
#include <random>
using namespace okww;
using namespace okww::runtime;
namespace {
QJsonArray matchRows(const QVector<Match> &rows) {
    QJsonArray result;
    for (const auto &r : rows)
        result.append(QJsonObject{{"name", r.name},
                                  {"box", QJsonArray{r.box.x(), r.box.y(), r.box.width(), r.box.height()}},
                                  {"score", r.confidence}});
    return result;
}
QJsonObject prediction(int scene, bool original) {
    std::mt19937 rng{uint32_t(scene)};
    const size_t rows = scene % 23 == 0 ? 0 : scene % 17 == 0 ? 513 : 32 + scene % 97;
    const size_t channels = 5 + scene % 4;
    std::vector<float> scores(rows * channels);
    for (size_t i = 0; i < rows; ++i) {
        scores[i] = float(int(rng() % 2300) - 200);
        scores[rows + i] = float(int(rng() % 1400) - 200);
        scores[2 * rows + i] = float(rng() % 400) - 20.f;
        scores[3 * rows + i] = float(rng() % 400) - 20.f;
        for (size_t c = 4; c < channels; ++c)
            scores[c * rows + i] = float(rng() % 11) / 10.f;
    }
    if (rows > 0 && scene % 11 == 0)
        scores[rows / 2] = std::numeric_limits<float>::infinity();
    if (rows > 0 && scene % 13 == 0)
        scores[4 * rows + rows / 3] = std::numeric_limits<float>::quiet_NaN();
    if (rows > 0 && scene % 19 == 0)
        scores[3 * rows + rows / 2] = 10000001;
    if (rows > 0 && scene % 31 == 0)
        scores[4 * rows] = std::numeric_limits<float>::infinity();
    const double gain = QJsonArray{.333333333333, 1., 2., .5}[scene % 4].toDouble();
    const int left = scene % 4 * 17, top = scene % 3 * 41;
    const double threshold = (scene % 6) / 10.;
    const int label = std::array{-1, 0, 1, 3}[scene % 4];
    int checkpoints = 0;
    const std::function<void()> checkpoint =
        scene % 5 == 0 ? std::function<void()>{} : std::function<void()>([&] {
            ++checkpoints;
            if (scene % 7 == 0 && checkpoints == scene % 4 + 1)
                throw Cancelled();
        });
    QJsonObject result;
    try {
        auto detections = original
                              ? detection_reference::prediction(scores.data(), scores.size(), channels, rows,
                                                                gain, left, top, threshold, label, checkpoint)
                              : echoPrediction(scores.data(), scores.size(), channels, rows, gain, left, top,
                                               threshold, label, checkpoint);
        result["matches"] = matchRows(detections);
    } catch (const std::exception &error) {
        result["error"] = errorText(error);
        result["type"] = typeid(error).name();
    }
    result["checkpoints"] = checkpoints;
    return result;
}
QJsonObject inference(int scene, bool original) {
    int polls = 0, checkpoints = 0;
    double elapsed = 0;
    QJsonArray events;
    const auto invoke = [&](rust::WorkflowOp op, const double *args, double *out) {
        events.append(int(op));
        if (int(op) % 13 == scene % 13 && events.size() == scene % 7 + 1)
            fail("sdk-first-failure");
        switch (op) {
        case rust::WorkflowOp::InferenceSynchronous:
            break;
        case rust::WorkflowOp::Checkpoint:
            if (scene % 7 == 0 && ++checkpoints == scene % 4 + 1)
                throw Cancelled();
            break;
        case rust::WorkflowOp::InferenceStartTimer:
            elapsed = 0;
            break;
        case rust::WorkflowOp::InferenceStart:
            break;
        case rust::WorkflowOp::InferenceReady:
            ++polls;
            elapsed += args[0];
            out[0] = polls > scene % 601;
            break;
        case rust::WorkflowOp::InferenceElapsedMs:
            out[0] = elapsed;
            break;
        case rust::WorkflowOp::InferenceTimeout:
            fail("model-timeout");
            break;
        case rust::WorkflowOp::InferenceCancel:
            if (scene % 3 == 0)
                fail("cancel-failed");
            break;
        case rust::WorkflowOp::InferenceDrain:
            if (scene % 5 == 0)
                fail("drain-failed");
            break;
        default:
            fail("unknown-test-sdk");
        }
    };
    auto action = [&](rust::WorkflowOp op, double arg = 0) {
        double result[3]{};
        invoke(op, &arg, result);
        return result[0];
    };
    QJsonObject result;
    try {
        if (!original) {
            const std::array args{double(scene % 2)};
            rust::workflowRun(rust::WorkflowMethod::InferenceWait, args, invoke);
        } else {
            if (scene % 2 == 0)
                action(rust::WorkflowOp::InferenceSynchronous);
            else {
                action(rust::WorkflowOp::Checkpoint);
                action(rust::WorkflowOp::InferenceStartTimer);
                action(rust::WorkflowOp::InferenceStart);
                try {
                    while (!action(rust::WorkflowOp::InferenceReady, 20)) {
                        action(rust::WorkflowOp::Checkpoint);
                        if (action(rust::WorkflowOp::InferenceElapsedMs) > 10000)
                            action(rust::WorkflowOp::InferenceTimeout);
                    }
                    action(rust::WorkflowOp::Checkpoint);
                } catch (...) {
                    const auto cause = std::current_exception();
                    try {
                        action(rust::WorkflowOp::InferenceCancel);
                    } catch (...) {
                    }
                    try {
                        action(rust::WorkflowOp::InferenceDrain);
                    } catch (...) {
                    }
                    std::rethrow_exception(cause);
                }
            }
        }
    } catch (const std::exception &error) {
        result["error"] = errorText(error);
        result["type"] = typeid(error).name();
    }
    result["events"] = events;
    result["polls"] = polls;
    result["checkpoints"] = checkpoints;
    return result;
}
} // namespace
class DetectionContracts final : public QObject {
    Q_OBJECT
  private slots:
    void predictions_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 4096; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void predictions() {
        QFETCH(int, scene);
        QCOMPARE(prediction(scene, false), prediction(scene, true));
    }
    void inference_data() { predictions_data(); }
    void inference() {
        QFETCH(int, scene);
        QCOMPARE(::inference(scene, false), ::inference(scene, true));
    }
    void tensors_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 256; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void tensors() {
        QFETCH(int, scene);
        const size_t width = scene % 17 + 1, height = scene % 13 + 1, stride = width * 3 + scene % 7;
        std::vector<uint8_t> bytes(stride * height);
        for (size_t i = 0; i < bytes.size(); ++i)
            bytes[i] = uint8_t(i * 37 + scene);
        std::vector<float> out(width * height * 3), expected(out.size());
        QVERIFY(rust::okww_rs_echo_tensor(bytes.data(), width, height, stride, out.data(), out.size()));
        for (size_t y = 0; y < height; ++y)
            for (size_t x = 0; x < width; ++x)
                for (size_t c = 0; c < 3; ++c)
                    expected[c * height * width + y * width + x] = bytes[y * stride + x * 3 + 2 - c] / 255.f;
        for (size_t i = 0; i < out.size(); ++i)
            QCOMPARE(std::bit_cast<uint32_t>(out[i]), std::bit_cast<uint32_t>(expected[i]));
    }
    void layouts_data() { predictions_data(); }
    void layouts() {
        QFETCH(int, scene);
        const int width = 31 + scene % 2541, height = 29 + scene % 1421, tw = (scene % 3 + 1) * 320, th = tw;
        const double gain = std::min(double(tw) / width, double(th) / height);
        const int w = pixelRound(width * gain), h = pixelRound(height * gain);
        const int left = pixelRound((tw - w) / 2. - .1), top = pixelRound((th - h) / 2. - .1);
        double actual[5]{};
        QVERIFY(rust::okww_rs_echo_layout(width, height, tw, th, actual));
        const std::array expected{gain, double(w), double(h), double(left), double(top)};
        for (int i = 0; i < 5; ++i)
            QCOMPARE(actual[i], expected[size_t(i)]);
    }
};
QTEST_GUILESS_MAIN(DetectionContracts)
#include "test_detection_contract.moc"
