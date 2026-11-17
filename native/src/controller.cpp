#include "controller.hpp"
#include "controller_math_abi.hpp"
#include "platform_workflow.hpp"
#include "rust_core.hpp"
#include <ws2tcpip.h>
namespace okww {
static constexpr uint32_t CONNECT = 0xAF3C2828, MOVE = 0xAEDE7345, LEFT = 0x9823AE8D, RIGHT = 0x238D8212,
                          MIDDLE = 0x97A3AE8D, WHEEL = 0xFFEEAD38, KEYBOARD = 0x123C2C2F;

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
            rust::okww_rs_packet_header(c.uuid_, QRandomGenerator::global()->generate(), seq, command,
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
                               : send(socket, reinterpret_cast<const char *>(bytes), int(length), 0);
                },
                [](void *value, int64_t remaining) -> int32_t {
                    const auto socket = static_cast<SocketHost *>(value)->socket;
                    fd_set fds;
                    FD_ZERO(&fds);
                    FD_SET(socket, &fds);
                    timeval wait{long(remaining / 1000000000), long((remaining % 1000000000) / 1000)};
                    return select(0, &fds, nullptr, nullptr, &wait);
                },
                [](void *value, uint8_t *bytes, size_t length) -> int32_t {
                    return recv(static_cast<SocketHost *>(value)->socket, reinterpret_cast<char *>(bytes),
                                int(length), 0);
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
            c.target_ = std::make_unique<DesktopTarget>(c.cfg_.get("kmbox.target").toObject());
            break;
        case rust::WorkflowOp::ControllerConnectDesktopSource:
            out[0] = c.cfg_.s("capture.source") == "desktop";
            break;
        case rust::WorkflowOp::ControllerConnectCreateCaptureTarget:
            c.captureTarget_ = std::make_unique<DesktopTarget>(desktopCaptureTarget(c.cfg_));
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
            c.socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            break;
        case rust::WorkflowOp::ControllerConnectAddress:
            destination = {};
            destination.sin_family = AF_INET;
            destination.sin_port = htons(u_short(c.cfg_.n("kmbox.port")));
            break;
        case rust::WorkflowOp::ControllerConnectParseAddress:
            InetPtonW(AF_INET, reinterpret_cast<LPCWSTR>(c.cfg_.s("kmbox.host").utf16()),
                      &destination.sin_addr);
            break;
        case rust::WorkflowOp::ControllerLifecycleSocketValid:
            out[0] = c.socket_ != INVALID_SOCKET;
            break;
        case rust::WorkflowOp::ControllerConnectPeer:
            out[0] =
                ::connect(c.socket_, reinterpret_cast<sockaddr *>(&destination), sizeof(destination)) == 0;
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
            closesocket(c.socket_);
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

static void controllerInputFailure(int kind) {
    switch (kind) {
    case 0:
        fail("移动超出 HID 范围");
    case 1:
        fail("KMBox Net 使用相对定位");
    case 2:
        fail("SendInput 坐标无效");
    case 3:
        fail("点击位置位于指定窗口外，请检查高级设置中的窗口与截图配置");
    case 4:
        fail("未知鼠标按键");
    case 5:
        fail("无效 HID 键码");
    case 6:
        fail("同时按下的键超过盒子容量");
    case 7:
        fail("滚轮步数过大");
    default:
        fail("Rust 键鼠输入错误宿主无效");
    }
}
struct ControllerInputSdk {
    Controller &c;
    int x = 0, y = 0, width = 0, height = 0, code = 0, steps = 0, index = -1;
    bool down = false;
    const QString *name = nullptr;
    QRect rect, desktop;
    QPoint point, normal;
    INPUT input{};
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::ControllerInputCoordinates:
            out[0] = x;
            out[1] = y;
            break;
        case rust::WorkflowOp::ControllerInputDimensions:
            out[0] = width;
            out[1] = height;
            break;
        case rust::WorkflowOp::ControllerInputLocal:
            out[0] = c.local();
            break;
        case rust::WorkflowOp::ControllerInputRelativeMouse:
            input.type = INPUT_MOUSE;
            input.mi.dx = x;
            input.mi.dy = y;
            input.mi.dwFlags = MOUSEEVENTF_MOVE;
            break;
        case rust::WorkflowOp::ControllerInputSend:
            c.localSend({input});
            break;
        case rust::WorkflowOp::ControllerInputNetMove:
            c.mouse(MOVE, x, y);
            break;
        case rust::WorkflowOp::ControllerInputHasCaptureTarget:
            out[0] = bool(c.captureTarget_);
            break;
        case rust::WorkflowOp::ControllerInputCaptureRect:
            rect = c.captureTarget_->rect();
            break;
        case rust::WorkflowOp::ControllerInputHasTarget:
            out[0] = bool(c.target_);
            break;
        case rust::WorkflowOp::ControllerInputTargetRect:
            rect = c.target_->rect();
            break;
        case rust::WorkflowOp::ControllerInputDryRun:
            out[0] = c.cfg_.b("kmbox.dry_run");
            break;
        case rust::WorkflowOp::ControllerInputImageRect:
            rect = QRect(0, 0, width, height);
            break;
        case rust::WorkflowOp::ControllerInputForegroundRect:
            rect = foregroundClientRect();
            break;
        case rust::WorkflowOp::ControllerInputMapPoint: {
            std::array<int32_t, 2> value{};
            if (!rust::okww_rs_controller_map_point(x, y, width, height, rect.x(), rect.y(), rect.width(),
                                                    rect.height(), value.data()))
                fail("Rust 鼠标坐标换算接口异常");
            point = QPoint(value[0], value[1]);
            break;
        }
        case rust::WorkflowOp::ControllerInputTargetContainsPoint:
            out[0] = c.target_->rect().contains(point);
            break;
        case rust::WorkflowOp::ControllerInputUseImageDesktop:
            desktop = rect;
            break;
        case rust::WorkflowOp::ControllerInputVirtualDesktop:
            desktop = virtualDesktopRect();
            break;
        case rust::WorkflowOp::ControllerInputNormalizePoint:
            normal = desktopAbsolute(point, desktop);
            break;
        case rust::WorkflowOp::ControllerInputAbsoluteMouse:
            input.type = INPUT_MOUSE;
            input.mi.dx = normal.x();
            input.mi.dy = normal.y();
            input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
            break;
        case rust::WorkflowOp::ControllerInputButtonIndex:
            index = QStringList{"left", "right", "middle", "x1", "x2"}.indexOf(*name);
            out[0] = index;
            break;
        case rust::WorkflowOp::ControllerInputDown:
            out[0] = down;
            break;
        case rust::WorkflowOp::ControllerInputButtonHeld:
            out[0] = (c.buttons_ & (1 << index)) != 0;
            break;
        case rust::WorkflowOp::ControllerInputButtonMouse: {
            static constexpr DWORD downs[]{MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_RIGHTDOWN,
                                           MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_XDOWN, MOUSEEVENTF_XDOWN};
            static constexpr DWORD ups[]{MOUSEEVENTF_LEFTUP, MOUSEEVENTF_RIGHTUP, MOUSEEVENTF_MIDDLEUP,
                                         MOUSEEVENTF_XUP, MOUSEEVENTF_XUP};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = down ? downs[index] : ups[index];
            break;
        }
        case rust::WorkflowOp::ControllerInputButtonData:
            input.mi.mouseData = index == 3 ? XBUTTON1 : XBUTTON2;
            break;
        case rust::WorkflowOp::ControllerInputSetButton:
            c.buttons_ |= 1 << index;
            break;
        case rust::WorkflowOp::ControllerInputClearButton:
            c.buttons_ &= ~(1 << index);
            break;
        case rust::WorkflowOp::ControllerInputSendReleasing:
            c.localSend({input}, !down);
            break;
        case rust::WorkflowOp::ControllerInputNetLeft:
            c.mouse(LEFT);
            break;
        case rust::WorkflowOp::ControllerInputNetMiddle:
            c.mouse(MIDDLE);
            break;
        case rust::WorkflowOp::ControllerInputNetRight:
            c.mouse(RIGHT);
            break;
        case rust::WorkflowOp::ControllerInputCode:
            out[0] = code;
            break;
        case rust::WorkflowOp::ControllerInputModifierHeld:
            out[0] = (c.modifiers_ & (1 << (code - 0xe0))) != 0;
            break;
        case rust::WorkflowOp::ControllerInputKeyHeld:
            out[0] = c.keys_.contains(code);
            break;
        case rust::WorkflowOp::ControllerInputHid:
            input = hidInput(code, down);
            break;
        case rust::WorkflowOp::ControllerInputSetModifier:
            c.modifiers_ |= 1 << (code - 0xe0);
            break;
        case rust::WorkflowOp::ControllerInputClearModifier:
            c.modifiers_ &= ~(1 << (code - 0xe0));
            break;
        case rust::WorkflowOp::ControllerInputInsertKey:
            c.keys_.insert(code);
            break;
        case rust::WorkflowOp::ControllerInputEraseKey:
            c.keys_.erase(code);
            break;
        case rust::WorkflowOp::ControllerInputKeyCount:
            out[0] = double(c.keys_.size());
            break;
        case rust::WorkflowOp::ControllerInputKeyboardReport:
            c.keyboardReport();
            break;
        case rust::WorkflowOp::ControllerInputSteps:
            out[0] = steps;
            break;
        case rust::WorkflowOp::ControllerInputWheelMouse:
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = MOUSEEVENTF_WHEEL;
            input.mi.mouseData = DWORD(int32_t(a[0]));
            break;
        case rust::WorkflowOp::ControllerInputNetWheel:
            c.mouse(WHEEL, 0, 0, int(a[0]));
            break;
        case rust::WorkflowOp::ControllerInputFailure:
            controllerInputFailure(int(a[0]));
            break;
        default:
            fail("Rust 键鼠输入宿主无效");
        }
    }
};
void Controller::move(int x, int y) {
    std::lock_guard lock(mutex_);
    ControllerInputSdk sdk{*this};
    sdk.x = x;
    sdk.y = y;
    rust::platformWorkflowRun(rust::WorkflowMethod::ControllerMove, {}, sdk);
}
void Controller::absolute(int x, int y, int width, int height) {
    std::lock_guard lock(mutex_);
    ControllerInputSdk sdk{*this};
    sdk.x = x;
    sdk.y = y;
    sdk.width = width;
    sdk.height = height;
    rust::platformWorkflowRun(rust::WorkflowMethod::ControllerAbsolute, {}, sdk);
}
void Controller::button(const QString &name, bool down) {
    std::lock_guard lock(mutex_);
    ControllerInputSdk sdk{*this};
    sdk.name = &name;
    sdk.down = down;
    rust::platformWorkflowRun(rust::WorkflowMethod::ControllerButton, {}, sdk);
}
void Controller::key(int code, bool down) {
    std::lock_guard lock(mutex_);
    ControllerInputSdk sdk{*this};
    sdk.code = code;
    sdk.down = down;
    rust::platformWorkflowRun(rust::WorkflowMethod::ControllerKey, {}, sdk);
}
void Controller::wheel(int steps) {
    std::lock_guard lock(mutex_);
    ControllerInputSdk sdk{*this};
    sdk.steps = steps;
    rust::platformWorkflowRun(rust::WorkflowMethod::ControllerWheel, {}, sdk);
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

bool Controller::connected() {
    std::lock_guard lock(mutex_);
    return connected_;
}

QJsonObject Controller::state() {
    std::lock_guard lock(mutex_);
    ControllerProtocolSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::ControllerState, {}, sdk);
    return sdk.value;
}

struct ControllerLocalSdk {
    Controller &c;
    std::vector<INPUT> &inputs;
    bool releasing;
    QJsonObject environment;
    QStringList names, labels;
    QString cls;
    i64 began = 0;
    UINT sent = 0;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::ControllerLocalConnected:
            out[0] = c.connected_;
            break;
        case rust::WorkflowOp::ControllerLocalReleasing:
            out[0] = releasing;
            break;
        case rust::WorkflowOp::ControllerLocalDryRun:
            out[0] = c.cfg_.b("kmbox.dry_run");
            break;
        case rust::WorkflowOp::ControllerLocalHasEnvironment:
            out[0] = bool(c.localEnvironment_);
            break;
        case rust::WorkflowOp::ControllerLocalReadProvider:
            c.lastLocalEnvironment_ = c.localEnvironment_();
            break;
        case rust::WorkflowOp::ControllerLocalReadSystem:
            c.lastLocalEnvironment_ = localInputEnvironment();
            break;
        case rust::WorkflowOp::ControllerLocalCopyEnvironment:
            environment = c.lastLocalEnvironment_;
            break;
        case rust::WorkflowOp::ControllerLocalEnvironmentModifiers:
            out[0] = environment["modifiers"].toInt();
            break;
        case rust::WorkflowOp::ControllerLocalHeldModifiers:
            out[0] = c.modifiers_;
            break;
        case rust::WorkflowOp::ControllerLocalModifierLabels:
            labels = QStringList{"左 Ctrl", "左 Shift", "左 Alt", "左 Win",
                                 "右 Ctrl", "右 Shift", "右 Alt", "右 Win"};
            break;
        case rust::WorkflowOp::ControllerLocalAppendModifier:
            names.append(labels[int(a[0])]);
            break;
        case rust::WorkflowOp::ControllerLocalForeignModifierError:
            throw LocalInputBlocked("等待额外按住的 " + names.join(" / ") + " 松开");
        case rust::WorkflowOp::ControllerLocalExplorer:
            out[0] = environment["executable"].toString().compare("explorer.exe", Qt::CaseInsensitive) == 0;
            break;
        case rust::WorkflowOp::ControllerLocalReadClass:
            cls = environment["class"].toString();
            break;
        case rust::WorkflowOp::ControllerLocalDialogClass:
            out[0] = cls == "#32770";
            break;
        case rust::WorkflowOp::ControllerLocalRunTitle:
            out[0] = QStringList{"运行", "執行", "Run"}.contains(environment["title"].toString().trimmed(),
                                                                 Qt::CaseInsensitive);
            break;
        case rust::WorkflowOp::ControllerLocalInputCount:
            out[0] = double(inputs.size());
            break;
        case rust::WorkflowOp::ControllerLocalInputKeyboard:
            out[0] = inputs[size_t(a[0])].type == INPUT_KEYBOARD;
            break;
        case rust::WorkflowOp::ControllerLocalInputEscape:
            out[0] = inputs[size_t(a[0])].ki.wScan == 1;
            break;
        case rust::WorkflowOp::ControllerLocalInputScanCode:
            out[0] = (inputs[size_t(a[0])].ki.dwFlags & KEYEVENTF_SCANCODE) != 0;
            break;
        case rust::WorkflowOp::ControllerLocalOwnWindow:
            out[0] = environment["own_window"].toBool();
            break;
        case rust::WorkflowOp::ControllerLocalExcludedClass:
            out[0] = QStringList{"#32770", "Shell_TrayWnd", "Shell_SecondaryTrayWnd", "Progman", "WorkerW"}
                         .contains(cls);
            break;
        case rust::WorkflowOp::ControllerLocalFocusError:
            throw LocalInputBlocked("等待输入焦点返回游戏或远程窗口：" + environment["title"].toString(),
                                    a[0] != 0 ? LocalInputBlocked::Recovery::DismissRunDialog
                                              : LocalInputBlocked::Recovery::Wait);
        case rust::WorkflowOp::ControllerLocalDropEnvironmentScope:
            cls = {};
            labels = {};
            names = {};
            environment = {};
            break;
        case rust::WorkflowOp::ControllerLocalHasTarget:
            out[0] = bool(c.target_);
            break;
        case rust::WorkflowOp::ControllerLocalTargetRect:
            c.target_->rect();
            break;
        case rust::WorkflowOp::ControllerLocalTargetForeground:
            out[0] = c.target_->foreground();
            break;
        case rust::WorkflowOp::ControllerLocalTargetError:
            throw LocalInputBlocked("等待指定的游戏或远程窗口返回前台");
        case rust::WorkflowOp::ControllerLocalInputMouse:
            out[0] = inputs[size_t(a[0])].type == INPUT_MOUSE;
            break;
        case rust::WorkflowOp::ControllerLocalTagMouse:
            inputs[size_t(a[0])].mi.dwExtraInfo = localInputTag;
            break;
        case rust::WorkflowOp::ControllerLocalTagKeyboard:
            inputs[size_t(a[0])].ki.dwExtraInfo = localInputTag;
            break;
        case rust::WorkflowOp::ControllerLocalRecord:
            c.recordPacket(QByteArray(reinterpret_cast<const char *>(inputs.data()),
                                      qsizetype(inputs.size() * sizeof(INPUT))));
            break;
        case rust::WorkflowOp::ControllerLocalClock:
            began = nowNs();
            break;
        case rust::WorkflowOp::ControllerLocalSendInputs:
            sent = SendInput(UINT(inputs.size()), inputs.data(), sizeof(INPUT));
            break;
        case rust::WorkflowOp::ControllerLocalSendComplete:
            out[0] = sent == inputs.size();
            break;
        case rust::WorkflowOp::ControllerLocalLatency:
            c.noteLatency(began);
            break;
        case rust::WorkflowOp::ControllerLocalFailure:
            if (a[0] == 0)
                fail("SendInput 未连接");
            else
                fail("SendInput 未完整发送输入，请检查目标程序权限是否高于本软件");
            break;
        default:
            fail("Rust 本机输入保护宿主无效");
        }
    }
};
void Controller::localSend(std::vector<INPUT> inputs, bool releasing) {
    ControllerLocalSdk sdk{*this, inputs, releasing};
    rust::platformWorkflowRun(rust::WorkflowMethod::ControllerLocalSend, {}, sdk);
}

} // namespace okww
