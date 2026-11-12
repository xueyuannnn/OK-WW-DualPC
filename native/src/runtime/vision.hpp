#pragma once
#include "execution.hpp"
#include <opencv2/core.hpp>
#include <optional>

namespace okww::runtime {
cv::Mat readImage(const QString &path);
cv::Mat decode(const Frame &frame);
cv::Mat inferenceImage(const Frame &frame, const Config &config);
class InferenceFrames {
    Config config_;
    FramePtr source_;
    cv::Mat image_;
    uint64_t conversions_ = 0;
  public:
    explicit InferenceFrames(Config config) : config_(std::move(config)) {}
    // Runtime consumers treat published recognition images as read-only.
    cv::Mat get(FramePtr frame);
    uint64_t conversions() const { return conversions_; }
};
enum class Filter { None, White, Dialog, Binary244, Binary220, InvertWhite, Negative80 };
cv::Mat filtered(const cv::Mat &image, Filter filter);
struct Match {
    QString name;
    QRect box;
    double confidence = 0;
    QPointF center() const { return {box.x() + box.width() / 2., box.y() + box.height() / 2.}; }
};
struct MatchOptions {
    double threshold = .8, horizontal = .002, vertical = .002;
    std::optional<QRect> search;
    Filter filter = Filter::None;
    bool grayscale = false;
    int limit = 1;
    double templateScale = 1;
    double templateTop = 1, searchHeight = 1;
    int targetHeight = 0;
    double rotation = 0;
};
class Templates {
    struct Source {
        QString path;
        QRect box;
        bool hc = false, vc = false;
        Filter filter = Filter::None;
    };
    struct Scaled {
        cv::Mat mat;
        QRect box;
    };
    QMap<QString, Source> sources_;
    QMap<QString, cv::Mat> images_;
    QMap<QString, Scaled> cache_;
    QSize size_;
    std::function<void()> checkpoint_;
    const Scaled &scaled(const QString &name, QSize size);

  public:
    explicit Templates(const QString &coco);
    void add(const QString &coco);
    void setCheckpoint(std::function<void()> checkpoint) { checkpoint_ = std::move(checkpoint); }
    QStringList names() const { return sources_.keys(); }
    bool contains(const QString &name) const { return sources_.contains(name); }
    QRect box(const QString &name, QSize size) { return scaled(name, size).box; }
    cv::Mat pattern(const QString &name, QSize size) { return scaled(name, size).mat.clone(); }
    QVector<Match> find(const cv::Mat &frame, const QString &name, MatchOptions options = {});
    std::optional<Match> one(const cv::Mat &frame, const QString &name, MatchOptions options = {});
    static QRect anchored(QRect original, QSize source, QSize target, bool hc = false, bool vc = false);
};
double colorPercentage(const cv::Mat &frame, QRect box, cv::Scalar bgrMin, cv::Scalar bgrMax);
QRect relativeBox(const cv::Mat &frame, double x, double y, double right, double bottom);
QPoint gamePoint(QSize size,double x,double y,bool hcenter=false,bool vcenter=false);
QRect gameBox(const cv::Mat &frame,double x,double y,double right,double bottom,bool hcenter=false,bool vcenter=false);
} // namespace okww::runtime
