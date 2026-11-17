#pragma once
#include "capture.hpp"
#include "platform_workflow.hpp"
#include <QTransform>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <optional>
namespace okww::capture_duplication_test {
struct State;
inline thread_local State *active = nullptr;
struct Ref {
    QString kind;
    int id, refs = 1;
    Ref(QString k, int i) : kind(std::move(k)), id(i) {}
    virtual ~Ref() = default;
    void add();
    void release();
};
template <class T> struct Ptr {
    T *value = nullptr;
    Ptr() = default;
    Ptr(const Ptr &o) : value(o.value) {
        if (value)
            value->add();
    }
    Ptr(Ptr &&o) noexcept : value(std::exchange(o.value, nullptr)) {}
    ~Ptr() { Reset(); }
    Ptr &operator=(const Ptr &o) {
        if (this != std::addressof(o)) {
            Reset();
            value = o.value;
            if (value)
                value->add();
        }
        return *this;
    }
    Ptr &operator=(Ptr &&o) noexcept {
        if (this != std::addressof(o)) {
            Reset();
            value = std::exchange(o.value, nullptr);
        }
        return *this;
    }
    void Reset() {
        if (value)
            std::exchange(value, nullptr)->release();
    }
    void Attach(T *p) {
        Reset();
        value = p;
    }
    T *operator->() const { return value; }
    T *Get() const { return value; }
    Ptr *operator&() { return this; }
    explicit operator bool() const { return value != nullptr; }
    template <class U> HRESULT As(Ptr<U> *out) const;
};
struct State {
    int scene, acquisitions = 0, stagingCount = 0;
    QJsonArray trace;
    std::vector<std::unique_ptr<Ref>> objects;
    struct Texture *lastStaging = nullptr;
    explicit State(int s) : scene(s) {}
    template <class T, class... A> T *make(A &&...a) {
        auto p = std::make_unique<T>(std::forward<A>(a)...);
        auto *raw = p.get();
        objects.push_back(std::move(p));
        return raw;
    }
    HRESULT step(const QString &name, int id = -1) {
        trace.append(QJsonObject{{"call", name}, {"id", id}});
        const QStringList failures{"none",      "factory",           "desc",    "device", "output-interface",
                                   "duplicate", "texture-interface", "staging", "map",    "acquire"};
        return name == failures[(scene / 32) % failures.size()] ? E_FAIL : S_OK;
    }
};
inline void Ref::add() { active->trace.append(QJsonObject{{"add", kind}, {"id", id}, {"refs", ++refs}}); }
inline void Ref::release() {
    active->trace.append(QJsonObject{{"release", kind}, {"id", id}, {"refs", --refs}});
}
struct Texture : Ref {
    D3D11_TEXTURE2D_DESC desc{};
    QByteArray bytes;
    int pitch = 0;
    Texture(int i, D3D11_TEXTURE2D_DESC d)
        : Ref("texture", i), desc(d), bytes(int((d.Width * 4 + 8) * d.Height), char(0)),
          pitch(int(d.Width * 4 + 8)) {
        for (int y = 0; y < int(d.Height); ++y)
            for (int x = 0; x < int(d.Width); ++x) {
                uint32_t pixel =
                    0xff000000u | uint32_t((i * 31 + x * 5 + y * 9 + active->scene) & 255) * 0x010101;
                memcpy(bytes.data() + y * pitch + x * 4, &pixel, 4);
            }
    }
    void GetDesc(D3D11_TEXTURE2D_DESC *out) {
        active->step("texture-desc", id);
        *out = desc;
    }
};
inline D3D11_TEXTURE2D_DESC sourceDesc() {
    D3D11_TEXTURE2D_DESC d{};
    d.Width = 8;
    d.Height = 6;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = active->acquisitions % 2 ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = 8;
    d.MiscFlags = 2;
    return d;
}
struct Resource : Ref {
    explicit Resource(int i) : Ref("resource", i) {}
};
struct OutputDuplication : Ref {
    OutputDuplication() : Ref("duplication", 0) {}
    HRESULT AcquireNextFrame(UINT timeout, DXGI_OUTDUPL_FRAME_INFO *info, Ptr<Resource> *out) {
        const int index = ++active->acquisitions;
        active->trace.append(QJsonObject{{"timeout", int(timeout)}});
        if ((active->scene + index) % 17 == 0) {
            active->step("acquire-timeout");
            return DXGI_ERROR_WAIT_TIMEOUT;
        }
        const HRESULT result = active->step("acquire", index);
        if (FAILED(result))
            return result;
        info->LastPresentTime.QuadPart =
            index == 2 && active->scene % 3 == 0 ? 0 : INT64_C(0x123456789abcdef0);
        out->Attach(active->make<Resource>(index));
        return S_OK;
    }
    HRESULT ReleaseFrame() { return active->step("release-frame"); }
};
struct Context : Ref {
    Context() : Ref("context", 0) {}
    void CopyResource(Texture *target, Texture *source) {
        active->step("copy-resource");
        target->bytes = source->bytes;
        target->pitch = source->pitch;
    }
    void CopySubresourceRegion(Texture *target, UINT, UINT, UINT, UINT, Texture *source, UINT,
                               const D3D11_BOX *box) {
        active->trace.append(QJsonObject{{"copy-left", int(box->left)},
                                         {"top", int(box->top)},
                                         {"right", int(box->right)},
                                         {"bottom", int(box->bottom)}});
        for (UINT y = box->top; y < box->bottom; ++y)
            memcpy(target->bytes.data() + int(y - box->top) * target->pitch,
                   source->bytes.constData() + int(y) * source->pitch + int(box->left) * 4,
                   size_t(box->right - box->left) * 4);
    }
    HRESULT Map(Texture *texture, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE *out) {
        HRESULT result = active->step("map", texture->id);
        if (SUCCEEDED(result)) {
            out->pData = texture->bytes.data();
            out->RowPitch = UINT(texture->pitch);
        }
        return result;
    }
    void Unmap(Texture *texture, UINT) { active->step("unmap", texture->id); }
};
struct Device : Ref {
    Device() : Ref("device", 0) {}
    HRESULT CreateTexture2D(const D3D11_TEXTURE2D_DESC *desc, void *, Ptr<Texture> *out) {
        HRESULT result = active->step("staging", ++active->stagingCount);
        active->trace.append(QJsonObject{{"width", int(desc->Width)},
                                         {"height", int(desc->Height)},
                                         {"format", int(desc->Format)},
                                         {"usage", int(desc->Usage)},
                                         {"cpu", int(desc->CPUAccessFlags)},
                                         {"bind", int(desc->BindFlags)},
                                         {"misc", int(desc->MiscFlags)}});
        if (SUCCEEDED(result)) {
            active->lastStaging = active->make<Texture>(100 + active->stagingCount, *desc);
            out->Attach(active->lastStaging);
        }
        return result;
    }
};
struct ExtendedOutput : Ref {
    explicit ExtendedOutput(int i) : Ref("output1", i) {}
    HRESULT DuplicateOutput(Device *, Ptr<OutputDuplication> *out) {
        HRESULT result = active->step("duplicate", id);
        if (SUCCEEDED(result))
            out->Attach(active->make<OutputDuplication>());
        return result;
    }
};
struct Output : Ref {
    explicit Output(int i) : Ref("output", i) {}
    HRESULT GetDesc(DXGI_OUTPUT_DESC *out) {
        const HRESULT result = active->step("desc", id);
        const int x = id % 2 ? 10 : 0;
        out->DesktopCoordinates = {x, 0, x + 8, 6};
        out->AttachedToDesktop = active->scene % 13 != 0;
        out->Rotation = DXGI_MODE_ROTATION(active->scene % 6);
        return result;
    }
};
struct Adapter : Ref {
    explicit Adapter(int i) : Ref("adapter", i) {}
    HRESULT EnumOutputs(UINT i, Ptr<Output> *out) {
        active->trace.append(QJsonObject{{"enum-output", int(i)}, {"adapter", id}});
        if (i >= 2)
            return DXGI_ERROR_NOT_FOUND;
        out->Attach(active->make<Output>(id * 10 + int(i)));
        return S_OK;
    }
};
struct Factory : Ref {
    Factory() : Ref("factory", 0) {}
    HRESULT EnumAdapters1(UINT i, Ptr<Adapter> *out) {
        active->trace.append(QJsonObject{{"enum-adapter", int(i)}});
        if (i >= 2)
            return DXGI_ERROR_NOT_FOUND;
        out->Attach(active->make<Adapter>(int(i)));
        return S_OK;
    }
};
template <class T> template <class U> HRESULT Ptr<T>::As(Ptr<U> *out) const {
    if constexpr (std::is_same_v<U, ExtendedOutput>) {
        HRESULT result = active->step("output-interface", value->id);
        if (FAILED(result))
            return result;
        out->Attach(active->make<U>(value->id));
    } else {
        HRESULT result = active->step("texture-interface", value->id);
        if (FAILED(result))
            return result;
        out->Attach(active->make<U>(value->id, sourceDesc()));
    }
    return S_OK;
}
inline HRESULT createFactory(Ptr<Factory> *out) {
    HRESULT result = active->step("factory");
    if (SUCCEEDED(result))
        out->Attach(active->make<Factory>());
    return result;
}
inline HRESULT createDevice(Adapter *a, D3D_DRIVER_TYPE, void *, UINT flags, void *, UINT, UINT,
                            Ptr<Device> *device, void *, Ptr<Context> *context) {
    active->trace.append(QJsonObject{{"bgra", int(flags)}});
    HRESULT result = active->step("device", a->id);
    if (SUCCEEDED(result)) {
        device->Attach(active->make<Device>());
        context->Attach(active->make<Context>());
    }
    return result;
}
namespace original {
struct Duplication {
    Ptr<Device> device;
    Ptr<Context> context;
    Ptr<OutputDuplication> output;
    Ptr<Texture> staging;
    QRect bounds;
    DXGI_MODE_ROTATION rotation = DXGI_MODE_ROTATION_IDENTITY;
    QImage last;
    explicit Duplication(QRect target) {
        Ptr<Factory> factory;
        hr(capture_duplication_test::createFactory(&factory), "DXGI 初始化失败");
        Ptr<Adapter> adapter;
        Ptr<Output> selected;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            Ptr<Output> candidate;
            for (UINT j = 0; adapter->EnumOutputs(j, &candidate) != DXGI_ERROR_NOT_FOUND; ++j) {
                DXGI_OUTPUT_DESC desc{};
                hr(candidate->GetDesc(&desc), "读取 DXGI 显示器失败");
                const auto &r = desc.DesktopCoordinates;
                QRect screen(r.left, r.top, r.right - r.left, r.bottom - r.top);
                if (desc.AttachedToDesktop && screen.contains(target)) {
                    selected = candidate;
                    bounds = screen;
                    rotation = desc.Rotation;
                    break;
                }
                candidate.Reset();
            }
            if (selected)
                break;
            adapter.Reset();
        }
        if (!selected)
            fail("DXGI 目标需完整位于同一显示器内，请移动窗口或选择 Win32 截图");
        hr(capture_duplication_test::createDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                                  D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                                  D3D11_SDK_VERSION, &device, nullptr, &context),
           "创建 DXGI 采集设备失败");
        Ptr<ExtendedOutput> output1;
        hr(selected.As(&output1), "显示器不支持 DXGI 桌面复制");
        hr(output1->DuplicateOutput(device.Get(), &output),
           "DXGI 桌面复制不可用，请检查桌面会话或改用 Win32");
    }
    QImage frame(QRect target, bool &changed) {
        changed = false;
        if (!bounds.contains(target))
            fail("目标窗口移出了 DXGI 显示器，请重新连接");
        DXGI_OUTDUPL_FRAME_INFO info{};
        Ptr<Resource> resource;
        const auto result = output->AcquireNextFrame(0, &info, &resource);
        if (result == DXGI_ERROR_WAIT_TIMEOUT) {
            // A successful timeout confirms the duplicated desktop is unchanged.
            return last;
        }
        hr(result, "DXGI 采集已中断，请重新连接（桌面切换或显示模式改变）");
        struct Release {
            OutputDuplication *output;
            ~Release() { output->ReleaseFrame(); }
        } release{output.Get()};
        // Pointer-only notifications do not alter the desktop texture.
        if (!last.isNull() && info.LastPresentTime.QuadPart == 0)
            return last;
        Ptr<Texture> texture;
        hr(resource.As(&texture), "DXGI 图像纹理不可用");
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        const bool rotated =
            rotation != DXGI_MODE_ROTATION_IDENTITY && rotation != DXGI_MODE_ROTATION_UNSPECIFIED;
        const auto crop = target.translated(-bounds.topLeft());
        D3D11_TEXTURE2D_DESC wanted = desc;
        wanted.Width = rotated ? desc.Width : UINT(crop.width());
        wanted.Height = rotated ? desc.Height : UINT(crop.height());
        wanted.BindFlags = 0;
        wanted.MiscFlags = 0;
        wanted.Usage = D3D11_USAGE_STAGING;
        wanted.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        D3D11_TEXTURE2D_DESC current{};
        if (staging)
            staging->GetDesc(&current);
        if (!staging || current.Width != wanted.Width || current.Height != wanted.Height ||
            current.Format != wanted.Format) {
            staging.Reset();
            hr(device->CreateTexture2D(&wanted, nullptr, &staging), "DXGI 读取缓冲区分配失败");
        }
        if (rotated)
            context->CopyResource(staging.Get(), texture.Get());
        else {
            D3D11_BOX box{UINT(crop.left()),
                          UINT(crop.top()),
                          0,
                          UINT(crop.x() + crop.width()),
                          UINT(crop.y() + crop.height()),
                          1};
            context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, texture.Get(), 0, &box);
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "DXGI 图像读取失败");
        struct Unmap {
            Context *context;
            Texture *texture;
            ~Unmap() { context->Unmap(texture, 0); }
        } unmap{context.Get(), staging.Get()};
        QImage image(static_cast<uchar *>(mapped.pData), int(wanted.Width), int(wanted.Height),
                     int(mapped.RowPitch), QImage::Format_RGB32);
        if (rotated) {
            const int angle = rotation == DXGI_MODE_ROTATION_ROTATE90    ? 90
                              : rotation == DXGI_MODE_ROTATION_ROTATE180 ? 180
                                                                         : 270;
            last = image.transformed(QTransform().rotate(angle)).copy(crop);
        } else
            last = image.copy();
        changed = true;
        return last;
    }
};
} // namespace original
namespace current {
struct Duplication {
    Ptr<Device> device;
    Ptr<Context> context;
    Ptr<OutputDuplication> output;
    Ptr<Texture> staging;
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
    Ptr<Factory> factory;
    Ptr<Adapter> adapter;
    Ptr<Output> selected, candidate;
    DXGI_OUTPUT_DESC outputDesc{};
    QRect screen;
    Ptr<ExtendedOutput> output1;
    DXGI_OUTDUPL_FRAME_INFO info{};
    Ptr<Resource> resource;
    HRESULT acquired = S_OK;
    struct Release {
        OutputDuplication *output;
        ~Release() { output->ReleaseFrame(); }
    };
    std::optional<Release> release;
    Ptr<Texture> texture;
    D3D11_TEXTURE2D_DESC desc{}, wanted{}, current{};
    QRect crop;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    struct Unmap {
        Context *context;
        Texture *texture;
        ~Unmap() { context->Unmap(texture, 0); }
    };
    std::optional<Unmap> unmap;
    QImage image;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::DesktopDupCreateFactory:
            hr(capture_duplication_test::createFactory(&factory), "DXGI 初始化失败");
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
            hr(capture_duplication_test::createDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                                      D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                                      D3D11_SDK_VERSION, &d.device, nullptr, &d.context),
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
} // namespace current
inline QJsonObject imageResult(const QImage &image) {
    return {{"width", image.width()},
            {"height", image.height()},
            {"hash", QString::fromLatin1(QCryptographicHash::hash(
                                             QByteArrayView(reinterpret_cast<const char *>(image.constBits()),
                                                            image.sizeInBytes()),
                                             QCryptographicHash::Sha256)
                                             .toHex())}};
}
template <class D> QJsonObject run() {
    QJsonObject result;
    const int x = active->scene % 7 == 0 ? 7 : active->scene % 5 == 0 ? 11 : 1;
    QRect target(x, 1, 4, 3);
    try {
        D duplication(target);
        QJsonArray frames;
        for (int i = 0; i < 3; ++i) {
            bool changed = true;
            try {
                const QRect current = active->scene % 19 == 0 && i == 1 ? target.translated(30, 0)
                                      : i == 2                          ? QRect(x, 1, 3, 2)
                                                                        : target;
                const auto frame = duplication.frame(current, changed);
                auto entry = imageResult(frame);
                entry["changed"] = changed;
                if (active->lastStaging)
                    active->lastStaging->bytes.fill(char(0));
                entry["after_reuse"] = imageResult(frame);
                frames.append(entry);
            } catch (const std::exception &e) {
                frames.append(QJsonObject{{"error", errorText(e)}, {"changed", changed}});
            }
        }
        result["frames"] = frames;
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    }
    return result;
}
inline QJsonObject outcome(int scene, bool rust) {
    State state(scene);
    active = &state;
    auto result = rust ? run<current::Duplication>() : run<original::Duplication>();
    result["trace"] = state.trace;
    return result;
}
} // namespace okww::capture_duplication_test
