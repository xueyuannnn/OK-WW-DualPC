#include "echo_detector.hpp"
#include "inference_wait.hpp"
#include <openvino/openvino.hpp>
#include <opencv2/imgproc.hpp>
namespace okww::runtime {
struct EchoDetector::Impl {
    ov::Core core;
    ov::CompiledModel model;
    ov::InferRequest request;
    int width, height;
    std::mutex mutex;
    explicit Impl(const QString &path) {
        if (!QFile::exists(path))
            fail("声骸识别模型不存在：" + path);
        model = core.compile_model(path.toStdString(), "CPU", ov::inference_num_threads(2));
        const auto shape = model.input().get_shape();
        if (shape.size() != 4 || shape[0] != 1 || shape[1] != 3 || shape[2] != shape[3] || shape[2] < 32 ||
            shape[2] > 4096)
            fail("声骸模型输入尺寸与原模型不一致");
        height = int(shape[2]);
        width = int(shape[3]);
        request = model.create_infer_request();
    }
};
EchoDetector::EchoDetector(const QString &model) : impl_(std::make_unique<Impl>(model)) {}
EchoDetector::~EchoDetector() = default;
QVector<Match> EchoDetector::detect(const cv::Mat &image, double threshold, int label,
                                   const std::function<void()> &checkpoint) {
    if (checkpoint) checkpoint();
    if (image.empty() || image.type() != CV_8UC3 || !std::isfinite(threshold) || threshold < 0 ||
        threshold > 1)
        fail("声骸识别图像或阈值无效");
    std::lock_guard lock(impl_->mutex);
    const auto &p = *impl_;
    const double gain = std::min(double(p.width) / image.cols, double(p.height) / image.rows);
    const int w = pixelRound(image.cols * gain), h = pixelRound(image.rows * gain);
    const int left = pixelRound((p.width - w) / 2. - .1), top = pixelRound((p.height - h) / 2. - .1);
    cv::Mat resized, padded;
    cv::resize(image, resized, {w, h}, 0, 0, cv::INTER_LINEAR);
    cv::copyMakeBorder(resized, padded, top, p.height - h - top, left, p.width - w - left,
                       cv::BORDER_CONSTANT, {114, 114, 114});
    ov::Tensor tensor(ov::element::f32, {1, 3, size_t(p.height), size_t(p.width)});
    auto data = tensor.data<float>();
    for (int y = 0; y < p.height; ++y)
        for (int x = 0; x < p.width; ++x) {
            const auto bgr = padded.at<cv::Vec3b>(y, x);
            for (int c = 0; c < 3; ++c)
                data[c * p.height * p.width + y * p.width + x] = bgr[2 - c] / 255.f;
        }
    impl_->request.set_input_tensor(tensor);
    inferWithCheckpoint(impl_->request, checkpoint, "声骸识别");
    const auto out = impl_->request.get_output_tensor();
    const auto shape = out.get_shape();
    if (shape.size() != 3 || shape[0] != 1 || shape[1] < 5 || shape[1] > 1000 ||
        out.get_element_type() != ov::element::f32)
        fail("声骸模型输出格式无效");
    const size_t rows = shape[2];
    const auto scores = out.data<const float>();
    QVector<Match> candidates;
    for (size_t row = 0; row < rows; ++row) {
        if (checkpoint && row % 256 == 0) checkpoint();
        float confidence = scores[4 * rows + row];
        size_t category = 0;
        for (size_t c = 5; c < shape[1]; ++c)
            if (scores[c * rows + row] > confidence) {
                confidence = scores[c * rows + row];
                category = c - 4;
            }
        // NMSBoxes excludes scores equal to the threshold.
        if (!std::isfinite(confidence) || confidence <= threshold || (label != -1 && int(category) != label))
            continue;
        // float32 arithmetic here matches NumPy's array postprocessing.
        const float x = scores[row] - float(left), y = scores[rows + row] - float(top);
        const float bw = scores[2 * rows + row], bh = scores[3 * rows + row], ratio = float(gain);
        const std::array<float, 4> values{(x - bw / 2) / ratio, (y - bh / 2) / ratio, bw / ratio, bh / ratio};
        if (std::any_of(values.begin(), values.end(),
                        [](float n) { return !std::isfinite(n) || std::abs(n) > 10000000; }))
            fail("声骸模型返回异常坐标");
        QRect box{int(values[0]), int(values[1]), int(values[2]), int(values[3])};
        if (box.width() > 0 && box.height() > 0)
            candidates.append({category == 0 ? "echo" : "unknown", box, confidence});
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const auto &a, const auto &b) { return a.confidence > b.confidence; });
    QVector<Match> kept;
    for (const auto &candidate : candidates) {
        if (checkpoint) checkpoint();
        bool suppressed = false;
        for (const auto &prior : kept) {
            const auto intersection = candidate.box.intersected(prior.box);
            const double area = double(intersection.width()) * intersection.height();
            const double total = double(candidate.box.width()) * candidate.box.height() +
                                 double(prior.box.width()) * prior.box.height() - area;
            if (total > 0 && area / total > .45) {
                suppressed = true;
                break;
            }
        }
        if (!suppressed)
            kept.append(candidate);
    }
    const auto less = [](const Match &a, const Match &b) {
        const bool row = !(a.box.y() > b.box.y() + b.box.height() || a.box.y() + a.box.height() < b.box.y());
        int delta = row ? a.box.x() - b.box.x() : a.box.y() - b.box.y();
        if (!delta)
            delta = row ? a.box.y() - b.box.y() : a.box.x() - b.box.x();
        if (!delta)
            delta = int(a.confidence * 1000 - b.confidence * 1000);
        if (!delta)
            delta = QString::compare(a.name, b.name);
        return delta < 0;
    };
    // The source compares intersecting vertical spans as a row. It is not a
    // strict weak ordering, so do not give it to std::sort (undefined behavior).
    // Preserve Python's natural initial run and stable binary insertion.
    qsizetype run = std::min<qsizetype>(2, kept.size());
    if (kept.size() > 1) {
        const bool descending = less(kept[1], kept[0]);
        while (run < kept.size() && less(kept[run], kept[run - 1]) == descending)
            ++run;
        if (descending)
            std::reverse(kept.begin(), kept.begin() + run);
    }
    for (qsizetype i = run; i < kept.size(); ++i) {
        const auto item = kept[i];
        qsizetype low = 0, high = i;
        while (low < high) {
            const auto mid = (low + high) / 2;
            if (less(item, kept[mid]))
                high = mid;
            else
                low = mid + 1;
        }
        for (qsizetype j = i; j > low; --j)
            kept[j] = kept[j - 1];
        kept[low] = item;
    }
    return kept;
}
QString echoWalkDirection(const QVector<Match> &echoes, QSize screen, bool moving) {
    if (echoes.empty())
        return "w";
    const auto &echo = echoes.front();
    const double dx = echo.center().x() - screen.width() * .5;
    if (std::abs(dx) < screen.height() * (moving ? .15 : .05))
        return echo.box.y() + echo.box.height() > screen.height() * .65 ? "s" : "w";
    return dx > 0 ? "d" : "a";
}
} // namespace okww::runtime
