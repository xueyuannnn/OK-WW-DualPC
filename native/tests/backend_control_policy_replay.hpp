#pragma once
#include "backend.hpp"
#include "platform_workflow.hpp"
#include "rust_core.hpp"
#include <optional>
namespace okww::backend_control_test {
struct State {
    int scene, waits = 0, recoveries = 0, states = 0;
    i64 clock = INT64_C(0x123456789abcde00);
    std::atomic<bool> *exit = nullptr;
    FramePtr frame;
    QJsonArray trace;
    explicit State(int s) : scene(s) {}
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
template <class D> void sleepFor(D duration) {
    active->trace.append(
        QJsonObject{{"sleep", int(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count())}});
    active->clock += 40000000;
    if (++active->waits >= active->scene % 5 + 1)
        *active->exit = true;
}
inline SHORT GetAsyncKeyState(int vk) {
    active->trace.append(QJsonObject{{"async", vk}});
    return vk >= 0 && vk < 256 && vk % 2 == 0 ? SHORT(0x8000) : 0;
}
struct Keys {
    bool down(int vk) {
        active->trace.append(QJsonObject{{"physical-down", vk}});
        return vk == 70;
    }
    bool physicalHotkey(int vk) {
        active->trace.append(QJsonObject{{"physical-hotkey", vk}});
        return vk >= 0 && vk < 256 && vk % 3 != 0;
    }
};
struct Box {
    bool connected() {
        active->trace.append(QJsonObject{{"connected", int(active->frame.use_count())}});
        return active->scene % 11 != 0;
    }
    QJsonObject state() {
        active->trace.append(QJsonObject{{"state", ++active->states}});
        if (active->scene % 43 == 0)
            throw std::runtime_error("state fixture");
        QJsonObject value{{"keyboard", active->scene % 13 != 0}};
        if (active->scene % 17)
            value["capability"] = QString("capability-%1").arg(active->states);
        return value;
    }
    void release() {
        active->trace.append("release");
        if (active->scene % 23 == 0)
            throw std::runtime_error("release fixture");
        if (active->scene % 29 == 0)
            throw 27;
    }
    void move(int x, int y) {
        active->trace.append(QJsonArray{"move", x, y});
        if (active->scene % 19 == 0)
            throw LocalInputBlocked("move focus", LocalInputBlocked::Recovery::DismissRunDialog);
        if (active->scene % 31 == 0)
            throw std::runtime_error("move fixture");
        if (active->scene % 37 == 0)
            throw 31;
    }
};
struct BackendState {
    mutable Mutex mutex_;
    Config cfg_ = Config::defaults();
    std::unique_ptr<Box> box_;
    std::unique_ptr<Keys> physicalKeys_;
    bool armed_ = false;
    QString error_;
    QJsonObject inputFault_;
    i64 heartbeat_ = 0, captureRecoveryBegan_ = 0;
    std::atomic<bool> exit_{false};
    FramePtr frameLocked() {
        active->trace.append("frame");
        return active->scene % 5 != 0 || (active->recoveries && active->scene % 7 != 0) ? active->frame
                                                                                        : nullptr;
    }
    void disarmFaultLocked(const QString &reason, const QString &message) {
        active->trace.append(QJsonObject{{"fault", reason}, {"message", message}});
        if (active->scene % 47 == 0)
            throw std::runtime_error("fault fixture");
        armed_ = false;
        inputFault_ = {{"reason", reason}};
        error_ = "任务已暂停：" + message;
    }
    bool recoverCaptureLocked() {
        active->trace.append("recover");
        ++active->recoveries;
        return active->scene % 7 != 0;
    }
};
#define BACKEND_CONTROL_FIXTURE_CLASS                                                                        \
    class Backend : public BackendState {                                                                    \
      public:                                                                                                \
        void arm(bool);                                                                                      \
        bool heartbeat();                                                                                    \
        QJsonObject testMove();                                                                              \
        bool physicalKeyDown(int);                                                                           \
        bool physicalHotkey(int);                                                                            \
        void monitor();                                                                                      \
    };
namespace original {
BACKEND_CONTROL_FIXTURE_CLASS
void Backend::arm(bool enabled) {
    std::lock_guard lock(mutex_);
    if (enabled) {
        if (!frameLocked() || !box_ || !box_->connected())
            fail("请先启动采集与盒子连接线程");
        if (!box_->state()["keyboard"].toBool())
            fail(box_->state()["capability"].toString("盒子没有可用键盘路由"));
        heartbeat_ = nowNs();
        captureRecoveryBegan_ = 0;
        error_.clear();
        inputFault_ = {};
    }
    armed_ = enabled;
    if (!enabled && box_)
        box_->release();
}
bool Backend::heartbeat() {
    std::lock_guard lock(mutex_);
    if (!armed_)
        return false;
    heartbeat_ = nowNs();
    return true;
}
QJsonObject Backend::testMove() {
    std::lock_guard lock(mutex_);
    if (!box_ || !box_->connected())
        fail("请先连接键鼠盒子，再测试移动");
    const bool local = cfg_.s("kmbox.protocol") == "sendinput";
    const int x = local ? 40 : int(std::lround(40 * cfg_.d("kmbox.counts_per_pixel_x"))),
              y = local ? 40 : int(std::lround(40 * cfg_.d("kmbox.counts_per_pixel_y")));
    try {
        // Serialize with task input and retain its held buttons and keys. A
        // successful test must not cancel a heavy attack or interrupt movement.
        box_->move(x, y);
    } catch (const std::exception &error) {
        armed_ = false;
        error_ = "测试移动失败：" + errorText(error);
        try {
            box_->release();
        } catch (...) {
        }
        throw;
    }
    return {{"dry_run", cfg_.b("kmbox.dry_run")}, {"armed", armed_}, {"counts_x", x}, {"counts_y", y}};
}
bool Backend::physicalKeyDown(int vk) {
    std::lock_guard lock(mutex_);
    return physicalKeys_ ? physicalKeys_->down(vk)
                         : bool(backend_control_test::GetAsyncKeyState(vk) & 0x8000);
}
bool Backend::physicalHotkey(int vk) {
    std::lock_guard lock(mutex_);
    return !physicalKeys_ || physicalKeys_->physicalHotkey(vk);
}
void Backend::monitor() {
    while (!exit_) {
        backend_control_test::sleepFor(std::chrono::milliseconds(40));
        std::lock_guard lock(mutex_);
        if (!armed_)
            continue;
        if (!box_ || !box_->connected())
            disarmFaultLocked("box_disconnected", "键鼠盒子连接中断");
        else if (nowNs() - heartbeat_ > 1500000000LL)
            disarmFaultLocked("heartbeat_timeout", "任务心跳超过 1500 毫秒未更新");
        else if (!frameLocked()) {
            recoverCaptureLocked();
        }
    }
}
} // namespace original
namespace current {
BACKEND_CONTROL_FIXTURE_CLASS
static void backendControlFailure(int kind) {
    switch (kind) {
    case 0:
        fail("请先启动采集与盒子连接线程");
    case 1:
        fail("请先连接键鼠盒子，再测试移动");
    default:
        fail("Rust 后台操作错误宿主无效");
    }
}
struct BackendControlSdk {
    Backend &b;
    bool enabled = false;
    int vk = 0, x = 0, y = 0;
    FramePtr frame;
    std::exception_ptr moveError;
    QJsonObject value;
    void operator()(rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &error) {
        switch (op) {
        case rust::WorkflowOp::BackendControlEnabled:
            out[0] = enabled;
            break;
        case rust::WorkflowOp::BackendControlReadFrame:
            frame = b.frameLocked();
            break;
        case rust::WorkflowOp::BackendControlFramePresent:
            out[0] = bool(frame);
            break;
        case rust::WorkflowOp::BackendControlDropFrame:
            frame.reset();
            break;
        case rust::WorkflowOp::BackendControlHasBox:
            out[0] = bool(b.box_);
            break;
        case rust::WorkflowOp::BackendControlBoxConnected:
            out[0] = b.box_->connected();
            break;
        case rust::WorkflowOp::BackendControlBoxKeyboard:
            out[0] = b.box_->state()["keyboard"].toBool();
            break;
        case rust::WorkflowOp::BackendControlCapabilityError:
            fail(b.box_->state()["capability"].toString("盒子没有可用键盘路由"));
            break;
        case rust::WorkflowOp::BackendControlHeartbeatNow:
            b.heartbeat_ = nowNs();
            break;
        case rust::WorkflowOp::BackendControlResetRecovery:
            b.captureRecoveryBegan_ = 0;
            break;
        case rust::WorkflowOp::BackendControlClearError:
            b.error_.clear();
            break;
        case rust::WorkflowOp::BackendControlClearFault:
            b.inputFault_ = {};
            break;
        case rust::WorkflowOp::BackendControlSetArmed:
            b.armed_ = a[0] != 0;
            break;
        case rust::WorkflowOp::BackendControlRelease:
            b.box_->release();
            break;
        case rust::WorkflowOp::BackendControlArmed:
            out[0] = b.armed_;
            break;
        case rust::WorkflowOp::BackendControlLocal:
            out[0] = b.cfg_.s("kmbox.protocol") == "sendinput";
            break;
        case rust::WorkflowOp::BackendControlLocalTestCounts:
            x = y = 40;
            break;
        case rust::WorkflowOp::BackendControlNetTestCounts:
            x = int(std::lround(40 * b.cfg_.d("kmbox.counts_per_pixel_x")));
            y = int(std::lround(40 * b.cfg_.d("kmbox.counts_per_pixel_y")));
            break;
        case rust::WorkflowOp::BackendControlMove:
            b.box_->move(x, y);
            break;
        case rust::WorkflowOp::BackendControlSaveMoveError:
            moveError = error;
            break;
        case rust::WorkflowOp::BackendControlMoveError:
            try {
                std::rethrow_exception(error);
            } catch (const std::exception &e) {
                b.error_ = "测试移动失败：" + errorText(e);
            }
            break;
        case rust::WorkflowOp::BackendControlRethrowMoveError:
            std::rethrow_exception(moveError);
            break;
        case rust::WorkflowOp::BackendControlMoveResult:
            value = {{"dry_run", b.cfg_.b("kmbox.dry_run")},
                     {"armed", b.armed_},
                     {"counts_x", x},
                     {"counts_y", y}};
            break;
        case rust::WorkflowOp::BackendControlHasPhysicalKeys:
            out[0] = bool(b.physicalKeys_);
            break;
        case rust::WorkflowOp::BackendControlPhysicalDown:
            out[0] = b.physicalKeys_->down(vk);
            break;
        case rust::WorkflowOp::BackendControlAsyncDown:
            out[0] = (backend_control_test::GetAsyncKeyState(vk) & 0x8000) != 0;
            break;
        case rust::WorkflowOp::BackendControlPhysicalHotkey:
            out[0] = b.physicalKeys_->physicalHotkey(vk);
            break;
        case rust::WorkflowOp::BackendControlFailure:
            backendControlFailure(int(a[0]));
            break;
        default:
            fail("Rust 后台操作宿主无效");
        }
    }
};
struct BackendMonitorSdk {
    Backend &b;
    std::optional<std::unique_lock<Mutex>> lock;
    void operator()(rust::WorkflowOp op, const double *, double *out) {
        switch (op) {
        case rust::WorkflowOp::BackendMonitorExit:
            out[0] = bool(b.exit_);
            break;
        case rust::WorkflowOp::BackendMonitorSleep:
            backend_control_test::sleepFor(std::chrono::milliseconds(40));
            break;
        case rust::WorkflowOp::BackendMonitorLock:
            lock.emplace(b.mutex_);
            break;
        case rust::WorkflowOp::BackendMonitorUnlock:
            lock.reset();
            break;
        case rust::WorkflowOp::BackendMonitorArmed:
            out[0] = b.armed_;
            break;
        case rust::WorkflowOp::BackendMonitorHasBox:
            out[0] = bool(b.box_);
            break;
        case rust::WorkflowOp::BackendMonitorBoxConnected:
            out[0] = b.box_->connected();
            break;
        case rust::WorkflowOp::BackendMonitorDisarmBox:
            b.disarmFaultLocked("box_disconnected", "键鼠盒子连接中断");
            break;
        case rust::WorkflowOp::BackendMonitorHeartbeatExpired:
            out[0] = rust::okww_rs_watchdog(nowNs(), true, true, b.heartbeat_) == 2;
            break;
        case rust::WorkflowOp::BackendMonitorDisarmHeartbeat:
            b.disarmFaultLocked("heartbeat_timeout", "任务心跳超过 1500 毫秒未更新");
            break;
        case rust::WorkflowOp::BackendMonitorFreshFrame:
            out[0] = bool(b.frameLocked());
            break;
        case rust::WorkflowOp::BackendMonitorRecover:
            b.recoverCaptureLocked();
            break;
        default:
            fail("Rust 后台监测宿主无效");
        }
    }
};
void Backend::arm(bool enabled) {
    std::lock_guard lock(mutex_);
    BackendControlSdk sdk{*this};
    sdk.enabled = enabled;
    rust::platformWorkflowRun(rust::WorkflowMethod::BackendArm, {}, sdk);
}
bool Backend::heartbeat() {
    std::lock_guard lock(mutex_);
    BackendControlSdk sdk{*this};
    return rust::platformWorkflowRun(rust::WorkflowMethod::BackendHeartbeat, {}, sdk)[0] != 0;
}
QJsonObject Backend::testMove() {
    std::lock_guard lock(mutex_);
    BackendControlSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::BackendTestMove, {}, sdk);
    return sdk.value;
}
bool Backend::physicalKeyDown(int vk) {
    std::lock_guard lock(mutex_);
    BackendControlSdk sdk{*this};
    sdk.vk = vk;
    return rust::platformWorkflowRun(rust::WorkflowMethod::BackendPhysicalDown, {}, sdk)[0] != 0;
}
bool Backend::physicalHotkey(int vk) {
    std::lock_guard lock(mutex_);
    BackendControlSdk sdk{*this};
    sdk.vk = vk;
    return rust::platformWorkflowRun(rust::WorkflowMethod::BackendPhysicalHotkey, {}, sdk)[0] != 0;
}
void Backend::monitor() {
    BackendMonitorSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::BackendMonitor, {}, sdk);
}
} // namespace current
#undef BACKEND_CONTROL_FIXTURE_CLASS
template <class B> QJsonObject run(int method) {
    B b;
    const int scene = active->scene;
    active->exit = &b.exit_;
    if (scene % 7 != 0)
        b.box_ = std::make_unique<Box>();
    if ((scene / 5) % 2)
        b.physicalKeys_ = std::make_unique<Keys>();
    b.armed_ = scene % 3 != 0;
    b.error_ = "prior";
    b.inputFault_ = {{"old", true}};
    b.captureRecoveryBegan_ = 123;
    const i64 ages[]{0, 1460000000, 1460000001, 2000000000};
    b.heartbeat_ = active->clock - ages[(scene / 11) % 4];
    b.cfg_.set("kmbox.protocol", scene % 2 ? "kmbox_net" : "sendinput");
    b.cfg_.set("kmbox.dry_run", scene % 5 == 0);
    const double counts[]{.0125, .0375, 1., 1.5, .01875};
    b.cfg_.set("kmbox.counts_per_pixel_x", counts[(scene / 3) % 5]);
    b.cfg_.set("kmbox.counts_per_pixel_y", counts[(scene / 7) % 5]);
    const int keys[]{-1, 0, 4, 70, 255, 256};
    const int vk = keys[(scene / 13) % 6];
    QJsonObject result;
    try {
        switch (method) {
        case 0:
            b.arm((scene / 2) % 2 != 0);
            break;
        case 1:
            result["heartbeat"] = b.heartbeat();
            break;
        case 2:
            result["move"] = b.testMove();
            break;
        case 3:
            result["down"] = b.physicalKeyDown(vk);
            break;
        case 4:
            result["hotkey"] = b.physicalHotkey(vk);
            break;
        case 5:
            b.monitor();
            break;
        }
    } catch (const LocalInputBlocked &e) {
        result["error"] = errorText(e);
        result["recovery_action"] = int(e.recovery);
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    } catch (...) {
        result["error"] = "unknown";
    }
    result["armed"] = b.armed_;
    result["backend_error"] = b.error_;
    result["fault"] = b.inputFault_;
    result["heartbeat_time"] = QString::number(b.heartbeat_);
    result["capture_recovery"] = QString::number(b.captureRecoveryBegan_);
    result["exited"] = bool(b.exit_);
    return result;
}
inline QJsonObject outcome(int scene, int method, bool rust) {
    State state(scene);
    active = &state;
    state.frame = std::make_shared<Frame>();
    auto result = rust ? run<current::Backend>(method) : run<original::Backend>(method);
    result["trace"] = state.trace;
    return result;
}
} // namespace okww::backend_control_test
