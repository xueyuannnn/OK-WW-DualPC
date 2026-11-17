#pragma once
#include "capture.hpp"
#include "capture_math_abi.hpp"
#include "platform_workflow.hpp"
namespace okww::desktop_helpers_test {
struct State {
    int scene, waits = 0, bitmaps = 0;
    i64 clock = 0;
    QJsonArray trace;
    std::atomic<bool> stop{false};
    std::array<uint8_t, 32> data{};
    HGDIOBJ selected = reinterpret_cast<HGDIOBJ>(uintptr_t(5));
    explicit State(int s) : scene(s) { stop = s % 11 == 0; }
};
inline thread_local State *active = nullptr;
inline std::chrono::steady_clock::time_point now() {
    active->trace.append(QJsonObject{{"now", QString::number(active->clock)}});
    auto value = std::chrono::steady_clock::time_point(std::chrono::nanoseconds(active->clock));
    active->clock += 1000000;
    return value;
}
inline void sleepUntil(std::chrono::steady_clock::time_point deadline) {
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline.time_since_epoch()).count();
    active->trace.append(QJsonObject{{"sleep", QString::number(ns)}});
    active->clock = ns;
    if (++active->waits >= 3)
        active->stop = true;
}
inline HANDLE CreateWaitableTimerExW(void *, void *, DWORD flags, DWORD access) {
    active->trace.append(QJsonObject{{"create-timer", int(flags)}, {"access", qint64(access)}});
    return active->scene % 7 ? reinterpret_cast<HANDLE>(uintptr_t(1)) : nullptr;
}
inline BOOL CloseHandle(HANDLE) {
    active->trace.append("close-timer");
    return FALSE;
}
inline BOOL SetWaitableTimer(HANDLE, const LARGE_INTEGER *due, LONG, void *, void *, BOOL) {
    active->trace.append(QJsonObject{{"due", QString::number(due->QuadPart)}});
    return active->scene % 5 ? TRUE : FALSE;
}
inline DWORD WaitForSingleObject(HANDLE, DWORD timeout) {
    active->trace.append(QJsonObject{{"wait", int(timeout)}});
    const int index = ++active->waits;
    if (active->scene % 3 == 0)
        return WAIT_FAILED;
    if (active->scene % 3 == 1 && index >= 3)
        return WAIT_OBJECT_0;
    if (index >= 3)
        active->stop = true;
    return WAIT_TIMEOUT;
}
inline HDC GetDC(HWND) {
    active->trace.append("get-screen");
    return active->scene % 7 ? reinterpret_cast<HDC>(uintptr_t(2)) : nullptr;
}
inline HDC CreateCompatibleDC(HDC) {
    active->trace.append("create-dc");
    return active->scene % 11 ? reinterpret_cast<HDC>(uintptr_t(3)) : nullptr;
}
inline HGDIOBJ SelectObject(HDC, HGDIOBJ value) {
    active->trace.append(QJsonObject{{"select", qint64(quintptr(value))}});
    return std::exchange(active->selected, value);
}
inline BOOL DeleteObject(HGDIOBJ value) {
    active->trace.append(QJsonObject{{"delete-bitmap", qint64(quintptr(value))}});
    return FALSE;
}
inline BOOL DeleteDC(HDC) {
    active->trace.append("delete-dc");
    return FALSE;
}
inline int ReleaseDC(HWND, HDC) {
    active->trace.append("release-screen");
    return 0;
}
inline HBITMAP CreateDIBSection(HDC, const BITMAPINFO *info, UINT, void **data, HANDLE, DWORD) {
    active->trace.append(QJsonObject{{"bitmap", ++active->bitmaps},
                                     {"width", int(info->bmiHeader.biWidth)},
                                     {"height", int(info->bmiHeader.biHeight)},
                                     {"bits", info->bmiHeader.biBitCount},
                                     {"planes", info->bmiHeader.biPlanes},
                                     {"compression", qint64(info->bmiHeader.biCompression)},
                                     {"header", int(info->bmiHeader.biSize)}});
    *data = active->scene % 17 ? active->data.data() : nullptr;
    return active->scene % 13 ? reinterpret_cast<HBITMAP>(uintptr_t(100 + active->bitmaps)) : nullptr;
}
namespace original {
struct FramePacer {
    HANDLE timer = desktop_helpers_test::CreateWaitableTimerExW(
        nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    ~FramePacer() {
        if (timer)
            desktop_helpers_test::CloseHandle(timer);
    }
    void wait(std::chrono::steady_clock::time_point deadline, const std::atomic<bool> &stop) {
        const auto remaining =
            std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - desktop_helpers_test::now())
                .count();
        if (remaining <= 0 || stop)
            return;
        LARGE_INTEGER due{};
        due.QuadPart = -std::max<i64>(1, remaining / 100);
        if (timer && desktop_helpers_test::SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
            while (!stop) {
                const auto result = desktop_helpers_test::WaitForSingleObject(timer, 20);
                if (result == WAIT_OBJECT_0)
                    return;
                if (result != WAIT_TIMEOUT)
                    fail("截图帧率定时器等待失败");
            }
        } else
            while (!stop && desktop_helpers_test::now() < deadline)
                desktop_helpers_test::sleepUntil(
                    std::min(deadline, desktop_helpers_test::now() + std::chrono::milliseconds(20)));
    }
};
struct GdiFrame {
    HDC screen = desktop_helpers_test::GetDC(nullptr),
        dc = screen ? desktop_helpers_test::CreateCompatibleDC(screen) : nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ old = nullptr;
    void *data = nullptr;
    QSize size;
    ~GdiFrame() {
        if (old)
            desktop_helpers_test::SelectObject(dc, old);
        if (bitmap)
            desktop_helpers_test::DeleteObject(bitmap);
        if (dc)
            desktop_helpers_test::DeleteDC(dc);
        if (screen)
            desktop_helpers_test::ReleaseDC(nullptr, screen);
    }
    void resize(QSize wanted) {
        if (!screen || !dc)
            fail("无法创建 Win32 截图上下文");
        if (size == wanted)
            return;
        if (old) {
            desktop_helpers_test::SelectObject(dc, old);
            old = nullptr;
        }
        if (bitmap) {
            desktop_helpers_test::DeleteObject(bitmap);
            bitmap = nullptr;
        }
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = wanted.width();
        info.bmiHeader.biHeight = -wanted.height();
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        bitmap = desktop_helpers_test::CreateDIBSection(screen, &info, DIB_RGB_COLORS, &data, nullptr, 0);
        if (!bitmap || !data)
            fail("Win32 截图缓冲区分配失败");
        old = desktop_helpers_test::SelectObject(dc, bitmap);
        size = wanted;
    }
};
} // namespace original
namespace current {
struct FramePacer {
    HANDLE timer = nullptr;
    FramePacer();
    ~FramePacer();
    void wait(std::chrono::steady_clock::time_point deadline, const std::atomic<bool> &stop);
};
struct DesktopPacerSdk {
    FramePacer &p;
    std::chrono::steady_clock::time_point deadline{};
    const std::atomic<bool> *stop = nullptr;
    i64 remaining = 0;
    LARGE_INTEGER due{};
    void operator()(rust::WorkflowOp op, const double *, double *out) {
        switch (op) {
        case rust::WorkflowOp::DesktopPacerCreateTimer:
            p.timer = desktop_helpers_test::CreateWaitableTimerExW(
                nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
            break;
        case rust::WorkflowOp::DesktopPacerHasTimer:
            out[0] = p.timer != nullptr;
            break;
        case rust::WorkflowOp::DesktopPacerCloseTimer:
            desktop_helpers_test::CloseHandle(p.timer);
            break;
        case rust::WorkflowOp::DesktopPacerReadRemaining:
            remaining =
                std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - desktop_helpers_test::now())
                    .count();
            break;
        case rust::WorkflowOp::DesktopPacerPositiveRemaining:
            out[0] = remaining > 0;
            break;
        case rust::WorkflowOp::DesktopPacerStopped:
            out[0] = bool(*stop);
            break;
        case rust::WorkflowOp::DesktopPacerPrepareDue:
            due.QuadPart = rust::okww_rs_capture_timer_due(remaining);
            break;
        case rust::WorkflowOp::DesktopPacerSetTimer:
            out[0] = desktop_helpers_test::SetWaitableTimer(p.timer, &due, 0, nullptr, nullptr, FALSE) != 0;
            break;
        case rust::WorkflowOp::DesktopPacerWaitTimer:
            out[0] = desktop_helpers_test::WaitForSingleObject(p.timer, 20);
            break;
        case rust::WorkflowOp::DesktopPacerBeforeDeadline:
            out[0] = desktop_helpers_test::now() < deadline;
            break;
        case rust::WorkflowOp::DesktopPacerSleep:
            desktop_helpers_test::sleepUntil(
                std::min(deadline, desktop_helpers_test::now() + std::chrono::milliseconds(20)));
            break;
        case rust::WorkflowOp::DesktopPacerFailure:
            fail("截图帧率定时器等待失败");
            break;
        default:
            fail("Rust 桌面采集定时器宿主无效");
        }
    }
};
FramePacer::FramePacer() {
    DesktopPacerSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopPacerCreate, {}, sdk);
}
FramePacer::~FramePacer() {
    DesktopPacerSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopPacerDestroy, {}, sdk);
}
void FramePacer::wait(std::chrono::steady_clock::time_point deadline, const std::atomic<bool> &stop) {
    DesktopPacerSdk sdk{*this, deadline, &stop};
    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopPacerWait, {}, sdk);
}
struct GdiFrame {
    HDC screen = nullptr, dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ old = nullptr;
    void *data = nullptr;
    QSize size;
    GdiFrame();
    ~GdiFrame();
    void resize(QSize wanted);
};
struct DesktopGdiSdk {
    GdiFrame &g;
    QSize wanted;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::DesktopGdiGetScreen:
            g.screen = desktop_helpers_test::GetDC(nullptr);
            break;
        case rust::WorkflowOp::DesktopGdiHasScreen:
            out[0] = g.screen != nullptr;
            break;
        case rust::WorkflowOp::DesktopGdiCreateDc:
            g.dc = desktop_helpers_test::CreateCompatibleDC(g.screen);
            break;
        case rust::WorkflowOp::DesktopGdiHasDc:
            out[0] = g.dc != nullptr;
            break;
        case rust::WorkflowOp::DesktopGdiHasOld:
            out[0] = g.old != nullptr;
            break;
        case rust::WorkflowOp::DesktopGdiRestoreOld:
            desktop_helpers_test::SelectObject(g.dc, g.old);
            break;
        case rust::WorkflowOp::DesktopGdiClearOld:
            g.old = nullptr;
            break;
        case rust::WorkflowOp::DesktopGdiHasBitmap:
            out[0] = g.bitmap != nullptr;
            break;
        case rust::WorkflowOp::DesktopGdiDeleteBitmap:
            desktop_helpers_test::DeleteObject(g.bitmap);
            break;
        case rust::WorkflowOp::DesktopGdiClearBitmap:
            g.bitmap = nullptr;
            break;
        case rust::WorkflowOp::DesktopGdiDeleteDc:
            desktop_helpers_test::DeleteDC(g.dc);
            break;
        case rust::WorkflowOp::DesktopGdiReleaseScreen:
            desktop_helpers_test::ReleaseDC(nullptr, g.screen);
            break;
        case rust::WorkflowOp::DesktopGdiSameSize:
            out[0] = g.size == wanted;
            break;
        case rust::WorkflowOp::DesktopGdiCreateBitmap: {
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = wanted.width();
            info.bmiHeader.biHeight = -wanted.height();
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            g.bitmap =
                desktop_helpers_test::CreateDIBSection(g.screen, &info, DIB_RGB_COLORS, &g.data, nullptr, 0);
            break;
        }
        case rust::WorkflowOp::DesktopGdiHasData:
            out[0] = g.data != nullptr;
            break;
        case rust::WorkflowOp::DesktopGdiSelectBitmap:
            g.old = desktop_helpers_test::SelectObject(g.dc, g.bitmap);
            break;
        case rust::WorkflowOp::DesktopGdiSetSize:
            g.size = wanted;
            break;
        case rust::WorkflowOp::DesktopGdiFailure:
            if (a[0] == 0)
                fail("无法创建 Win32 截图上下文");
            else
                fail("Win32 截图缓冲区分配失败");
            break;
        default:
            fail("Rust Win32 截图缓冲区宿主无效");
        }
    }
};
GdiFrame::GdiFrame() {
    DesktopGdiSdk sdk{*this, {}};
    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopGdiCreate, {}, sdk);
}
GdiFrame::~GdiFrame() {
    DesktopGdiSdk sdk{*this, {}};
    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopGdiDestroy, {}, sdk);
}
void GdiFrame::resize(QSize wanted) {
    DesktopGdiSdk sdk{*this, wanted};
    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopGdiResize, {}, sdk);
}
} // namespace current
template <class P> QJsonObject pacer() {
    QJsonObject result;
    try {
        P value;
        const auto deadline = std::chrono::steady_clock::time_point(
            std::chrono::nanoseconds((active->scene % 4 == 0   ? -1
                                      : active->scene % 4 == 1 ? 99
                                      : active->scene % 4 == 2 ? 50000000
                                                               : INT64_C(1234567891234567))));
        value.wait(deadline, active->stop);
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    }
    result["stopped"] = bool(active->stop);
    return result;
}
template <class G> QJsonObject gdi() {
    QJsonObject result;
    try {
        G value;
        const QSize first(active->scene % 2 ? 640 : 1919, 1080);
        value.resize(first);
        value.resize(first);
        value.resize(QSize(1280, 720));
        result["width"] = value.size.width();
        result["height"] = value.size.height();
        result["data"] = value.data == active->data.data();
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    }
    return result;
}
inline QJsonObject outcome(int scene, bool isPacer, bool rust) {
    State state(scene);
    active = &state;
    auto result = isPacer ? (rust ? pacer<current::FramePacer>() : pacer<original::FramePacer>())
                          : (rust ? gdi<current::GdiFrame>() : gdi<original::GdiFrame>());
    result["trace"] = state.trace;
    return result;
}
} // namespace okww::desktop_helpers_test
