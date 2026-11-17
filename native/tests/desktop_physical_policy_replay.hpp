#pragma once
#include "desktop.hpp"
#include "platform_workflow.hpp"
namespace okww::desktop_physical_test {
struct State {
    int scene, messages = 0;
    i64 clock = INT64_C(0x123456789abcde00);
    QJsonArray trace;
    std::function<void()> queued;
    HOOKPROC callback = nullptr;
    explicit State(int s) : scene(s) {}
};
inline thread_local State *active = nullptr;
inline std::array<std::atomic<bool>, 256> physical{};
inline std::array<std::atomic<i64>, 256> physicalPress{}, injectedPress{};
inline std::atomic<bool> hookActive{false};
inline i64 nowNs() {
    active->trace.append("now");
    return active->clock += 100;
}
inline LRESULT CallNextHookEx(HHOOK, int code, WPARAM message, LPARAM data) {
    active->trace.append(
        QJsonObject{{"next-hook", code}, {"message", QString::number(message)}, {"has-data", data != 0}});
    return LRESULT(INT64_C(0x123456789abcdef6));
}
struct FakeThread {
    bool live = false;
    FakeThread() = default;
    template <class F, class... A> explicit FakeThread(F f, A... args) {
        active->trace.append("create-thread");
        if (active->scene % 11 == 0)
            throw std::runtime_error("thread fixture");
        active->queued = std::bind(f, args...);
        live = true;
    }
    FakeThread(FakeThread &&other) noexcept : live(std::exchange(other.live, false)) {}
    FakeThread &operator=(FakeThread &&other) noexcept {
        live = std::exchange(other.live, false);
        return *this;
    }
    bool joinable() const {
        active->trace.append("joinable");
        return live;
    }
    void join() {
        active->trace.append("join");
        if (active->queued) {
            auto f = std::exchange(active->queued, {});
            f();
        }
        live = false;
    }
};
template <class D> void sleepFor(D duration) {
    active->trace.append(
        QJsonObject{{"sleep", int(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count())}});
    if (active->queued) {
        auto f = std::exchange(active->queued, {});
        f();
    }
}
inline BOOL PeekMessageW(MSG *, HWND, UINT, UINT, UINT remove) {
    active->trace.append(QJsonObject{{"peek", int(remove)}});
    return FALSE;
}
inline DWORD GetCurrentThreadId() {
    active->trace.append("thread-id");
    return 0xf1234567;
}
inline HMODULE GetModuleHandleW(LPCWSTR) {
    active->trace.append("module");
    return reinterpret_cast<HMODULE>(uintptr_t(1));
}
inline HHOOK SetWindowsHookExW(int id, HOOKPROC callback, HMODULE, DWORD thread) {
    active->trace.append(QJsonObject{{"set-hook", id}, {"thread", qint64(thread)}});
    active->callback = callback;
    return active->scene % 7 ? reinterpret_cast<HHOOK>(UINT64_C(0x123456789abcdef5)) : nullptr;
}
inline BOOL GetMessageW(MSG *out, HWND, UINT, UINT) {
    const int index = active->messages++;
    active->trace.append(QJsonObject{{"get-message", index}});
    if (index >= active->scene % 4)
        return active->scene % 5 ? 0 : -1;
    out->message = WM_KEYDOWN;
    out->wParam = WPARAM(64 + index);
    return 1;
}
inline BOOL TranslateMessage(const MSG *message) {
    active->trace.append(QJsonObject{{"translate", int(message->message)}});
    return FALSE;
}
inline LRESULT DispatchMessageW(const MSG *message) {
    active->trace.append(QJsonObject{{"dispatch", int(message->message)}});
    KBDLLHOOKSTRUCT key{};
    key.vkCode = DWORD(message->wParam);
    key.flags = active->scene % 2 ? LLKHF_INJECTED : 0;
    return active->callback(HC_ACTION, WM_KEYDOWN, reinterpret_cast<LPARAM>(&key));
}
inline BOOL UnhookWindowsHookEx(HHOOK hook) {
    active->trace.append(QJsonObject{{"unhook", QString::number(quintptr(hook))}});
    return FALSE;
}
inline BOOL PostThreadMessageW(DWORD id, UINT message, WPARAM, LPARAM) {
    active->trace.append(QJsonObject{{"post-quit", qint64(id)}, {"message", int(message)}});
    return FALSE;
}
#define PHYSICAL_FIXTURE_CLASS                                                                               \
    class PhysicalKeys {                                                                                     \
      public:                                                                                                \
        FakeThread thread_;                                                                                  \
        std::atomic<DWORD> threadId_{0};                                                                     \
        std::atomic<bool> ready_{false}, failed_{false};                                                     \
        PhysicalKeys();                                                                                      \
        ~PhysicalKeys();                                                                                     \
        void run();                                                                                          \
        bool down(int) const;                                                                                \
        bool physicalHotkey(int) const;                                                                      \
    };
namespace original {
PHYSICAL_FIXTURE_CLASS
LRESULT CALLBACK keyHook(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION) {
        const auto *key = reinterpret_cast<KBDLLHOOKSTRUCT *>(data);
        if (key->vkCode < physical.size()) {
            const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
            if (!(key->flags & LLKHF_INJECTED)) {
                physical[key->vkCode] = down;
                if (down)
                    physicalPress[key->vkCode] = nowNs();
            } else if (down)
                injectedPress[key->vkCode] = nowNs();
        }
    }
    return desktop_physical_test::CallNextHookEx(nullptr, code, message, data);
}
PhysicalKeys::PhysicalKeys() {
    if (hookActive.exchange(true))
        fail("本机按键监听已经运行");
    thread_ = FakeThread([this] {
        MSG message{};
        desktop_physical_test::PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
        threadId_ = desktop_physical_test::GetCurrentThreadId();
        for (auto &key : physical)
            key = false;
        for (auto &time : physicalPress)
            time = 0;
        for (auto &time : injectedPress)
            time = 0;
        const auto hook = desktop_physical_test::SetWindowsHookExW(
            WH_KEYBOARD_LL, keyHook, desktop_physical_test::GetModuleHandleW(nullptr), 0);
        failed_ = !hook;
        ready_ = true;
        if (hook) {
            while (desktop_physical_test::GetMessageW(&message, nullptr, 0, 0) > 0) {
                desktop_physical_test::TranslateMessage(&message);
                desktop_physical_test::DispatchMessageW(&message);
            }
            desktop_physical_test::UnhookWindowsHookEx(hook);
        }
    });
    while (!ready_)
        desktop_physical_test::sleepFor(std::chrono::milliseconds(1));
    if (failed_) {
        thread_.join();
        hookActive = false;
        fail("无法监听本机物理按键，SendInput 连接已取消");
    }
}
PhysicalKeys::~PhysicalKeys() {
    if (threadId_)
        desktop_physical_test::PostThreadMessageW(threadId_, WM_QUIT, 0, 0);
    if (thread_.joinable())
        thread_.join();
    hookActive = false;
}
bool PhysicalKeys::down(int vk) const { return vk >= 0 && vk < 256 && physical[vk]; }
bool PhysicalKeys::physicalHotkey(int vk) const {
    return vk >= 0 && vk < 256 && physicalPress[vk].load() > injectedPress[vk].load();
}
} // namespace original
namespace current {
PHYSICAL_FIXTURE_CLASS
struct PhysicalHookSdk {
    int code;
    WPARAM message;
    LPARAM data;
    const KBDLLHOOKSTRUCT *key = nullptr;
    LRESULT value = 0;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::PhysicalHookAction:
            out[0] = code == HC_ACTION;
            break;
        case rust::WorkflowOp::PhysicalHookRead:
            key = reinterpret_cast<const KBDLLHOOKSTRUCT *>(data);
            break;
        case rust::WorkflowOp::PhysicalHookUsage:
            out[0] = key->vkCode;
            break;
        case rust::WorkflowOp::PhysicalHookCapacity:
            out[0] = double(physical.size());
            break;
        case rust::WorkflowOp::PhysicalHookKeyDown:
            out[0] = message == WM_KEYDOWN;
            break;
        case rust::WorkflowOp::PhysicalHookSysDown:
            out[0] = message == WM_SYSKEYDOWN;
            break;
        case rust::WorkflowOp::PhysicalHookInjected:
            out[0] = (key->flags & LLKHF_INJECTED) != 0;
            break;
        case rust::WorkflowOp::PhysicalHookSetDown:
            physical[key->vkCode] = a[0] != 0;
            break;
        case rust::WorkflowOp::PhysicalHookSetPhysicalPress:
            physicalPress[key->vkCode] = nowNs();
            break;
        case rust::WorkflowOp::PhysicalHookSetInjectedPress:
            injectedPress[key->vkCode] = nowNs();
            break;
        case rust::WorkflowOp::PhysicalHookCallNext:
            value = desktop_physical_test::CallNextHookEx(nullptr, code, message, data);
            break;
        default:
            fail("Rust 物理按键回调宿主无效");
        }
    }
};
LRESULT CALLBACK keyHook(int code, WPARAM message, LPARAM data) {
    PhysicalHookSdk sdk{code, message, data};
    rust::platformWorkflowRun(rust::WorkflowMethod::PhysicalHook, {}, sdk);
    return sdk.value;
}
struct PhysicalKeysSdk {
    PhysicalKeys &p;
    MSG message{};
    HHOOK hook = nullptr;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::PhysicalClaimHook:
            out[0] = hookActive.exchange(true);
            break;
        case rust::WorkflowOp::PhysicalCreateThread:
            p.thread_ = FakeThread(&PhysicalKeys::run, &p);
            break;
        case rust::WorkflowOp::PhysicalReady:
            out[0] = bool(p.ready_);
            break;
        case rust::WorkflowOp::PhysicalWaitReady:
            desktop_physical_test::sleepFor(std::chrono::milliseconds(1));
            break;
        case rust::WorkflowOp::PhysicalFailed:
            out[0] = bool(p.failed_);
            break;
        case rust::WorkflowOp::PhysicalJoinThread:
            p.thread_.join();
            break;
        case rust::WorkflowOp::PhysicalClearActive:
            hookActive = false;
            break;
        case rust::WorkflowOp::PhysicalHasThreadId:
            out[0] = p.threadId_ != 0;
            break;
        case rust::WorkflowOp::PhysicalPostQuit:
            desktop_physical_test::PostThreadMessageW(p.threadId_, WM_QUIT, 0, 0);
            break;
        case rust::WorkflowOp::PhysicalThreadJoinable:
            out[0] = p.thread_.joinable();
            break;
        case rust::WorkflowOp::PhysicalPeekMessage:
            desktop_physical_test::PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
            break;
        case rust::WorkflowOp::PhysicalStoreThreadId:
            p.threadId_ = desktop_physical_test::GetCurrentThreadId();
            break;
        case rust::WorkflowOp::PhysicalResetDown:
            physical[size_t(a[0])] = false;
            break;
        case rust::WorkflowOp::PhysicalResetPress:
            physicalPress[size_t(a[0])] = 0;
            break;
        case rust::WorkflowOp::PhysicalResetInjected:
            injectedPress[size_t(a[0])] = 0;
            break;
        case rust::WorkflowOp::PhysicalInstallHook:
            hook = desktop_physical_test::SetWindowsHookExW(
                WH_KEYBOARD_LL, keyHook, desktop_physical_test::GetModuleHandleW(nullptr), 0);
            break;
        case rust::WorkflowOp::PhysicalStoreFailed:
            p.failed_ = !hook;
            break;
        case rust::WorkflowOp::PhysicalStoreReady:
            p.ready_ = true;
            break;
        case rust::WorkflowOp::PhysicalHasHook:
            out[0] = hook != nullptr;
            break;
        case rust::WorkflowOp::PhysicalGetMessage:
            out[0] = desktop_physical_test::GetMessageW(&message, nullptr, 0, 0);
            break;
        case rust::WorkflowOp::PhysicalTranslateMessage:
            desktop_physical_test::TranslateMessage(&message);
            break;
        case rust::WorkflowOp::PhysicalDispatchMessage:
            desktop_physical_test::DispatchMessageW(&message);
            break;
        case rust::WorkflowOp::PhysicalUnhook:
            desktop_physical_test::UnhookWindowsHookEx(hook);
            break;
        case rust::WorkflowOp::PhysicalFailure:
            if (a[0] == 0)
                fail("本机按键监听已经运行");
            else
                fail("无法监听本机物理按键，SendInput 连接已取消");
            break;
        default:
            fail("Rust 物理按键监听线程宿主无效");
        }
    }
};
struct PhysicalKeyReadSdk {
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::PhysicalReadDown:
            out[0] = bool(physical[int(a[0])]);
            break;
        case rust::WorkflowOp::PhysicalReadLaterPress:
            out[0] = physicalPress[int(a[0])].load() > injectedPress[int(a[0])].load();
            break;
        default:
            fail("Rust 物理按键状态宿主无效");
        }
    }
};
PhysicalKeys::PhysicalKeys() {
    PhysicalKeysSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::PhysicalCreate, {}, sdk);
}
PhysicalKeys::~PhysicalKeys() {
    PhysicalKeysSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::PhysicalDestroy, {}, sdk);
}
bool PhysicalKeys::down(int vk) const {
    PhysicalKeyReadSdk sdk;
    const std::array<double, 1> a{double(vk)};
    return rust::platformWorkflowRun(rust::WorkflowMethod::PhysicalDown, a, sdk)[0] != 0;
}
bool PhysicalKeys::physicalHotkey(int vk) const {
    PhysicalKeyReadSdk sdk;
    const std::array<double, 1> a{double(vk)};
    return rust::platformWorkflowRun(rust::WorkflowMethod::PhysicalHotkey, a, sdk)[0] != 0;
}
void PhysicalKeys::run() {
    PhysicalKeysSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::PhysicalWorker, {}, sdk);
}
} // namespace current
#undef PHYSICAL_FIXTURE_CLASS
inline QJsonObject snapshot() {
    QJsonArray down, press, injected;
    for (int i = 0; i < 256; ++i) {
        down.append(bool(physical[i]));
        press.append(QString::number(physicalPress[i].load()));
        injected.append(QString::number(injectedPress[i].load()));
    }
    return {{"down", down}, {"press", press}, {"injected", injected}, {"active", bool(hookActive)}};
}
template <class P> QJsonObject lifecycle() {
    QJsonObject result;
    try {
        P p;
        QJsonArray reads;
        physical[64] = true;
        physicalPress[64] = INT64_C(0x123456789abcde01);
        injectedPress[64] = INT64_C(0x123456789abcde00);
        for (int vk : {-1, 0, 64, 255, 256})
            reads.append(QJsonObject{{"vk", vk}, {"down", p.down(vk)}, {"hotkey", p.physicalHotkey(vk)}});
        injectedPress[64] = INT64_C(0x123456789abcde02);
        reads.append(p.physicalHotkey(64));
        result["reads"] = reads;
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    }
    return result;
}
inline QJsonObject outcome(int scene, bool life, bool rust) {
    State state(scene);
    active = &state;
    for (int i = 0; i < 256; ++i) {
        physical[i] = i % 3 == 0;
        physicalPress[i] = state.clock + i;
        injectedPress[i] = state.clock - i;
    }
    hookActive = life && scene % 17 == 0;
    QJsonObject result;
    if (life)
        result = rust ? lifecycle<current::PhysicalKeys>() : lifecycle<original::PhysicalKeys>();
    else {
        KBDLLHOOKSTRUCT key{};
        key.vkCode = DWORD((scene / 3) % 300);
        key.flags = (scene / 19) % 2 ? LLKHF_INJECTED : 0;
        const WPARAM messages[]{WM_KEYDOWN, WM_SYSKEYDOWN, WM_KEYUP, WM_SYSKEYUP, WM_NULL};
        const WPARAM message = messages[(scene / 7) % 5];
        const int code = scene % 3 == 0 ? HC_ACTION : scene % 3 == 1 ? 3 : -1;
        result["result"] =
            QString::number(rust ? current::keyHook(code, message, reinterpret_cast<LPARAM>(&key))
                                 : original::keyHook(code, message, reinterpret_cast<LPARAM>(&key)));
    }
    result["state"] = snapshot();
    result["trace"] = state.trace;
    return result;
}
} // namespace okww::desktop_physical_test
