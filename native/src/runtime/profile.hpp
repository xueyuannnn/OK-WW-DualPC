#pragma once
#include "common.hpp"

namespace okww::runtime {
class Profile {
    QString directory_;
    QJsonObject schema_;
    mutable std::recursive_mutex mutex_;
    QMap<QString, QJsonObject> values_;
    QJsonObject defaults(const QString &group) const;
    QString filename(const QString &group) const;

  public:
    Profile(QString directory, const QString &catalog);
    QJsonObject group(const QString &id) const;
    QJsonValue value(const QString &id, const QString &key, QJsonValue fallback = {}) const;
    void set(const QString &id, const QString &key, const QJsonValue &value);
    void reset(const QString &id);
    QJsonObject catalog() const;
};
} // namespace okww::runtime
