#include "desktop.hpp"
#include "desktop_input_abi.hpp"
#include "platform_workflow.hpp"
#include <array>
#include <map>

namespace okww {
namespace {
struct DpiScope {
    DPI_AWARENESS_CONTEXT previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ~DpiScope() {
        if (previous)
            SetThreadDpiAwarenessContext(previous);
    }
};
QString windowTitle(HWND window) {
    wchar_t text[1024]{};
    GetWindowTextW(window, text, 1024);
    return QString::fromWCharArray(text);
}
QString windowClass(HWND window) {
    wchar_t text[256]{};
    GetClassNameW(window, text, 256);
    return QString::fromWCharArray(text);
}

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
            process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);
            break;
        case rust::WorkflowOp::DesktopProcessExists:
            out[0] = process != nullptr;
            break;
        case rust::WorkflowOp::DesktopProcessQuery:
            out[0] = QueryFullProcessImageNameW(process, 0, text, &size) != 0;
            break;
        case rust::WorkflowOp::DesktopProcessClose:
            CloseHandle(process);
            break;
        case rust::WorkflowOp::DesktopProcessText:
            value = QString::fromWCharArray(text, int(size));
            break;
        default:
            fail("Rust 进程路径宿主无效");
        }
    }
};
QString processPath(DWORD id) {
    DesktopProcessSdk sdk{id};
    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopProcessPath, {}, sdk);
    return sdk.value;
}

struct DesktopClientSdk {
    HWND window;
    RECT rect{};
    POINT origin{};
    QRect value;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::DesktopClientVisible:
            out[0] = IsWindowVisible(window) != 0;
            break;
        case rust::WorkflowOp::DesktopClientIconic:
            out[0] = IsIconic(window) != 0;
            break;
        case rust::WorkflowOp::DesktopClientGetRect:
            out[0] = GetClientRect(window, &rect) != 0;
            break;
        case rust::WorkflowOp::DesktopClientOrigin:
            out[0] = ClientToScreen(window, &origin) != 0;
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
QRect clientRect(HWND window) {
    DpiScope dpi;
    DesktopClientSdk sdk{window};
    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopClientRect, {}, sdk);
    return sdk.value;
}

std::array<std::atomic<bool>, 256> physical{};
std::array<std::atomic<i64>, 256> physicalPress{}, injectedPress{};
std::atomic<bool> hookActive{false};

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
            value = CallNextHookEx(nullptr, code, message, data);
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

} // namespace

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
            out[0] = GetMonitorInfoW(monitor, &info) != 0;
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
            GetWindowThreadProcessId(window, &id);
            break;
        case rust::WorkflowOp::DesktopTargetsWindowVisible:
            out[0] = IsWindowVisible(window) != 0;
            break;
        case rust::WorkflowOp::DesktopTargetsWindowOwned:
            out[0] = GetWindow(window, GW_OWNER) != nullptr;
            break;
        case rust::WorkflowOp::DesktopTargetsWindowOwnProcess:
            out[0] = id == GetCurrentProcessId();
            break;
        case rust::WorkflowOp::DesktopTargetsWindowShell:
            out[0] = window == GetShellWindow();
            break;
        case rust::WorkflowOp::DesktopTargetsWindowExcludedStyle:
            out[0] = (GetWindowLongPtrW(window, GWL_EXSTYLE) & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) != 0;
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
            EnumDisplayMonitors(
                nullptr, nullptr,
                [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL {
                    DesktopTargetsMonitorSdk sdk{*reinterpret_cast<QJsonArray *>(context), monitor};
                    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopTargetsMonitor, {}, sdk);
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&targets));
            break;
        case rust::WorkflowOp::DesktopTargetsEnumerateWindows:
            EnumWindows(
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
            out[0] =
                GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) == ERROR_SUCCESS;
            break;
        case rust::WorkflowOp::DesktopRefreshAllocatePaths:
            paths = std::vector<DISPLAYCONFIG_PATH_INFO>(pathCount);
            break;
        case rust::WorkflowOp::DesktopRefreshAllocateModes:
            modes = std::vector<DISPLAYCONFIG_MODE_INFO>(modeCount);
            break;
        case rust::WorkflowOp::DesktopRefreshQuery:
            out[0] = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount,
                                        modes.data(), nullptr);
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
            out[0] = DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS;
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
            out[0] = EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(device.utf16()), ENUM_CURRENT_SETTINGS,
                                            &fallback, 0) != 0;
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
double displayRefreshRate(const QString &device) {
    DesktopRefreshSdk sdk{device};
    return rust::platformWorkflowRun(rust::WorkflowMethod::DesktopDisplayRefresh, {}, sdk)[0];
}

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
QJsonObject dxgiDisplay(const Config &config) {
    DesktopDisplaySdk sdk{config};
    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopDxgiDisplay, {}, sdk);
    return sdk.value;
}

struct DesktopForegroundSdk {
    HWND window = nullptr;
    QRect value;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::DesktopForegroundRead:
            window = GetForegroundWindow();
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
QRect foregroundClientRect() {
    DesktopForegroundSdk sdk;
    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopForegroundClient, {}, sdk);
    return sdk.value;
}

struct DesktopCaptureTargetSdk {
    const Config &config;
    QJsonObject target, value;
    void operator()(rust::WorkflowOp op, const double *, double *out) {
        switch (op) {
        case rust::WorkflowOp::DesktopTargetCaptureDxgi:
            out[0] = config.s("capture.local_backend") == "DXGI";
            break;
        case rust::WorkflowOp::DesktopTargetConfiguredDisplay:
            value = dxgiDisplay(config);
            break;
        case rust::WorkflowOp::DesktopTargetLocalIdentity:
            target = config.get("capture.local_target").toObject();
            break;
        case rust::WorkflowOp::DesktopTargetIdentityEmpty:
            out[0] = target.isEmpty();
            break;
        case rust::WorkflowOp::DesktopTargetDefaultDisplay:
            value = dxgiDisplay(Config::defaults());
            break;
        case rust::WorkflowOp::DesktopTargetReturnIdentity:
            value = target;
            break;
        default:
            fail("Rust 本机截图目标宿主无效");
        }
    }
};
QJsonObject desktopCaptureTarget(const Config &config) {
    DesktopCaptureTargetSdk sdk{config};
    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopCaptureTarget, {}, sdk);
    return sdk.value;
}

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
            out[0] = GetMonitorInfoW(monitor, &info) != 0;
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
            EnumDisplayMonitors(
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
            GetWindowThreadProcessId(pinned, &pinnedProcess);
            break;
        case rust::WorkflowOp::DesktopIdentityPinnedIsWindow:
            out[0] = IsWindow(pinned) != 0;
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
            GetWindowThreadProcessId(d.window_, &process);
            break;
        case rust::WorkflowOp::DesktopIdentityRectIsWindow:
            out[0] = IsWindow(d.window_) != 0;
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
            out[0] = GetMonitorInfoW(d.monitor_, &info) != 0;
            break;
        case rust::WorkflowOp::DesktopIdentityRectMonitor:
            value = QRect(info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right - info.rcMonitor.left,
                          info.rcMonitor.bottom - info.rcMonitor.top);
            break;
        case rust::WorkflowOp::DesktopIdentityRectForeground:
            out[0] = GetAncestor(GetForegroundWindow(), GA_ROOT) == d.window_;
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

QPoint desktopAbsolute(QPoint point, QRect desktop) {
    std::array<int32_t, 2> value{};
    if (!rust::okww_rs_desktop_absolute(point.x(), point.y(), desktop.x(), desktop.y(), desktop.width(),
                                        desktop.height(), desktop.contains(point), value.data()))
        fail("鼠标坐标超出本机桌面");
    return {value[0], value[1]};
}
bool DesktopTarget::foreground() const {
    DesktopIdentityRectSdk sdk{*this};
    return rust::platformWorkflowRun(rust::WorkflowMethod::DesktopIdentityForeground, {}, sdk)[0] != 0;
}

QRect virtualDesktopRect() {
    DpiScope dpi;
    return {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
            GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN)};
}
INPUT hidInput(int usage, bool down) {
    std::array<uint32_t, 2> value{};
    if (!rust::okww_rs_desktop_hid(usage, down, value.data()))
        fail("SendInput 不支持此 HID 键码：" + QString::number(usage));
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wScan = WORD(value[0]);
    input.ki.dwFlags = value[1];
    input.ki.dwExtraInfo = localInputTag;
    return input;
}

struct DesktopEnvironmentSdk {
    HWND window = nullptr;
    DWORD pid = 0;
    int modifiers = 0;
    QJsonObject value;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::DesktopEnvironmentForeground:
            window = GetAncestor(GetForegroundWindow(), GA_ROOT);
            break;
        case rust::WorkflowOp::DesktopEnvironmentProcess:
            GetWindowThreadProcessId(window, &pid);
            break;
        case rust::WorkflowOp::DesktopEnvironmentKeyDown: {
            static const int keys[]{VK_LCONTROL, VK_LSHIFT, VK_LMENU, VK_LWIN,
                                    VK_RCONTROL, VK_RSHIFT, VK_RMENU, VK_RWIN};
            out[0] = (GetAsyncKeyState(keys[int(a[0])]) & 0x8000) != 0;
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
                     {"own_window", pid == GetCurrentProcessId()}};
            break;
        default:
            fail("Rust 本机输入环境宿主无效");
        }
    }
};
QJsonObject localInputEnvironment() {
    DesktopEnvironmentSdk sdk;
    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopInputEnvironment, {}, sdk);
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
            p.thread_ = std::thread(&PhysicalKeys::run, &p);
            break;
        case rust::WorkflowOp::PhysicalReady:
            out[0] = bool(p.ready_);
            break;
        case rust::WorkflowOp::PhysicalWaitReady:
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
            PostThreadMessageW(p.threadId_, WM_QUIT, 0, 0);
            break;
        case rust::WorkflowOp::PhysicalThreadJoinable:
            out[0] = p.thread_.joinable();
            break;
        case rust::WorkflowOp::PhysicalPeekMessage:
            PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
            break;
        case rust::WorkflowOp::PhysicalStoreThreadId:
            p.threadId_ = GetCurrentThreadId();
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
            hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyHook, GetModuleHandleW(nullptr), 0);
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
            out[0] = GetMessageW(&message, nullptr, 0, 0);
            break;
        case rust::WorkflowOp::PhysicalTranslateMessage:
            TranslateMessage(&message);
            break;
        case rust::WorkflowOp::PhysicalDispatchMessage:
            DispatchMessageW(&message);
            break;
        case rust::WorkflowOp::PhysicalUnhook:
            UnhookWindowsHookEx(hook);
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
void PhysicalKeys::run() {
    PhysicalKeysSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::PhysicalWorker, {}, sdk);
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

} // namespace okww
