#pragma once
#include "desktop.hpp"
#include "platform_workflow.hpp"
namespace okww::desktop_identity_test {
struct State {
    int scene, phase = 0;
    QJsonArray trace;
    explicit State(int s) : scene(s) {}
};
inline thread_local State *active = nullptr;
inline HWND handle(int index) {
    return reinterpret_cast<HWND>(UINT64_C(0x123456789abcdef0) + uint64_t(index));
}
inline QString handleText(int index) { return QString::number(quintptr(handle(index))); }
inline QJsonValue windowValue(HWND value) { return QString::number(quintptr(value)); }
struct DpiScope {
    DpiScope() { active->trace.append("dpi"); }
    ~DpiScope() { active->trace.append("dpi-end"); }
};
inline DWORD GetWindowThreadProcessId(HWND window, DWORD *out) {
    active->trace.append(QJsonObject{{"pid", windowValue(window)}});
    *out = active->phase == 1 && active->scene % 29 == 0 ? 999 : 456;
    return 5;
}
inline BOOL IsWindow(HWND window) {
    active->trace.append(QJsonObject{{"is-window", windowValue(window)}});
    return active->scene % 7 != 0 && !(active->phase == 1 && active->scene % 31 == 0);
}
inline QString windowClass(HWND window) {
    active->trace.append(QJsonObject{{"class", windowValue(window)}});
    if (active->scene % 53 == 0)
        throw std::runtime_error("class fixture");
    return active->scene % 11 ? "GameWindow" : "OtherWindow";
}
inline QString processPath(DWORD pid) {
    active->trace.append(QJsonObject{{"path", qint64(pid)}});
    if (active->scene % 59 == 0)
        throw 27;
    return active->scene % 17 ? "C:/game.EXE" : "C:/other.exe";
}
inline QRect clientRect(HWND window) {
    active->trace.append(QJsonObject{{"client", windowValue(window)}});
    if (active->scene % 61 == 0)
        throw std::runtime_error("client fixture");
    return {-1920, 0, 1920, 1080};
}
inline HWND GetForegroundWindow() {
    active->trace.append("foreground");
    return handle(active->scene % 3);
}
inline HWND GetAncestor(HWND window, UINT flag) {
    active->trace.append(QJsonObject{{"ancestor", windowValue(window)}, {"flag", int(flag)}});
    return window;
}
template <class T> BOOL GetMonitorInfoW(HMONITOR monitor, T *info) {
    const int index = int(quintptr(monitor));
    active->trace.append(QJsonObject{{"monitor-info", index},
                                     {"extended", std::is_same_v<T, MONITORINFOEXW>},
                                     {"size", int(info->cbSize)}});
    if (active->scene % 43 == 0)
        throw std::runtime_error("monitor fixture");
    if (active->scene % 19 == 0)
        return FALSE;
    info->rcMonitor = {1920 * (index - 1), 0, 1920 * index, 1080};
    if constexpr (std::is_same_v<T, MONITORINFOEXW>) {
        const QString name = active->scene % 5 == 0 ? "DISPLAY1" : QString("DISPLAY%1").arg(index);
        const auto text = name.toStdWString();
        std::copy(text.begin(), text.end(), info->szDevice);
        info->szDevice[text.size()] = 0;
    }
    return TRUE;
}
inline BOOL EnumDisplayMonitors(HDC, LPCRECT, MONITORENUMPROC callback, LPARAM context) {
    active->trace.append("enum-monitors");
    for (int i = 1; i <= 2; ++i)
        callback(reinterpret_cast<HMONITOR>(uintptr_t(i)), nullptr, nullptr, context);
    return FALSE;
}
inline QJsonArray desktopTargets(bool windowsOnly = false) {
    active->trace.append(QJsonObject{{"targets", windowsOnly}});
    QJsonArray values;
    const int count = active->scene % 4;
    for (int i = 0; i < count; ++i) {
        QJsonObject value{{"kind", "window"},
                          {"hwnd", handleText(i)},
                          {"pid", 456},
                          {"exe", active->scene % 13 == 0 ? "C:/other.exe" : "c:/GAME.exe"},
                          {"title", i == 1 && active->scene % 2 ? "Other" : "Game"},
                          {"class", "GameWindow"}};
        if (active->scene % 23 == 0)
            value["class"] = 37;
        if (active->scene % 41 == 0)
            value["pid"] = "456";
        values.append(value);
    }
    return values;
}
#define DESKTOP_TARGET_FIXTURE_CLASS                                                                         \
    class DesktopTarget {                                                                                    \
      public:                                                                                                \
        QJsonObject identity_;                                                                               \
        HWND window_ = nullptr;                                                                              \
        HMONITOR monitor_ = nullptr;                                                                         \
        DWORD process_ = 0;                                                                                  \
        explicit DesktopTarget(QJsonObject);                                                                 \
        QRect rect() const;                                                                                  \
        bool foreground() const;                                                                             \
    };
namespace original {
DESKTOP_TARGET_FIXTURE_CLASS
DesktopTarget::DesktopTarget(QJsonObject identity) : identity_(std::move(identity)) {
    if (identity_["kind"].toString() == "monitor") {
        struct Search {
            QString name;
            HMONITOR result = nullptr;
        } search{identity_["id"].toString()};
        desktop_identity_test::EnumDisplayMonitors(
            nullptr, nullptr,
            [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL {
                auto &search = *reinterpret_cast<Search *>(context);
                MONITORINFOEXW info{};
                info.cbSize = sizeof(info);
                if (desktop_identity_test::GetMonitorInfoW(monitor, &info) &&
                    search.name == QString::fromWCharArray(info.szDevice))
                    search.result = monitor;
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&search));
        monitor_ = search.result;
        if (!monitor_)
            fail("所选显示器未连接，请刷新后重新选择");
    } else if (identity_["kind"].toString() == "window") {
        auto matches = [&](const QJsonObject &candidate) {
            return candidate["exe"].toString().compare(identity_["exe"].toString(), Qt::CaseInsensitive) ==
                       0 &&
                   candidate["title"] == identity_["title"] && candidate["class"] == identity_["class"];
        };
        QJsonArray candidates;
        const auto pinned = reinterpret_cast<HWND>(quintptr(identity_["hwnd"].toString().toULongLong()));
        DWORD pinnedProcess = 0;
        if (pinned)
            desktop_identity_test::GetWindowThreadProcessId(pinned, &pinnedProcess);
        // Keep the selected handle when its process and saved identity still
        // match. A changed document/window title need not break a live target.
        if (pinned && desktop_identity_test::IsWindow(pinned) &&
            pinnedProcess == DWORD(identity_["pid"].toInt()) &&
            windowClass(pinned) == identity_["class"].toString() &&
            processPath(pinnedProcess).compare(identity_["exe"].toString(), Qt::CaseInsensitive) == 0) {
            window_ = pinned;
            process_ = pinnedProcess;
            rect();
            return;
        }
        for (auto value : desktopTargets(true)) {
            const auto candidate = value.toObject();
            if (!matches(candidate))
                continue;
            candidates.append(candidate);
            if (candidate["hwnd"] == identity_["hwnd"] && candidate["pid"] == identity_["pid"]) {
                candidates = QJsonArray{candidate};
                break;
            }
        }
        if (candidates.size() != 1)
            fail("未找到唯一匹配的目标窗口，请刷新窗口列表后重新选择");
        const auto candidate = candidates[0].toObject();
        window_ = reinterpret_cast<HWND>(quintptr(candidate["hwnd"].toString().toULongLong()));
        process_ = DWORD(candidate["pid"].toInt());
    } else
        fail("请选择显示器或窗口");
    rect();
}
QRect DesktopTarget::rect() const {
    DpiScope dpi;
    if (window_) {
        DWORD process = 0;
        desktop_identity_test::GetWindowThreadProcessId(window_, &process);
        if (!desktop_identity_test::IsWindow(window_) || process != process_ ||
            windowClass(window_) != identity_["class"].toString())
            fail("目标窗口已关闭，请重新选择并连接");
        return clientRect(window_);
    }
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!monitor_ || !desktop_identity_test::GetMonitorInfoW(monitor_, &info))
        fail("所选显示器已断开");
    return {info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right - info.rcMonitor.left,
            info.rcMonitor.bottom - info.rcMonitor.top};
}
bool DesktopTarget::foreground() const {
    return window_ && desktop_identity_test::GetAncestor(desktop_identity_test::GetForegroundWindow(),
                                                         GA_ROOT) == window_;
}
} // namespace original
namespace current {
DESKTOP_TARGET_FIXTURE_CLASS
static void desktopIdentityFailure(int kind) {
    switch (kind) {
    case 0:
        fail("所选显示器未连接，请刷新后重新选择");
    case 1:
        fail("未找到唯一匹配的目标窗口，请刷新窗口列表后重新选择");
    case 2:
        fail("请选择显示器或窗口");
    case 3:
        fail("目标窗口已关闭，请重新选择并连接");
    case 4:
        fail("所选显示器已断开");
    default:
        fail("Rust 桌面目标错误宿主无效");
    }
}
struct DesktopMonitorSearch {
    QString name;
    HMONITOR result = nullptr;
};
struct DesktopIdentityMonitorSdk {
    DesktopMonitorSearch &search;
    HMONITOR monitor;
    MONITORINFOEXW info{};
    void operator()(rust::WorkflowOp op, const double *, double *out) {
        switch (op) {
        case rust::WorkflowOp::DesktopIdentityMonitorInfo:
            info.cbSize = sizeof(info);
            out[0] = desktop_identity_test::GetMonitorInfoW(monitor, &info) != 0;
            break;
        case rust::WorkflowOp::DesktopIdentityMonitorNameMatches:
            out[0] = search.name == QString::fromWCharArray(info.szDevice);
            break;
        case rust::WorkflowOp::DesktopIdentityMonitorSelect:
            search.result = monitor;
            break;
        default:
            fail("Rust 目标显示器枚举宿主无效");
        }
    }
};
struct DesktopIdentitySdk {
    DesktopTarget &d;
    DesktopMonitorSearch search;
    QJsonArray candidates;
    HWND pinned = nullptr;
    DWORD pinnedProcess = 0;
    QJsonArray windows;
    QJsonObject candidate;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::DesktopIdentityIsMonitor:
            out[0] = d.identity_["kind"].toString() == "monitor";
            break;
        case rust::WorkflowOp::DesktopIdentityIsWindow:
            out[0] = d.identity_["kind"].toString() == "window";
            break;
        case rust::WorkflowOp::DesktopIdentitySearchName:
            search.name = d.identity_["id"].toString();
            break;
        case rust::WorkflowOp::DesktopIdentityEnumerateMonitors:
            desktop_identity_test::EnumDisplayMonitors(
                nullptr, nullptr,
                [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL {
                    DesktopIdentityMonitorSdk sdk{*reinterpret_cast<DesktopMonitorSearch *>(context),
                                                  monitor};
                    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopIdentityMonitor, {}, sdk);
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&search));
            break;
        case rust::WorkflowOp::DesktopIdentityStoreMonitor:
            d.monitor_ = search.result;
            break;
        case rust::WorkflowOp::DesktopIdentityHasMonitor:
            out[0] = d.monitor_ != nullptr;
            break;
        case rust::WorkflowOp::DesktopIdentityReadPinned:
            pinned = reinterpret_cast<HWND>(quintptr(d.identity_["hwnd"].toString().toULongLong()));
            break;
        case rust::WorkflowOp::DesktopIdentityHasPinned:
            out[0] = pinned != nullptr;
            break;
        case rust::WorkflowOp::DesktopIdentityReadPinnedProcess:
            desktop_identity_test::GetWindowThreadProcessId(pinned, &pinnedProcess);
            break;
        case rust::WorkflowOp::DesktopIdentityPinnedIsWindow:
            out[0] = desktop_identity_test::IsWindow(pinned) != 0;
            break;
        case rust::WorkflowOp::DesktopIdentityPinnedProcessMatches:
            out[0] = pinnedProcess == DWORD(d.identity_["pid"].toInt());
            break;
        case rust::WorkflowOp::DesktopIdentityPinnedClassMatches:
            out[0] = windowClass(pinned) == d.identity_["class"].toString();
            break;
        case rust::WorkflowOp::DesktopIdentityPinnedExeMatches:
            out[0] =
                processPath(pinnedProcess).compare(d.identity_["exe"].toString(), Qt::CaseInsensitive) == 0;
            break;
        case rust::WorkflowOp::DesktopIdentityStorePinnedWindow:
            d.window_ = pinned;
            break;
        case rust::WorkflowOp::DesktopIdentityStorePinnedProcess:
            d.process_ = pinnedProcess;
            break;
        case rust::WorkflowOp::DesktopIdentityValidateRect:
            d.rect();
            break;
        case rust::WorkflowOp::DesktopIdentityListWindows:
            windows = desktopTargets(true);
            break;
        case rust::WorkflowOp::DesktopIdentityWindowCount:
            out[0] = double(windows.size());
            break;
        case rust::WorkflowOp::DesktopIdentitySelectWindow:
            candidate = windows.at(qsizetype(a[0])).toObject();
            break;
        case rust::WorkflowOp::DesktopIdentityMatchesCandidate:
            out[0] = rust::platformWorkflowRun(rust::WorkflowMethod::DesktopIdentityMatch, {}, *this)[0];
            break;
        case rust::WorkflowOp::DesktopIdentityCandidateExeMatches:
            out[0] =
                candidate["exe"].toString().compare(d.identity_["exe"].toString(), Qt::CaseInsensitive) == 0;
            break;
        case rust::WorkflowOp::DesktopIdentityCandidateTitleMatches:
            out[0] = candidate["title"] == d.identity_["title"];
            break;
        case rust::WorkflowOp::DesktopIdentityCandidateClassMatches:
            out[0] = candidate["class"] == d.identity_["class"];
            break;
        case rust::WorkflowOp::DesktopIdentityAppendCandidate:
            candidates.append(candidate);
            break;
        case rust::WorkflowOp::DesktopIdentitySameHandle:
            out[0] = candidate["hwnd"] == d.identity_["hwnd"];
            break;
        case rust::WorkflowOp::DesktopIdentitySameProcess:
            out[0] = candidate["pid"] == d.identity_["pid"];
            break;
        case rust::WorkflowOp::DesktopIdentityKeepCandidate:
            candidates = QJsonArray{candidate};
            break;
        case rust::WorkflowOp::DesktopIdentityDropCandidate:
            candidate = {};
            break;
        case rust::WorkflowOp::DesktopIdentityDropWindows:
            windows = {};
            break;
        case rust::WorkflowOp::DesktopIdentityCandidateCount:
            out[0] = double(candidates.size());
            break;
        case rust::WorkflowOp::DesktopIdentityUniqueCandidate:
            candidate = candidates[0].toObject();
            break;
        case rust::WorkflowOp::DesktopIdentityStoreCandidateWindow:
            d.window_ = reinterpret_cast<HWND>(quintptr(candidate["hwnd"].toString().toULongLong()));
            break;
        case rust::WorkflowOp::DesktopIdentityStoreCandidateProcess:
            d.process_ = DWORD(candidate["pid"].toInt());
            break;
        case rust::WorkflowOp::DesktopIdentityDropCandidates:
            candidates = {};
            break;
        case rust::WorkflowOp::DesktopIdentityFailure:
            desktopIdentityFailure(int(a[0]));
            break;
        default:
            fail("Rust 桌面目标选择宿主无效");
        }
    }
};
DesktopTarget::DesktopTarget(QJsonObject identity) : identity_(std::move(identity)) {
    DesktopIdentitySdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopIdentityCreate, {}, sdk);
}
struct DesktopIdentityRectSdk {
    const DesktopTarget &d;
    DWORD process = 0;
    MONITORINFO info{};
    QRect value;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::DesktopIdentityRectHasWindow:
            out[0] = d.window_ != nullptr;
            break;
        case rust::WorkflowOp::DesktopIdentityRectReadProcess:
            desktop_identity_test::GetWindowThreadProcessId(d.window_, &process);
            break;
        case rust::WorkflowOp::DesktopIdentityRectIsWindow:
            out[0] = desktop_identity_test::IsWindow(d.window_) != 0;
            break;
        case rust::WorkflowOp::DesktopIdentityRectProcessMatches:
            out[0] = process == d.process_;
            break;
        case rust::WorkflowOp::DesktopIdentityRectClassMatches:
            out[0] = windowClass(d.window_) == d.identity_["class"].toString();
            break;
        case rust::WorkflowOp::DesktopIdentityRectClient:
            value = clientRect(d.window_);
            break;
        case rust::WorkflowOp::DesktopIdentityRectHasMonitor:
            out[0] = d.monitor_ != nullptr;
            break;
        case rust::WorkflowOp::DesktopIdentityRectMonitorInfo:
            info.cbSize = sizeof(info);
            out[0] = desktop_identity_test::GetMonitorInfoW(d.monitor_, &info) != 0;
            break;
        case rust::WorkflowOp::DesktopIdentityRectMonitor:
            value = QRect(info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right - info.rcMonitor.left,
                          info.rcMonitor.bottom - info.rcMonitor.top);
            break;
        case rust::WorkflowOp::DesktopIdentityRectForeground:
            out[0] = desktop_identity_test::GetAncestor(desktop_identity_test::GetForegroundWindow(),
                                                        GA_ROOT) == d.window_;
            break;
        case rust::WorkflowOp::DesktopIdentityFailure:
            desktopIdentityFailure(int(a[0]));
            break;
        default:
            fail("Rust 桌面目标状态宿主无效");
        }
    }
};
QRect DesktopTarget::rect() const {
    DpiScope dpi;
    DesktopIdentityRectSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopIdentityRect, {}, sdk);
    return sdk.value;
}
bool DesktopTarget::foreground() const {
    DesktopIdentityRectSdk sdk{*this};
    return rust::platformWorkflowRun(rust::WorkflowMethod::DesktopIdentityForeground, {}, sdk)[0] != 0;
}
} // namespace current
#undef DESKTOP_TARGET_FIXTURE_CLASS
inline QJsonArray rectValue(QRect value) { return {value.x(), value.y(), value.width(), value.height()}; }
template <class D> QJsonObject run() {
    QJsonObject identity;
    const int scene = active->scene;
    if (scene % 3 == 0)
        identity = {{"kind", "monitor"}, {"id", scene % 2 ? "DISPLAY1" : "DISPLAY2"}};
    else if (scene % 3 == 1) {
        identity = {{"kind", "window"},
                    {"hwnd", scene % 4 == 3 ? "0" : handleText(scene % 3)},
                    {"pid", scene % 13 ? 456 : 999},
                    {"exe", "C:/GAME.exe"},
                    {"class", scene % 23 == 0 ? QJsonValue(37) : QJsonValue("GameWindow")},
                    {"title", scene % 5 ? "Game" : "Changed title"}};
    } else
        identity = {{"kind", "other"}};
    QJsonObject result;
    try {
        D target(identity);
        result["window"] = windowValue(target.window_);
        result["monitor"] = QString::number(quintptr(target.monitor_));
        result["pid"] = qint64(target.process_);
        active->phase = 1;
        try {
            result["rect"] = rectValue(target.rect());
        } catch (const std::exception &e) {
            result["rect_error"] = errorText(e);
        } catch (...) {
            result["rect_error"] = "unknown";
        }
        active->phase = 2;
        result["foreground"] = target.foreground();
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    } catch (...) {
        result["error"] = "unknown";
    }
    return result;
}
inline QJsonObject outcome(int scene, bool rust) {
    State state(scene);
    active = &state;
    auto result = rust ? run<current::DesktopTarget>() : run<original::DesktopTarget>();
    result["trace"] = state.trace;
    return result;
}
} // namespace okww::desktop_identity_test
