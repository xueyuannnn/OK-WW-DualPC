#include "desktop.hpp"
#include <array>
#include <map>

namespace okww {
namespace {
struct DpiScope {
    DPI_AWARENESS_CONTEXT previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ~DpiScope() { if (previous) SetThreadDpiAwarenessContext(previous); }
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
QString processPath(DWORD id) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);
    if (!process) return {};
    wchar_t text[32768]{};
    DWORD size = 32768;
    const bool ok = QueryFullProcessImageNameW(process, 0, text, &size);
    CloseHandle(process);
    return ok ? QString::fromWCharArray(text, int(size)) : QString{};
}
QRect clientRect(HWND window) {
    DpiScope dpi;
    RECT rect{};
    POINT origin{};
    if (!IsWindowVisible(window) || IsIconic(window) || !GetClientRect(window, &rect) ||
        !ClientToScreen(window, &origin) || rect.right < 2 || rect.bottom < 2)
        fail("目标窗口不可见或已最小化，请恢复窗口后重新连接");
    return {origin.x, origin.y, rect.right, rect.bottom};
}
std::array<std::atomic<bool>, 256> physical{};
std::array<std::atomic<i64>, 256> physicalPress{}, injectedPress{};
std::atomic<bool> hookActive{false};
LRESULT CALLBACK keyHook(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION) {
        const auto *key = reinterpret_cast<KBDLLHOOKSTRUCT *>(data);
        if (key->vkCode < physical.size()) {
            const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
            if (!(key->flags & LLKHF_INJECTED)) {
                physical[key->vkCode] = down;
                if (down) physicalPress[key->vkCode] = nowNs();
            } else if (down) injectedPress[key->vkCode] = nowNs();
        }
    }
    return CallNextHookEx(nullptr, code, message, data);
}
}
QJsonArray desktopTargets(bool windowsOnly) {
    DpiScope dpi;
    QJsonArray targets;
    if (!windowsOnly) EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL {
            MONITORINFOEXW info{}; info.cbSize = sizeof(info);
            if (!GetMonitorInfoW(monitor, &info)) return TRUE;
            const auto &r = info.rcMonitor;
            auto name = QString::fromWCharArray(info.szDevice);
            double refresh=0;
            try {refresh=displayRefreshRate(name);} catch (const std::exception &) {}
            reinterpret_cast<QJsonArray *>(context)->append(QJsonObject{
                {"kind", "monitor"}, {"id", name}, {"primary",bool(info.dwFlags & MONITORINFOF_PRIMARY)},
                {"refresh_hz",refresh},
                {"label", QString("显示器 %1 · %2 × %3%4").arg(name).arg(r.right-r.left).arg(r.bottom-r.top)
                    .arg(info.dwFlags & MONITORINFOF_PRIMARY ? " · 主屏" : "")}});
            return TRUE;
        }, reinterpret_cast<LPARAM>(&targets));
    EnumWindows([](HWND window, LPARAM context) -> BOOL {
        DWORD id = 0; GetWindowThreadProcessId(window, &id);
        if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) || id == GetCurrentProcessId() ||
            window == GetShellWindow() || (GetWindowLongPtrW(window,GWL_EXSTYLE) & (WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE))) return TRUE;
        const auto title = windowTitle(window);
        if (title.isEmpty()) return TRUE;
        const auto exe = processPath(id);
        if (exe.isEmpty()) return TRUE;
        reinterpret_cast<QJsonArray *>(context)->append(QJsonObject{
            {"kind", "window"}, {"hwnd", QString::number(quintptr(window))}, {"pid", int(id)},
            {"title", title}, {"class", windowClass(window)}, {"exe", exe},
            {"label", title + " · " + QFileInfo(exe).fileName()}});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&targets));
    return targets;
}
double displayRefreshRate(const QString &device) {
    for (int attempt=0; attempt<3; ++attempt) {
        UINT32 pathCount=0, modeCount=0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pathCount,&modeCount) != ERROR_SUCCESS) break;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        const auto result=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&pathCount,paths.data(),&modeCount,modes.data(),nullptr);
        if (result==ERROR_INSUFFICIENT_BUFFER) continue;
        if (result!=ERROR_SUCCESS) break;
        for (UINT32 i=0;i<pathCount;++i) {
            const auto &path=paths[i];
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            source.header.size=sizeof(source);
            source.header.adapterId=path.sourceInfo.adapterId; source.header.id=path.sourceInfo.id;
            if (DisplayConfigGetDeviceInfo(&source.header)!=ERROR_SUCCESS ||
                device!=QString::fromWCharArray(source.viewGdiDeviceName)) continue;
            auto rate=path.targetInfo.refreshRate;
            if (path.targetInfo.modeInfoIdx<modeCount) {
                const auto &mode=modes[path.targetInfo.modeInfoIdx];
                if (mode.infoType==DISPLAYCONFIG_MODE_INFO_TYPE_TARGET)
                    rate=mode.targetMode.targetVideoSignalInfo.vSyncFreq;
            }
            if (rate.Numerator && rate.Denominator) return double(rate.Numerator)/rate.Denominator;
        }
        break;
    }
    DEVMODEW mode{}; mode.dmSize=sizeof(mode);
    if (EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(device.utf16()),ENUM_CURRENT_SETTINGS,&mode,0) &&
        mode.dmDisplayFrequency>1) return mode.dmDisplayFrequency;
    fail("无法读取显示器刷新率，请检查显示设置后重新连接");
}
QJsonObject dxgiDisplay(const Config &config) {
    const auto selected=config.s("capture.dxgi_monitor");
    QJsonObject primary;
    for (auto value:desktopTargets()) {
        const auto display=value.toObject();
        if (display["kind"].toString()!="monitor") continue;
        if (display["id"].toString()==selected) return display;
        if (display["primary"].toBool()) primary=display;
    }
    if (!selected.isEmpty()) fail("所选 DXGI 显示器未连接，请重新选择");
    if (primary.isEmpty()) fail("没有可用的主显示器");
    return primary;
}
QRect foregroundClientRect() {
    const auto window=GetForegroundWindow();
    if (!window) fail("当前没有可接收输入的前台窗口");
    return clientRect(window);
}
QJsonObject desktopCaptureTarget(const Config &config) {
    if (config.s("capture.local_backend")=="DXGI") return dxgiDisplay(config);
    const auto target=config.get("capture.local_target").toObject();
    return target.isEmpty() ? dxgiDisplay(Config::defaults()) : target;
}
DesktopTarget::DesktopTarget(QJsonObject identity) : identity_(std::move(identity)) {
    if (identity_["kind"].toString() == "monitor") {
        struct Search { QString name; HMONITOR result = nullptr; } search{identity_["id"].toString()};
        EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL {
            auto &search = *reinterpret_cast<Search *>(context);
            MONITORINFOEXW info{}; info.cbSize = sizeof(info);
            if (GetMonitorInfoW(monitor, &info) && search.name == QString::fromWCharArray(info.szDevice))
                search.result = monitor;
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        monitor_ = search.result;
        if (!monitor_) fail("所选显示器未连接，请刷新后重新选择");
    } else if (identity_["kind"].toString() == "window") {
        auto matches = [&](const QJsonObject &candidate) {
            return candidate["exe"].toString().compare(identity_["exe"].toString(), Qt::CaseInsensitive) == 0 &&
                candidate["title"] == identity_["title"] && candidate["class"] == identity_["class"];
        };
        QJsonArray candidates;
        const auto pinned = reinterpret_cast<HWND>(quintptr(identity_["hwnd"].toString().toULongLong()));
        DWORD pinnedProcess = 0;
        if (pinned) GetWindowThreadProcessId(pinned,&pinnedProcess);
        // Keep the selected handle when its process and saved identity still
        // match. A changed document/window title need not break a live target.
        if (pinned && IsWindow(pinned) && pinnedProcess == DWORD(identity_["pid"].toInt()) &&
            windowClass(pinned) == identity_["class"].toString() &&
            processPath(pinnedProcess).compare(identity_["exe"].toString(),Qt::CaseInsensitive) == 0) {
            window_ = pinned; process_ = pinnedProcess; rect(); return;
        }
        for (auto value : desktopTargets(true)) {
            const auto candidate = value.toObject();
            if (!matches(candidate)) continue;
            candidates.append(candidate);
            if (candidate["hwnd"] == identity_["hwnd"] && candidate["pid"] == identity_["pid"]) {
                candidates = QJsonArray{candidate}; break;
            }
        }
        if (candidates.size() != 1) fail("未找到唯一匹配的目标窗口，请刷新窗口列表后重新选择");
        const auto candidate = candidates[0].toObject();
        window_ = reinterpret_cast<HWND>(quintptr(candidate["hwnd"].toString().toULongLong()));
        process_ = DWORD(candidate["pid"].toInt());
    } else fail("请选择显示器或窗口");
    rect();
}
QRect DesktopTarget::rect() const {
    DpiScope dpi;
    if (window_) {
        DWORD process = 0; GetWindowThreadProcessId(window_, &process);
        if (!IsWindow(window_) || process != process_ || windowClass(window_) != identity_["class"].toString())
            fail("目标窗口已关闭，请重新选择并连接");
        return clientRect(window_);
    }
    MONITORINFO info{}; info.cbSize = sizeof(info);
    if (!monitor_ || !GetMonitorInfoW(monitor_, &info)) fail("所选显示器已断开");
    return {info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right-info.rcMonitor.left,
            info.rcMonitor.bottom-info.rcMonitor.top};
}
QPoint desktopAbsolute(QPoint point, QRect desktop) {
    if (desktop.width() < 2 || desktop.height() < 2 || !desktop.contains(point)) fail("鼠标坐标超出本机桌面");
    return {int(std::llround(double(point.x()-desktop.x()) * 65535 / (desktop.width()-1))),
            int(std::llround(double(point.y()-desktop.y()) * 65535 / (desktop.height()-1)))};
}
bool DesktopTarget::foreground() const {
    return window_ && GetAncestor(GetForegroundWindow(),GA_ROOT)==window_;
}
QRect virtualDesktopRect() {
    DpiScope dpi;
    return {GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN),
            GetSystemMetrics(SM_CXVIRTUALSCREEN),GetSystemMetrics(SM_CYVIRTUALSCREEN)};
}
INPUT hidInput(int usage, bool down) {
    // USB HID usage -> set-1 scan code. Game keys follow physical positions,
    // independent of the active Windows text input layout.
    static const std::map<int, int> codes{
        {4,0x1e},{5,0x30},{6,0x2e},{7,0x20},{8,0x12},{9,0x21},{10,0x22},{11,0x23},{12,0x17},
        {13,0x24},{14,0x25},{15,0x26},{16,0x32},{17,0x31},{18,0x18},{19,0x19},{20,0x10},
        {21,0x13},{22,0x1f},{23,0x14},{24,0x16},{25,0x2f},{26,0x11},{27,0x2d},{28,0x15},{29,0x2c},
        {30,2},{31,3},{32,4},{33,5},{34,6},{35,7},{36,8},{37,9},{38,10},{39,11},
        {40,0x1c},{41,1},{42,0xe},{43,0xf},{44,0x39},{45,0xc},{46,0xd},{47,0x1a},{48,0x1b},
        {49,0x2b},{50,0x2b},{51,0x27},{52,0x28},{53,0x29},{54,0x33},{55,0x34},{56,0x35},{57,0x3a},
        {58,0x3b},{59,0x3c},{60,0x3d},{61,0x3e},{62,0x3f},{63,0x40},{64,0x41},{65,0x42},
        {66,0x43},{67,0x44},{68,0x57},{69,0x58},{70,0x137},{71,0x46},
        {73,0x152},{74,0x147},{75,0x149},{76,0x153},{77,0x14f},{78,0x151},
        {79,0x14d},{80,0x14b},{81,0x150},{82,0x148},{83,0x45},{84,0x135},{85,0x37},{86,0x4a},
        {87,0x4e},{88,0x11c},{89,0x4f},{90,0x50},{91,0x51},{92,0x4b},{93,0x4c},{94,0x4d},
        {95,0x47},{96,0x48},{97,0x49},{98,0x52},{99,0x53},{100,0x56},{101,0x15d},
        {224,0x1d},{225,0x2a},{226,0x38},{227,0x15b},{228,0x11d},{229,0x36},{230,0x138},{231,0x15c}};
    const auto code = codes.find(usage);
    if (code == codes.end()) fail("SendInput 不支持此 HID 键码：" + QString::number(usage));
    INPUT input{}; input.type = INPUT_KEYBOARD;
    input.ki.wScan = WORD(code->second & 0xff);
    input.ki.dwFlags = KEYEVENTF_SCANCODE | (code->second & 0x100 ? KEYEVENTF_EXTENDEDKEY : 0) |
        (down ? 0 : KEYEVENTF_KEYUP);
    input.ki.dwExtraInfo = localInputTag;
    return input;
}
PhysicalKeys::PhysicalKeys() {
    if (hookActive.exchange(true)) fail("本机按键监听已经运行");
    thread_ = std::thread([this] {
        MSG message{}; PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
        threadId_ = GetCurrentThreadId();
        for (auto &key : physical) key = false;
        for (auto &time : physicalPress) time = 0;
        for (auto &time : injectedPress) time = 0;
        const auto hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyHook, GetModuleHandleW(nullptr), 0);
        failed_ = !hook; ready_ = true;
        if (hook) {
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            UnhookWindowsHookEx(hook);
        }
    });
    while (!ready_) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (failed_) {
        thread_.join(); hookActive = false;
        fail("无法监听本机物理按键，SendInput 连接已取消");
    }
}
PhysicalKeys::~PhysicalKeys() {
    if (threadId_) PostThreadMessageW(threadId_, WM_QUIT, 0, 0);
    if (thread_.joinable()) thread_.join();
    hookActive = false;
}
bool PhysicalKeys::down(int vk) const { return vk >= 0 && vk < 256 && physical[vk]; }
bool PhysicalKeys::physicalHotkey(int vk) const {
    return vk >= 0 && vk < 256 && physicalPress[vk].load() > injectedPress[vk].load();
}
} // namespace okww
