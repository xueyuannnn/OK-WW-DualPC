#pragma once
#include "controller.hpp"
#include "controller_math_abi.hpp"
#include "platform_workflow.hpp"
namespace okww::controller_input_test {
static constexpr uint32_t MOVE = 0xAEDE7345, LEFT = 0x9823AE8D, RIGHT = 0x238D8212, MIDDLE = 0x97A3AE8D,
                          WHEEL = 0xFFEEAD38;
struct State {
    int scene;
    QJsonArray trace;
    explicit State(int s) : scene(s) {}
    void failInput() {
        if (scene % 17 == 0)
            throw LocalInputBlocked("focus fixture", LocalInputBlocked::Recovery::DismissRunDialog);
        if (scene % 19 == 0)
            throw std::runtime_error("send fixture");
        if (scene % 31 == 0)
            throw 27;
    }
};
inline thread_local State *active = nullptr;
inline QJsonArray rectValue(QRect rect) { return {rect.x(), rect.y(), rect.width(), rect.height()}; }
struct Target {
    bool capture;
    QRect value;
    Target(bool c, QRect r) : capture(c), value(r) {}
    QRect rect() {
        active->trace.append(
            QJsonObject{{"target-rect", capture ? "capture" : "input"}, {"rect", rectValue(value)}});
        if (active->scene % 23 == 0)
            throw std::runtime_error("rect fixture");
        return value;
    }
};
inline QRect foregroundClientRect() {
    active->trace.append("foreground-rect");
    return active->scene % 2 ? QRect(-1920, 0, 1920, 1080) : QRect(0, 0, 1920, 1080);
}
inline QRect virtualDesktopRect() {
    active->trace.append("virtual-desktop");
    return {-1920, 0, 3840, 2160};
}
struct ControllerState {
    Config cfg_ = Config::defaults();
    std::recursive_mutex mutex_;
    int buttons_ = 0, modifiers_ = 0;
    std::set<int> keys_;
    std::unique_ptr<Target> captureTarget_, target_;
    bool local() const { return cfg_.s("kmbox.protocol") == "sendinput"; }
    void localSend(std::vector<INPUT> inputs, bool releasing = false) {
        QJsonArray values;
        for (const auto &i : inputs) {
            if (i.type == INPUT_MOUSE)
                values.append(QJsonObject{{"type", int(i.type)},
                                          {"x", int(i.mi.dx)},
                                          {"y", int(i.mi.dy)},
                                          {"flags", qint64(i.mi.dwFlags)},
                                          {"data", qint64(i.mi.mouseData)},
                                          {"tag", QString::number(i.mi.dwExtraInfo)}});
            else
                values.append(QJsonObject{{"type", int(i.type)},
                                          {"scan", i.ki.wScan},
                                          {"vk", i.ki.wVk},
                                          {"flags", qint64(i.ki.dwFlags)},
                                          {"tag", QString::number(i.ki.dwExtraInfo)}});
        }
        active->trace.append(QJsonObject{{"send", values},
                                         {"releasing", releasing},
                                         {"buttons", buttons_},
                                         {"modifiers", modifiers_},
                                         {"keys", int(keys_.size())}});
        active->failInput();
    }
    void mouse(uint32_t command, int x = 0, int y = 0, int wheel = 0) {
        active->trace.append(QJsonObject{{"command", qint64(command)},
                                         {"x", x},
                                         {"y", y},
                                         {"wheel", wheel},
                                         {"buttons", buttons_},
                                         {"modifiers", modifiers_}});
        active->failInput();
    }
    void keyboardReport() {
        QJsonArray keys;
        for (int code : keys_)
            keys.append(code);
        active->trace.append(QJsonObject{{"keyboard", keys}, {"modifiers", modifiers_}});
        active->failInput();
    }
};
#define CONTROLLER_INPUT_FIXTURE_CLASS                                                                       \
    class Controller : public ControllerState {                                                              \
      public:                                                                                                \
        void move(int, int);                                                                                 \
        void absolute(int, int, int, int);                                                                   \
        void button(const QString &, bool);                                                                  \
        void key(int, bool);                                                                                 \
        void wheel(int);                                                                                     \
    };
namespace original {
CONTROLLER_INPUT_FIXTURE_CLASS
void Controller::move(int x, int y) {
    std::lock_guard lock(mutex_);
    if (std::abs(int64_t(x)) > 32767 || std::abs(int64_t(y)) > 32767)
        fail("移动超出 HID 范围");
    if (local()) {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dx = x;
        input.mi.dy = y;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
        localSend({input});
    } else
        mouse(MOVE, x, y);
}
void Controller::absolute(int x, int y, int width, int height) {
    std::lock_guard lock(mutex_);
    if (!local())
        fail("KMBox Net 使用相对定位");
    if (width < 1 || height < 1 || x < 0 || y < 0 || x >= width || y >= height)
        fail("SendInput 坐标无效");
    // Local screenshots already supply desktop coordinates. With an external
    // capture source, map the image onto the current foreground client area.
    const QRect rect = captureTarget_            ? captureTarget_->rect()
                       : target_                 ? target_->rect()
                       : cfg_.b("kmbox.dry_run") ? QRect(0, 0, width, height)
                                                 : foregroundClientRect();
    const QPoint point(rect.x() + int(int64_t(x) * rect.width() / width),
                       rect.y() + int(int64_t(y) * rect.height() / height));
    if (target_ && !target_->rect().contains(point))
        fail("点击位置位于指定窗口外，请检查高级设置中的窗口与截图配置");
    const QRect desktop = cfg_.b("kmbox.dry_run") ? rect : virtualDesktopRect();
    const auto normal = desktopAbsolute(point, desktop);
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = normal.x();
    input.mi.dy = normal.y();
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    localSend({input});
}
void Controller::button(const QString &name, bool down) {
    std::lock_guard lock(mutex_);
    int i = QStringList{"left", "right", "middle", "x1", "x2"}.indexOf(name);
    if (i < 0)
        fail("未知鼠标按键");
    if (local()) {
        if (!down && !(buttons_ & (1 << i)))
            return;
        static constexpr DWORD downs[]{MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_MIDDLEDOWN,
                                       MOUSEEVENTF_XDOWN, MOUSEEVENTF_XDOWN};
        static constexpr DWORD ups[]{MOUSEEVENTF_LEFTUP, MOUSEEVENTF_RIGHTUP, MOUSEEVENTF_MIDDLEUP,
                                     MOUSEEVENTF_XUP, MOUSEEVENTF_XUP};
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = down ? downs[i] : ups[i];
        if (i >= 3)
            input.mi.mouseData = i == 3 ? XBUTTON1 : XBUTTON2;
        // Retain failed-down state so emergency cleanup can still send key-up.
        if (down)
            buttons_ |= 1 << i;
        localSend({input}, !down);
        if (!down)
            buttons_ &= ~(1 << i);
        return;
    }
    buttons_ = down ? buttons_ | (1 << i) : buttons_ & ~(1 << i);
    mouse(i == 0 ? LEFT : i == 2 ? MIDDLE : RIGHT);
}
void Controller::key(int code, bool down) {
    std::lock_guard lock(mutex_);
    if (code < 1 || code > 255)
        fail("无效 HID 键码");
    bool mod = code >= 0xe0 && code <= 0xe7;
    if (local()) {
        const bool held = mod ? bool(modifiers_ & (1 << (code - 0xe0))) : keys_.contains(code);
        if (!down && !held)
            return;
        const auto input = hidInput(code, down);
        if (down) {
            if (mod)
                modifiers_ |= 1 << (code - 0xe0);
            else
                keys_.insert(code);
        }
        localSend({input}, !down);
        if (!down) {
            if (mod)
                modifiers_ &= ~(1 << (code - 0xe0));
            else
                keys_.erase(code);
        }
        return;
    }
    if (mod)
        modifiers_ = down ? modifiers_ | (1 << (code - 0xe0)) : modifiers_ & ~(1 << (code - 0xe0));
    else if (down) {
        if (keys_.size() >= 10 && !keys_.contains(code))
            fail("同时按下的键超过盒子容量");
        keys_.insert(code);
    } else
        keys_.erase(code);
    keyboardReport();
}
void Controller::wheel(int steps) {
    std::lock_guard lock(mutex_);
    if (std::abs(int64_t(steps)) > 10000)
        fail("滚轮步数过大");
    if (local()) {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        input.mi.mouseData = DWORD(steps * WHEEL_DELTA);
        localSend({input});
        return;
    }
    while (steps) {
        const int n = std::clamp(steps, -127, 127);
        mouse(WHEEL, 0, 0, n);
        steps -= n;
    }
}
} // namespace original
namespace current {
CONTROLLER_INPUT_FIXTURE_CLASS
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
} // namespace current
#undef CONTROLLER_INPUT_FIXTURE_CLASS
template <class C> QJsonObject run(int method) {
    C c;
    const int scene = active->scene;
    c.cfg_.set("kmbox.protocol", (scene / 5) % 2 ? "kmbox_net" : "sendinput");
    c.cfg_.set("kmbox.dry_run", scene % 3 == 0);
    c.buttons_ = scene % 32;
    c.modifiers_ = scene % 256;
    for (int i = 0; i < scene % 12; ++i)
        c.keys_.insert(4 + i);
    if ((scene / 7) % 5 == 0)
        c.captureTarget_ = std::make_unique<Target>(true, QRect(-1920, 0, 1920, 1080));
    if ((scene / 13) % 7 == 0)
        c.target_ = std::make_unique<Target>(false, QRect(0, 0, 1920, 1080));
    QJsonObject result;
    try {
        switch (method) {
        case 0: {
            const int values[]{INT32_MIN, -32768, -32767, 0, 32767, 32768, INT32_MAX};
            c.move(values[scene % 7], values[(scene / 7) % 7]);
            break;
        }
        case 1: {
            const int widths[]{0, 1, 640, 1920, 3840};
            const int width = widths[scene % 5], height = scene % 11 ? 1080 : 0;
            const int xs[]{-1, 0, width / 2, width - 1, width};
            const int ys[]{-1, 0, height / 2, height - 1, height};
            c.absolute(xs[(scene / 5) % 5], ys[(scene / 25) % 5], width, height);
            break;
        }
        case 2: {
            const QStringList names{"left", "right", "middle", "x1", "x2", "missing"};
            c.button(names[scene % 6], (scene / 6) % 2 == 0);
            break;
        }
        case 3: {
            const int codes[]{-1, 0, 1, 4, 8, 30, 100, 224, 227, 231, 232, 255, 256, INT32_MAX};
            c.key(codes[scene % 14], (scene / 14) % 2 == 0);
            break;
        }
        case 4: {
            const int steps[]{INT32_MIN, -10001, -10000, -128,  -127,  -1,       0,
                              1,         127,    128,    10000, 10001, INT32_MAX};
            c.wheel(steps[scene % 13]);
            break;
        }
        }
    } catch (const LocalInputBlocked &e) {
        result["error"] = errorText(e);
        result["blocked"] = int(e.recovery);
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
    return result;
}
inline QJsonObject outcome(int scene, int method, bool rust) {
    State state(scene);
    active = &state;
    auto result = rust ? run<current::Controller>(method) : run<original::Controller>(method);
    result["trace"] = state.trace;
    return result;
}
} // namespace okww::controller_input_test
