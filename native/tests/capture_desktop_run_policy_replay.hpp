#pragma once
#include "capture.hpp"
#include "capture_math_abi.hpp"
#include "platform_workflow.hpp"
#include <optional>
namespace okww::capture_desktop_run_test {
struct State {
    int scene, rects = 0, frames = 0, waits = 0, published = 0;
    i64 clock = INT64_C(0x123456789abcde00);
    QJsonArray trace;
    std::array<uint8_t, 256> bytes{};
    std::atomic<bool> *stop = nullptr;
    explicit State(int s) : scene(s) {}
};
inline thread_local State *active = nullptr;
inline QJsonArray rectValue(QRect r) { return {r.x(), r.y(), r.width(), r.height()}; }
inline std::chrono::steady_clock::time_point now() {
    active->trace.append(QJsonObject{{"clock", QString::number(active->clock)}});
    auto result = std::chrono::steady_clock::time_point(std::chrono::nanoseconds(active->clock));
    active->clock += 1000000;
    return result;
}
inline i64 nowNs() {
    if (active->scene % 7 == 1)
        active->clock += INT64_C(61000000000);
    active->trace.append(QJsonObject{{"ns", QString::number(active->clock)}});
    return active->clock;
}
inline DPI_AWARENESS_CONTEXT SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT value) {
    active->trace.append(QJsonObject{{"dpi", QString::number(quintptr(value))}});
    return active->scene % 13 ? reinterpret_cast<DPI_AWARENESS_CONTEXT>(uintptr_t(1)) : nullptr;
}
inline QJsonObject desktopCaptureTarget(const Config &config) {
    active->trace.append(QJsonObject{{"identity", config.s("capture.local_backend")}});
    if (active->scene % 47 == 0)
        throw std::runtime_error("identity fixture");
    return {{"id", "DISPLAY1"}};
}
struct DesktopTarget {
    explicit DesktopTarget(const QJsonObject &) {
        active->trace.append("target");
        if (active->scene % 43 == 0)
            throw std::runtime_error("target fixture");
    }
    ~DesktopTarget() { active->trace.append("target-end"); }
    QRect rect() {
        const int index = active->rects++;
        active->trace.append(QJsonObject{{"rect", index}});
        if (active->scene % 7 == 5)
            return {0, 0, 8193, 1080};
        if (index && active->scene % 7 == 3 && index < 4)
            throw std::runtime_error("rect fixture");
        if (index && active->scene % 7 == 4)
            throw 27;
        if (index && active->scene % 7 == 1)
            return {200, 0, 8, 4};
        if (index && active->scene % 7 == 2)
            return {0, 0, 9, 4};
        return {0, 0, 8, 4};
    }
};
inline QRect virtualDesktopRect() {
    active->trace.append("virtual-desktop");
    return {0, 0, 100, 100};
}
inline double displayRefreshRate(const QString &name) {
    active->trace.append(QJsonObject{{"refresh", name}});
    if (active->scene % 41 == 0)
        throw std::runtime_error("refresh fixture");
    return 539.998;
}
struct Duplication {
    explicit Duplication(QRect initial) {
        active->trace.append(QJsonObject{{"duplication", rectValue(initial)}});
        if (active->scene % 17 == 0)
            throw std::runtime_error("duplication fixture");
    }
    ~Duplication() { active->trace.append("duplication-end"); }
    QImage frame(QRect rect, bool &changed) {
        const int index = ++active->frames;
        active->trace.append(QJsonObject{{"frame", index}, {"rect", rectValue(rect)}});
        if (active->scene % 37 == 0)
            throw std::runtime_error("frame fixture");
        changed = active->scene % 3 != 0;
        if (active->scene % 5 == 0 && index < 3)
            return {};
        QImage image(8, 4, QImage::Format_RGB32);
        image.fill(QColor(50, 80, 120));
        return image;
    }
};
struct GdiFrame {
    HDC screen = reinterpret_cast<HDC>(uintptr_t(2)), dc = reinterpret_cast<HDC>(uintptr_t(3));
    void *data = nullptr;
    GdiFrame() : data(active->bytes.data()) {
        active->trace.append("gdi");
        if (active->scene % 17 == 0)
            throw std::runtime_error("gdi fixture");
    }
    ~GdiFrame() { active->trace.append("gdi-end"); }
    void resize(QSize size) {
        active->trace.append(QJsonArray{"resize", size.width(), size.height()});
        if (active->scene % 37 == 0)
            throw std::runtime_error("resize fixture");
    }
};
inline BOOL BitBlt(HDC, int, int, int w, int h, HDC, int x, int y, DWORD flags) {
    active->trace.append(QJsonArray{"blit", w, h, x, y, qint64(flags)});
    return active->scene % 19 != 0;
}
inline BOOL GdiFlush() {
    active->trace.append("gdi-flush");
    return FALSE;
}
struct FramePacer {
    FramePacer() { active->trace.append("pacer"); }
    ~FramePacer() { active->trace.append("pacer-end"); }
    void wait(std::chrono::steady_clock::time_point deadline, const std::atomic<bool> &) {
        const i64 ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(deadline.time_since_epoch()).count();
        active->trace.append(QJsonObject{{"wait", QString::number(ns)}});
        active->clock = std::max(active->clock, ns);
        if (++active->waits >= 8)
            *active->stop = true;
        if (active->scene % 53 == 0)
            throw std::runtime_error("pacer fixture");
    }
};
struct CaptureState {
    Config config_ = Config::defaults();
    std::atomic<bool> stop_{false};
    double negotiatedFps_ = 0;
    QString recovery;
    void publish(const uint8_t *, size_t length, int w, int h, int stride, PixelFormat format, bool bottom,
                 i64 stamp, bool compare = false) {
        active->trace.append(QJsonObject{{"publish", ++active->published},
                                         {"length", int(length)},
                                         {"width", w},
                                         {"height", h},
                                         {"stride", stride},
                                         {"format", int(format)},
                                         {"bottom", bottom},
                                         {"stamp", QString::number(stamp)},
                                         {"compare", compare}});
        if (active->scene % 23 == 0)
            throw std::runtime_error("publish fixture");
        if (active->scene % 29 == 0)
            throw 31;
    }
    void verifyUnchangedDesktop(i64 stamp) {
        active->trace.append(QJsonObject{{"verify", QString::number(stamp)}});
        if (active->scene % 23 == 0)
            throw std::runtime_error("verify fixture");
    }
    void desktopRecovery(QString text) {
        active->trace.append(QJsonObject{{"recovery", text}});
        recovery = text;
        if (active->scene % 31 == 0)
            throw std::runtime_error("recovery setter fixture");
    }
};
namespace original {
struct Capture : CaptureState {
    void runDesktop();
};
void Capture::runDesktop() {
    const auto previousDpi =
        capture_desktop_run_test::SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    struct RestoreDpi {
        DPI_AWARENESS_CONTEXT context;
        ~RestoreDpi() {
            if (context)
                capture_desktop_run_test::SetThreadDpiAwarenessContext(context);
        }
    } restore{previousDpi};
    const bool dxgi = config_.s("capture.local_backend") == "DXGI";
    const auto identity = desktopCaptureTarget(config_);
    DesktopTarget target(identity);
    const auto initial = target.rect();
    if (initial.width() > 8192 || initial.height() > 8192)
        fail("本机截图尺寸不能超过 8192 像素");
    std::unique_ptr<Duplication> duplication;
    std::unique_ptr<GdiFrame> gdi;
    negotiatedFps_ = dxgi && config_.b("capture.dxgi_follow_refresh")
                         ? displayRefreshRate(identity["id"].toString())
                         : config_.d("capture.local_fps");
    const auto interval = std::chrono::nanoseconds(i64(1e9 / negotiatedFps_));
    FramePacer pacer;
    auto next = capture_desktop_run_test::now();
    i64 recoveryBegan = 0;
    while (!stop_) {
        try {
            const auto rect = target.rect();
            if (!virtualDesktopRect().contains(rect))
                fail("截图窗口超出桌面，请将窗口完整移回屏幕内再连接");
            if (rect.size() != initial.size())
                fail("截图窗口尺寸已改变，请重新连接以更新识别与输入坐标");
            if (dxgi) {
                if (!duplication)
                    duplication = std::make_unique<Duplication>(initial);
                bool changed = false;
                const auto image = duplication->frame(rect, changed);
                if (!image.isNull() && changed)
                    publish(image.constBits(), size_t(image.sizeInBytes()), image.width(), image.height(),
                            int(image.bytesPerLine()), PixelFormat::BGRA32, false, nowNs());
                else if (!image.isNull())
                    verifyUnchangedDesktop(nowNs());
                if (image.isNull()) {
                    pacer.wait(capture_desktop_run_test::now() + std::chrono::milliseconds(5), stop_);
                    continue;
                }
            } else {
                if (!gdi)
                    gdi = std::make_unique<GdiFrame>();
                gdi->resize(rect.size());
                if (!capture_desktop_run_test::BitBlt(gdi->dc, 0, 0, rect.width(), rect.height(), gdi->screen,
                                                      rect.x(), rect.y(), SRCCOPY | CAPTUREBLT))
                    fail("Win32 截图失败，请检查窗口是否可见");
                capture_desktop_run_test::GdiFlush();
                publish(static_cast<const uint8_t *>(gdi->data), size_t(rect.width()) * rect.height() * 4,
                        rect.width(), rect.height(), rect.width() * 4, PixelFormat::BGRA32, false, nowNs(),
                        true);
            }
            recoveryBegan = 0;
            desktopRecovery({});
        } catch (const std::exception &error) {
            if (!recoveryBegan)
                recoveryBegan = nowNs();
            desktopRecovery(errorText(error));
            duplication.reset();
            gdi.reset();
            if (nowNs() - recoveryBegan > i64(120e9))
                throw;
            pacer.wait(capture_desktop_run_test::now() + std::chrono::milliseconds(100), stop_);
            next = capture_desktop_run_test::now();
            continue;
        }
        next += interval;
        if (next < capture_desktop_run_test::now())
            next = capture_desktop_run_test::now();
        pacer.wait(next, stop_);
    }
}
} // namespace original
namespace current {
struct Capture : CaptureState {
    void runDesktop();
};
struct CaptureDesktopRunSdk {
    Capture &c;
    DPI_AWARENESS_CONTEXT previousDpi = nullptr;
    struct RestoreDpi {
        DPI_AWARENESS_CONTEXT context;
        ~RestoreDpi() {
            if (context)
                capture_desktop_run_test::SetThreadDpiAwarenessContext(context);
        }
    };
    std::optional<RestoreDpi> restore;
    QJsonObject identity;
    std::optional<DesktopTarget> target;
    QRect initial;
    std::unique_ptr<Duplication> duplication;
    std::unique_ptr<GdiFrame> gdi;
    std::chrono::nanoseconds interval{};
    std::optional<FramePacer> pacer;
    std::chrono::steady_clock::time_point next{};
    i64 recoveryBegan = 0;
    QRect rect;
    bool changed = false;
    QImage image;
    void operator()(rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &error) {
        switch (op) {
        case rust::WorkflowOp::DesktopRunSetDpi:
            previousDpi = capture_desktop_run_test::SetThreadDpiAwarenessContext(
                DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            break;
        case rust::WorkflowOp::DesktopRunDpiGuard:
            restore.emplace(previousDpi);
            break;
        case rust::WorkflowOp::DesktopRunIsDxgi:
            out[0] = c.config_.s("capture.local_backend") == "DXGI";
            break;
        case rust::WorkflowOp::DesktopRunIdentity:
            identity = desktopCaptureTarget(c.config_);
            break;
        case rust::WorkflowOp::DesktopRunTarget:
            target.emplace(identity);
            break;
        case rust::WorkflowOp::DesktopRunInitialRect:
            initial = target->rect();
            break;
        case rust::WorkflowOp::DesktopRunInitialSize:
            out[0] = initial.width();
            out[1] = initial.height();
            break;
        case rust::WorkflowOp::DesktopRunFollowRefresh:
            out[0] = c.config_.b("capture.dxgi_follow_refresh");
            break;
        case rust::WorkflowOp::DesktopRunDisplayRate:
            c.negotiatedFps_ = displayRefreshRate(identity["id"].toString());
            break;
        case rust::WorkflowOp::DesktopRunConfiguredRate:
            c.negotiatedFps_ = c.config_.d("capture.local_fps");
            break;
        case rust::WorkflowOp::DesktopRunInterval:
            interval = std::chrono::nanoseconds(i64(1e9 / c.negotiatedFps_));
            break;
        case rust::WorkflowOp::DesktopRunPacer:
            pacer.emplace();
            break;
        case rust::WorkflowOp::DesktopRunSetNextNow:
            next = capture_desktop_run_test::now();
            break;
        case rust::WorkflowOp::DesktopRunStopped:
            out[0] = bool(c.stop_);
            break;
        case rust::WorkflowOp::DesktopRunReadRect:
            rect = target->rect();
            break;
        case rust::WorkflowOp::DesktopRunRectInDesktop:
            out[0] = virtualDesktopRect().contains(rect);
            break;
        case rust::WorkflowOp::DesktopRunSameSize:
            out[0] = rect.size() == initial.size();
            break;
        case rust::WorkflowOp::DesktopRunHasDuplication:
            out[0] = bool(duplication);
            break;
        case rust::WorkflowOp::DesktopRunCreateDuplication:
            duplication = std::make_unique<Duplication>(initial);
            break;
        case rust::WorkflowOp::DesktopRunClearChanged:
            changed = false;
            break;
        case rust::WorkflowOp::DesktopRunDuplicationFrame:
            image = duplication->frame(rect, changed);
            break;
        case rust::WorkflowOp::DesktopRunImageNull:
            out[0] = image.isNull();
            break;
        case rust::WorkflowOp::DesktopRunImageChanged:
            out[0] = changed;
            break;
        case rust::WorkflowOp::DesktopRunPublishImage:
            c.publish(image.constBits(), size_t(image.sizeInBytes()), image.width(), image.height(),
                      int(image.bytesPerLine()), PixelFormat::BGRA32, false, nowNs());
            break;
        case rust::WorkflowOp::DesktopRunVerifyUnchanged:
            c.verifyUnchangedDesktop(nowNs());
            break;
        case rust::WorkflowOp::DesktopRunWaitMilliseconds:
            pacer->wait(capture_desktop_run_test::now() + std::chrono::milliseconds(int(a[0])), c.stop_);
            break;
        case rust::WorkflowOp::DesktopRunDropImage:
            image = QImage{};
            break;
        case rust::WorkflowOp::DesktopRunDropRect:
            rect = QRect{};
            break;
        case rust::WorkflowOp::DesktopRunHasGdi:
            out[0] = bool(gdi);
            break;
        case rust::WorkflowOp::DesktopRunCreateGdi:
            gdi = std::make_unique<GdiFrame>();
            break;
        case rust::WorkflowOp::DesktopRunResizeGdi:
            gdi->resize(rect.size());
            break;
        case rust::WorkflowOp::DesktopRunBitBlt:
            out[0] = capture_desktop_run_test::BitBlt(gdi->dc, 0, 0, rect.width(), rect.height(), gdi->screen,
                                                      rect.x(), rect.y(), SRCCOPY | CAPTUREBLT) != 0;
            break;
        case rust::WorkflowOp::DesktopRunGdiFlush:
            capture_desktop_run_test::GdiFlush();
            break;
        case rust::WorkflowOp::DesktopRunPublishGdi:
            c.publish(static_cast<const uint8_t *>(gdi->data), size_t(rect.width()) * rect.height() * 4,
                      rect.width(), rect.height(), rect.width() * 4, PixelFormat::BGRA32, false, nowNs(),
                      true);
            break;
        case rust::WorkflowOp::DesktopRunResetRecovery:
            recoveryBegan = 0;
            break;
        case rust::WorkflowOp::DesktopRunClearRecoveryStatus:
            c.desktopRecovery({});
            break;
        case rust::WorkflowOp::DesktopRunHasRecovery:
            out[0] = recoveryBegan != 0;
            break;
        case rust::WorkflowOp::DesktopRunBeginRecovery:
            recoveryBegan = nowNs();
            break;
        case rust::WorkflowOp::DesktopRunRecoveryStatus:
            try {
                std::rethrow_exception(error);
            } catch (const std::exception &e) {
                c.desktopRecovery(errorText(e));
            }
            break;
        case rust::WorkflowOp::DesktopRunResetDuplication:
            duplication.reset();
            break;
        case rust::WorkflowOp::DesktopRunResetGdi:
            gdi.reset();
            break;
        case rust::WorkflowOp::DesktopRunRecoveryExpired:
            out[0] = rust::okww_rs_capture_recovery_expired(nowNs(), recoveryBegan);
            break;
        case rust::WorkflowOp::DesktopRunAdvanceNext:
            next += interval;
            break;
        case rust::WorkflowOp::DesktopRunNextPast:
            out[0] = next < capture_desktop_run_test::now();
            break;
        case rust::WorkflowOp::DesktopRunWaitNext:
            pacer->wait(next, c.stop_);
            break;
        case rust::WorkflowOp::DesktopRunFailure:
            if (a[0] == 0)
                fail("本机截图尺寸不能超过 8192 像素");
            else if (a[0] == 1)
                fail("截图窗口超出桌面，请将窗口完整移回屏幕内再连接");
            else if (a[0] == 2)
                fail("截图窗口尺寸已改变，请重新连接以更新识别与输入坐标");
            else
                fail("Win32 截图失败，请检查窗口是否可见");
            break;
        default:
            fail("Rust 桌面采集流程宿主无效");
        }
    }
};
void Capture::runDesktop() {
    CaptureDesktopRunSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopCaptureRun, {}, sdk);
}
} // namespace current
template <class C> QJsonObject run(bool dxgi) {
    C capture;
    active->stop = &capture.stop_;
    capture.config_.set("capture.local_backend", dxgi ? "DXGI" : "WIN32");
    capture.config_.set("capture.dxgi_follow_refresh", active->scene % 2 == 0);
    capture.config_.set("capture.local_fps", 120.);
    QJsonObject result;
    try {
        capture.runDesktop();
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    } catch (...) {
        result["error"] = "unknown";
    }
    result["negotiated"] = capture.negotiatedFps_;
    result["stopped"] = bool(capture.stop_);
    result["recovery"] = capture.recovery;
    return result;
}
inline QJsonObject outcome(int scene, bool dxgi, bool rust) {
    State state(scene);
    active = &state;
    auto result = rust ? run<current::Capture>(dxgi) : run<original::Capture>(dxgi);
    result["trace"] = state.trace;
    return result;
}
} // namespace okww::capture_desktop_run_test
