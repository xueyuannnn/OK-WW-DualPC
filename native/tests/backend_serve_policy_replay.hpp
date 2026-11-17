#pragma once
#include "backend.hpp"
#include "platform_workflow.hpp"
namespace okww::backend_serve_test {
struct State {
    int scene, selects = 0, accepts = 0, clients = 0, chunk = 0, sends = 0, lastError = 0;
    bool shut = false;
    std::atomic<bool> *exit = nullptr;
    QList<QByteArray> chunks;
    QByteArray wire;
    QJsonArray trace;
    explicit State(int s) : scene(s) {}
    void prepare() {
        chunk = 0;
        sends = 0;
        shut = false;
        chunks.clear();
        const int mode = (scene + clients) % 13;
        QJsonObject body{{"n", clients}, {"text", "你好"}};
        if (mode == 1)
            body["fail"] = "std";
        if (mode == 2)
            body["fail"] = "blocked";
        if (mode == 12)
            body["fail"] = "unknown";
        QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact) + '\n';
        if (mode == 3)
            payload = "not-json\n";
        if (mode == 4)
            payload = "[1,2]\n";
        if (mode == 5)
            payload = "\n";
        if (mode == 6)
            payload += QJsonDocument(QJsonObject{{"second", true}}).toJson(QJsonDocument::Compact) + '\n';
        if (mode == 7) {
            chunks.append(payload.left(5));
            chunks.append(payload.mid(5));
        } else if (mode == 8) {
            for (int i = 0; i < 9; ++i)
                chunks.append(QByteArray(8192, 'x'));
        } else if (mode != 10 && mode != 11)
            chunks.append(payload);
    }
};
inline thread_local State *active = nullptr;
inline int select(int, fd_set *set, void *, void *, timeval *wait) {
    const int n = ++active->selects;
    active->trace.append(QJsonObject{{"select", n},
                                     {"count", int(set->fd_count)},
                                     {"seconds", qint64(wait->tv_sec)},
                                     {"microseconds", qint64(wait->tv_usec)}});
    if (n > 12)
        *active->exit = true;
    if (active->scene % 7 == 0 && n == 1)
        return 0;
    if (active->scene % 11 == 0 && n == 2)
        return -1;
    return 1;
}
inline SOCKET accept(SOCKET listener, void *, void *) {
    active->trace.append(QJsonObject{{"accept", QString::number(listener)}});
    if (active->scene % 17 == 0 && ++active->accepts == 1)
        return INVALID_SOCKET;
    ++active->clients;
    active->prepare();
    return SOCKET(UINT64_C(0x123456789abcdef0) + uint64_t(active->clients));
}
inline int setsockopt(SOCKET socket, int level, int name, const char *data, int length) {
    DWORD value = 0;
    memcpy(&value, data, size_t(std::min(length, int(sizeof(value)))));
    active->trace.append(QJsonObject{{"option-socket", QString::number(socket)},
                                     {"level", level},
                                     {"name", name},
                                     {"length", length},
                                     {"value", qint64(value)}});
    return -1;
}
inline int recv(SOCKET socket, char *out, int length, int flags) {
    active->trace.append(
        QJsonObject{{"recv", QString::number(socket)}, {"length", length}, {"flags", flags}});
    const int mode = (active->scene + active->clients) % 13;
    if (active->shut) {
        if (active->clients >= 2)
            *active->exit = true;
        return 0;
    }
    if (mode == 9 && active->chunk == 0) {
        ++active->chunk;
        active->lastError = WSAETIMEDOUT;
        return -1;
    }
    if (mode == 10) {
        active->lastError = WSAECONNRESET;
        if (active->clients >= 2)
            *active->exit = true;
        return -1;
    }
    const int index = active->chunk++ - (mode == 9 ? 1 : 0);
    if (index >= active->chunks.size()) {
        if (active->clients >= 2)
            *active->exit = true;
        return 0;
    }
    const auto &bytes = active->chunks[index];
    memcpy(out, bytes.constData(), size_t(bytes.size()));
    return int(bytes.size());
}
inline int WSAGetLastError() {
    active->trace.append("last-error");
    return active->lastError;
}
inline int send(SOCKET socket, const char *data, int length, int flags) {
    active->trace.append(QJsonObject{{"send", QString::number(socket)},
                                     {"bytes", QString::fromLatin1(QByteArray(data, length).toHex())},
                                     {"length", length},
                                     {"flags", flags}});
    if (active->scene % 19 == 0 && ++active->sends == 2)
        return 0;
    const int count = std::min(length, active->scene % 7 + 1);
    active->wire.append(data, count);
    return count;
}
inline int shutdown(SOCKET socket, int how) {
    active->trace.append(QJsonObject{{"shutdown", QString::number(socket)}, {"how", how}});
    active->shut = true;
    return -1;
}
inline int closesocket(SOCKET socket) {
    active->trace.append(QJsonObject{{"close", QString::number(socket)}});
    return -1;
}
struct BackendState {
    std::atomic<bool> exit_{false};
    SOCKET listen_ = SOCKET(UINT64_C(0x123456789abcde10));
    bool armed_ = true;
    QJsonObject dispatch(const QJsonObject &q) {
        active->trace.append(QJsonObject{{"dispatch", q}});
        const auto failure = q["fail"].toString();
        if (failure == "std")
            throw std::runtime_error("dispatch fixture");
        if (failure == "blocked")
            throw LocalInputBlocked("focus fixture", LocalInputBlocked::Recovery::DismissRunDialog);
        if (failure == "unknown")
            throw 27;
        return {{"echo", q}, {"reply", "通过"}};
    }
    void arm(bool enabled) {
        active->trace.append(QJsonObject{{"arm", enabled}});
        armed_ = enabled;
        if (active->scene % 23 == 0)
            throw std::runtime_error("arm fixture");
        if (active->scene % 29 == 0)
            throw 31;
    }
};
#define BACKEND_SERVE_FIXTURE_CLASS                                                                          \
    class Backend : public BackendState {                                                                    \
      public:                                                                                                \
        void serve();                                                                                        \
    };
namespace original {
BACKEND_SERVE_FIXTURE_CLASS
void Backend::serve() {
    while (!exit_) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(listen_, &set);
        timeval wait{0, 50000};
        if (backend_serve_test::select(0, &set, nullptr, nullptr, &wait) <= 0)
            continue;
        SOCKET client = backend_serve_test::accept(listen_, nullptr, nullptr);
        if (client == INVALID_SOCKET)
            continue;
        DWORD timeout = 100;
        backend_serve_test::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char *>(&timeout),
                                       sizeof(timeout));
        timeout = 1000;
        backend_serve_test::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char *>(&timeout),
                                       sizeof(timeout));
        BOOL yes = TRUE;
        backend_serve_test::setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char *>(&yes),
                                       sizeof(yes));
        QByteArray buffered;
        while (!exit_) {
            char bytes[8192];
            int n = backend_serve_test::recv(client, bytes, sizeof(bytes), 0);
            if (n == 0)
                break;
            if (n < 0) {
                if (backend_serve_test::WSAGetLastError() == WSAETIMEDOUT)
                    continue;
                break;
            }
            buffered.append(bytes, n);
            if (buffered.size() > 65536)
                break;
            int newline;
            while ((newline = int(buffered.indexOf('\n'))) >= 0) {
                auto line = buffered.left(newline);
                buffered.remove(0, newline + 1);
                QJsonObject reply;
                try {
                    QJsonParseError e;
                    auto doc = QJsonDocument::fromJson(line, &e);
                    if (e.error != QJsonParseError::NoError || !doc.isObject())
                        fail("无效 IPC 消息");
                    reply = {{"ok", true}, {"result", dispatch(doc.object())}};
                } catch (const std::exception &e) {
                    reply = {{"ok", false}, {"error", errorText(e)}};
                }
                auto out = QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n';
                int sent = 0;
                while (sent < out.size()) {
                    int count =
                        backend_serve_test::send(client, out.constData() + sent, int(out.size()) - sent, 0);
                    if (count <= 0)
                        break;
                    sent += count;
                }
                if (sent != out.size()) {
                    backend_serve_test::shutdown(client, SD_BOTH);
                    break;
                }
            }
        }
        backend_serve_test::closesocket(client);
        try {
            arm(false);
        } catch (...) {
        }
    }
}
} // namespace original
namespace current {
BACKEND_SERVE_FIXTURE_CLASS
struct BackendServeSdk {
    Backend &b;
    SOCKET client = INVALID_SOCKET;
    QByteArray buffered;
    char bytes[8192];
    int received = 0;
    QByteArray line;
    QJsonObject reply;
    QByteArray output;
    int sent = 0;
    QJsonParseError parseError;
    QJsonDocument document;
    void operator()(rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &error) {
        switch (op) {
        case rust::WorkflowOp::BackendServeExit:
            out[0] = bool(b.exit_);
            break;
        case rust::WorkflowOp::BackendServeReadable: {
            fd_set set;
            FD_ZERO(&set);
            FD_SET(b.listen_, &set);
            timeval wait{0, 50000};
            out[0] = backend_serve_test::select(0, &set, nullptr, nullptr, &wait) > 0;
            break;
        }
        case rust::WorkflowOp::BackendServeAccept:
            client = backend_serve_test::accept(b.listen_, nullptr, nullptr);
            break;
        case rust::WorkflowOp::BackendServeClientValid:
            out[0] = client != INVALID_SOCKET;
            break;
        case rust::WorkflowOp::BackendServeReceiveTimeout: {
            DWORD timeout = 100;
            backend_serve_test::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                                           reinterpret_cast<char *>(&timeout), sizeof(timeout));
            break;
        }
        case rust::WorkflowOp::BackendServeSendTimeout: {
            DWORD timeout = 1000;
            backend_serve_test::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
                                           reinterpret_cast<char *>(&timeout), sizeof(timeout));
            break;
        }
        case rust::WorkflowOp::BackendServeNoDelay: {
            BOOL yes = TRUE;
            backend_serve_test::setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char *>(&yes),
                                           sizeof(yes));
            break;
        }
        case rust::WorkflowOp::BackendServeReceive:
            received = backend_serve_test::recv(client, bytes, sizeof(bytes), 0);
            out[0] = received;
            break;
        case rust::WorkflowOp::BackendServeTimedOut:
            out[0] = backend_serve_test::WSAGetLastError() == WSAETIMEDOUT;
            break;
        case rust::WorkflowOp::BackendServeAppendBuffer:
            buffered.append(bytes, received);
            break;
        case rust::WorkflowOp::BackendServeBufferSize:
            out[0] = double(buffered.size());
            break;
        case rust::WorkflowOp::BackendServeNewline:
            out[0] = int(buffered.indexOf('\n'));
            break;
        case rust::WorkflowOp::BackendServeReadLine:
            line = buffered.left(int(a[0]));
            break;
        case rust::WorkflowOp::BackendServeRemoveLine:
            buffered.remove(0, int(a[0]) + 1);
            break;
        case rust::WorkflowOp::BackendServeParseDocument:
            parseError = {};
            document = QJsonDocument::fromJson(line, &parseError);
            break;
        case rust::WorkflowOp::BackendServeParseValid:
            out[0] = parseError.error == QJsonParseError::NoError;
            break;
        case rust::WorkflowOp::BackendServeDocumentObject:
            out[0] = document.isObject();
            break;
        case rust::WorkflowOp::BackendServeInvalidMessage:
            fail("无效 IPC 消息");
            break;
        case rust::WorkflowOp::BackendServeSuccessReply:
            reply = {{"ok", true}, {"result", b.dispatch(document.object())}};
            break;
        case rust::WorkflowOp::BackendServeDropDocument:
            document = QJsonDocument{};
            break;
        case rust::WorkflowOp::BackendServeErrorReply:
            try {
                std::rethrow_exception(error);
            } catch (const std::exception &e) {
                reply = {{"ok", false}, {"error", errorText(e)}};
            }
            break;
        case rust::WorkflowOp::BackendServeEncodeReply:
            output = QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n';
            break;
        case rust::WorkflowOp::BackendServeResetSent:
            sent = 0;
            break;
        case rust::WorkflowOp::BackendServeHasUnsent:
            out[0] = sent < output.size();
            break;
        case rust::WorkflowOp::BackendServeSend:
            out[0] =
                backend_serve_test::send(client, output.constData() + sent, int(output.size()) - sent, 0);
            break;
        case rust::WorkflowOp::BackendServeAdvanceSent:
            sent += int(a[0]);
            break;
        case rust::WorkflowOp::BackendServeSendComplete:
            out[0] = sent == output.size();
            break;
        case rust::WorkflowOp::BackendServeShutdownClient:
            backend_serve_test::shutdown(client, SD_BOTH);
            break;
        case rust::WorkflowOp::BackendServeDropLineScope:
            output = QByteArray{};
            reply = QJsonObject{};
            line = QByteArray{};
            break;
        case rust::WorkflowOp::BackendServeCloseClient:
            backend_serve_test::closesocket(client);
            break;
        case rust::WorkflowOp::BackendServeDisarm:
            b.arm(false);
            break;
        case rust::WorkflowOp::BackendServeDropBuffer:
            buffered = QByteArray{};
            break;
        default:
            fail("Rust 本机通信服务宿主无效");
        }
    }
};
void Backend::serve() {
    BackendServeSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::BackendServe, {}, sdk);
}
} // namespace current
#undef BACKEND_SERVE_FIXTURE_CLASS
template <class B> QJsonObject run() {
    B b;
    active->exit = &b.exit_;
    QJsonObject result;
    try {
        b.serve();
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    } catch (...) {
        result["error"] = "unknown";
    }
    result["armed"] = b.armed_;
    result["exited"] = bool(b.exit_);
    return result;
}
inline QJsonObject outcome(int scene, bool rust) {
    State state(scene);
    active = &state;
    auto result = rust ? run<current::Backend>() : run<original::Backend>();
    result["wire"] = QString::fromLatin1(state.wire.toHex());
    result["trace"] = state.trace;
    return result;
}
} // namespace okww::backend_serve_test
