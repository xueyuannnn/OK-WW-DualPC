// PP-OCR preprocessing and DB/CTC postprocessing follow the project's OnnxOCR
// implementation (PaddlePaddle Authors, Apache-2.0). Inference is native OpenVINO.
#include "ocr.hpp"
#include <openvino/openvino.hpp>
#include <opencv2/imgproc.hpp>
#include <numeric>
#include "clipper.hpp"

namespace okww::runtime {
using Quad = std::array<cv::Point2f, 4>;
static std::pair<Quad, float> miniBox(const std::vector<cv::Point2f> &points) {
    const auto rect = cv::minAreaRect(points);
    Quad p;
    rect.points(p.data());
    std::sort(p.begin(), p.end(), [](auto a, auto b) { return a.x < b.x; });
    const int a = p[1].y > p[0].y ? 0 : 1, d = 1 - a, b = p[3].y > p[2].y ? 2 : 3, c = 5 - b;
    return {{p[a], p[b], p[c], p[d]}, std::min(rect.size.width, rect.size.height)};
}
static double boxScore(const cv::Mat &scores, const Quad &quad) {
    float minX = quad[0].x, maxX = minX, minY = quad[0].y, maxY = minY;
    for (auto p : quad) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    int x = std::clamp(int(std::floor(minX)), 0, scores.cols - 1),
        y = std::clamp(int(std::floor(minY)), 0, scores.rows - 1);
    int r = std::clamp(int(std::ceil(maxX)), 0, scores.cols - 1),
        b = std::clamp(int(std::ceil(maxY)), 0, scores.rows - 1);
    cv::Rect area(x, y, r - x + 1, b - y + 1);
    cv::Mat mask = cv::Mat::zeros(area.size(), CV_8U);
    std::vector<cv::Point> polygon;
    for (auto p : quad)
        polygon.emplace_back(int(p.x - x), int(p.y - y));
    cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{polygon}, cv::Scalar(1));
    return cv::mean(scores(area), mask)[0];
}
static std::vector<cv::Point2f> unclip(const Quad &points) {
    const std::vector<cv::Point2f> polygon(points.begin(), points.end());
    const double perimeter = cv::arcLength(polygon, true), area = std::abs(cv::contourArea(polygon));
    if (perimeter <= 0)
        return {};
    ClipperLib::Path path;
    for (auto p : points)
        path.emplace_back(ClipperLib::cInt(p.x), ClipperLib::cInt(p.y));
    ClipperLib::ClipperOffset offset;
    offset.AddPath(path, ClipperLib::jtRound, ClipperLib::etClosedPolygon);
    ClipperLib::Paths paths;
    offset.Execute(paths, area * 1.6 / perimeter);
    std::vector<cv::Point2f> expanded;
    for (const auto &item : paths)
        for (auto p : item)
            expanded.emplace_back(float(p.X), float(p.Y));
    return expanded;
}
static cv::Mat textCrop(const cv::Mat &image, const Quad &p) {
    const int w = int(std::max(cv::norm(p[0] - p[1]), cv::norm(p[2] - p[3])));
    const int h = int(std::max(cv::norm(p[0] - p[3]), cv::norm(p[1] - p[2])));
    if (w <= 0 || h <= 0)
        return {};
    Quad target{{{0, 0}, {float(w), 0}, {float(w), float(h)}, {0, float(h)}}};
    cv::Mat crop;
    cv::warpPerspective(image, crop, cv::getPerspectiveTransform(p.data(), target.data()), {w, h},
                        cv::INTER_CUBIC, cv::BORDER_REPLICATE);
    if (double(h) / w >= 1.5)
        cv::rotate(crop, crop, cv::ROTATE_90_COUNTERCLOCKWISE);
    return crop;
}
struct Ocr::Impl {
    ov::Core core;
    ov::CompiledModel det, rec, cls;
    ov::InferRequest detRequest, recRequest, clsRequest;
    QStringList alphabet;
    QString models;
    std::mutex mutex;
    explicit Impl(const QString &path) : models(path) {
        QFile dictionary(path + "/ppocrv5_dict.txt");
        if (!dictionary.open(QIODevice::ReadOnly))
            fail("OCR 字典不存在：" + dictionary.fileName());
        alphabet.append("");
        while (!dictionary.atEnd()) {
            QByteArray line = dictionary.readLine();
            while (line.endsWith('\n') || line.endsWith('\r'))
                line.chop(1);
            alphabet.append(QString::fromUtf8(line));
        }
        alphabet.append(" ");
        det = compile(path + "/det/det.onnx");
        rec = compile(path + "/rec/rec.onnx");
        detRequest = det.create_infer_request();
        recRequest = rec.create_infer_request();
    }
    ov::CompiledModel compile(const QString &path) {
        if (!QFile::exists(path))
            fail("OCR 模型不存在：" + path);
        return core.compile_model(path.toStdString(), "CPU", ov::inference_num_threads(2));
    }
    static ov::Tensor normalized(const cv::Mat &image, int height, int width, bool detector) {
        ov::Tensor tensor(ov::element::f32, {1, 3, size_t(height), size_t(width)});
        auto out = tensor.data<float>();
        std::fill(out, out + tensor.get_size(), 0.f);
        const int resizedWidth =
            detector ? width : std::min(width, int(std::ceil(double(height) * image.cols / image.rows)));
        cv::Mat resized;
        cv::resize(image, resized, {resizedWidth, height});
        const float mean[]{.485f, .456f, .406f}, dev[]{.229f, .224f, .225f};
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < resizedWidth; ++x) {
                const auto bgr = resized.at<cv::Vec3b>(y, x);
                for (int c = 0; c < 3; ++c)
                    out[c * height * width + y * width + x] =
                        detector ? (bgr[c] / 255.f - mean[c]) / dev[c] : bgr[c] / 127.5f - 1.f;
            }
        return tensor;
    }
    std::vector<Quad> detect(const cv::Mat &image) {
        double ratio = std::min(1., 960. / std::max(image.cols, image.rows));
        int h = std::max(32, pixelRound(int(image.rows * ratio) / 32.) * 32),
            w = std::max(32, pixelRound(int(image.cols * ratio) / 32.) * 32);
        detRequest.set_input_tensor(normalized(image, h, w, true));
        detRequest.infer();
        auto tensor = detRequest.get_output_tensor();
        auto shape = tensor.get_shape();
        if (shape.size() != 4 || shape[0] != 1 || shape[1] != 1 ||
            tensor.get_element_type() != ov::element::f32)
            fail("OCR 检测输出格式不支持");
        cv::Mat scores(int(shape[2]), int(shape[3]), CV_32F, tensor.data<float>()), mask;
        cv::compare(scores, .3, mask, cv::CMP_GT);
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mask, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
        std::vector<Quad> boxes;
        for (size_t i = 0; i < std::min<size_t>(1000, contours.size()); ++i) {
            std::vector<cv::Point2f> contour;
            for (auto p : contours[i])
                contour.emplace_back(float(p.x), float(p.y));
            auto [quad, side] = miniBox(contour);
            if (side < 3 || boxScore(scores, quad) < .5)
                continue;
            auto expanded = unclip(quad);
            if (expanded.size() < 3)
                continue;
            std::tie(quad, side) = miniBox(expanded);
            if (side < 5)
                continue;
            for (auto &p : quad) {
                p.x = float(std::clamp(pixelRound(p.x / scores.cols * image.cols), 0, image.cols - 1));
                p.y = float(std::clamp(pixelRound(p.y / scores.rows * image.rows), 0, image.rows - 1));
            }
            if (cv::norm(quad[0] - quad[1]) <= 3 || cv::norm(quad[0] - quad[3]) <= 3)
                continue;
            boxes.push_back(quad);
        }
        std::sort(boxes.begin(), boxes.end(), [](const auto &a, const auto &b) {
            return a[0].y == b[0].y ? a[0].x < b[0].x : a[0].y < b[0].y;
        });
        for (int i = 0; i < int(boxes.size()) - 1; ++i)
            for (int j = i; j >= 0; --j) {
                if (std::abs(boxes[j + 1][0].y - boxes[j][0].y) < 10 && boxes[j + 1][0].x < boxes[j][0].x)
                    std::swap(boxes[j + 1], boxes[j]);
                else
                    break;
            }
        return boxes;
    }
    void orient(cv::Mat &image) {
        if (!cls) {
            cls = compile(models + "/cls/cls.onnx");
            clsRequest = cls.create_infer_request();
        }
        clsRequest.set_input_tensor(normalized(image, 48, 192, false));
        clsRequest.infer();
        auto tensor = clsRequest.get_output_tensor();
        if (tensor.get_size() != 2 || tensor.get_element_type() != ov::element::f32)
            fail("OCR 方向输出无效");
        const auto scores = tensor.data<float>();
        if (scores[1] > .9f && scores[1] > scores[0])
            cv::rotate(image, image, cv::ROTATE_180);
    }
    std::vector<std::pair<QString, double>> recognize(const std::vector<cv::Mat> &images) {
        std::vector<size_t> indices(images.size());
        std::iota(indices.begin(), indices.end(), 0);
        std::stable_sort(indices.begin(), indices.end(), [&](auto a, auto b) {
            return double(images[a].cols) / images[a].rows < double(images[b].cols) / images[b].rows;
        });
        std::vector<std::pair<QString, double>> results(images.size());
        for (size_t begin = 0; begin < indices.size(); begin += 6) {
            const size_t end = std::min(begin + 6, indices.size()), count = end - begin;
            double maxRatio = 320. / 48;
            for (size_t i = begin; i < end; ++i)
                maxRatio = std::max(maxRatio, double(images[indices[i]].cols) / images[indices[i]].rows);
            const int width = int(48 * maxRatio);
            if (width > 8192)
                fail("OCR 文本行过长");
            ov::Tensor batch(ov::element::f32, {count, 3, 48, size_t(width)});
            for (size_t i = begin; i < end; ++i) {
                auto item = normalized(images[indices[i]], 48, width, false);
                memcpy(batch.data<float>() + (i - begin) * item.get_size(), item.data(),
                       item.get_byte_size());
            }
            recRequest.set_input_tensor(batch);
            recRequest.infer();
            auto output = recRequest.get_output_tensor();
            auto shape = output.get_shape();
            if (shape.size() != 3 || shape[0] != count || shape[2] != size_t(alphabet.size()) ||
                output.get_element_type() != ov::element::f32)
                fail("OCR 识别模型与字典不匹配");
            auto data = output.data<float>();
            for (size_t i = 0; i < count; ++i) {
                QString text;
                double score = 0;
                int selected = 0, last = -1;
                for (size_t t = 0; t < shape[1]; ++t) {
                    auto row = data + (i * shape[1] + t) * shape[2];
                    int index = int(std::max_element(row, row + shape[2]) - row);
                    if (index && index != last) {
                        text += alphabet[index];
                        score += row[index];
                        ++selected;
                    }
                    last = index;
                }
                results[indices[begin + i]] = {text, selected ? score / selected : 0};
            }
        }
        return results;
    }
};
Ocr::Ocr(const QString &directory) : impl_(std::make_unique<Impl>(directory)) {}
Ocr::~Ocr() = default;
QVector<TextLine> Ocr::read(const cv::Mat &image, std::optional<QRect> area, double threshold, bool detection,
                            bool angle) {
    if (image.empty())
        return {};
    if (image.type() != CV_8UC3 || threshold < 0 || threshold > 1)
        fail("OCR 图像或阈值无效");
    std::lock_guard lock(impl_->mutex);
    QRect bounds =
        area.value_or(QRect(0, 0, image.cols, image.rows)).intersected({0, 0, image.cols, image.rows});
    if (bounds.isEmpty())
        return {};
    auto source = image(cv::Rect(bounds.x(), bounds.y(), bounds.width(), bounds.height()));
    QVector<TextLine> result;
    if (!detection) {
        cv::Mat crop = source.clone();
        if (angle)
            impl_->orient(crop);
        auto [text, score] = impl_->recognize({crop}).front();
        if (score >= threshold && !text.isEmpty())
            result.append({text, bounds, score});
        return result;
    }
    std::vector<cv::Mat> crops;
    std::vector<QRect> rectangles;
    for (const auto &quad : impl_->detect(source)) {
        auto crop = textCrop(source, quad);
        if (crop.empty())
            continue;
        if (angle)
            impl_->orient(crop);
        const auto r = cv::boundingRect(std::vector<cv::Point2f>(quad.begin(), quad.end()));
        crops.push_back(crop);
        rectangles.emplace_back(r.x + bounds.x(), r.y + bounds.y(), r.width, r.height);
    }
    const auto recognized = impl_->recognize(crops);
    for (size_t i = 0; i < recognized.size(); ++i) {
        const auto &[text, score] = recognized[i];
        if (score >= threshold && !text.isEmpty())
            result.append({text, rectangles[i], score});
    }
    return result;
}
} // namespace okww::runtime
