#pragma once
#include "controller.hpp"
#include "platform_workflow.hpp"
namespace okww::controller_local_test {
struct State {
    int scene;
    QJsonArray trace;
    QJsonObject environment;
    i64 clock = INT64_C(0x123456789abcde00);
    explicit State(int s) : scene(s) {}
};
inline thread_local State *active = nullptr;
inline i64 nowNs() {
    active->trace.append(QJsonObject{{"clock", QString::number(active->clock)}});
    return active->clock += 1000000;
}
inline QByteArray wire(const INPUT *inputs, size_t count) {
    return QByteArray(reinterpret_cast<const char *>(inputs), qsizetype(count * sizeof(INPUT)));
}
inline UINT SendInput(UINT count, INPUT *inputs, int size) {
    active->trace.append(QJsonObject{
        {"send", QString::fromLatin1(wire(inputs, count).toHex())}, {"count", int(count)}, {"size", size}});
    return active->scene % 19 == 0 && count ? count - 1 : count;
}
inline QJsonObject localInputEnvironment() {
    active->trace.append("system-environment");
    if (active->scene % 43 == 0)
        throw std::runtime_error("environment fixture");
    if (active->scene % 47 == 0)
        throw 27;
    return active->environment;
}
struct Target {
    QRect rect() {
        active->trace.append("target-rect");
        if (active->scene % 31 == 0)
            throw std::runtime_error("rect fixture");
        return {0, 0, 1920, 1080};
    }
    bool foreground() {
        active->trace.append("target-foreground");
        return active->scene % 5 != 0;
    }
};
struct ControllerState {
    Config cfg_ = Config::defaults();
    bool connected_ = true;
    int modifiers_ = 0;
    std::unique_ptr<Target> target_;
    std::function<QJsonObject()> localEnvironment_;
    QJsonObject lastLocalEnvironment_{{"old", true}};
    QList<QByteArray> history_;
    int latency = 0;
    void recordPacket(const QByteArray &packet) {
        active->trace.append(QJsonObject{{"record", QString::fromLatin1(packet.toHex())}});
        history_.append(packet);
    }
    void noteLatency(i64 began) {
        ++latency;
        active->trace.append(
            QJsonObject{{"latency-began", QString::number(began)}, {"end", QString::number(nowNs())}});
    }
};
#define CONTROLLER_LOCAL_FIXTURE_CLASS                                                                       \
    class Controller : public ControllerState {                                                              \
      public:                                                                                                \
        void localSend(std::vector<INPUT>, bool);                                                            \
    };
namespace original {
CONTROLLER_LOCAL_FIXTURE_CLASS
void Controller::localSend(std::vector<INPUT> inputs, bool releasing) {
    if (!connected_)
        fail("SendInput 未连接");
    bool cancelRunDialog = false;
    if (!releasing && (!cfg_.b("kmbox.dry_run") || localEnvironment_)) {
        lastLocalEnvironment_ =
            localEnvironment_ ? localEnvironment_() : controller_local_test::localInputEnvironment();
        const auto environment = lastLocalEnvironment_;
        const int foreignModifiers = environment["modifiers"].toInt() & ~modifiers_ & 0x88;
        if (foreignModifiers) {
            QStringList names;
            const QStringList labels{"左 Ctrl", "左 Shift", "左 Alt", "左 Win",
                                     "右 Ctrl", "右 Shift", "右 Alt", "右 Win"};
            for (int i = 0; i < 8; ++i)
                if (foreignModifiers & (1 << i))
                    names.append(labels[i]);
            throw LocalInputBlocked("等待额外按住的 " + names.join(" / ") + " 松开");
        }
        const bool shell =
            environment["executable"].toString().compare("explorer.exe", Qt::CaseInsensitive) == 0;
        const auto cls = environment["class"].toString();
        const bool runDialog = shell && cls == "#32770" &&
                               QStringList{"运行", "執行", "Run"}.contains(
                                   environment["title"].toString().trimmed(), Qt::CaseInsensitive);
        cancelRunDialog =
            runDialog && !inputs.empty() && std::all_of(inputs.begin(), inputs.end(), [](const INPUT &input) {
                return input.type == INPUT_KEYBOARD && input.ki.wScan == 1 &&
                       (input.ki.dwFlags & KEYEVENTF_SCANCODE);
            });
        if (environment["own_window"].toBool() ||
            (shell && !cancelRunDialog &&
             QStringList{"#32770", "Shell_TrayWnd", "Shell_SecondaryTrayWnd", "Progman", "WorkerW"}.contains(
                 cls)))
            throw LocalInputBlocked("等待输入焦点返回游戏或远程窗口：" + environment["title"].toString(),
                                    runDialog ? LocalInputBlocked::Recovery::DismissRunDialog
                                              : LocalInputBlocked::Recovery::Wait);
    }
    if (target_ && !releasing && !cancelRunDialog) {
        target_->rect();
        if (!target_->foreground())
            throw LocalInputBlocked("等待指定的游戏或远程窗口返回前台");
    }
    for (auto &input : inputs) {
        if (input.type == INPUT_MOUSE)
            input.mi.dwExtraInfo = localInputTag;
        if (input.type == INPUT_KEYBOARD)
            input.ki.dwExtraInfo = localInputTag;
    }
    if (cfg_.b("kmbox.dry_run")) {
        recordPacket(QByteArray(reinterpret_cast<const char *>(inputs.data()),
                                qsizetype(inputs.size() * sizeof(INPUT))));
        return;
    }
    const auto began = nowNs();
    const auto sent = controller_local_test::SendInput(UINT(inputs.size()), inputs.data(), sizeof(INPUT));
    if (sent != inputs.size())
        fail("SendInput 未完整发送输入，请检查目标程序权限是否高于本软件");
    noteLatency(began);
}
} // namespace original
namespace current {
CONTROLLER_LOCAL_FIXTURE_CLASS
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
            c.lastLocalEnvironment_ = controller_local_test::localInputEnvironment();
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
            sent = controller_local_test::SendInput(UINT(inputs.size()), inputs.data(), sizeof(INPUT));
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
} // namespace current
#undef CONTROLLER_LOCAL_FIXTURE_CLASS
inline std::vector<INPUT> inputs() {
    std::vector<INPUT> values;
    auto keyboard = [&](int scan, DWORD flags) {
        INPUT value;
        memset(&value, 0, sizeof(value));
        value.type = INPUT_KEYBOARD;
        value.ki.wScan = WORD(scan);
        value.ki.dwFlags = flags;
        value.ki.dwExtraInfo = 123;
        values.push_back(value);
    };
    auto mouse = [&] {
        INPUT value;
        memset(&value, 0, sizeof(value));
        value.type = INPUT_MOUSE;
        value.mi.dx = 7;
        value.mi.dy = -9;
        value.mi.dwFlags = MOUSEEVENTF_MOVE;
        value.mi.dwExtraInfo = 456;
        values.push_back(value);
    };
    switch (active->scene % 8) {
    case 0:
        break;
    case 1:
        keyboard(1, KEYEVENTF_SCANCODE);
        break;
    case 2:
        keyboard(1, KEYEVENTF_SCANCODE);
        keyboard(1, KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP);
        break;
    case 3:
        keyboard(18, KEYEVENTF_SCANCODE);
        break;
    case 4:
        mouse();
        break;
    case 5:
        keyboard(1, 0);
        break;
    case 6:
        keyboard(1, KEYEVENTF_SCANCODE);
        mouse();
        break;
    case 7: {
        INPUT value;
        memset(&value, 0, sizeof(value));
        value.type = 42;
        values.push_back(value);
        break;
    }
    }
    return values;
}
template <class C> QJsonObject run(bool releasing) {
    C c;
    const int scene = active->scene;
    c.connected_ = scene % 17 != 0;
    c.modifiers_ = scene % 256;
    c.cfg_.set("kmbox.dry_run", (scene / 5) % 2 == 0);
    if ((scene / 3) % 2)
        c.localEnvironment_ = [] {
            active->trace.append("provider-environment");
            if (active->scene % 43 == 0)
                throw std::runtime_error("provider fixture");
            if (active->scene % 47 == 0)
                throw 27;
            return active->environment;
        };
    if ((scene / 7) % 2)
        c.target_ = std::make_unique<Target>();
    QJsonObject result;
    try {
        c.localSend(inputs(), releasing);
    } catch (const LocalInputBlocked &e) {
        result["error"] = errorText(e);
        result["recovery"] = int(e.recovery);
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    } catch (...) {
        result["error"] = "unknown";
    }
    result["last_environment"] = c.lastLocalEnvironment_;
    result["latency_count"] = c.latency;
    result["history_count"] = c.history_.size();
    return result;
}
inline QJsonObject outcome(int scene, bool releasing, bool rust) {
    State state(scene);
    active = &state;
    const QStringList titles{"运行", "執行", "Run", "  rUn \n", "Game", "Other"},
        classes{"#32770",  "GameWindow", "Shell_TrayWnd", "Shell_SecondaryTrayWnd",
                "Progman", "WorkerW",    "other"};
    const int modifiers[]{0, 8, 128, 136, -1, 255};
    state.environment = {{"executable", (scene / 13) % 2 ? "Explorer.EXE" : "game.exe"},
                         {"title", titles[(scene / 8) % titles.size()]},
                         {"class", classes[(scene / 5) % classes.size()]},
                         {"own_window", scene % 11 == 0},
                         {"modifiers", modifiers[(scene / 17) % 6]}};
    auto result = rust ? run<current::Controller>(releasing) : run<original::Controller>(releasing);
    result["trace"] = state.trace;
    return result;
}
} // namespace okww::controller_local_test
