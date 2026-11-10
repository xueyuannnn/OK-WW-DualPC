#pragma once
#include "vision.hpp"

namespace okww::runtime {
// User images live with the profile. Bundled recognition assets are read-only.
class TemplateStore {
    QString directory_;
    QJsonObject document() const;
    QString imagePath(const QString &name) const;

  public:
    explicit TemplateStore(QString directory) : directory_(std::move(directory)) {}
    QString cocoPath() const { return directory_ + "/coco_annotations.json"; }
    QJsonArray images() const;
    QJsonArray annotations(int image) const;
    int add(const QImage &image);
    void saveAnnotations(int image, const QJsonArray &regions);
    void remove(int image);
    void publish(const QString &assets, const QString &enumFile = {}) const;
};
} // namespace okww::runtime
