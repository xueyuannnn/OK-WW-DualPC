#pragma once
#include "vision.hpp"

namespace okww::runtime {
struct TextLine {
    QString text;
    QRect box;
    double confidence = 0;
    QPointF center() const { return {box.x() + box.width() / 2., box.y() + box.height() / 2.}; }
    QJsonObject json() const {
        return {{"text", text},
                {"confidence", confidence},
                {"box", QJsonArray{box.x(), box.y(), box.width(), box.height()}}};
    }
};
class Ocr {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit Ocr(const QString &modelDirectory);
    ~Ocr();
    QVector<TextLine> read(const cv::Mat &image, std::optional<QRect> area = {}, double threshold = .5,
                           bool detection = true, bool angleClassification = false,
                           const std::function<void()> &checkpoint = {});
};
} // namespace okww::runtime
