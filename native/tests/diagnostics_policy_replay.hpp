#pragma once
#include "app_info.hpp"
#include "common.hpp"
#include "diagnostic_abi.hpp"
#include "platform_workflow.hpp"
#include <optional>
namespace okww::runtime::diagnostic_test {
struct State {
    int scene, dates = 0;
    QJsonArray trace;
    explicit State(int s) : scene(s) {}
};
inline thread_local State *active = nullptr;
inline QDateTime baseDate(int scene) {
    return QDateTime::fromString("2026-11-16T12:34:56.123+08:00", Qt::ISODateWithMs).addMSecs(scene);
}
inline QDateTime dateTime() {
    active->trace.append("date");
    return baseDate(active->scene).addMSecs(active->dates++);
}
inline QUuid uuid() {
    active->trace.append("uuid");
    return QUuid("00000000-0000-0000-0000-000000000001");
}
inline QString osName() {
    active->trace.append("os");
    return "Fixture OS";
}
inline BOOL memoryStatus(LPMEMORYSTATUSEX memory) {
    active->trace.append("memory");
    memory->ullAvailPhys = (1ULL << 60) + 43;
    memory->ullAvailPageFile = 123456789;
    return active->scene % 4 != 3;
}
namespace original {
inline QJsonObject saveDiagnostic(const QString &data, QJsonObject event) {
    const auto id = diagnostic_test::dateTime().toString("yyyyMMdd-HHmmss-zzz") + '-' +
                    diagnostic_test::uuid().toString(QUuid::Id128).left(8);
    const auto folder = data + "/logs/diagnostics/" + id;
    event["diagnostic_id"] = id;
    event["diagnostic_directory"] = folder;
    event["diagnostic_report"] = folder + "/report.json";
    event["timestamp"] = diagnostic_test::dateTime().toString(Qt::ISODateWithMs);
    event["version"] = product::releaseVersion;
    event["os"] = diagnostic_test::osName();
    MEMORYSTATUSEX memory{sizeof(memory)};
    if (diagnostic_test::memoryStatus(&memory))
        event["memory"] = QJsonObject{{"available_physical_bytes", double(memory.ullAvailPhys)},
                                      {"available_commit_bytes", double(memory.ullAvailPageFile)}};
    try {
        if (!QDir().mkpath(folder))
            fail("无法创建诊断目录");
        for (const auto &name : {"native.log", "workshop.log"}) {
            QFile input(data + "/logs/" + name);
            if (!input.open(QIODevice::ReadOnly))
                continue;
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
} // namespace original
namespace current {
inline QJsonObject saveDiagnostic(const QString &data, QJsonObject event) {
    QString id, folder;
    MEMORYSTATUSEX memory{sizeof(memory)};
    std::optional<QFile> input;
    std::optional<QSaveFile> output;
    QByteArray tail;
    static const char *const names[]{"native.log", "workshop.log"};
    rust::platformWorkflowRun(
        rust::WorkflowMethod::SaveDiagnostic, {},
        [&](rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &error) {
            switch (op) {
            case rust::WorkflowOp::DiagnosticMetadata:
                id = diagnostic_test::dateTime().toString("yyyyMMdd-HHmmss-zzz") + '-' +
                     diagnostic_test::uuid().toString(QUuid::Id128).left(8);
                folder = data + "/logs/diagnostics/" + id;
                event["diagnostic_id"] = id;
                event["diagnostic_directory"] = folder;
                event["diagnostic_report"] = folder + "/report.json";
                event["timestamp"] = diagnostic_test::dateTime().toString(Qt::ISODateWithMs);
                event["version"] = product::releaseVersion;
                event["os"] = diagnostic_test::osName();
                break;
            case rust::WorkflowOp::DiagnosticMemoryStatus:
                out[0] = diagnostic_test::memoryStatus(&memory);
                break;
            case rust::WorkflowOp::DiagnosticMemoryFields:
                event["memory"] = QJsonObject{{"available_physical_bytes", double(memory.ullAvailPhys)},
                                              {"available_commit_bytes", double(memory.ullAvailPageFile)}};
                break;
            case rust::WorkflowOp::DiagnosticMakeDirectory:
                out[0] = QDir().mkpath(folder);
                break;
            case rust::WorkflowOp::DiagnosticCreateInput:
                input.emplace(data + "/logs/" + names[int(a[0])]);
                break;
            case rust::WorkflowOp::DiagnosticOpenInput:
                out[0] = input->open(QIODevice::ReadOnly);
                break;
            case rust::WorkflowOp::DiagnosticDropInput:
                input.reset();
                break;
            case rust::WorkflowOp::DiagnosticSeekTail:
                input->seek(rust::okww_rs_diagnostic_tail_start(input->size()));
                break;
            case rust::WorkflowOp::DiagnosticCreateOutput:
                output.emplace(folder + '/' + names[int(a[0])]);
                break;
            case rust::WorkflowOp::DiagnosticReadTail:
                tail = input->read(qint64(a[0]));
                break;
            case rust::WorkflowOp::DiagnosticOpenOutput:
                out[0] = output->open(QIODevice::WriteOnly);
                break;
            case rust::WorkflowOp::DiagnosticWriteTail:
                out[0] = output->write(tail) == tail.size();
                break;
            case rust::WorkflowOp::DiagnosticCommitOutput:
                out[0] = output->commit();
                break;
            case rust::WorkflowOp::DiagnosticDropLog:
                tail = {};
                output.reset();
                input.reset();
                break;
            case rust::WorkflowOp::DiagnosticWriteReport:
                writeJson(event["diagnostic_report"].toString(), event);
                break;
            case rust::WorkflowOp::DiagnosticWriteErrorField:
                try {
                    std::rethrow_exception(error);
                } catch (const std::exception &e) {
                    event["diagnostic_write_error"] = errorText(e);
                }
                break;
            case rust::WorkflowOp::DiagnosticFailure: {
                static const char *const errors[]{"无法创建诊断目录", "无法保存诊断日志"};
                fail(QString::fromUtf8(errors[int(a[0])]));
            }
            default:
                fail("Rust 诊断记录宿主无效");
            }
        });
    return event;
}
} // namespace current
} // namespace okww::runtime::diagnostic_test
