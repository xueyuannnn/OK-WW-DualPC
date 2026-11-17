#pragma once
#include "runtime/workflow_abi.hpp"
#include "runtime/workshop.hpp"
namespace okww::runtime::workshop_file_test {
struct Channel {
    QJsonArray *trace;
    int scene, poll = 0;
    bool running = false, sent = false;
    QJsonObject request;
    Channel(QJsonArray *t, int s) : trace(t), scene(s) {}
    QProcess::ProcessState state() {
        trace->append("state");
        return running ? QProcess::Running : QProcess::NotRunning;
    }
    void kill() {
        trace->append("kill");
        running = false;
    }
    bool waitForFinished(int ms) {
        trace->append(QJsonObject{{"finished", ms}});
        return true;
    }
    void setProcessEnvironment(QProcessEnvironment) { trace->append("environment"); }
    void setWorkingDirectory(QString p) { trace->append(QJsonObject{{"cwd", p}}); }
    void start(QString p, QStringList args) {
        trace->append(QJsonObject{{"start", p}, {"args", QJsonArray::fromStringList(args)}});
        running = true;
    }
    bool waitForStarted(int ms) {
        trace->append(QJsonObject{{"started", ms}});
        return scene % 17 != 0;
    }
    qint64 processId() {
        trace->append("pid");
        return 1234;
    }
    qint64 write(QByteArray payload) {
        trace->append(QJsonObject{{"write", QString::fromUtf8(payload)}});
        request = QJsonDocument::fromJson(payload).object();
        return scene % 13 ? payload.size() : payload.size() - 1;
    }
    void waitForBytesWritten(int ms) { trace->append(QJsonObject{{"written", ms}}); }
    QByteArray readAllStandardError() {
        trace->append("stderr");
        return QByteArray(scene % 5 ? 17 : 70000, 'a' + poll % 26);
    }
    QByteArray readAllStandardOutput() {
        trace->append("stdout");
        if (sent || poll < scene % 3)
            return {};
        sent = true;
        if (scene % 29 == 0)
            return QByteArray(16 * 1024 * 1024 + 1, 'x');
        if (scene % 31 == 0)
            return "partial";
        QJsonObject reply{{"request_id", scene % 23 ? request["request_id"] : QJsonValue("stale")},
                          {"manager_protocol", scene % 37 ? 1 : 2},
                          {"ok", scene % 11 != 0},
                          {"data", QJsonObject{{"scene", scene}}},
                          {"error", "manager-error"}};
        return QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n';
    }
    void waitForReadyRead(int ms) {
        trace->append(QJsonObject{{"ready", ms}});
        ++poll;
        if (scene % 7 == 0)
            running = false;
    }
    QString errorString() {
        trace->append("error-string");
        return "fake-error";
    }
};
struct Timer {
    QJsonArray *trace;
    int scene, reads = 0;
    void start() { trace->append("timer-start"); }
    qint64 elapsed() {
        trace->append("elapsed");
        return ++reads * (scene % 41 ? 1 : 20000);
    }
};
[[noreturn]] static void workshopFileFailure(int code, const QJsonObject &reply, Channel *process) {
    switch (code) {
    case 1:
        fail("工坊管理器启动失败：" + process->errorString());
    case 3:
        fail(reply["error"].toString("工坊管理器返回无效，请查看兼容引擎日志"));
    case 7:
        fail(reply["error"].toString("工坊文件操作失败"));
    default:
        break;
    }
    static const QStringList messages{"缺少工坊 Python 兼容引擎，请使用完整包",
                                      "",
                                      "工坊代码检查超时",
                                      "",
                                      "工坊文件管理通道已断开",
                                      "工坊管理器返回数据过大",
                                      "工坊文件管理接口不匹配，请更新完整包",
                                      "",
                                      "工坊文件管理器已退出"};
    fail(messages.at(code));
}

struct Replay {
    QJsonArray trace;
    int scene;
    QString root = "root", data = "data";
    Channel *process = nullptr;
    QByteArray buffer, errors;
    quint64 sequence = 0;
    std::atomic<qint64> pid{0};
    explicit Replay(int s) : scene(s) {
        if (s % 3) {
            process = new Channel(&trace, scene);
            process->running = s % 2;
        }
        if (s % 43 == 0)
            buffer = "{\"request_id\":\"old\"}\n";
        if (s % 47 == 0)
            sequence = UINT64_MAX - 1;
        errors = "prior";
    }
    ~Replay() { delete process; }
    QString workshopPython(QString r) {
        trace.append(QJsonObject{{"python", r}});
        return scene % 19 ? "python" : "";
    }
    QString workshopRoot(QString r) {
        trace.append(QJsonObject{{"root", r}});
        return "engine";
    }
    QProcessEnvironment environment() { return {}; }
    QJsonObject exchange(QJsonObject request) {
        if (!process)
            process = new Channel(&trace, scene);
        if (process->state() == QProcess::NotRunning) {
            buffer.clear();
            errors.clear();
            const auto python = workshopPython(root), engine = workshopRoot(root);
            if (python.isEmpty())
                fail("缺少工坊 Python 兼容引擎，请使用完整包");
            process->setProcessEnvironment(environment());
            process->setWorkingDirectory(engine);
            process->start(python, {engine + "/native/bridge/workshop_manager.py", "--root", engine, "--data",
                                    data, "--persistent"});
            if (!process->waitForStarted(10000))
                fail("工坊管理器启动失败：" + process->errorString());
            pid = process->processId();
        }
        const auto id = QString::number(++sequence);
        request["request_id"] = id;
        const auto payload = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
        if (process->write(payload) != payload.size())
            fail("工坊文件管理通道已断开");
        process->waitForBytesWritten(1000);
        Timer elapsed{&trace, scene};
        elapsed.start();
        while (elapsed.elapsed() < 60000) {
            errors += process->readAllStandardError();
            errors = errors.right(65536);
            buffer += process->readAllStandardOutput();
            if (buffer.size() > 16 * 1024 * 1024)
                fail("工坊管理器返回数据过大");
            if (const auto newline = buffer.indexOf('\n'); newline >= 0) {
                const auto reply = QJsonDocument::fromJson(buffer.left(newline)).object();
                buffer.remove(0, newline + 1);
                if (reply["request_id"] != id || reply["manager_protocol"].toInt() != 1)
                    fail("工坊文件管理接口不匹配，请更新完整包");
                if (!reply["ok"].toBool())
                    fail(reply["error"].toString("工坊文件操作失败"));
                return reply["data"].toObject();
            }
            if (process->state() == QProcess::NotRunning)
                fail("工坊文件管理器已退出");
            process->waitForReadyRead(15);
        }
        fail("工坊代码检查超时");
    }
    void stop() {
        if (process && process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(3000);
        }
        pid = 0;
        buffer.clear();
        errors.clear();
    }
    QJsonObject run(rust::WorkflowMethod method, QJsonObject request = {}) {
        QString python, engine, id;
        QByteArray payload;
        QJsonObject reply, result;
        Timer elapsed{&trace, scene};
        qsizetype newline = -1;
        rust::workflowRun(method, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::WorkshopFileHasProcess:
                out[0] = process != nullptr;
                break;
            case rust::WorkflowOp::WorkshopFileProcessRunning:
                out[0] = process->state() != QProcess::NotRunning;
                break;
            case rust::WorkflowOp::WorkshopFileKillProcess:
                process->kill();
                break;
            case rust::WorkflowOp::WorkshopFileWaitFinishedIgnored:
                process->waitForFinished(int(a[0]));
                break;
            case rust::WorkflowOp::WorkshopFileResetPid:
                pid = 0;
                break;
            case rust::WorkflowOp::WorkshopFileClearBuffer:
                buffer.clear();
                break;
            case rust::WorkflowOp::WorkshopFileClearErrors:
                errors.clear();
                break;
            case rust::WorkflowOp::WorkshopFileCreateProcess:
                process = new Channel(&trace, scene);
                break;
            case rust::WorkflowOp::WorkshopFileResolveRuntime:
                python = workshopPython(root);
                engine = workshopRoot(root);
                break;
            case rust::WorkflowOp::WorkshopFilePythonEmpty:
                out[0] = python.isEmpty();
                break;
            case rust::WorkflowOp::WorkshopFileSetEnvironment:
                process->setProcessEnvironment(environment());
                break;
            case rust::WorkflowOp::WorkshopFileSetWorkingDirectory:
                process->setWorkingDirectory(engine);
                break;
            case rust::WorkflowOp::WorkshopFileStartPersistentManager:
                process->start(python, {engine + "/native/bridge/workshop_manager.py", "--root", engine,
                                        "--data", data, "--persistent"});
                break;
            case rust::WorkflowOp::WorkshopFileWaitStarted:
                out[0] = process->waitForStarted(int(a[0]));
                break;
            case rust::WorkflowOp::WorkshopFileRememberPid:
                pid = process->processId();
                break;
            case rust::WorkflowOp::WorkshopFileAssignRequestIdentity:
                id = QString::number(++sequence);
                request["request_id"] = id;
                break;
            case rust::WorkflowOp::WorkshopFileEncodePayload:
                payload = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
                break;
            case rust::WorkflowOp::WorkshopFileWritePayload:
                out[0] = process->write(payload) == payload.size();
                break;
            case rust::WorkflowOp::WorkshopFileWaitWritten:
                process->waitForBytesWritten(int(a[0]));
                break;
            case rust::WorkflowOp::WorkshopFileStartTimer:
                elapsed.start();
                break;
            case rust::WorkflowOp::WorkshopFileElapsed:
                out[0] = double(elapsed.elapsed());
                break;
            case rust::WorkflowOp::WorkshopFileReadStderr:
                errors += process->readAllStandardError();
                break;
            case rust::WorkflowOp::WorkshopFileTrimStderr:
                errors = errors.right(qsizetype(a[0]));
                break;
            case rust::WorkflowOp::WorkshopFileReadStdout:
                buffer += process->readAllStandardOutput();
                break;
            case rust::WorkflowOp::WorkshopFileBufferSize:
                out[0] = double(buffer.size());
                break;
            case rust::WorkflowOp::WorkshopFileNewline:
                newline = buffer.indexOf('\n');
                out[0] = double(newline);
                break;
            case rust::WorkflowOp::WorkshopFileParseReply:
                reply = QJsonDocument::fromJson(buffer.left(newline)).object();
                break;
            case rust::WorkflowOp::WorkshopFileConsumeLine:
                buffer.remove(0, newline + 1);
                break;
            case rust::WorkflowOp::WorkshopFileReplyIdentityMatches:
                out[0] = std::as_const(reply)["request_id"] == id;
                break;
            case rust::WorkflowOp::WorkshopFileManagerProtocol:
                out[0] = std::as_const(reply)["manager_protocol"].toInt();
                break;
            case rust::WorkflowOp::WorkshopFileReplyOk:
                out[0] = std::as_const(reply)["ok"].toBool();
                break;
            case rust::WorkflowOp::WorkshopFileReturnData:
                result = std::as_const(reply)["data"].toObject();
                break;
            case rust::WorkflowOp::WorkshopFileWaitReadyRead:
                process->waitForReadyRead(int(a[0]));
                break;
            case rust::WorkflowOp::WorkshopFileFailure:
                workshopFileFailure(int(a[0]), reply, process);
                break;
            default:
                fail("Rust 工坊持久文件会话宿主无效");
            }
        });
        return result;
    }
    QJsonObject outcome(bool rust, bool command) {
        QJsonObject result;
        auto request = QJsonObject{{"op", "mode"}, {"team", QJsonArray{"A", "B", "C"}}};
        try {
            if (command) {
                if (rust) {
                    rust::workflowRun(rust::WorkflowMethod::WorkshopFileCommand, {},
                                      [&](rust::WorkflowOp op, const double *, double *) {
                                          if (op == rust::WorkflowOp::WorkshopFileExchangeNested)
                                              result["reply"] =
                                                  run(rust::WorkflowMethod::WorkshopFileExchange, request);
                                          else if (op == rust::WorkflowOp::WorkshopFileStopNested)
                                              run(rust::WorkflowMethod::WorkshopFileStop);
                                          else
                                              fail("unexpected-command-op");
                                      });
                } else {
                    try {
                        result["reply"] = exchange(request);
                    } catch (...) {
                        stop();
                        throw;
                    }
                }
            } else
                result["reply"] =
                    rust ? run(rust::WorkflowMethod::WorkshopFileExchange, request) : exchange(request);
        } catch (const std::exception &e) {
            result["error"] = errorText(e);
        }
        result["trace"] = trace;
        result["buffer"] = QString::fromUtf8(buffer);
        result["errors"] = QString::fromUtf8(errors);
        result["sequence"] = QString::number(sequence);
        result["pid"] = double(pid.load());
        return result;
    }
};
} // namespace okww::runtime::workshop_file_test
