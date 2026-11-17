#pragma once
#include "runtime/workflow_abi.hpp"
#include <numeric>
#include <opencv2/core.hpp>
#include <openvino/openvino.hpp>
namespace okww::runtime::ocr_policy_test {
struct Probe {
    int scene, ordinal = 0;
    QJsonArray events;
    QStringList alphabet{"", "字", "体", "A", " ", QString(QChar(char16_t(0xd800))), "尾"};
    struct Request {
        Probe &owner;
        ov::Tensor input;
        void set_input_tensor(ov::Tensor value) {
            input = value;
            owner.note("input",
                       {double(value.get_shape()[0]), double(value.get_shape()[3]), value.data<float>()[0]});
        }
        ov::Tensor get_output_tensor() {
            owner.note("output");
            auto count = input.get_shape()[0], steps = size_t(owner.scene % 67 + 1),
                 chars = size_t(owner.alphabet.size());
            if (owner.scene % 29 == 0)
                ++chars;
            ov::Tensor result(ov::element::f32, {count, steps, chars});
            for (size_t i = 0; i < result.get_size(); ++i)
                result.data<float>()[i] = float((i * 37 + size_t(owner.scene)) % 7) / 10.f;
            if (owner.scene % 11 == 0)
                for (size_t i = 0; i < count * steps; ++i)
                    result.data<float>()[i * chars] = std::numeric_limits<float>::quiet_NaN();
            if (owner.scene % 13 == 0)
                result.data<float>()[result.get_size() / 2] = std::numeric_limits<float>::infinity();
            return result;
        }
    } recRequest{*this};
    explicit Probe(int scene) : scene(scene) {}
    void note(QString name, QJsonArray args = {}) {
        events.append(QJsonObject{{"op", name}, {"args", args}});
        if (++ordinal == scene % 31 + 1 && scene % 5 == 0)
            fail("scripted-ocr-sdk-failure");
    }
    ov::Tensor normalized(const cv::Mat &image, int height, int width, bool detector) {
        note("normalize", {image.cols, image.rows, height, width, detector});
        ov::Tensor item(ov::element::f32, {1, 3, size_t(height), size_t(width)});
        std::fill(item.data<float>(), item.data<float>() + item.get_size(),
                  float(image.cols + image.rows) / 1000.f);
        return item;
    }
    void infer(Request &, const std::function<void()> &) { note("infer"); }
    std::vector<std::pair<QString, double>> original(const std::vector<cv::Mat> &images,
                                                     const std::function<void()> &checkpoint) {
#include "reference_ocr_recognition_body.hpp"
    }
    std::vector<std::pair<QString, double>> migrated(const std::vector<cv::Mat> &images,
                                                     const std::function<void()> &checkpoint) {
        std::vector<size_t> indices;
        std::vector<std::pair<QString, double>> results;
        ov::Tensor batch, output;
        ov::Shape shape;
        QString text;
        rust::workflowRun(rust::WorkflowMethod::OcrRecognize, {},
                          [&](rust::WorkflowOp op, const double *a, double *out) {
                              switch (op) {
#include "ocr_recognition_host_cases.hpp"
                              default:
                                  fail("Unexpected OCR policy host");
                              }
                          });
        return results;
    }
};
inline QJsonObject replay(int scene, bool original) {
    Probe p(scene);
    std::vector<cv::Mat> images;
    for (int i = 0; i < scene % 15; ++i) {
        cv::Mat image;
        image.cols = scene % 19 == 0 ? 10000 : 16 + (scene * 13 + i * 37) % 320;
        image.rows = scene % 19 == 0 ? 10 : 24 + (i % 3) * 12;
        images.push_back(image);
    }
    const std::function<void()> checkpoint =
        scene % 3 == 0 ? std::function<void()>{} : std::function<void()>([&] { p.note("checkpoint"); });
    QJsonObject result;
    QJsonArray texts;
    try {
        const auto rows = original ? p.original(images, checkpoint) : p.migrated(images, checkpoint);
        for (const auto &[text, score] : rows)
            texts.append(QJsonObject{{"text", text},
                                     {"null", text.isNull()},
                                     {"score", score},
                                     {"nan", std::isnan(score)},
                                     {"inf", std::isinf(score)}});
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
        result["type"] = typeid(e).name();
    }
    result["texts"] = texts;
    result["events"] = p.events;
    return result;
}
} // namespace okww::runtime::ocr_policy_test
