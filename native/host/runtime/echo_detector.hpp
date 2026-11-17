#pragma once
#include "vision.hpp"
namespace okww::runtime {
// The original echo.onnx model, native OpenVINO inference and original
// letterbox / class-agnostic NMS / screen-order selection.
class EchoDetector {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit EchoDetector(const QString &model);
    ~EchoDetector();
    QVector<Match> detect(const cv::Mat &image, double threshold = .5, int label = 0,
                          const std::function<void()> &checkpoint = {});
};
QString echoWalkDirection(const QVector<Match> &echoes, QSize screen, bool alreadyMoving);
} // namespace okww::runtime
