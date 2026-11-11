#pragma once
#include "common.hpp"
#include "app_info.hpp"

namespace okww::runtime {
class ExecutionFailure : public std::runtime_error {
  public:
    QString details;
    ExecutionFailure(const QString &message, QString trace)
        : std::runtime_error(message.toUtf8().constData()), details(std::move(trace)) {}
};

// Local records deliberately omit configuration/credentials. Keep recent logs
// bounded so reporting an error cannot duplicate a large runtime log in memory.
inline QJsonObject saveDiagnostic(const QString &data, QJsonObject event) {
    const auto id = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") + '-' +
                    QUuid::createUuid().toString(QUuid::Id128).left(8);
    const auto folder = data + "/logs/diagnostics/" + id;
    event["diagnostic_id"] = id;
    event["diagnostic_directory"] = folder;
    event["diagnostic_report"] = folder + "/report.json";
    event["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    event["version"] = product::version;
    event["os"] = QSysInfo::prettyProductName();
    MEMORYSTATUSEX memory{sizeof(memory)};
    if (GlobalMemoryStatusEx(&memory))
        event["memory"] = QJsonObject{{"available_physical_bytes", double(memory.ullAvailPhys)},
                                      {"available_commit_bytes", double(memory.ullAvailPageFile)}};
    try {
        if (!QDir().mkpath(folder)) fail("无法创建诊断目录");
        for (const auto &name : {"native.log", "workshop.log"}) {
            QFile input(data + "/logs/" + name);
            if (!input.open(QIODevice::ReadOnly)) continue;
            input.seek(std::max<qint64>(0, input.size() - 65536));
            QSaveFile output(folder + '/' + name);
            const auto tail = input.read(65536);
            if (!output.open(QIODevice::WriteOnly) || output.write(tail) != tail.size() || !output.commit())
                fail("无法保存诊断日志");
        }
        writeJson(event["diagnostic_report"].toString(), event);
    } catch (const std::exception &e) {
        event["diagnostic_write_error"] = errorText(e);
    }
    return event;
}
} // namespace okww::runtime
