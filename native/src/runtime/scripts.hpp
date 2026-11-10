#pragma once
#include "task.hpp"
#include <QProcess>
namespace okww::runtime {
class ScriptDisabled final : public std::runtime_error {
  public:
    ScriptDisabled() : std::runtime_error("脚本已停用") {}
};
QJsonObject scriptManage(const QString &root, const QString &data, QJsonObject request);
QJsonArray scriptCatalog(const QString &data);
QString recordedScript(const QJsonArray &events, QString name = "RecordedTask");
class ScriptRuntime {
    TaskContext &c_;
    QString root_, data_;
    QProcess process_;
    QByteArray buffer_, errors_;
    HANDLE mapping_ = nullptr;
    void *mapped_ = nullptr;
    std::function<bool()> enabled_;
    void write(QJsonObject message);
    QJsonObject receive();
    QJsonObject rpc(QJsonObject request);
    void start();
    void stop() noexcept;

  public:
    std::function<bool(const QString &)> builtin;
    ScriptRuntime(TaskContext &context, QString root, QString data)
        : c_(context), root_(std::move(root)), data_(std::move(data)) {}
    ~ScriptRuntime() { stop(); }
    QJsonObject run(const QJsonObject &script, std::function<bool()> enabled = {});
};
} // namespace okww::runtime
