#pragma once
#include "controller.hpp"
#include "platform_workflow.hpp"
#include <ws2tcpip.h>
namespace okww::controller_lifecycle_test {
static constexpr uint32_t CONNECT = 0xAF3C2828, MOVE = 0xAEDE7345;
struct State {
    int scene;
    QJsonArray trace;
    explicit State(int s) : scene(s) {}
    void step(const QString &name, int id = -1) {
        trace.append(QJsonObject{{"call", name}, {"id", id}});
        if ((scene + id) % 19 == 0)
            throw std::runtime_error((name + QString("-%1").arg(id)).toStdString());
        if ((scene + id) % 31 == 0)
            throw 27;
    }
};
inline thread_local State *active = nullptr;
struct Target {
    QString tag;
    explicit Target(QJsonObject identity) : tag(identity["tag"].toString()) {
        active->trace.append(QJsonObject{{"target", tag}});
        if ((active->scene + (tag == "capture" ? 1 : 0)) % 23 == 0)
            throw std::runtime_error("target fixture");
    }
    ~Target() { active->trace.append(QJsonObject{{"target-end", tag}}); }
};
inline QJsonObject captureTarget(const Config &) {
    active->trace.append("capture-identity");
    return {{"kind", "monitor"}, {"id", "DISPLAY1"}, {"tag", "capture"}};
}
inline SOCKET socket(int family, int kind, int protocol) {
    active->trace.append(QJsonArray{"socket", family, kind, protocol});
    return active->scene % 11 ? SOCKET(UINT64_C(0x123456789abcde10)) : INVALID_SOCKET;
}
inline int InetPtonW(int family, LPCWSTR text, void *out) {
    active->trace.append(QJsonArray{"address", family, QString::fromWCharArray(text)});
    *static_cast<uint32_t *>(out) = 0x12345678;
    return 0;
}
inline int connect(SOCKET value, const sockaddr *address, int length) {
    const auto *ip = reinterpret_cast<const sockaddr_in *>(address);
    active->trace.append(QJsonObject{{"connect", QString::number(value)},
                                     {"family", ip->sin_family},
                                     {"port", ntohs(ip->sin_port)},
                                     {"address", qint64(ip->sin_addr.s_addr)},
                                     {"length", length}});
    return active->scene % 13 ? 0 : -1;
}
inline int closesocket(SOCKET value) {
    active->trace.append(QJsonObject{{"close-socket", QString::number(value)}});
    return -1;
}
struct ControllerState {
    Config cfg_ = Config::defaults();
    SOCKET socket_ = INVALID_SOCKET;
    std::recursive_mutex mutex_;
    uint32_t index_ = 99, uuid_ = 77;
    bool connected_ = false;
    int buttons_ = 0, modifiers_ = 0;
    std::set<int> keys_;
    QString capability_ = "old";
    std::deque<double> latencyMs_{.2, .4};
    std::unique_ptr<Target> captureTarget_, target_;
    bool local() const { return cfg_.s("kmbox.protocol") == "sendinput"; }
    void exchange(uint32_t command) { active->step("exchange", command == CONNECT ? 1 : 2); }
    void key(int code, bool down) {
        active->step(down ? "key-down" : "key-up", code);
        if (code >= 0xe0 && code <= 0xe7)
            modifiers_ &= ~(1 << (code - 0xe0));
        else
            keys_.erase(code);
    }
    void button(const QString &name, bool down) {
        const int index = QStringList{"left", "right", "middle", "x1", "x2"}.indexOf(name);
        active->step(down ? "button-down" : "button-up", index);
        buttons_ &= ~(1 << index);
    }
    void move(int x, int y) {
        active->trace.append(QJsonArray{"move", x, y});
        active->step("probe-move");
    }
    void mouse(uint32_t command) {
        active->trace.append(QJsonObject{{"mouse", qint64(command)},
                                         {"buttons", buttons_},
                                         {"modifiers", modifiers_},
                                         {"keys", int(keys_.size())}});
        active->step("mouse-report");
    }
    void keyboardReport() {
        active->trace.append(
            QJsonObject{{"keyboard", int(keys_.size())}, {"buttons", buttons_}, {"modifiers", modifiers_}});
        active->step("keyboard-report");
    }
};
#define CONTROLLER_LIFECYCLE_FIXTURE_CLASS                                                                   \
    class Controller : public ControllerState {                                                              \
      public:                                                                                                \
        void connect();                                                                                      \
        void probe();                                                                                        \
        void release();                                                                                      \
        void close() noexcept;                                                                               \
    };
namespace original {
CONTROLLER_LIFECYCLE_FIXTURE_CLASS
void Controller::connect() {
    std::lock_guard lock(mutex_);
    if (connected_)
        return;
    close();
    cfg_.validateBox();
    try {
        index_ = 0;
        latencyMs_.clear();
        if (local()) {
            if (!cfg_.b("kmbox.dry_run")) {
                if (cfg_.b("kmbox.target_enabled"))
                    target_ = std::make_unique<Target>(cfg_.get("kmbox.target").toObject());
                if (cfg_.s("capture.source") == "desktop") {
                    captureTarget_ = std::make_unique<Target>(controller_lifecycle_test::captureTarget(cfg_));
                }
            }
            capability_ = "SendInput · 本机键盘与鼠标";
            connected_ = true;
            return;
        }
        uuid_ = cfg_.s("kmbox.uuid").toUInt(nullptr, 16);
        if (!cfg_.b("kmbox.dry_run")) {
            socket_ = controller_lifecycle_test::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            sockaddr_in dest{};
            dest.sin_family = AF_INET;
            dest.sin_port = htons(u_short(cfg_.n("kmbox.port")));
            controller_lifecycle_test::InetPtonW(
                AF_INET, reinterpret_cast<LPCWSTR>(cfg_.s("kmbox.host").utf16()), &dest.sin_addr);
            if (socket_ == INVALID_SOCKET ||
                controller_lifecycle_test::connect(socket_, reinterpret_cast<sockaddr *>(&dest),
                                                   sizeof(dest)) != 0)
                fail("KMBox 网络连接失败");
        }
        exchange(CONNECT);
        capability_ = "键盘与鼠标可用";
        connected_ = true;
        release();
    } catch (...) {
        close();
        throw;
    }
}
void Controller::probe() {
    std::lock_guard lock(mutex_);
    if (!connected_)
        fail("盒子未连接");
    if (!local())
        move(0, 0);
}
void Controller::release() {
    std::lock_guard lock(mutex_);
    if (local()) {
        // Release only controls this connection pressed, never the user's keys.
        const auto keys = keys_;
        const auto modifiers = modifiers_, buttons = buttons_;
        std::exception_ptr error;
        const auto attempt = [&](auto action) {
            try {
                action();
            } catch (...) {
                if (!error)
                    error = std::current_exception();
            }
        };
        for (auto code : keys)
            attempt([&] { key(code, false); });
        for (int i = 0; i < 8; ++i)
            if (modifiers & (1 << i))
                attempt([&] { key(0xe0 + i, false); });
        for (int i = 0; i < 5; ++i)
            if (buttons & (1 << i))
                attempt([&] { button(QStringList{"left", "right", "middle", "x1", "x2"}[i], false); });
        if (error)
            std::rethrow_exception(error);
        return;
    }
    buttons_ = modifiers_ = 0;
    keys_.clear();
    std::exception_ptr error;
    try {
        mouse(MOVE);
    } catch (...) {
        error = std::current_exception();
    }
    try {
        keyboardReport();
    } catch (...) {
        if (!error)
            error = std::current_exception();
    }
    if (error)
        std::rethrow_exception(error);
}
void Controller::close() noexcept {
    std::lock_guard lock(mutex_);
    if (connected_ || socket_ != INVALID_SOCKET) {
        try {
            release();
        } catch (...) {
        }
    }
    if (socket_ != INVALID_SOCKET)
        controller_lifecycle_test::closesocket(socket_);
    socket_ = INVALID_SOCKET;
    connected_ = false;
    captureTarget_.reset();
    target_.reset();
    buttons_ = modifiers_ = 0;
    keys_.clear();
}
} // namespace original
namespace current {
CONTROLLER_LIFECYCLE_FIXTURE_CLASS
struct ControllerLifecycleSdk {
    Controller &c;
    sockaddr_in destination{};
    std::set<int> keys;
    std::set<int>::const_iterator key;
    std::exception_ptr releaseError;
    void operator()(rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &error) {
        switch (op) {
        case rust::WorkflowOp::ControllerLifecycleConnected:
            out[0] = c.connected_;
            break;
        case rust::WorkflowOp::ControllerLifecycleClose:
            c.close();
            break;
        case rust::WorkflowOp::ControllerConnectValidate:
            c.cfg_.validateBox();
            break;
        case rust::WorkflowOp::ControllerConnectResetSequence:
            c.index_ = 0;
            break;
        case rust::WorkflowOp::ControllerConnectResetLatency:
            c.latencyMs_.clear();
            break;
        case rust::WorkflowOp::ControllerLifecycleLocal:
            out[0] = c.local();
            break;
        case rust::WorkflowOp::ControllerLifecycleDryRun:
            out[0] = c.cfg_.b("kmbox.dry_run");
            break;
        case rust::WorkflowOp::ControllerConnectTargetEnabled:
            out[0] = c.cfg_.b("kmbox.target_enabled");
            break;
        case rust::WorkflowOp::ControllerConnectCreateTarget:
            c.target_ = std::make_unique<Target>(c.cfg_.get("kmbox.target").toObject());
            break;
        case rust::WorkflowOp::ControllerConnectDesktopSource:
            out[0] = c.cfg_.s("capture.source") == "desktop";
            break;
        case rust::WorkflowOp::ControllerConnectCreateCaptureTarget:
            c.captureTarget_ = std::make_unique<Target>(controller_lifecycle_test::captureTarget(c.cfg_));
            break;
        case rust::WorkflowOp::ControllerConnectLocalCapability:
            c.capability_ = "SendInput · 本机键盘与鼠标";
            break;
        case rust::WorkflowOp::ControllerConnectMarkConnected:
            c.connected_ = true;
            break;
        case rust::WorkflowOp::ControllerConnectUuid:
            c.uuid_ = c.cfg_.s("kmbox.uuid").toUInt(nullptr, 16);
            break;
        case rust::WorkflowOp::ControllerConnectSocket:
            c.socket_ = controller_lifecycle_test::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            break;
        case rust::WorkflowOp::ControllerConnectAddress:
            destination = {};
            destination.sin_family = AF_INET;
            destination.sin_port = htons(u_short(c.cfg_.n("kmbox.port")));
            break;
        case rust::WorkflowOp::ControllerConnectParseAddress:
            controller_lifecycle_test::InetPtonW(
                AF_INET, reinterpret_cast<LPCWSTR>(c.cfg_.s("kmbox.host").utf16()), &destination.sin_addr);
            break;
        case rust::WorkflowOp::ControllerLifecycleSocketValid:
            out[0] = c.socket_ != INVALID_SOCKET;
            break;
        case rust::WorkflowOp::ControllerConnectPeer:
            out[0] = controller_lifecycle_test::connect(c.socket_, reinterpret_cast<sockaddr *>(&destination),
                                                        sizeof(destination)) == 0;
            break;
        case rust::WorkflowOp::ControllerConnectExchange:
            c.exchange(CONNECT);
            break;
        case rust::WorkflowOp::ControllerConnectNetCapability:
            c.capability_ = "键盘与鼠标可用";
            break;
        case rust::WorkflowOp::ControllerLifecycleRelease:
            c.release();
            break;
        case rust::WorkflowOp::ControllerReleaseSnapshotKeys:
            keys = c.keys_;
            break;
        case rust::WorkflowOp::ControllerReleaseSnapshotStates:
            out[0] = c.modifiers_;
            out[1] = c.buttons_;
            break;
        case rust::WorkflowOp::ControllerReleaseBeginKeys:
            key = keys.cbegin();
            break;
        case rust::WorkflowOp::ControllerReleaseHasKey:
            out[0] = key != keys.cend();
            break;
        case rust::WorkflowOp::ControllerReleaseKey:
            c.key(*key, false);
            break;
        case rust::WorkflowOp::ControllerReleaseNextKey:
            ++key;
            break;
        case rust::WorkflowOp::ControllerReleaseModifier:
            c.key(0xe0 + int(a[0]), false);
            break;
        case rust::WorkflowOp::ControllerReleaseButton:
            c.button(QStringList{"left", "right", "middle", "x1", "x2"}[int(a[0])], false);
            break;
        case rust::WorkflowOp::ControllerReleaseHasError:
            out[0] = bool(releaseError);
            break;
        case rust::WorkflowOp::ControllerReleaseSaveError:
            releaseError = error;
            break;
        case rust::WorkflowOp::ControllerReleaseRethrow:
            std::rethrow_exception(releaseError);
            break;
        case rust::WorkflowOp::ControllerLifecycleClearHeldState:
            c.buttons_ = c.modifiers_ = 0;
            break;
        case rust::WorkflowOp::ControllerLifecycleClearKeys:
            c.keys_.clear();
            break;
        case rust::WorkflowOp::ControllerReleaseMouse:
            c.mouse(MOVE);
            break;
        case rust::WorkflowOp::ControllerReleaseKeyboard:
            c.keyboardReport();
            break;
        case rust::WorkflowOp::ControllerCloseSocket:
            controller_lifecycle_test::closesocket(c.socket_);
            break;
        case rust::WorkflowOp::ControllerCloseInvalidateSocket:
            c.socket_ = INVALID_SOCKET;
            break;
        case rust::WorkflowOp::ControllerCloseDisconnected:
            c.connected_ = false;
            break;
        case rust::WorkflowOp::ControllerCloseCaptureTarget:
            c.captureTarget_.reset();
            break;
        case rust::WorkflowOp::ControllerCloseTarget:
            c.target_.reset();
            break;
        case rust::WorkflowOp::ControllerProbeMove:
            c.move(0, 0);
            break;
        case rust::WorkflowOp::ControllerLifecycleFailure:
            if (a[0] == 0)
                fail("KMBox 网络连接失败");
            else
                fail("盒子未连接");
            break;
        default:
            fail("Rust 键鼠连接生命周期宿主无效");
        }
    }
};
void Controller::connect() {
    std::lock_guard lock(mutex_);
    ControllerLifecycleSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::ControllerConnect, {}, sdk);
}
void Controller::probe() {
    std::lock_guard lock(mutex_);
    ControllerLifecycleSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::ControllerProbe, {}, sdk);
}
void Controller::release() {
    std::lock_guard lock(mutex_);
    ControllerLifecycleSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::ControllerRelease, {}, sdk);
}
void Controller::close() noexcept {
    std::lock_guard lock(mutex_);
    ControllerLifecycleSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::ControllerClose, {}, sdk);
}
} // namespace current
#undef CONTROLLER_LIFECYCLE_FIXTURE_CLASS
template <class C> QJsonObject run(int method) {
    C c;
    const int scene = active->scene;
    c.cfg_.set("kmbox.protocol", scene % 2 ? "kmbox_net" : "sendinput");
    c.cfg_.set("kmbox.dry_run", scene % 3 == 0);
    c.cfg_.set("kmbox.target_enabled", scene % 5 == 0);
    c.cfg_.set("kmbox.target", QJsonObject{{"kind", "window"},
                                           {"tag", "input"},
                                           {"hwnd", "123"},
                                           {"pid", 1},
                                           {"exe", "game.exe"},
                                           {"title", "Game"},
                                           {"class", "GameWindow"}});
    c.cfg_.set("capture.source", scene % 7 == 0 ? "desktop" : "capture_card");
    c.cfg_.set("kmbox.uuid", scene % 17 ? "12345678" : "invalid");
    c.cfg_.set("kmbox.host", "127.0.0.1");
    c.connected_ = scene % 4 == 0;
    c.socket_ = scene % 6 == 0 ? SOCKET(UINT64_C(0x123456789abcde20)) : INVALID_SOCKET;
    c.buttons_ = scene % 32;
    c.modifiers_ = scene % 256;
    for (int i = 0; i < scene % 12; ++i)
        c.keys_.insert(4 + i);
    QJsonObject result;
    try {
        switch (method) {
        case 0:
            c.connect();
            break;
        case 1:
            c.release();
            break;
        case 2:
            c.close();
            break;
        case 3:
            c.probe();
            break;
        }
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    } catch (...) {
        result["error"] = "unknown";
    }
    QJsonArray keys;
    for (int k : c.keys_)
        keys.append(k);
    result["keys"] = keys;
    result["buttons"] = c.buttons_;
    result["modifiers"] = c.modifiers_;
    result["connected"] = c.connected_;
    result["socket"] = QString::number(c.socket_);
    result["index"] = qint64(c.index_);
    result["uuid"] = qint64(c.uuid_);
    result["capability"] = c.capability_;
    result["latency_count"] = int(c.latencyMs_.size());
    result["target"] = bool(c.target_);
    result["capture_target"] = bool(c.captureTarget_);
    return result;
}
inline QJsonObject outcome(int scene, int method, bool rust) {
    State state(scene);
    active = &state;
    auto result = rust ? run<current::Controller>(method) : run<original::Controller>(method);
    result["trace"] = state.trace;
    return result;
}
} // namespace okww::controller_lifecycle_test
