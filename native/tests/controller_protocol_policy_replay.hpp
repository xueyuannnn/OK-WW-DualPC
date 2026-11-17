#pragma once
#include "controller.hpp"
#include "controller_math_abi.hpp"
#include "platform_workflow.hpp"
#include "rust_core.hpp"
#include <bit>
namespace okww::controller_protocol_test {
static constexpr uint32_t MOVE = 0xAEDE7345, KEYBOARD = 0x123C2C2F;
struct State {
    int scene, receives = 0;
    uint32_t randomState = 0;
    i64 clock = INT64_C(0x123456789abcde00);
    QJsonArray trace;
    QByteArray sent;
    explicit State(int s) : scene(s), randomState(uint32_t(s)) {}
};
inline thread_local State *active = nullptr;
inline i64 nowNs() {
    active->clock += 1000000;
    active->trace.append(QJsonObject{{"clock", QString::number(active->clock)}});
    return active->clock;
}
inline uint32_t random() {
    active->trace.append("random");
    return active->randomState = active->randomState * 1664525u + 1013904223u;
}
inline int send(SOCKET socket, const char *data, int length, int flags) {
    active->sent = QByteArray(data, length);
    active->trace.append(QJsonObject{{"send", QString::fromLatin1(active->sent.toHex())},
                                     {"socket", QString::number(socket)},
                                     {"flags", flags}});
    return active->scene % 7 == 1 ? -1 : length;
}
inline int select(int, fd_set *reads, void *, void *, timeval *wait) {
    active->trace.append(QJsonObject{{"select", int(reads->fd_count)},
                                     {"seconds", qint64(wait->tv_sec)},
                                     {"microseconds", qint64(wait->tv_usec)}});
    return active->scene % 7 == 2 ? 0 : active->scene % 7 == 3 ? -1 : 1;
}
inline int recv(SOCKET, char *data, int length, int flags) {
    active->trace.append(QJsonObject{{"recv", length}, {"flags", flags}});
    ++active->receives;
    if (active->scene % 7 == 4)
        return -1;
    QByteArray reply = active->sent.left(16);
    if (active->scene % 7 == 5)
        reply.resize(8);
    if (active->scene % 7 == 6 && active->receives % 2)
        reply[8] = char(reply[8] ^ 1);
    memcpy(data, reply.constData(), size_t(reply.size()));
    return int(reply.size());
}
#define CONTROLLER_PROTOCOL_FIXTURE_CLASS                                                                    \
    class Controller {                                                                                       \
      public:                                                                                                \
        Config cfg_ = Config::defaults();                                                                    \
        SOCKET socket_ = SOCKET(123);                                                                        \
        std::recursive_mutex mutex_;                                                                         \
        uint32_t index_ = 0xfffffffe, uuid_ = 0x1234abcd;                                                    \
        bool connected_ = true;                                                                              \
        int buttons_ = 0, modifiers_ = 0;                                                                    \
        std::set<int> keys_;                                                                                 \
        QString capability_ = "键盘与鼠标可用";                                                              \
        QList<QByteArray> history_;                                                                          \
        std::deque<double> latencyMs_;                                                                       \
        double latencyP50_ = .25, latencyP95_ = .5;                                                          \
        QJsonObject lastLocalEnvironment_;                                                                   \
        bool local() const { return cfg_.s("kmbox.protocol") == "sendinput"; }                               \
        void noteLatency(i64);                                                                               \
        void recordPacket(const QByteArray &);                                                               \
        void exchange(uint32_t, const QByteArray &payload = {});                                             \
        void mouse(uint32_t, int = 0, int = 0, int = 0);                                                     \
        void keyboardReport();                                                                               \
        QJsonObject state();                                                                                 \
    };
namespace original {
CONTROLLER_PROTOCOL_FIXTURE_CLASS
static void u32(QByteArray &p, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        p.append(char(v >> (i * 8)));
}
static uint32_t get32(const QByteArray &p, int n) { return qFromLittleEndian<uint32_t>(p.constData() + n); }
void Controller::noteLatency(i64 began) {
    latencyMs_.push_back((nowNs() - began) / 1e6);
    if (latencyMs_.size() > 128)
        latencyMs_.pop_front();
    std::vector<double> sorted(latencyMs_.begin(), latencyMs_.end());
    std::sort(sorted.begin(), sorted.end());
    latencyP50_ = sorted[size_t(std::ceil(.5 * sorted.size())) - 1];
    latencyP95_ = sorted[size_t(std::ceil(.95 * sorted.size())) - 1];
}
void Controller::recordPacket(const QByteArray &p) {
    history_.append(p);
    while (history_.size() > 256)
        history_.removeFirst();
}
void Controller::exchange(uint32_t command, const QByteArray &payload) {
    const auto began = nowNs();
    const auto seq = index_++;
    QByteArray p;
    u32(p, uuid_);
    u32(p, controller_protocol_test::random());
    u32(p, seq);
    u32(p, command);
    p += payload;
    if (cfg_.b("kmbox.dry_run")) {
        recordPacket(p);
        return;
    }
    if (socket_ == INVALID_SOCKET ||
        controller_protocol_test::send(socket_, p.constData(), int(p.size()), 0) != p.size()) {
        connected_ = false;
        fail("KMBox 发送失败");
    }
    const i64 deadline = nowNs() + i64(cfg_.d("kmbox.timeout") * 1e9);
    while (nowNs() < deadline) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(socket_, &fds);
        auto remaining = std::max<i64>(1, deadline - nowNs());
        timeval wait{long(remaining / 1000000000), long((remaining % 1000000000) / 1000)};
        if (controller_protocol_test::select(0, &fds, nullptr, nullptr, &wait) <= 0)
            break;
        char buffer[2048];
        int count = controller_protocol_test::recv(socket_, buffer, sizeof(buffer), 0);
        if (count < 16)
            continue;
        QByteArray reply(buffer, count);
        if (get32(reply, 8) == seq && get32(reply, 12) == command) {
            noteLatency(began);
            return;
        }
    }
    connected_ = false;
    fail("KMBox 未返回匹配应答；没有重复发送移动指令");
}
void Controller::mouse(uint32_t command, int x, int y, int wheel) {
    QByteArray payload;
    for (int value : {buttons_, x, y, wheel})
        u32(payload, uint32_t(value));
    payload.append(40, '\0');
    exchange(command, payload);
}
void Controller::keyboardReport() {
    QByteArray payload;
    payload.append(char(modifiers_)).append(char(0));
    for (auto code : keys_)
        payload.append(char(code));
    payload.append(12 - payload.size(), '\0');
    exchange(KEYBOARD, payload);
}
QJsonObject Controller::state() {
    std::lock_guard lock(mutex_);
    QJsonArray keys;
    for (auto k : keys_)
        keys.append(k);
    return {{"connected", connected_},
            {"keyboard", true},
            {"absolute", local()},
            {"protocol", cfg_.s("kmbox.protocol")},
            {"capability", capability_},
            {"buttons", buttons_},
            {"modifiers", modifiers_},
            {"local_environment", lastLocalEnvironment_},
            {"keys", keys},
            {"dry_run", cfg_.b("kmbox.dry_run")},
            {"transport_samples", int(latencyMs_.size())},
            {"transport_p50_ms", latencyMs_.empty() ? QJsonValue() : QJsonValue(latencyP50_)},
            {"transport_p95_ms", latencyMs_.empty() ? QJsonValue() : QJsonValue(latencyP95_)},
            {"transport_scope", local()
                                    ? "Windows SendInput call; excludes application consumption"
                                    : "host send to matching protocol reply; excludes game/USB consumption"}};
}
} // namespace original
namespace current {
CONTROLLER_PROTOCOL_FIXTURE_CLASS
static void controllerProtocolFailure(int kind) {
    switch (kind) {
    case 0:
        fail("Rust 延迟统计接口异常");
    case 1:
        fail("KMBox 发送失败");
    case 2:
        fail("KMBox 未返回匹配应答；没有重复发送移动指令");
    case 3:
        fail("键盘报告超出按键上限");
    case 4:
        fail("Rust 键盘报告接口异常");
    default:
        fail("Rust 键鼠协议错误宿主无效");
    }
}
struct ControllerProtocolSdk {
    Controller &c;
    const QByteArray *source = nullptr;
    i64 began = 0;
    uint32_t command = 0, seq = 0;
    int x = 0, y = 0, wheel = 0;
    QByteArray packet, payload;
    std::array<double, 128> sorted{};
    double points[2]{};
    std::array<int32_t, 10> keys{};
    std::set<int>::const_iterator key;
    QJsonArray stateKeys;
    QJsonObject value;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::ControllerLatencyAppend:
            c.latencyMs_.push_back(rust::okww_rs_controller_elapsed_ms(nowNs(), began));
            break;
        case rust::WorkflowOp::ControllerLatencyCount:
            out[0] = double(c.latencyMs_.size());
            break;
        case rust::WorkflowOp::ControllerLatencyPop:
            c.latencyMs_.pop_front();
            break;
        case rust::WorkflowOp::ControllerLatencyCopy:
            std::copy(c.latencyMs_.begin(), c.latencyMs_.end(), sorted.begin());
            break;
        case rust::WorkflowOp::ControllerLatencyPercentiles:
            out[0] = rust::okww_rs_latency_percentiles(sorted.data(), c.latencyMs_.size(), points);
            break;
        case rust::WorkflowOp::ControllerLatencyStore:
            c.latencyP50_ = points[0];
            c.latencyP95_ = points[1];
            break;
        case rust::WorkflowOp::ControllerHistoryAppend:
            c.history_.append(*source);
            break;
        case rust::WorkflowOp::ControllerHistoryCount:
            out[0] = double(c.history_.size());
            break;
        case rust::WorkflowOp::ControllerHistoryPop:
            c.history_.removeFirst();
            break;
        case rust::WorkflowOp::ControllerExchangeClock:
            began = nowNs();
            break;
        case rust::WorkflowOp::ControllerExchangeSequence:
            seq = c.index_++;
            break;
        case rust::WorkflowOp::ControllerExchangeAllocate:
            packet = QByteArray(16, '\0');
            break;
        case rust::WorkflowOp::ControllerExchangeHeader:
            rust::okww_rs_packet_header(c.uuid_, controller_protocol_test::random(), seq, command,
                                        reinterpret_cast<uint8_t *>(packet.data()));
            break;
        case rust::WorkflowOp::ControllerExchangePayload:
            packet += *source;
            break;
        case rust::WorkflowOp::ControllerProtocolDryRun:
            out[0] = c.cfg_.b("kmbox.dry_run");
            break;
        case rust::WorkflowOp::ControllerExchangeRecord:
            c.recordPacket(packet);
            break;
        case rust::WorkflowOp::ControllerExchangeTransport: {
            struct SocketHost {
                SOCKET socket;
            } context{c.socket_};
            const rust::TransportHost host{
                &context, [](void *) -> int64_t { return nowNs(); },
                [](void *value, const uint8_t *bytes, size_t length) -> int32_t {
                    const auto socket = static_cast<SocketHost *>(value)->socket;
                    return socket == INVALID_SOCKET
                               ? -1
                               : controller_protocol_test::send(socket, reinterpret_cast<const char *>(bytes),
                                                                int(length), 0);
                },
                [](void *value, int64_t remaining) -> int32_t {
                    const auto socket = static_cast<SocketHost *>(value)->socket;
                    fd_set fds;
                    FD_ZERO(&fds);
                    FD_SET(socket, &fds);
                    timeval wait{long(remaining / 1000000000), long((remaining % 1000000000) / 1000)};
                    return controller_protocol_test::select(0, &fds, nullptr, nullptr, &wait);
                },
                [](void *value, uint8_t *bytes, size_t length) -> int32_t {
                    return controller_protocol_test::recv(static_cast<SocketHost *>(value)->socket,
                                                          reinterpret_cast<char *>(bytes), int(length), 0);
                }};
            out[0] = rust::okww_rs_transport_exchange(
                &host, reinterpret_cast<const uint8_t *>(packet.constData()), size_t(packet.size()),
                i64(c.cfg_.d("kmbox.timeout") * 1e9), seq, command);
            break;
        }
        case rust::WorkflowOp::ControllerExchangeLatency:
            c.noteLatency(began);
            break;
        case rust::WorkflowOp::ControllerExchangeDisconnected:
            c.connected_ = false;
            break;
        case rust::WorkflowOp::ControllerMouseAllocate:
            payload = QByteArray(56, '\0');
            break;
        case rust::WorkflowOp::ControllerMouseEncode:
            rust::okww_rs_mouse_report(c.buttons_, x, y, wheel, reinterpret_cast<uint8_t *>(payload.data()));
            break;
        case rust::WorkflowOp::ControllerMouseExchange:
            c.exchange(command, payload);
            break;
        case rust::WorkflowOp::ControllerProtocolBeginKeys:
            key = c.keys_.cbegin();
            break;
        case rust::WorkflowOp::ControllerProtocolHasKey:
            out[0] = key != c.keys_.cend();
            break;
        case rust::WorkflowOp::ControllerProtocolNextKey:
            ++key;
            break;
        case rust::WorkflowOp::ControllerKeyboardCopyKey:
            keys[size_t(a[0])] = *key;
            break;
        case rust::WorkflowOp::ControllerKeyboardAllocate:
            payload = QByteArray(12, '\0');
            break;
        case rust::WorkflowOp::ControllerKeyboardEncode:
            out[0] = rust::okww_rs_keyboard_report(c.modifiers_, keys.data(), size_t(a[0]),
                                                   reinterpret_cast<uint8_t *>(payload.data()));
            break;
        case rust::WorkflowOp::ControllerKeyboardExchange:
            c.exchange(KEYBOARD, payload);
            break;
        case rust::WorkflowOp::ControllerStateAppendKey:
            stateKeys.append(*key);
            break;
        case rust::WorkflowOp::ControllerStateResult:
            value = {{"connected", c.connected_},
                     {"keyboard", true},
                     {"absolute", c.local()},
                     {"protocol", c.cfg_.s("kmbox.protocol")},
                     {"capability", c.capability_},
                     {"buttons", c.buttons_},
                     {"modifiers", c.modifiers_},
                     {"local_environment", c.lastLocalEnvironment_},
                     {"keys", stateKeys},
                     {"dry_run", c.cfg_.b("kmbox.dry_run")},
                     {"transport_samples", int(c.latencyMs_.size())},
                     {"transport_p50_ms", c.latencyMs_.empty() ? QJsonValue() : QJsonValue(c.latencyP50_)},
                     {"transport_p95_ms", c.latencyMs_.empty() ? QJsonValue() : QJsonValue(c.latencyP95_)},
                     {"transport_scope",
                      c.local() ? "Windows SendInput call; excludes application consumption"
                                : "host send to matching protocol reply; excludes game/USB consumption"}};
            break;
        case rust::WorkflowOp::ControllerProtocolFailure:
            controllerProtocolFailure(int(a[0]));
            break;
        default:
            fail("Rust 键鼠协议宿主无效");
        }
    }
};
void Controller::noteLatency(i64 began) {
    ControllerProtocolSdk sdk{*this};
    sdk.began = began;
    rust::platformWorkflowRun(rust::WorkflowMethod::ControllerLatency, {}, sdk);
}
void Controller::recordPacket(const QByteArray &packet) {
    ControllerProtocolSdk sdk{*this};
    sdk.source = &packet;
    rust::platformWorkflowRun(rust::WorkflowMethod::ControllerRecord, {}, sdk);
}
void Controller::exchange(uint32_t command, const QByteArray &payload) {
    ControllerProtocolSdk sdk{*this};
    sdk.command = command;
    sdk.source = &payload;
    rust::platformWorkflowRun(rust::WorkflowMethod::ControllerExchange, {}, sdk);
}
void Controller::mouse(uint32_t command, int x, int y, int wheel) {
    ControllerProtocolSdk sdk{*this};
    sdk.command = command;
    sdk.x = x;
    sdk.y = y;
    sdk.wheel = wheel;
    rust::platformWorkflowRun(rust::WorkflowMethod::ControllerMouseReport, {}, sdk);
}
void Controller::keyboardReport() {
    ControllerProtocolSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::ControllerKeyboardReport, {}, sdk);
}
QJsonObject Controller::state() {
    std::lock_guard lock(mutex_);
    ControllerProtocolSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::ControllerState, {}, sdk);
    return sdk.value;
}
} // namespace current
#undef CONTROLLER_PROTOCOL_FIXTURE_CLASS
template <class C> QJsonObject run(int method) {
    C c;
    c.cfg_.set("kmbox.protocol", active->scene % 3 ? "kmbox_net" : "sendinput");
    c.cfg_.set("kmbox.dry_run", active->scene % 2 == 0);
    c.cfg_.set("kmbox.timeout", .01);
    c.socket_ = active->scene % 13 ? SOCKET(123) : INVALID_SOCKET;
    c.buttons_ = active->scene % 32;
    c.modifiers_ = active->scene % 256;
    c.lastLocalEnvironment_ = {{"title", "游戏窗口"}};
    const int keyCount = active->scene % 11;
    for (int i = 0; i < keyCount; ++i)
        c.keys_.insert(i == 0 && active->scene % 17 == 0 ? 300 : 4 + i);
    const int samples = active->scene % 129;
    for (int i = 0; i < samples; ++i)
        c.latencyMs_.push_back(double((i * 17 + active->scene) % 1000) / 1000.);
    for (int i = 0; i < 250 + active->scene % 8; ++i)
        c.history_.append(QByteArray::number(i));
    QJsonObject result;
    try {
        switch (method) {
        case 0:
            c.noteLatency(active->clock - 1234567);
            break;
        case 1:
            c.recordPacket(QByteArray("新报文"));
            break;
        case 2:
            c.exchange(MOVE, QByteArray::number(active->scene));
            c.exchange(KEYBOARD, QByteArray("second"));
            break;
        case 3:
            c.mouse(MOVE, active->scene * 13 - 30000, 10000 - active->scene * 19, active->scene % 255 - 127);
            break;
        case 4:
            c.keyboardReport();
            break;
        case 5:
            result["state"] = c.state();
            break;
        }
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    } catch (...) {
        result["error"] = "unknown";
    }
    QJsonArray samplesJson;
    for (double sample : c.latencyMs_)
        samplesJson.append(sample);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const auto &packet : c.history_)
        hash.addData(packet);
    result["history_count"] = c.history_.size();
    result["history_hash"] = QString::fromLatin1(hash.result().toHex());
    result["history_last"] = QString::fromLatin1(c.history_.last().toHex());
    result["latency"] = samplesJson;
    result["p50"] = c.latencyP50_;
    result["p95"] = c.latencyP95_;
    result["sequence"] = qint64(c.index_);
    result["connected"] = c.connected_;
    return result;
}
inline QJsonObject outcome(int scene, int method, bool rust) {
    State state(scene);
    active = &state;
    auto result = rust ? run<current::Controller>(method) : run<original::Controller>(method);
    result["trace"] = state.trace;
    return result;
}
} // namespace okww::controller_protocol_test
