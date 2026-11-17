#pragma once
#include "desktop.hpp"
#include "platform_workflow.hpp"
namespace okww::desktop_targets_test {
struct State {
    int scene;
    QJsonArray trace;
    explicit State(int s) : scene(s) {}
};
inline thread_local State *active = nullptr;
inline HWND handle(int index) {
    return reinterpret_cast<HWND>(UINT64_C(0x123456789abcdef0) + uint64_t(index));
}
inline int index(HWND window) { return int(quintptr(window) - UINT64_C(0x123456789abcdef0)); }
struct DpiScope {
    DpiScope() { active->trace.append("dpi"); }
    ~DpiScope() { active->trace.append("dpi-end"); }
};
inline BOOL EnumDisplayMonitors(HDC, LPCRECT, MONITORENUMPROC callback, LPARAM context) {
    active->trace.append("enum-monitors");
    for (int i = 1; i <= 3; ++i)
        callback(reinterpret_cast<HMONITOR>(uintptr_t(i)), nullptr, nullptr, context);
    return FALSE;
}
inline BOOL GetMonitorInfoW(HMONITOR monitor, MONITORINFOEXW *info) {
    const int i = int(quintptr(monitor));
    active->trace.append(QJsonObject{{"monitor-info", i}, {"size", int(info->cbSize)}});
    if ((active->scene + i) % 11 == 0)
        return FALSE;
    info->rcMonitor = {1920 * (i - 1), 0, 1920 * i, 1080};
    info->dwFlags = (active->scene + i) % 3 == 0 ? MONITORINFOF_PRIMARY : 0;
    const auto text = QString("DISPLAY%1").arg(i).toStdWString();
    std::copy(text.begin(), text.end(), info->szDevice);
    info->szDevice[text.size()] = 0;
    return TRUE;
}
inline double displayRefreshRate(const QString &name) {
    active->trace.append(QJsonObject{{"refresh", name}});
    if (active->scene % 13 == 0)
        throw std::runtime_error("refresh fixture");
    if (active->scene % 17 == 0)
        throw 27;
    return active->scene % 2 ? 239.997 : 539.998;
}
inline BOOL EnumWindows(WNDENUMPROC callback, LPARAM context) {
    active->trace.append("enum-windows");
    for (int i = 1; i <= 3; ++i)
        callback(handle(i), context);
    return FALSE;
}
inline DWORD GetWindowThreadProcessId(HWND window, DWORD *out) {
    const int i = index(window);
    active->trace.append(QJsonObject{{"pid", i}});
    *out = (active->scene + i) % 7 == 0 ? 42 : 100 + i;
    return 3;
}
inline BOOL IsWindowVisible(HWND window) {
    const int i = index(window);
    active->trace.append(QJsonObject{{"visible", i}});
    return (active->scene + i) % 5 != 0;
}
inline HWND GetWindow(HWND window, UINT flag) {
    const int i = index(window);
    active->trace.append(QJsonObject{{"owner", i}, {"flag", int(flag)}});
    return (active->scene + i) % 11 == 0 ? handle(4) : nullptr;
}
inline DWORD GetCurrentProcessId() {
    active->trace.append("own-pid");
    return 42;
}
inline HWND GetShellWindow() {
    active->trace.append("shell");
    return handle(3);
}
inline LONG_PTR GetWindowLongPtrW(HWND window, int flag) {
    const int i = index(window);
    active->trace.append(QJsonObject{{"style", i}, {"flag", flag}});
    return (active->scene + i) % 13 == 0   ? WS_EX_TOOLWINDOW
           : (active->scene + i) % 17 == 0 ? WS_EX_NOACTIVATE
                                           : 0;
}
inline QString windowTitle(HWND window) {
    const int i = index(window);
    active->trace.append(QJsonObject{{"title", i}});
    if ((active->scene + i) % 19 == 0)
        return {};
    return QString("游戏窗口 %1").arg(i);
}
inline QString processPath(DWORD pid) {
    active->trace.append(QJsonObject{{"path", int(pid)}});
    if ((active->scene + int(pid)) % 23 == 0)
        return {};
    return "C:/Game/游戏.exe";
}
inline QString windowClass(HWND window) {
    const int i = index(window);
    active->trace.append(QJsonObject{{"class", i}});
    if ((active->scene + i) % 29 == 0)
        throw std::runtime_error("class fixture");
    return "GameWindow";
}
namespace original {
QJsonArray desktopTargets(bool windowsOnly) {
    DpiScope dpi;
    QJsonArray targets;
    if (!windowsOnly)
        desktop_targets_test::EnumDisplayMonitors(
            nullptr, nullptr,
            [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL {
                MONITORINFOEXW info{};
                info.cbSize = sizeof(info);
                if (!desktop_targets_test::GetMonitorInfoW(monitor, &info))
                    return TRUE;
                const auto &r = info.rcMonitor;
                auto name = QString::fromWCharArray(info.szDevice);
                double refresh = 0;
                try {
                    refresh = displayRefreshRate(name);
                } catch (const std::exception &) {
                }
                reinterpret_cast<QJsonArray *>(context)->append(
                    QJsonObject{{"kind", "monitor"},
                                {"id", name},
                                {"primary", bool(info.dwFlags & MONITORINFOF_PRIMARY)},
                                {"refresh_hz", refresh},
                                {"label", QString("显示器 %1 · %2 × %3%4")
                                              .arg(name)
                                              .arg(r.right - r.left)
                                              .arg(r.bottom - r.top)
                                              .arg(info.dwFlags & MONITORINFOF_PRIMARY ? " · 主屏" : "")}});
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&targets));
    desktop_targets_test::EnumWindows(
        [](HWND window, LPARAM context) -> BOOL {
            DWORD id = 0;
            desktop_targets_test::GetWindowThreadProcessId(window, &id);
            if (!desktop_targets_test::IsWindowVisible(window) ||
                desktop_targets_test::GetWindow(window, GW_OWNER) ||
                id == desktop_targets_test::GetCurrentProcessId() ||
                window == desktop_targets_test::GetShellWindow() ||
                (desktop_targets_test::GetWindowLongPtrW(window, GWL_EXSTYLE) &
                 (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)))
                return TRUE;
            const auto title = windowTitle(window);
            if (title.isEmpty())
                return TRUE;
            const auto exe = processPath(id);
            if (exe.isEmpty())
                return TRUE;
            reinterpret_cast<QJsonArray *>(context)->append(
                QJsonObject{{"kind", "window"},
                            {"hwnd", QString::number(quintptr(window))},
                            {"pid", int(id)},
                            {"title", title},
                            {"class", windowClass(window)},
                            {"exe", exe},
                            {"label", title + " · " + QFileInfo(exe).fileName()}});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&targets));
    return targets;
}
} // namespace original
namespace current {
struct DesktopTargetsMonitorSdk {
    QJsonArray &targets;
    HMONITOR monitor;
    MONITORINFOEXW info{};
    QString name;
    double refresh = 0;
    void operator()(rust::WorkflowOp op, const double *, double *out) {
        switch (op) {
        case rust::WorkflowOp::DesktopTargetsMonitorInfo:
            info.cbSize = sizeof(info);
            out[0] = desktop_targets_test::GetMonitorInfoW(monitor, &info) != 0;
            break;
        case rust::WorkflowOp::DesktopTargetsMonitorName:
            name = QString::fromWCharArray(info.szDevice);
            break;
        case rust::WorkflowOp::DesktopTargetsMonitorRefresh:
            refresh = displayRefreshRate(name);
            break;
        case rust::WorkflowOp::DesktopTargetsAppendMonitor: {
            const auto &r = info.rcMonitor;
            targets.append(
                QJsonObject{{"kind", "monitor"},
                            {"id", name},
                            {"primary", bool(info.dwFlags & MONITORINFOF_PRIMARY)},
                            {"refresh_hz", refresh},
                            {"label", QString("显示器 %1 · %2 × %3%4")
                                          .arg(name)
                                          .arg(r.right - r.left)
                                          .arg(r.bottom - r.top)
                                          .arg(info.dwFlags & MONITORINFOF_PRIMARY ? " · 主屏" : "")}});
            break;
        }
        default:
            fail("Rust 显示器列表回调宿主无效");
        }
    }
};
struct DesktopTargetsWindowSdk {
    QJsonArray &targets;
    HWND window;
    DWORD id = 0;
    QString title, exe;
    void operator()(rust::WorkflowOp op, const double *, double *out) {
        switch (op) {
        case rust::WorkflowOp::DesktopTargetsWindowProcess:
            desktop_targets_test::GetWindowThreadProcessId(window, &id);
            break;
        case rust::WorkflowOp::DesktopTargetsWindowVisible:
            out[0] = desktop_targets_test::IsWindowVisible(window) != 0;
            break;
        case rust::WorkflowOp::DesktopTargetsWindowOwned:
            out[0] = desktop_targets_test::GetWindow(window, GW_OWNER) != nullptr;
            break;
        case rust::WorkflowOp::DesktopTargetsWindowOwnProcess:
            out[0] = id == desktop_targets_test::GetCurrentProcessId();
            break;
        case rust::WorkflowOp::DesktopTargetsWindowShell:
            out[0] = window == desktop_targets_test::GetShellWindow();
            break;
        case rust::WorkflowOp::DesktopTargetsWindowExcludedStyle:
            out[0] = (desktop_targets_test::GetWindowLongPtrW(window, GWL_EXSTYLE) &
                      (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) != 0;
            break;
        case rust::WorkflowOp::DesktopTargetsWindowTitle:
            title = windowTitle(window);
            break;
        case rust::WorkflowOp::DesktopTargetsWindowTitleEmpty:
            out[0] = title.isEmpty();
            break;
        case rust::WorkflowOp::DesktopTargetsWindowExe:
            exe = processPath(id);
            break;
        case rust::WorkflowOp::DesktopTargetsWindowExeEmpty:
            out[0] = exe.isEmpty();
            break;
        case rust::WorkflowOp::DesktopTargetsAppendWindow:
            targets.append(QJsonObject{{"kind", "window"},
                                       {"hwnd", QString::number(quintptr(window))},
                                       {"pid", int(id)},
                                       {"title", title},
                                       {"class", windowClass(window)},
                                       {"exe", exe},
                                       {"label", title + " · " + QFileInfo(exe).fileName()}});
            break;
        default:
            fail("Rust 窗口列表回调宿主无效");
        }
    }
};
struct DesktopTargetsSdk {
    QJsonArray targets;
    void operator()(rust::WorkflowOp op, const double *, double *) {
        switch (op) {
        case rust::WorkflowOp::DesktopTargetsEnumerateMonitors:
            desktop_targets_test::EnumDisplayMonitors(
                nullptr, nullptr,
                [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL {
                    DesktopTargetsMonitorSdk sdk{*reinterpret_cast<QJsonArray *>(context), monitor};
                    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopTargetsMonitor, {}, sdk);
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&targets));
            break;
        case rust::WorkflowOp::DesktopTargetsEnumerateWindows:
            desktop_targets_test::EnumWindows(
                [](HWND window, LPARAM context) -> BOOL {
                    DesktopTargetsWindowSdk sdk{*reinterpret_cast<QJsonArray *>(context), window};
                    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopTargetsWindow, {}, sdk);
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&targets));
            break;
        default:
            fail("Rust 桌面目标枚举宿主无效");
        }
    }
};
QJsonArray desktopTargets(bool windowsOnly) {
    DpiScope dpi;
    DesktopTargetsSdk sdk;
    const std::array<double, 1> args{double(windowsOnly)};
    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopTargets, args, sdk);
    return std::move(sdk.targets);
}
} // namespace current
inline QJsonObject outcome(int scene, bool windowsOnly, bool rust) {
    State state(scene);
    active = &state;
    QJsonObject result;
    try {
        result["targets"] =
            rust ? current::desktopTargets(windowsOnly) : original::desktopTargets(windowsOnly);
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    } catch (...) {
        result["error"] = "unknown";
    }
    result["trace"] = state.trace;
    return result;
}
} // namespace okww::desktop_targets_test
