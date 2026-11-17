#pragma once
#include "backend.hpp"
#include "platform_workflow.hpp"
namespace okww::backend_dispatch_test {
struct State {
    int scene;
    bool connected = true;
    FramePtr frame;
    QByteArray mapping = QByteArray(128, char(0x65));
    i64 clock = INT64_C(0x123456789abcde00);
    QJsonArray trace;
    explicit State(int s) : scene(s) { connected = s % 11 != 0; }
};
inline thread_local State *active = nullptr;
inline i64 nowNs() {
    active->trace.append(QJsonObject{{"clock", QString::number(active->clock)}});
    return active->clock += 1000000;
}
struct Mutex {
    std::recursive_mutex value;
    void lock() {
        active->trace.append("lock");
        value.lock();
    }
    void unlock() {
        active->trace.append("unlock");
        value.unlock();
    }
};
struct Box {
    bool connected() {
        active->trace.append("connected");
        return active->connected;
    }
    QJsonObject state() {
        active->trace.append("box-state");
        if (active->scene % 17 == 0)
            throw std::runtime_error("state fixture");
        return {{"connected", active->connected}, {"capability", "fake"}};
    }
    void action(const QString &name, QJsonArray args = {}) {
        active->trace.append(QJsonObject{{"input", name}, {"args", args}});
        switch (active->scene % 6) {
        case 1:
            active->connected = false;
            throw std::runtime_error("transport fixture");
        case 2:
            throw LocalInputBlocked("focus fixture", LocalInputBlocked::Recovery::DismissRunDialog);
        case 3:
            throw 27;
        case 4:
            throw std::runtime_error("send fixture");
        default:
            break;
        }
    }
    void release() {
        active->trace.append("release");
        if (active->scene % 19 == 0)
            throw std::runtime_error("release fixture");
        if (active->scene % 23 == 0)
            throw 29;
    }
    void probe() {
        active->trace.append("probe");
        if (!active->connected)
            throw std::runtime_error("probe not connected");
    }
    void move(int x, int y) { action("move", {x, y}); }
    void absolute(int x, int y, int w, int h) { action("absolute", {x, y, w, h}); }
    void button(const QString &name, bool down) { action("button", {name, down}); }
    void key(int code, bool down) { action("key", {code, down}); }
    void wheel(int steps) { action("wheel", {steps}); }
};
struct BackendState {
    mutable Mutex mutex_;
    Config cfg_ = Config::defaults();
    QString token_ = "test-token";
    std::unique_ptr<Box> box_;
    i64 heartbeat_ = 0;
    bool armed_ = true;
    QString error_;
    QJsonObject inputFault_;
    void *mapped_ = nullptr;
    QJsonObject snapshot() {
        active->trace.append("snapshot");
        return {{"armed", armed_},
                {"heartbeat", QString::number(heartbeat_)},
                {"error", error_},
                {"fault", inputFault_}};
    }
    FramePtr frameLocked() {
        active->trace.append("frame");
        if (active->scene % 41 == 0)
            throw std::runtime_error("frame fixture");
        return active->scene % 5 ? active->frame : nullptr;
    }
    bool recoverCaptureLocked() {
        active->trace.append("recover");
        if (active->scene % 7 == 0) {
            armed_ = false;
            error_ = "recovery failed";
            return false;
        }
        return true;
    }
    void disarmFaultLocked(const QString &reason, const QString &message) {
        active->trace.append(QJsonObject{{"fault", reason}, {"message", message}});
        if (active->scene % 43 == 0)
            throw std::runtime_error("fault fixture");
        armed_ = false;
        error_ = "任务已暂停：" + message;
        inputFault_ = {{"reason", reason}};
    }
};
#define BACKEND_DISPATCH_FIXTURE_CLASS                                                                       \
    class Backend : public BackendState {                                                                    \
      public:                                                                                                \
        static constexpr int mappingCapacity = 64 * 1024 * 1024;                                             \
        QJsonObject dispatch(const QJsonObject &);                                                           \
    };
namespace original {
BACKEND_DISPATCH_FIXTURE_CLASS
QJsonObject Backend::dispatch(const QJsonObject &q) {
    std::lock_guard lock(mutex_);
    if (q["token"].toString() != token_)
        fail("兼容引擎身份校验失败");
    const auto op = q["op"].toString();
    if (op == "heartbeat") {
        heartbeat_ = nowNs();
        return snapshot();
    }
    if (op == "state" || op == "connect")
        return snapshot();
    if (op == "frame") {
        auto f = frameLocked();
        if (!f)
            return {{"available", false}};
        if (f->bytes.size() > mappingCapacity)
            fail("采集帧超出共享内存大小");
        memcpy(mapped_, f->bytes.constData(), f->bytes.size());
        return {{"available", true},
                {"size", int(f->bytes.size())},
                {"width", f->width},
                {"height", f->height},
                {"stride", f->stride},
                {"format", formatName(f->format)},
                {"bottom_up", f->bottomUp},
                {"sequence", QString::number(f->sequence)},
                {"age_ms", double(nowNs() - f->captured) / 1e6}};
    }
    if (op == "release") {
        if (box_)
            box_->release();
        return {};
    }
    if (op == "probe") {
        if (!box_)
            fail("盒子未连接");
        box_->probe();
        return snapshot();
    }
    // Releasing a control is always allowed, including after Pause or a stale
    // frame. Never turn a cleanup key-up into a second, misleading exception.
    if ((op == "key" || op == "button") && !q["down"].toBool()) {
        if (box_ && box_->connected()) {
            if (op == "key")
                box_->key(q["code"].toInt(), false);
            else
                box_->button(q["name"].toString(), false);
        }
        return box_ ? box_->state() : QJsonObject{};
    }
    if (armed_ && (!box_ || !box_->connected()))
        disarmFaultLocked("box_disconnected", "键鼠盒子连接中断");
    else if (armed_ && !frameLocked()) {
        if (recoverCaptureLocked())
            throw LocalInputBlocked("等待采集画面自动恢复");
    }
    if (!armed_)
        fail(error_.isEmpty() ? "C++ 输入通道已暂停，请先启动自动任务" : error_);
    try {
        if (op == "move")
            box_->move(q["x"].toInt(), q["y"].toInt());
        else if (op == "absolute")
            box_->absolute(q["x"].toInt(), q["y"].toInt(), q["width"].toInt(), q["height"].toInt());
        else if (op == "button")
            box_->button(q["name"].toString(), q["down"].toBool());
        else if (op == "key")
            box_->key(q["code"].toInt(), q["down"].toBool());
        else if (op == "wheel")
            box_->wheel(q["steps"].toInt());
        else
            fail("未知的原生输入操作");
    } catch (const LocalInputBlocked &) {
        // No down/move was sent. Keep the device session armed so the runtime
        // can wait for focus/modifiers to recover without restarting threads.
        box_->release();
        throw;
    } catch (const std::exception &error) {
        if (cfg_.s("kmbox.protocol") == "sendinput")
            disarmFaultLocked("local_input_error", "本机输入异常：" + errorText(error));
        else if (!box_->connected())
            disarmFaultLocked("input_transport_error", "键鼠盒子通信失败：" + errorText(error));
        throw;
    }
    return box_->state();
}
} // namespace original
namespace current {
BACKEND_DISPATCH_FIXTURE_CLASS
static void backendRpcFailure(int kind) {
    switch (kind) {
    case 0:
        fail("兼容引擎身份校验失败");
    case 1:
        fail("采集帧超出共享内存大小");
    case 2:
        fail("盒子未连接");
    default:
        fail("Rust 兼容引擎错误宿主无效");
    }
}
struct BackendDispatchSdk {
    Backend &b;
    const QJsonObject &q;
    QString command;
    FramePtr frame;
    QJsonObject value;
    void operator()(rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &error) {
        switch (op) {
        case rust::WorkflowOp::BackendRpcAuthenticated:
            out[0] = q["token"].toString() == b.token_;
            break;
        case rust::WorkflowOp::BackendRpcCommand:
            command = q["op"].toString();
            out[0] = QStringList{"heartbeat", "state",  "connect", "frame",    "release", "probe",
                                 "key",       "button", "move",    "absolute", "wheel"}
                         .indexOf(command);
            break;
        case rust::WorkflowOp::BackendRpcHeartbeat:
            b.heartbeat_ = nowNs();
            break;
        case rust::WorkflowOp::BackendRpcSnapshot:
            value = b.snapshot();
            break;
        case rust::WorkflowOp::BackendRpcReadFrame:
            frame = b.frameLocked();
            break;
        case rust::WorkflowOp::BackendRpcFramePresent:
            out[0] = bool(frame);
            break;
        case rust::WorkflowOp::BackendRpcFrameUnavailable:
            value = {{"available", false}};
            break;
        case rust::WorkflowOp::BackendRpcFrameFitsMapping:
            out[0] = frame->bytes.size() <= Backend::mappingCapacity;
            break;
        case rust::WorkflowOp::BackendRpcCopyFrame:
            memcpy(b.mapped_, frame->bytes.constData(), size_t(frame->bytes.size()));
            break;
        case rust::WorkflowOp::BackendRpcFrameResult:
            value = {{"available", true},
                     {"size", int(frame->bytes.size())},
                     {"width", frame->width},
                     {"height", frame->height},
                     {"stride", frame->stride},
                     {"format", formatName(frame->format)},
                     {"bottom_up", frame->bottomUp},
                     {"sequence", QString::number(frame->sequence)},
                     {"age_ms", double(nowNs() - frame->captured) / 1e6}};
            break;
        case rust::WorkflowOp::BackendRpcHasBox:
            out[0] = bool(b.box_);
            break;
        case rust::WorkflowOp::BackendRpcRelease:
            b.box_->release();
            break;
        case rust::WorkflowOp::BackendRpcProbe:
            b.box_->probe();
            break;
        case rust::WorkflowOp::BackendRpcDown:
            out[0] = q["down"].toBool();
            break;
        case rust::WorkflowOp::BackendRpcBoxConnected:
            out[0] = b.box_->connected();
            break;
        case rust::WorkflowOp::BackendRpcKeyUp:
            b.box_->key(q["code"].toInt(), false);
            break;
        case rust::WorkflowOp::BackendRpcButtonUp:
            b.box_->button(q["name"].toString(), false);
            break;
        case rust::WorkflowOp::BackendRpcBoxState:
            value = b.box_->state();
            break;
        case rust::WorkflowOp::BackendRpcArmed:
            out[0] = b.armed_;
            break;
        case rust::WorkflowOp::BackendRpcDisarmBox:
            b.disarmFaultLocked("box_disconnected", "键鼠盒子连接中断");
            break;
        case rust::WorkflowOp::BackendRpcFreshFrame:
            out[0] = bool(b.frameLocked());
            break;
        case rust::WorkflowOp::BackendRpcRecoverFrame:
            out[0] = b.recoverCaptureLocked();
            break;
        case rust::WorkflowOp::BackendRpcWaitFrame:
            throw LocalInputBlocked("等待采集画面自动恢复");
        case rust::WorkflowOp::BackendRpcPausedError:
            fail(b.error_.isEmpty() ? "C++ 输入通道已暂停，请先启动自动任务" : b.error_);
            break;
        case rust::WorkflowOp::BackendRpcMove:
            b.box_->move(q["x"].toInt(), q["y"].toInt());
            break;
        case rust::WorkflowOp::BackendRpcAbsolute:
            b.box_->absolute(q["x"].toInt(), q["y"].toInt(), q["width"].toInt(), q["height"].toInt());
            break;
        case rust::WorkflowOp::BackendRpcButton:
            b.box_->button(q["name"].toString(), q["down"].toBool());
            break;
        case rust::WorkflowOp::BackendRpcKey:
            b.box_->key(q["code"].toInt(), q["down"].toBool());
            break;
        case rust::WorkflowOp::BackendRpcWheel:
            b.box_->wheel(q["steps"].toInt());
            break;
        case rust::WorkflowOp::BackendRpcUnknownCommand:
            fail("未知的原生输入操作");
            break;
        case rust::WorkflowOp::BackendRpcErrorIsLocalBlocked:
            try {
                std::rethrow_exception(error);
            } catch (const LocalInputBlocked &) {
                out[0] = 1;
            } catch (...) {
                out[0] = 0;
            }
            break;
        case rust::WorkflowOp::BackendRpcLocalProtocol:
            out[0] = b.cfg_.s("kmbox.protocol") == "sendinput";
            break;
        case rust::WorkflowOp::BackendRpcDisarmLocalError:
            try {
                std::rethrow_exception(error);
            } catch (const std::exception &e) {
                b.disarmFaultLocked("local_input_error", "本机输入异常：" + errorText(e));
            }
            break;
        case rust::WorkflowOp::BackendRpcDisarmTransportError:
            try {
                std::rethrow_exception(error);
            } catch (const std::exception &e) {
                b.disarmFaultLocked("input_transport_error", "键鼠盒子通信失败：" + errorText(e));
            }
            break;
        case rust::WorkflowOp::BackendRpcFailure:
            backendRpcFailure(int(a[0]));
            break;
        default:
            fail("Rust 兼容引擎命令宿主无效");
        }
    }
};
QJsonObject Backend::dispatch(const QJsonObject &q) {
    std::lock_guard lock(mutex_);
    BackendDispatchSdk sdk{*this, q};
    rust::platformWorkflowRun(rust::WorkflowMethod::BackendDispatch, {}, sdk);
    return sdk.value;
}
} // namespace current
#undef BACKEND_DISPATCH_FIXTURE_CLASS
template <class B> QJsonObject run(int command) {
    B b;
    const int scene = active->scene;
    if (scene % 7 != 0)
        b.box_ = std::make_unique<Box>();
    b.armed_ = scene % 3 != 0;
    b.error_ = scene % 2 ? "paused reason" : "";
    b.cfg_.set("kmbox.protocol", scene % 2 ? "sendinput" : "kmbox_net");
    b.mapped_ = active->mapping.data();
    const QStringList commands{"heartbeat", "state",  "connect", "frame",    "release", "probe",
                               "key",       "button", "move",    "absolute", "wheel",   "unknown"};
    QJsonObject request{{"token", scene % 13 ? "test-token" : "wrong"},
                        {"op", commands[command]},
                        {"down", (scene / 3) % 2 == 0},
                        {"code", scene % 256},
                        {"name", scene % 2 ? "left" : "right"},
                        {"x", 12},
                        {"y", -34},
                        {"width", 1920},
                        {"height", 1080},
                        {"steps", -3}};
    if (scene % 101 == 0)
        request["op"] = 17;
    QJsonObject result;
    try {
        result["reply"] = b.dispatch(request);
    } catch (const LocalInputBlocked &e) {
        result["error"] = errorText(e);
        result["recovery_action"] = int(e.recovery);
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    } catch (...) {
        result["error"] = "unknown";
    }
    result["request"] = request;
    result["armed"] = b.armed_;
    result["backend_error"] = b.error_;
    result["heartbeat"] = QString::number(b.heartbeat_);
    result["fault"] = b.inputFault_;
    result["mapping_hash"] =
        QString::fromLatin1(QCryptographicHash::hash(active->mapping, QCryptographicHash::Sha256).toHex());
    return result;
}
inline QJsonObject outcome(int scene, int command, bool rust) {
    State state(scene);
    active = &state;
    auto frame = std::make_shared<Frame>();
    frame->width = 8;
    frame->height = 4;
    frame->stride = 24;
    frame->format = PixelFormat::BGR24;
    frame->bottomUp = scene % 2 != 0;
    frame->bytes = QByteArray(command == 3 && scene % 97 == 0 ? 64 * 1024 * 1024 + 1 : 96, char(scene % 256));
    frame->captured = state.clock - 5000000;
    frame->sequence = UINT64_C(0xf123456789abcdef);
    state.frame = frame;
    frame.reset();
    auto result = rust ? run<current::Backend>(command) : run<original::Backend>(command);
    result["trace"] = state.trace;
    return result;
}
} // namespace okww::backend_dispatch_test
