#include "capture.hpp"
#include "capture_math_abi.hpp"
#include "desktop.hpp"
#include "platform_workflow.hpp"
#include <QTransform>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <optional>
#include <wrl/client.h>

namespace okww {
using Microsoft::WRL::ComPtr;
namespace {

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
            p.timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                             TIMER_ALL_ACCESS);
            break;
        case rust::WorkflowOp::DesktopPacerHasTimer:
            out[0] = p.timer != nullptr;
            break;
        case rust::WorkflowOp::DesktopPacerCloseTimer:
            CloseHandle(p.timer);
            break;
        case rust::WorkflowOp::DesktopPacerReadRemaining:
            remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline -
                                                                             std::chrono::steady_clock::now())
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
            out[0] = SetWaitableTimer(p.timer, &due, 0, nullptr, nullptr, FALSE) != 0;
            break;
        case rust::WorkflowOp::DesktopPacerWaitTimer:
            out[0] = WaitForSingleObject(p.timer, 20);
            break;
        case rust::WorkflowOp::DesktopPacerBeforeDeadline:
            out[0] = std::chrono::steady_clock::now() < deadline;
            break;
        case rust::WorkflowOp::DesktopPacerSleep:
            std::this_thread::sleep_until(
                std::min(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(20)));
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
            g.screen = GetDC(nullptr);
            break;
        case rust::WorkflowOp::DesktopGdiHasScreen:
            out[0] = g.screen != nullptr;
            break;
        case rust::WorkflowOp::DesktopGdiCreateDc:
            g.dc = CreateCompatibleDC(g.screen);
            break;
        case rust::WorkflowOp::DesktopGdiHasDc:
            out[0] = g.dc != nullptr;
            break;
        case rust::WorkflowOp::DesktopGdiHasOld:
            out[0] = g.old != nullptr;
            break;
        case rust::WorkflowOp::DesktopGdiRestoreOld:
            SelectObject(g.dc, g.old);
            break;
        case rust::WorkflowOp::DesktopGdiClearOld:
            g.old = nullptr;
            break;
        case rust::WorkflowOp::DesktopGdiHasBitmap:
            out[0] = g.bitmap != nullptr;
            break;
        case rust::WorkflowOp::DesktopGdiDeleteBitmap:
            DeleteObject(g.bitmap);
            break;
        case rust::WorkflowOp::DesktopGdiClearBitmap:
            g.bitmap = nullptr;
            break;
        case rust::WorkflowOp::DesktopGdiDeleteDc:
            DeleteDC(g.dc);
            break;
        case rust::WorkflowOp::DesktopGdiReleaseScreen:
            ReleaseDC(nullptr, g.screen);
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
            g.bitmap = CreateDIBSection(g.screen, &info, DIB_RGB_COLORS, &g.data, nullptr, 0);
            break;
        }
        case rust::WorkflowOp::DesktopGdiHasData:
            out[0] = g.data != nullptr;
            break;
        case rust::WorkflowOp::DesktopGdiSelectBitmap:
            g.old = SelectObject(g.dc, g.bitmap);
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

struct Duplication {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> output;
    ComPtr<ID3D11Texture2D> staging;
    QRect bounds;
    DXGI_MODE_ROTATION rotation = DXGI_MODE_ROTATION_IDENTITY;
    QImage last;
    explicit Duplication(QRect target);
    QImage frame(QRect target, bool &changed);
};
struct DesktopDuplicationSdk {
    Duplication &d;
    QRect target;
    bool *changed = nullptr;
    ComPtr<IDXGIFactory1> factory;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<IDXGIOutput> selected, candidate;
    DXGI_OUTPUT_DESC outputDesc{};
    QRect screen;
    ComPtr<IDXGIOutput1> output1;
    DXGI_OUTDUPL_FRAME_INFO info{};
    ComPtr<IDXGIResource> resource;
    HRESULT acquired = S_OK;
    struct Release {
        IDXGIOutputDuplication *output;
        ~Release() { output->ReleaseFrame(); }
    };
    std::optional<Release> release;
    ComPtr<ID3D11Texture2D> texture;
    D3D11_TEXTURE2D_DESC desc{}, wanted{}, current{};
    QRect crop;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    struct Unmap {
        ID3D11DeviceContext *context;
        ID3D11Texture2D *texture;
        ~Unmap() { context->Unmap(texture, 0); }
    };
    std::optional<Unmap> unmap;
    QImage image;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::DesktopDupCreateFactory:
            hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "DXGI 初始化失败");
            break;
        case rust::WorkflowOp::DesktopDupNextAdapter:
            out[0] = factory->EnumAdapters1(UINT(a[0]), &adapter) != DXGI_ERROR_NOT_FOUND;
            break;
        case rust::WorkflowOp::DesktopDupNextOutput:
            out[0] = adapter->EnumOutputs(UINT(a[0]), &candidate) != DXGI_ERROR_NOT_FOUND;
            break;
        case rust::WorkflowOp::DesktopDupOutputDesc:
            outputDesc = {};
            hr(candidate->GetDesc(&outputDesc), "读取 DXGI 显示器失败");
            break;
        case rust::WorkflowOp::DesktopDupScreenRect: {
            const auto &r = outputDesc.DesktopCoordinates;
            screen = QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
            break;
        }
        case rust::WorkflowOp::DesktopDupAttached:
            out[0] = outputDesc.AttachedToDesktop != 0;
            break;
        case rust::WorkflowOp::DesktopDupContainsTarget:
            out[0] = screen.contains(target);
            break;
        case rust::WorkflowOp::DesktopDupSelectOutput:
            selected = candidate;
            break;
        case rust::WorkflowOp::DesktopDupStoreBounds:
            d.bounds = screen;
            break;
        case rust::WorkflowOp::DesktopDupStoreRotation:
            d.rotation = outputDesc.Rotation;
            break;
        case rust::WorkflowOp::DesktopDupResetCandidate:
            candidate.Reset();
            break;
        case rust::WorkflowOp::DesktopDupHasSelected:
            out[0] = bool(selected);
            break;
        case rust::WorkflowOp::DesktopDupResetAdapter:
            adapter.Reset();
            break;
        case rust::WorkflowOp::DesktopDupCreateDevice:
            hr(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &d.device,
                                 nullptr, &d.context),
               "创建 DXGI 采集设备失败");
            break;
        case rust::WorkflowOp::DesktopDupOutputInterface:
            hr(selected.As(&output1), "显示器不支持 DXGI 桌面复制");
            break;
        case rust::WorkflowOp::DesktopDupDuplicateOutput:
            hr(output1->DuplicateOutput(d.device.Get(), &d.output),
               "DXGI 桌面复制不可用，请检查桌面会话或改用 Win32");
            break;
        case rust::WorkflowOp::DesktopDupFailure:
            if (a[0] == 0)
                fail("DXGI 目标需完整位于同一显示器内，请移动窗口或选择 Win32 截图");
            else
                fail("目标窗口移出了 DXGI 显示器，请重新连接");
            break;
        case rust::WorkflowOp::DesktopDupClearChanged:
            *changed = false;
            break;
        case rust::WorkflowOp::DesktopDupBoundsContain:
            out[0] = d.bounds.contains(target);
            break;
        case rust::WorkflowOp::DesktopDupAcquire:
            acquired = d.output->AcquireNextFrame(0, &info, &resource);
            break;
        case rust::WorkflowOp::DesktopDupAcquireTimeout:
            out[0] = acquired == DXGI_ERROR_WAIT_TIMEOUT;
            break;
        case rust::WorkflowOp::DesktopDupCheckAcquire:
            hr(acquired, "DXGI 采集已中断，请重新连接（桌面切换或显示模式改变）");
            break;
        case rust::WorkflowOp::DesktopDupReleaseGuard:
            release.emplace(d.output.Get());
            break;
        case rust::WorkflowOp::DesktopDupLastNull:
            out[0] = d.last.isNull();
            break;
        case rust::WorkflowOp::DesktopDupPointerOnly:
            out[0] = info.LastPresentTime.QuadPart == 0;
            break;
        case rust::WorkflowOp::DesktopDupTextureInterface:
            hr(resource.As(&texture), "DXGI 图像纹理不可用");
            break;
        case rust::WorkflowOp::DesktopDupTextureDesc:
            texture->GetDesc(&desc);
            break;
        case rust::WorkflowOp::DesktopDupRotation:
            out[0] = d.rotation;
            break;
        case rust::WorkflowOp::DesktopDupCrop:
            crop = target.translated(-d.bounds.topLeft());
            break;
        case rust::WorkflowOp::DesktopDupWantedDesc:
            wanted = desc;
            wanted.Width = a[0] != 0 ? desc.Width : UINT(crop.width());
            wanted.Height = a[0] != 0 ? desc.Height : UINT(crop.height());
            wanted.BindFlags = 0;
            wanted.MiscFlags = 0;
            wanted.Usage = D3D11_USAGE_STAGING;
            wanted.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            break;
        case rust::WorkflowOp::DesktopDupHasStaging:
            out[0] = bool(d.staging);
            break;
        case rust::WorkflowOp::DesktopDupCurrentDesc:
            d.staging->GetDesc(&current);
            break;
        case rust::WorkflowOp::DesktopDupSameWidth:
            out[0] = current.Width == wanted.Width;
            break;
        case rust::WorkflowOp::DesktopDupSameHeight:
            out[0] = current.Height == wanted.Height;
            break;
        case rust::WorkflowOp::DesktopDupSameFormat:
            out[0] = current.Format == wanted.Format;
            break;
        case rust::WorkflowOp::DesktopDupResetStaging:
            d.staging.Reset();
            break;
        case rust::WorkflowOp::DesktopDupCreateStaging:
            hr(d.device->CreateTexture2D(&wanted, nullptr, &d.staging), "DXGI 读取缓冲区分配失败");
            break;
        case rust::WorkflowOp::DesktopDupCopyResource:
            d.context->CopyResource(d.staging.Get(), texture.Get());
            break;
        case rust::WorkflowOp::DesktopDupCopyRegion: {
            D3D11_BOX box{UINT(crop.left()),
                          UINT(crop.top()),
                          0,
                          UINT(crop.x() + crop.width()),
                          UINT(crop.y() + crop.height()),
                          1};
            d.context->CopySubresourceRegion(d.staging.Get(), 0, 0, 0, 0, texture.Get(), 0, &box);
            break;
        }
        case rust::WorkflowOp::DesktopDupMap:
            hr(d.context->Map(d.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "DXGI 图像读取失败");
            break;
        case rust::WorkflowOp::DesktopDupUnmapGuard:
            unmap.emplace(d.context.Get(), d.staging.Get());
            break;
        case rust::WorkflowOp::DesktopDupBorrowImage:
            image = QImage(static_cast<uchar *>(mapped.pData), int(wanted.Width), int(wanted.Height),
                           int(mapped.RowPitch), QImage::Format_RGB32);
            break;
        case rust::WorkflowOp::DesktopDupCopyRotated:
            d.last = image.transformed(QTransform().rotate(a[0])).copy(crop);
            break;
        case rust::WorkflowOp::DesktopDupCopyImage:
            d.last = image.copy();
            break;
        case rust::WorkflowOp::DesktopDupSetChanged:
            *changed = true;
            break;
        default:
            fail("Rust DXGI 桌面复制宿主无效");
        }
    }
};
Duplication::Duplication(QRect target) {
    DesktopDuplicationSdk sdk{*this, target};
    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopDuplicationCreate, {}, sdk);
}
QImage Duplication::frame(QRect target, bool &changed) {
    DesktopDuplicationSdk sdk{*this, target, &changed};
    rust::platformWorkflowRun(rust::WorkflowMethod::DesktopDuplicationFrame, {}, sdk);
    return last;
}

} // namespace

struct CaptureDesktopRunSdk {
    Capture &c;
    DPI_AWARENESS_CONTEXT previousDpi = nullptr;
    struct RestoreDpi {
        DPI_AWARENESS_CONTEXT context;
        ~RestoreDpi() {
            if (context)
                SetThreadDpiAwarenessContext(context);
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
            previousDpi = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
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
            next = std::chrono::steady_clock::now();
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
            pacer->wait(std::chrono::steady_clock::now() + std::chrono::milliseconds(int(a[0])), c.stop_);
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
            out[0] = BitBlt(gdi->dc, 0, 0, rect.width(), rect.height(), gdi->screen, rect.x(), rect.y(),
                            SRCCOPY | CAPTUREBLT) != 0;
            break;
        case rust::WorkflowOp::DesktopRunGdiFlush:
            GdiFlush();
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
            out[0] = next < std::chrono::steady_clock::now();
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

} // namespace okww
