#pragma once
#include "reference_scripts.hpp"
namespace okww::runtime::script_test {
struct Channel {
    QJsonArray *trace;
    int scene, poll = 0;
    QByteArray output;
    QByteArray readAllStandardError() {
        trace->append(QJsonObject{{"stderr", poll}});
        return QByteArray(scene % 5 ? 17 : 70000, char('a' + poll % 26));
    }
    QByteArray readAllStandardOutput() {
        trace->append(QJsonObject{{"stdout", poll}});
        return poll == scene % 3 ? output : QByteArray{};
    }
    QProcess::ProcessState state() {
        trace->append(QJsonObject{{"state", poll}});
        return poll > 4 || scene % 17 == 0 ? QProcess::NotRunning : QProcess::Running;
    }
    void waitForReadyRead(int ms) {
        trace->append(QJsonObject{{"wait", ms}});
        ++poll;
    }
};
struct ReceiveReplay {
    struct Check {
        QJsonArray *trace;
        int scene, calls = 0;
        void checkpoint() {
            trace->append("checkpoint");
            if (scene % 23 == 0 && ++calls == 2)
                throw Cancelled();
        }
    };
    struct Context {
        Check execution;
    };
    QJsonArray trace;
    Context c_;
    Channel process_;
    QByteArray buffer_, errors_;
    std::function<bool()> enabled_;
    int scene, writes = 0;
    explicit ReceiveReplay(int s) : c_{{&trace, s}}, process_{&trace, s}, scene(s) {
        QJsonArray lines;
        QByteArray bytes = "\nnot-json\n{}\n[1]\n";
        for (int i = 0; i < 1 + s % 5; ++i)
            bytes += QJsonDocument(QJsonObject{{"event", "rpc"}, {"request", QJsonObject{{"number", i}}}})
                         .toJson(QJsonDocument::Compact) +
                     '\n';
        bytes +=
            QJsonDocument(QJsonObject{{"event", "done"}, {"ok", true}, {"data", QJsonObject{{"scene", s}}}})
                .toJson(QJsonDocument::Compact) +
            '\n';
        if (s % 19 == 0)
            bytes = "partial";
        if (s % 29 == 0)
            bytes = QByteArray(16 * 1024 * 1024 + 1, 'x');
        if (s % 31 == 0)
            buffer_ = "{\"event\":\"initial\"}\n";
        process_.output = bytes;
        if (s % 3)
            enabled_ = [&] {
                trace.append("enabled");
                return scene % 13 != 0;
            };
    }
    void write(QJsonObject message) {
        trace.append(QJsonObject{{"write", message}});
        ++writes;
        if (scene % 7 == 0 && writes == 1)
            fail("write-failed");
        if (scene % 41 == 0 && writes == 2)
            fail("error-write-failed");
    }
    QJsonObject rpc(QJsonObject request) {
        trace.append(QJsonObject{{"rpc", request}});
        if (scene % 11 == 0)
            throw Cancelled();
        if (scene % 37 == 0)
            throw 37;
        if (scene % 5 == 0)
            fail("rpc-failed");
        return {{"answer", request["number"]}};
    }
    [[noreturn]] void scriptFailure(int code, const QJsonObject &, const QByteArray &errors) {
        if (code == 9)
            throw ScriptDisabled();
        if (code == 10)
            fail("脚本返回数据过大");
        if (code == 11)
            fail("脚本兼容引擎已退出：" + QString::fromUtf8(errors.right(2000)));
        fail("unexpected-receive-error");
    }
    QJsonObject receiveOriginal() {
        for (;;) {
            c_.execution.checkpoint();
            if (enabled_ && !enabled_())
                throw ScriptDisabled();
            errors_ += process_.readAllStandardError();
            errors_ = errors_.right(65536);
            buffer_ += process_.readAllStandardOutput();
            if (buffer_.size() > 16 * 1024 * 1024)
                fail("脚本返回数据过大");
            while (buffer_.contains('\n')) {
                const auto at = buffer_.indexOf('\n');
                const auto message = QJsonDocument::fromJson(buffer_.left(at)).object();
                buffer_.remove(0, at + 1);
                if (message["event"] == "rpc") {
                    try {
                        write({{"ok", true}, {"data", rpc(message["request"].toObject())}});
                    } catch (const Cancelled &) {
                        throw;
                    } catch (const std::exception &error) {
                        write({{"ok", false}, {"error", errorText(error)}});
                    }
                } else if (!message.isEmpty())
                    return message;
            }
            if (process_.state() == QProcess::NotRunning)
                fail("脚本兼容引擎已退出：" + QString::fromUtf8(errors_.right(2000)));
            process_.waitForReadyRead(15);
        }
    }
    QJsonObject receiveRust() {
        QJsonObject result, message, rpcResult, reply;
        qsizetype lineAt = 0;
        rust::workflowRun(
            rust::WorkflowMethod::ScriptReceive, {},
            [&](rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &error) {
                switch (op) {
                case rust::WorkflowOp::Checkpoint:
                    c_.execution.checkpoint();
                    break;
                case rust::WorkflowOp::ScriptFailure:
                    scriptFailure(int(a[0]), reply, errors_);
                    break;
                case rust::WorkflowOp::ScriptProcessRunning:
                    out[0] = process_.state() != QProcess::NotRunning;
                    break;
                case rust::WorkflowOp::ScriptHasEnabledPredicate:
                    out[0] = bool(enabled_);
                    break;
                case rust::WorkflowOp::ScriptEnabled:
                    out[0] = enabled_();
                    break;
                case rust::WorkflowOp::ScriptReadStderr:
                    errors_ += process_.readAllStandardError();
                    break;
                case rust::WorkflowOp::ScriptTrimStderr:
                    errors_ = errors_.right(qsizetype(a[0]));
                    break;
                case rust::WorkflowOp::ScriptReadStdout:
                    buffer_ += process_.readAllStandardOutput();
                    break;
                case rust::WorkflowOp::ScriptBufferBytes:
                    out[0] = double(buffer_.size());
                    break;
                case rust::WorkflowOp::ScriptBufferHasLine:
                    out[0] = buffer_.contains('\n');
                    break;
                case rust::WorkflowOp::ScriptParseLine:
                    lineAt = buffer_.indexOf('\n');
                    message = QJsonDocument::fromJson(buffer_.left(lineAt)).object();
                    break;
                case rust::WorkflowOp::ScriptConsumeLine:
                    buffer_.remove(0, lineAt + 1);
                    break;
                case rust::WorkflowOp::ScriptMessageRpc:
                    out[0] = std::as_const(message)["event"] == "rpc";
                    break;
                case rust::WorkflowOp::ScriptInvokeRpc:
                    rpcResult = rpc(std::as_const(message)["request"].toObject());
                    break;
                case rust::WorkflowOp::ScriptWriteRpcSuccess:
                    write({{"ok", true}, {"data", rpcResult}});
                    break;
                case rust::WorkflowOp::ScriptWriteRpcError:
                    try {
                        std::rethrow_exception(error);
                    } catch (const std::exception &e) {
                        write({{"ok", false}, {"error", errorText(e)}});
                    }
                    break;
                case rust::WorkflowOp::ScriptMessageEmpty:
                    out[0] = message.isEmpty();
                    break;
                case rust::WorkflowOp::ScriptReturnMessage:
                    result = message;
                    break;
                case rust::WorkflowOp::ScriptWaitReadyRead:
                    process_.waitForReadyRead(int(a[0]));
                    break;

                default:
                    fail("unexpected-receive-sdk");
                }
            });
        return result;
    }
    QJsonObject run(bool rust) {
        QJsonObject result;
        try {
            result["reply"] = rust ? receiveRust() : receiveOriginal();
        } catch (const Cancelled &e) {
            result["cancelled"] = true;
            result["error"] = errorText(e);
        } catch (const std::exception &e) {
            result["error"] = errorText(e);
        } catch (int v) {
            result["unknown"] = v;
        }
        result["trace"] = trace;
        result["buffer"] = QString::fromUtf8(buffer_);
        result["errors"] = QString::fromUtf8(errors_);
        return result;
    }
};
} // namespace okww::runtime::script_test
