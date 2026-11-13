#include "capture.hpp"
#include "desktop.hpp"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <QTransform>

namespace okww {
using Microsoft::WRL::ComPtr;
namespace {
struct FramePacer {
    HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
    ~FramePacer(){if(timer) CloseHandle(timer);}
    void wait(std::chrono::steady_clock::time_point deadline,const std::atomic<bool> &stop) {
        const auto remaining=std::chrono::duration_cast<std::chrono::nanoseconds>(deadline-std::chrono::steady_clock::now()).count();
        if (remaining<=0 || stop) return;
        LARGE_INTEGER due{}; due.QuadPart=-std::max<i64>(1,remaining/100);
        if (timer && SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE)) {
            while (!stop) {
                const auto result=WaitForSingleObject(timer,20);
                if (result==WAIT_OBJECT_0) return;
                if (result!=WAIT_TIMEOUT) fail("截图帧率定时器等待失败");
            }
        } else while (!stop && std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_until(std::min(deadline,std::chrono::steady_clock::now()+std::chrono::milliseconds(20)));
    }
};
struct GdiFrame {
    HDC screen = GetDC(nullptr), dc = screen ? CreateCompatibleDC(screen) : nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ old = nullptr;
    void *data = nullptr;
    QSize size;
    ~GdiFrame() {
        if (old) SelectObject(dc, old);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        if (screen) ReleaseDC(nullptr, screen);
    }
    void resize(QSize wanted) {
        if (!screen || !dc) fail("无法创建 Win32 截图上下文");
        if (size == wanted) return;
        if (old) { SelectObject(dc, old); old = nullptr; }
        if (bitmap) { DeleteObject(bitmap); bitmap = nullptr; }
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = wanted.width(); info.bmiHeader.biHeight = -wanted.height();
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &data, nullptr, 0);
        if (!bitmap || !data) fail("Win32 截图缓冲区分配失败");
        old = SelectObject(dc, bitmap); size = wanted;
    }
};
struct Duplication {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> output;
    ComPtr<ID3D11Texture2D> staging;
    QRect bounds;
    DXGI_MODE_ROTATION rotation = DXGI_MODE_ROTATION_IDENTITY;
    QImage last;
    explicit Duplication(QRect target) {
        ComPtr<IDXGIFactory1> factory;
        hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "DXGI 初始化失败");
        ComPtr<IDXGIAdapter1> adapter;
        ComPtr<IDXGIOutput> selected;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            ComPtr<IDXGIOutput> candidate;
            for (UINT j = 0; adapter->EnumOutputs(j, &candidate) != DXGI_ERROR_NOT_FOUND; ++j) {
                DXGI_OUTPUT_DESC desc{};
                hr(candidate->GetDesc(&desc), "读取 DXGI 显示器失败");
                const auto &r = desc.DesktopCoordinates;
                QRect screen(r.left, r.top, r.right-r.left, r.bottom-r.top);
                if (desc.AttachedToDesktop && screen.contains(target)) {
                    selected = candidate; bounds = screen; rotation = desc.Rotation; break;
                }
                candidate.Reset();
            }
            if (selected) break;
            adapter.Reset();
        }
        if (!selected) fail("DXGI 目标需完整位于同一显示器内，请移动窗口或选择 Win32 截图");
        hr(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                            nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context), "创建 DXGI 采集设备失败");
        ComPtr<IDXGIOutput1> output1;
        hr(selected.As(&output1), "显示器不支持 DXGI 桌面复制");
        hr(output1->DuplicateOutput(device.Get(), &output), "DXGI 桌面复制不可用，请检查桌面会话或改用 Win32");
    }
    QImage frame(QRect target) {
        if (!bounds.contains(target)) fail("目标窗口移出了 DXGI 显示器，请重新连接");
        DXGI_OUTDUPL_FRAME_INFO info{};
        ComPtr<IDXGIResource> resource;
        const auto result = output->AcquireNextFrame(0, &info, &resource);
        if (result == DXGI_ERROR_WAIT_TIMEOUT) {
            // A successful timeout confirms the duplicated desktop is unchanged.
            return last;
        }
        hr(result, "DXGI 采集已中断，请重新连接（桌面切换或显示模式改变）");
        struct Release { IDXGIOutputDuplication *output; ~Release(){ output->ReleaseFrame(); } } release{output.Get()};
        ComPtr<ID3D11Texture2D> texture;
        hr(resource.As(&texture), "DXGI 图像纹理不可用");
        D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
        const bool rotated = rotation != DXGI_MODE_ROTATION_IDENTITY && rotation != DXGI_MODE_ROTATION_UNSPECIFIED;
        const auto crop = target.translated(-bounds.topLeft());
        D3D11_TEXTURE2D_DESC wanted = desc;
        wanted.Width = rotated ? desc.Width : UINT(crop.width());
        wanted.Height = rotated ? desc.Height : UINT(crop.height());
        wanted.BindFlags = 0; wanted.MiscFlags = 0; wanted.Usage = D3D11_USAGE_STAGING;
        wanted.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        D3D11_TEXTURE2D_DESC current{};
        if (staging) staging->GetDesc(&current);
        if (!staging || current.Width != wanted.Width || current.Height != wanted.Height || current.Format != wanted.Format) {
            staging.Reset(); hr(device->CreateTexture2D(&wanted, nullptr, &staging), "DXGI 读取缓冲区分配失败");
        }
        if (rotated) context->CopyResource(staging.Get(), texture.Get());
        else {
            D3D11_BOX box{UINT(crop.left()), UINT(crop.top()), 0, UINT(crop.x()+crop.width()), UINT(crop.y()+crop.height()), 1};
            context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, texture.Get(), 0, &box);
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "DXGI 图像读取失败");
        struct Unmap { ID3D11DeviceContext *context; ID3D11Texture2D *texture; ~Unmap(){context->Unmap(texture,0);} } unmap{context.Get(),staging.Get()};
        QImage image(static_cast<uchar *>(mapped.pData), int(wanted.Width), int(wanted.Height),
                     int(mapped.RowPitch), QImage::Format_RGB32);
        if (rotated) {
            const int angle = rotation == DXGI_MODE_ROTATION_ROTATE90 ? 90 : rotation == DXGI_MODE_ROTATION_ROTATE180 ? 180 : 270;
            last = image.transformed(QTransform().rotate(angle)).copy(crop);
        } else last = image.copy();
        return last;
    }
};
}
void Capture::runDesktop() {
    const auto previousDpi = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    struct RestoreDpi { DPI_AWARENESS_CONTEXT context; ~RestoreDpi(){if(context) SetThreadDpiAwarenessContext(context);} } restore{previousDpi};
    const bool dxgi = config_.s("capture.local_backend") == "DXGI";
    const auto identity=desktopCaptureTarget(config_);
    DesktopTarget target(identity);
    const auto initial = target.rect();
    if (initial.width() > 8192 || initial.height() > 8192) fail("本机截图尺寸不能超过 8192 像素");
    std::unique_ptr<Duplication> duplication;
    std::unique_ptr<GdiFrame> gdi;
    if (dxgi) duplication = std::make_unique<Duplication>(initial);
    else gdi = std::make_unique<GdiFrame>();
    negotiatedFps_ = dxgi && config_.b("capture.dxgi_follow_refresh")
        ? displayRefreshRate(identity["id"].toString()) : config_.d("capture.local_fps");
    const auto interval = std::chrono::nanoseconds(i64(1e9 / negotiatedFps_));
    FramePacer pacer;
    auto next = std::chrono::steady_clock::now();
    while (!stop_) {
        const auto rect = target.rect();
        if (!virtualDesktopRect().contains(rect)) fail("截图窗口超出桌面，请将窗口完整移回屏幕内再连接");
        if (rect.size() != initial.size()) fail("截图窗口尺寸已改变，请重新连接以更新识别与输入坐标");
        if (dxgi) {
            const auto image = duplication->frame(rect);
            if (!image.isNull()) publish(image.constBits(), size_t(image.sizeInBytes()), image.width(), image.height(),
                                        int(image.bytesPerLine()), PixelFormat::BGRA32, false, nowNs());
        } else {
            gdi->resize(rect.size());
            if (!BitBlt(gdi->dc, 0, 0, rect.width(), rect.height(), gdi->screen, rect.x(), rect.y(), SRCCOPY | CAPTUREBLT))
                fail("Win32 截图失败，请检查窗口是否可见");
            GdiFlush();
            publish(static_cast<const uint8_t *>(gdi->data), size_t(rect.width())*rect.height()*4,
                    rect.width(), rect.height(), rect.width()*4, PixelFormat::BGRA32, false, nowNs());
        }
        next += interval;
        if (next < std::chrono::steady_clock::now()) next = std::chrono::steady_clock::now();
        pacer.wait(next,stop_);
    }
}
} // namespace okww
