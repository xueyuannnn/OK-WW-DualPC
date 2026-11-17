#pragma once
#include "desktop.hpp"
#include "platform_workflow.hpp"
namespace okww::desktop_platform_test {
struct State {
    int scene, query = 0;
    QJsonArray trace;
    explicit State(int s) : scene(s) {}
};
inline thread_local State *active = nullptr;
inline HANDLE OpenProcess(DWORD access, BOOL, DWORD id) {
    active->trace.append(QJsonObject{{"open", qint64(id)}, {"access", qint64(access)}});
    return active->scene % 17 ? reinterpret_cast<HANDLE>(uintptr_t(0x123456789abcdef0)) : nullptr;
}
inline BOOL QueryFullProcessImageNameW(HANDLE, DWORD, wchar_t *text, DWORD *size) {
    active->trace.append(QJsonObject{{"query-path", qint64(*size)}});
    const auto path = QString::fromUtf16(u"C:/Game/游戏\0extra.exe", 19).toStdWString();
    std::copy(path.begin(), path.end(), text);
    *size = DWORD(path.size());
    return active->scene % 13 != 0;
}
inline BOOL CloseHandle(HANDLE handle) {
    active->trace.append(QJsonObject{{"close", QString::number(quintptr(handle))}});
    return FALSE;
}
inline BOOL IsWindowVisible(HWND) {
    active->trace.append("visible");
    return active->scene % 7 != 0;
}
inline BOOL IsIconic(HWND) {
    active->trace.append("iconic");
    return active->scene % 11 == 0;
}
inline BOOL GetClientRect(HWND, RECT *out) {
    active->trace.append("client-rect");
    *out = {0, 0, active->scene % 5 == 0 ? 1 : 1920, active->scene % 3 == 0 ? 0 : 1080};
    return active->scene % 13 != 0;
}
inline BOOL ClientToScreen(HWND, POINT *out) {
    active->trace.append("client-origin");
    *out = {-1920, 23};
    return active->scene % 17 != 0;
}
inline HWND GetForegroundWindow() {
    active->trace.append("foreground");
    return active->scene % 19 ? reinterpret_cast<HWND>(uintptr_t(0x123456789abcdef1)) : nullptr;
}
inline HWND GetAncestor(HWND window, UINT flag) {
    active->trace.append(QJsonObject{{"ancestor", int(flag)}});
    return window;
}
inline DWORD GetWindowThreadProcessId(HWND, DWORD *out) {
    active->trace.append("window-pid");
    *out = active->scene % 23 == 0 ? 42 : 0xf1234567;
    return 10;
}
inline SHORT GetAsyncKeyState(int key) {
    active->trace.append(QJsonObject{{"async-key", key}});
    return (active->scene + key) % 3 == 0 ? SHORT(0x8000) : SHORT(1);
}
inline DWORD GetCurrentProcessId() {
    active->trace.append("own-pid");
    return 42;
}
struct DpiScope {
    DpiScope() { active->trace.append("dpi"); }
    ~DpiScope() { active->trace.append("dpi-end"); }
};
inline QString windowTitle(HWND) {
    active->trace.append("title");
    if (active->scene % 29 == 0)
        throw std::runtime_error("title fixture");
    return "游戏窗口";
}
inline QString windowClass(HWND) {
    active->trace.append("class");
    return "UnrealWindow";
}
inline LONG GetDisplayConfigBufferSizes(UINT32 flags, UINT32 *paths, UINT32 *modes) {
    active->trace.append(QJsonObject{{"buffer-sizes", int(flags)}});
    *paths = UINT32(active->scene % 4);
    *modes = 2;
    return active->scene % 17 ? ERROR_SUCCESS : ERROR_INVALID_PARAMETER;
}
inline LONG QueryDisplayConfig(UINT32, UINT32 *pathCount, DISPLAYCONFIG_PATH_INFO *paths, UINT32 *modeCount,
                               DISPLAYCONFIG_MODE_INFO *modes, void *) {
    const int query = ++active->query;
    active->trace.append(
        QJsonObject{{"display-query", query}, {"paths", int(*pathCount)}, {"modes", int(*modeCount)}});
    if (active->scene % 7 < query && active->scene % 7 < 3)
        return ERROR_INSUFFICIENT_BUFFER;
    if (active->scene % 11 == 0)
        return ERROR_INVALID_PARAMETER;
    for (UINT32 i = 0; i < *pathCount; ++i) {
        paths[i].sourceInfo.id = 100 + i;
        paths[i].sourceInfo.adapterId = {123, 456};
        paths[i].targetInfo.refreshRate = {active->scene % 13 ? 60000u : 0u, active->scene % 19 ? 1001u : 0u};
        paths[i].targetInfo.modeInfoIdx = (active->scene + i) % 3 == 0 ? UINT32_MAX : i % 2;
    }
    for (UINT32 i = 0; i < *modeCount; ++i) {
        modes[i].infoType = (active->scene + i) % 2 ? DISPLAYCONFIG_MODE_INFO_TYPE_TARGET
                                                    : DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE;
        modes[i].targetMode.targetVideoSignalInfo.vSyncFreq = {active->scene % 23 ? 144000u : 0u,
                                                               active->scene % 29 ? 1000u : 0u};
    }
    return ERROR_SUCCESS;
}
inline LONG DisplayConfigGetDeviceInfo(DISPLAYCONFIG_DEVICE_INFO_HEADER *header) {
    active->trace.append(QJsonObject{{"source-name", int(header->id)},
                                     {"type", int(header->type)},
                                     {"size", int(header->size)},
                                     {"adapter-low", int(header->adapterId.LowPart)},
                                     {"adapter-high", int(header->adapterId.HighPart)}});
    auto *source = reinterpret_cast<DISPLAYCONFIG_SOURCE_DEVICE_NAME *>(header);
    const QString name = (header->id + active->scene) % 3 ? "DISPLAY1" : "OTHER";
    const auto wide = name.toStdWString();
    std::copy(wide.begin(), wide.end(), source->viewGdiDeviceName);
    source->viewGdiDeviceName[wide.size()] = 0;
    return active->scene % 31 ? ERROR_SUCCESS : ERROR_INVALID_PARAMETER;
}
inline BOOL EnumDisplaySettingsExW(LPCWSTR device, DWORD mode, DEVMODEW *out, DWORD) {
    active->trace.append(QJsonObject{
        {"fallback", QString::fromWCharArray(device)}, {"mode", qint64(mode)}, {"size", out->dmSize}});
    const DWORD rates[]{0, 1, 60, 240, 540};
    out->dmDisplayFrequency = rates[active->scene % 5];
    return active->scene % 37 != 0;
}
inline QJsonArray desktopTargets(bool windowsOnly = false) {
    active->trace.append(QJsonObject{{"targets", windowsOnly}});
    if (active->scene % 41 == 0)
        throw std::runtime_error("targets fixture");
    if (active->scene % 43 == 0)
        throw 27;
    QJsonArray list{QJsonObject{{"kind", "window"}, {"id", "DISPLAY1"}}};
    if (active->scene % 17 == 0)
        return list;
    list.append(QJsonObject{{"kind", "monitor"}, {"id", "DISPLAY1"}, {"primary", active->scene % 3 != 0}});
    list.append(QJsonObject{{"kind", "monitor"}, {"id", "DISPLAY2"}, {"primary", active->scene % 7 == 0}});
    return list;
}
namespace original {
QString processPath(DWORD id) {
    HANDLE process = desktop_platform_test::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);
    if (!process)
        return {};
    wchar_t text[32768]{};
    DWORD size = 32768;
    const bool ok = desktop_platform_test::QueryFullProcessImageNameW(process, 0, text, &size);
    desktop_platform_test::CloseHandle(process);
    return ok ? QString::fromWCharArray(text, int(size)) : QString{};
}
QRect clientRect(HWND window) {
    DpiScope dpi;
    RECT rect{};
    POINT origin{};
    if (!desktop_platform_test::IsWindowVisible(window) || desktop_platform_test::IsIconic(window) ||
        !desktop_platform_test::GetClientRect(window, &rect) ||
        !desktop_platform_test::ClientToScreen(window, &origin) || rect.right < 2 || rect.bottom < 2)
        fail("目标窗口不可见或已最小化，请恢复窗口后重新连接");
    return {origin.x, origin.y, rect.right, rect.bottom};
}
double displayRefreshRate(const QString &device) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        UINT32 pathCount = 0, modeCount = 0;
        if (desktop_platform_test::GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount,
                                                               &modeCount) != ERROR_SUCCESS)
            break;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        const auto result = desktop_platform_test::QueryDisplayConfig(
            QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
        if (result == ERROR_INSUFFICIENT_BUFFER)
            continue;
        if (result != ERROR_SUCCESS)
            break;
        for (UINT32 i = 0; i < pathCount; ++i) {
            const auto &path = paths[i];
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            source.header.size = sizeof(source);
            source.header.adapterId = path.sourceInfo.adapterId;
            source.header.id = path.sourceInfo.id;
            if (desktop_platform_test::DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
                device != QString::fromWCharArray(source.viewGdiDeviceName))
                continue;
            auto rate = path.targetInfo.refreshRate;
            if (path.targetInfo.modeInfoIdx < modeCount) {
                const auto &mode = modes[path.targetInfo.modeInfoIdx];
                if (mode.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET)
                    rate = mode.targetMode.targetVideoSignalInfo.vSyncFreq;
            }
            if (rate.Numerator && rate.Denominator)
                return double(rate.Numerator) / rate.Denominator;
        }
        break;
    }
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (desktop_platform_test::EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(device.utf16()),
                                                      ENUM_CURRENT_SETTINGS, &mode, 0) &&
        mode.dmDisplayFrequency > 1)
        return mode.dmDisplayFrequency;
    fail("无法读取显示器刷新率，请检查显示设置后重新连接");
}
QJsonObject dxgiDisplay(const Config &config) {
    const auto selected = config.s("capture.dxgi_monitor");
    QJsonObject primary;
    for (auto value : desktopTargets()) {
        const auto display = value.toObject();
        if (display["kind"].toString() != "monitor")
            continue;
        if (display["id"].toString() == selected)
            return display;
        if (display["primary"].toBool())
            primary = display;
    }
    if (!selected.isEmpty())
        fail("所选 DXGI 显示器未连接，请重新选择");
    if (primary.isEmpty())
        fail("没有可用的主显示器");
    return primary;
}
QRect foregroundClientRect() {
    const auto window = desktop_platform_test::GetForegroundWindow();
    if (!window)
        fail("当前没有可接收输入的前台窗口");
    return clientRect(window);
}
QJsonObject desktopCaptureTarget(const Config &config) {
    if (config.s("capture.local_backend") == "DXGI")
        return desktop_platform_test::original::dxgiDisplay(config);
    const auto target = config.get("capture.local_target").toObject();
    return target.isEmpty() ? desktop_platform_test::original::dxgiDisplay(Config::defaults()) : target;
}
QJsonObject localInputEnvironment() {
    const auto window =
        desktop_platform_test::GetAncestor(desktop_platform_test::GetForegroundWindow(), GA_ROOT);
    DWORD pid = 0;
    desktop_platform_test::GetWindowThreadProcessId(window, &pid);
    int modifiers = 0;
    const int virtualKeys[]{VK_LCONTROL, VK_LSHIFT, VK_LMENU, VK_LWIN,
                            VK_RCONTROL, VK_RSHIFT, VK_RMENU, VK_RWIN};
    for (int i = 0; i < 8; ++i)
        if (desktop_platform_test::GetAsyncKeyState(virtualKeys[i]) & 0x8000)
            modifiers |= 1 << i;
    return {{"title", windowTitle(window)},
            {"class", windowClass(window)},
            {"pid", int(pid)},
            {"hwnd", QString::number(quintptr(window))},
            {"executable", QFileInfo(processPath(pid)).fileName()},
            {"modifiers", modifiers},
            {"own_window", pid == desktop_platform_test::GetCurrentProcessId()}};
}
} // namespace original
namespace current {
QString processPath(DWORD);
QRect clientRect(HWND);
QJsonObject dxgiDisplay(const Config &);
static void desktopPlatformFailure(int kind) {
    switch (kind) {
    case 0:
        fail("目标窗口不可见或已最小化，请恢复窗口后重新连接");
    case 1:
        fail("当前没有可接收输入的前台窗口");
    case 2:
        fail("无法读取显示器刷新率，请检查显示设置后重新连接");
    case 3:
        fail("所选 DXGI 显示器未连接，请重新选择");
    case 4:
        fail("没有可用的主显示器");
    default:
        fail("Rust 桌面平台错误宿主无效");
    }
}
struct DesktopProcessSdk {
    DWORD id;
    HANDLE process = nullptr;
    wchar_t text[32768]{};
    DWORD size = 32768;
    QString value;
    void operator()(rust::WorkflowOp op, const double *, double *out) {
        switch (op) {
        case rust::WorkflowOp::DesktopProcessOpen:
            process = desktop_platform_test::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);
            break;
        case rust::WorkflowOp::DesktopProcessExists:
            out[0] = process != nullptr;
            break;
        case rust::WorkflowOp::DesktopProcessQuery:
            out[0] = desktop_platform_test::QueryFullProcessImageNameW(process, 0, text, &size) != 0;
            break;
        case rust::WorkflowOp::DesktopProcessClose:
            desktop_platform_test::CloseHandle(process);
            break;
        case rust::WorkflowOp::DesktopProcessText:
            value = QString::fromWCharArray(text, int(size));
            break;
        default:
            fail("Rust 进程路径宿主无效");
        }
    }
};
struct DesktopClientSdk {
    HWND window;
    RECT rect{};
    POINT origin{};
    QRect value;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::DesktopClientVisible:
            out[0] = desktop_platform_test::IsWindowVisible(window) != 0;
            break;
        case rust::WorkflowOp::DesktopClientIconic:
            out[0] = desktop_platform_test::IsIconic(window) != 0;
            break;
        case rust::WorkflowOp::DesktopClientGetRect:
            out[0] = desktop_platform_test::GetClientRect(window, &rect) != 0;
            break;
        case rust::WorkflowOp::DesktopClientOrigin:
            out[0] = desktop_platform_test::ClientToScreen(window, &origin) != 0;
            break;
        case rust::WorkflowOp::DesktopClientWidth:
            out[0] = rect.right;
            break;
        case rust::WorkflowOp::DesktopClientHeight:
            out[0] = rect.bottom;
            break;
        case rust::WorkflowOp::DesktopClientResult:
            value = QRect(origin.x, origin.y, rect.right, rect.bottom);
            break;
        case rust::WorkflowOp::DesktopPlatformFailure:
            desktopPlatformFailure(int(a[0]));
            break;
        default:
            fail("Rust 窗口客户区宿主无效");
        }
    }
};
struct DesktopRefreshSdk {
    const QString &device;
    UINT32 pathCount = 0, modeCount = 0;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    const DISPLAYCONFIG_PATH_INFO *path = nullptr;
    DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
    DISPLAYCONFIG_RATIONAL rate{};
    const DISPLAYCONFIG_MODE_INFO *mode = nullptr;
    DEVMODEW fallback{};
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::DesktopRefreshResetCounts:
            pathCount = modeCount = 0;
            break;
        case rust::WorkflowOp::DesktopRefreshBufferSizes:
            out[0] = desktop_platform_test::GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount,
                                                                        &modeCount) == ERROR_SUCCESS;
            break;
        case rust::WorkflowOp::DesktopRefreshAllocatePaths:
            paths = std::vector<DISPLAYCONFIG_PATH_INFO>(pathCount);
            break;
        case rust::WorkflowOp::DesktopRefreshAllocateModes:
            modes = std::vector<DISPLAYCONFIG_MODE_INFO>(modeCount);
            break;
        case rust::WorkflowOp::DesktopRefreshQuery:
            out[0] = desktop_platform_test::QueryDisplayConfig(
                QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
            break;
        case rust::WorkflowOp::DesktopRefreshDropBuffers:
            modes = decltype(modes){};
            paths = decltype(paths){};
            break;
        case rust::WorkflowOp::DesktopRefreshPathCount:
            out[0] = pathCount;
            break;
        case rust::WorkflowOp::DesktopRefreshSelectPath:
            path = &paths[UINT32(a[0])];
            break;
        case rust::WorkflowOp::DesktopRefreshSourceRequest:
            source = {};
            source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            source.header.size = sizeof(source);
            source.header.adapterId = path->sourceInfo.adapterId;
            source.header.id = path->sourceInfo.id;
            break;
        case rust::WorkflowOp::DesktopRefreshGetSource:
            out[0] = desktop_platform_test::DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS;
            break;
        case rust::WorkflowOp::DesktopRefreshNameMatches:
            out[0] = device == QString::fromWCharArray(source.viewGdiDeviceName);
            break;
        case rust::WorkflowOp::DesktopRefreshPathRate:
            rate = path->targetInfo.refreshRate;
            break;
        case rust::WorkflowOp::DesktopRefreshModeIndexValid:
            out[0] = path->targetInfo.modeInfoIdx < modeCount;
            break;
        case rust::WorkflowOp::DesktopRefreshSelectMode:
            mode = &modes[path->targetInfo.modeInfoIdx];
            break;
        case rust::WorkflowOp::DesktopRefreshModeIsTarget:
            out[0] = mode->infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET;
            break;
        case rust::WorkflowOp::DesktopRefreshModeRate:
            rate = mode->targetMode.targetVideoSignalInfo.vSyncFreq;
            break;
        case rust::WorkflowOp::DesktopRefreshRateRatio:
            out[0] = rate.Numerator;
            out[1] = rate.Denominator;
            break;
        case rust::WorkflowOp::DesktopRefreshFallbackInit:
            fallback = {};
            fallback.dmSize = sizeof(fallback);
            break;
        case rust::WorkflowOp::DesktopRefreshFallbackQuery:
            out[0] = desktop_platform_test::EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(device.utf16()),
                                                                   ENUM_CURRENT_SETTINGS, &fallback, 0) != 0;
            break;
        case rust::WorkflowOp::DesktopRefreshFallbackRate:
            out[0] = fallback.dmDisplayFrequency;
            break;
        case rust::WorkflowOp::DesktopPlatformFailure:
            desktopPlatformFailure(int(a[0]));
            break;
        default:
            fail("Rust 显示器刷新率宿主无效");
        }
    }
};
struct DesktopDisplaySdk {
    const Config &config;
    QString selected;
    QJsonObject primary;
    QJsonArray targets;
    QJsonObject display, value;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::DesktopDisplaySelectedName:
            selected = config.s("capture.dxgi_monitor");
            break;
        case rust::WorkflowOp::DesktopDisplayListTargets:
            targets = desktopTargets();
            break;
        case rust::WorkflowOp::DesktopDisplayTargetCount:
            out[0] = double(targets.size());
            break;
        case rust::WorkflowOp::DesktopDisplaySelectTarget:
            display = targets.at(qsizetype(a[0])).toObject();
            break;
        case rust::WorkflowOp::DesktopDisplayIsMonitor:
            out[0] = display["kind"].toString() == "monitor";
            break;
        case rust::WorkflowOp::DesktopDisplayNameMatches:
            out[0] = display["id"].toString() == selected;
            break;
        case rust::WorkflowOp::DesktopDisplayReturnTarget:
            value = display;
            break;
        case rust::WorkflowOp::DesktopDisplayIsPrimary:
            out[0] = display["primary"].toBool();
            break;
        case rust::WorkflowOp::DesktopDisplaySavePrimary:
            primary = display;
            break;
        case rust::WorkflowOp::DesktopDisplayDropTarget:
            display = {};
            break;
        case rust::WorkflowOp::DesktopDisplayDropTargets:
            targets = {};
            break;
        case rust::WorkflowOp::DesktopDisplayNameEmpty:
            out[0] = selected.isEmpty();
            break;
        case rust::WorkflowOp::DesktopDisplayPrimaryEmpty:
            out[0] = primary.isEmpty();
            break;
        case rust::WorkflowOp::DesktopDisplayReturnPrimary:
            value = primary;
            break;
        case rust::WorkflowOp::DesktopPlatformFailure:
            desktopPlatformFailure(int(a[0]));
            break;
        default:
            fail("Rust DXGI 显示器选择宿主无效");
        }
    }
};
struct DesktopForegroundSdk {
    HWND window = nullptr;
    QRect value;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::DesktopForegroundRead:
            window = desktop_platform_test::GetForegroundWindow();
            break;
        case rust::WorkflowOp::DesktopForegroundExists:
            out[0] = window != nullptr;
            break;
        case rust::WorkflowOp::DesktopForegroundRect:
            value = clientRect(window);
            break;
        case rust::WorkflowOp::DesktopPlatformFailure:
            desktopPlatformFailure(int(a[0]));
            break;
        default:
            fail("Rust 前台客户区宿主无效");
        }
    }
};
struct DesktopCaptureTargetSdk {
    const Config &config;
    QJsonObject target, value;
    void operator()(rust::WorkflowOp op, const double *, double *out) {
        switch (op) {
        case rust::WorkflowOp::DesktopTargetCaptureDxgi:
            out[0] = config.s("capture.local_backend") == "DXGI";
            break;
        case rust::WorkflowOp::DesktopTargetConfiguredDisplay:
            value = desktop_platform_test::current::dxgiDisplay(config);
            break;
        case rust::WorkflowOp::DesktopTargetLocalIdentity:
            target = config.get("capture.local_target").toObject();
            break;
        case rust::WorkflowOp::DesktopTargetIdentityEmpty:
            out[0] = target.isEmpty();
            break;
        case rust::WorkflowOp::DesktopTargetDefaultDisplay:
            value = desktop_platform_test::current::dxgiDisplay(Config::defaults());
            break;
        case rust::WorkflowOp::DesktopTargetReturnIdentity:
            value = target;
            break;
        default:
            fail("Rust 本机截图目标宿主无效");
        }
    }
};
struct DesktopEnvironmentSdk {
    HWND window = nullptr;
    DWORD pid = 0;
    int modifiers = 0;
    QJsonObject value;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::DesktopEnvironmentForeground:
            window =
                desktop_platform_test::GetAncestor(desktop_platform_test::GetForegroundWindow(), GA_ROOT);
            break;
        case rust::WorkflowOp::DesktopEnvironmentProcess:
            desktop_platform_test::GetWindowThreadProcessId(window, &pid);
            break;
        case rust::WorkflowOp::DesktopEnvironmentKeyDown: {
            static const int keys[]{VK_LCONTROL, VK_LSHIFT, VK_LMENU, VK_LWIN,
                                    VK_RCONTROL, VK_RSHIFT, VK_RMENU, VK_RWIN};
            out[0] = (desktop_platform_test::GetAsyncKeyState(keys[int(a[0])]) & 0x8000) != 0;
            break;
        }
        case rust::WorkflowOp::DesktopEnvironmentAddModifier:
            modifiers |= 1 << int(a[0]);
            break;
        case rust::WorkflowOp::DesktopEnvironmentResult:
            value = {{"title", windowTitle(window)},
                     {"class", windowClass(window)},
                     {"pid", int(pid)},
                     {"hwnd", QString::number(quintptr(window))},
                     {"executable", QFileInfo(processPath(pid)).fileName()},
                     {"modifiers", modifiers},
                     {"own_window", pid == desktop_platform_test::GetCurrentProcessId()}};
            break;
        default:
            fail("Rust 本机输入环境宿主无效");
        }
    }
};
QString processPath(DWORD id) {
    DesktopProcessSdk sdk{id};
    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopProcessPath, {}, sdk);
    return sdk.value;
}
QRect clientRect(HWND window) {
    DpiScope dpi;
    DesktopClientSdk sdk{window};
    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopClientRect, {}, sdk);
    return sdk.value;
}
double displayRefreshRate(const QString &device) {
    DesktopRefreshSdk sdk{device};
    return rust::platformWorkflowRun(rust::WorkflowMethod::DesktopDisplayRefresh, {}, sdk)[0];
}
QJsonObject dxgiDisplay(const Config &config) {
    DesktopDisplaySdk sdk{config};
    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopDxgiDisplay, {}, sdk);
    return sdk.value;
}
QRect foregroundClientRect() {
    DesktopForegroundSdk sdk;
    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopForegroundClient, {}, sdk);
    return sdk.value;
}
QJsonObject desktopCaptureTarget(const Config &config) {
    DesktopCaptureTargetSdk sdk{config};
    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopCaptureTarget, {}, sdk);
    return sdk.value;
}
QJsonObject localInputEnvironment() {
    DesktopEnvironmentSdk sdk;
    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopInputEnvironment, {}, sdk);
    return sdk.value;
}
} // namespace current
inline QJsonArray rectValue(QRect value) { return {value.x(), value.y(), value.width(), value.height()}; }
inline QJsonObject outcome(int scene, int method, bool rust) {
    State state(scene);
    active = &state;
    QJsonObject result;
    Config config = Config::defaults();
    const QStringList names{"", "DISPLAY1", "DISPLAY2", "missing"};
    config.set("capture.dxgi_monitor", names[scene % 4]);
    config.set("capture.local_backend", scene % 2 ? "DXGI" : "WIN32");
    if (scene % 3 == 0)
        config.set("capture.local_target", QJsonObject{{"kind", "window"}, {"title", "保存的窗口"}});
    const HWND window = reinterpret_cast<HWND>(uintptr_t(0x123456789abcdef2));
    try {
        switch (method) {
        case 0: {
            auto value = rust ? current::processPath(DWORD(scene)) : original::processPath(DWORD(scene));
            result["text"] = value;
            result["null"] = value.isNull();
            break;
        }
        case 1:
            result["rect"] = rectValue(rust ? current::clientRect(window) : original::clientRect(window));
            break;
        case 2:
            result["rect"] =
                rectValue(rust ? current::foregroundClientRect() : original::foregroundClientRect());
            break;
        case 3:
            result["refresh"] =
                rust ? current::displayRefreshRate("DISPLAY1") : original::displayRefreshRate("DISPLAY1");
            break;
        case 4:
            result["display"] = rust ? current::dxgiDisplay(config) : original::dxgiDisplay(config);
            break;
        case 5:
            result["target"] =
                rust ? current::desktopCaptureTarget(config) : original::desktopCaptureTarget(config);
            break;
        case 6:
            result["environment"] =
                rust ? current::localInputEnvironment() : original::localInputEnvironment();
            break;
        }
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    } catch (...) {
        result["error"] = "unknown";
    }
    result["trace"] = state.trace;
    return result;
}
} // namespace okww::desktop_platform_test
