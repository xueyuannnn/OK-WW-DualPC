#pragma once
#include "common.hpp"
namespace okww::runtime {
using ScheduleCommand = std::function<QByteArray(const QStringList &)>;
// Uses only per-profile, explicitly registered Windows tasks. Other apps' and
// older Python installations' scheduled tasks are never changed implicitly.
class Scheduler {
    QString executable_, root_, data_, user_, prefix_;
    ScheduleCommand command_;
    QJsonObject registry() const;
    void save(QJsonObject registry) const;
    void validateName(const QString &name) const;
    void registerTask(const QString &name, const QString &definition) const;

  public:
    Scheduler(QString executable, QString root, QString data, QString user = {},
              ScheduleCommand command = {});
    static QString currentUser();
    static QByteArray runCommand(const QStringList &arguments);
    static QString quoteArgument(const QString &argument);
    QString xml(const QJsonObject &settings) const;
    QJsonArray list() const;
    QString add(QJsonObject settings);
    void update(const QString &name, QJsonObject settings);
    void remove(const QString &name);
    void setEnabled(const QString &name, bool enabled);
};
} // namespace okww::runtime
