#pragma once
#include "capture.hpp"
#include "capture_math_abi.hpp"
#include "platform_workflow.hpp"
#include <dshow.h>
namespace okww::capture_dshow_test {
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
struct MediaType {
    int id = -1;
    BITMAPINFOHEADER header{};
    REFERENCE_TIME per = 83333;
    bool bitmap = true, period = true;
};
struct State {
    int scene, waits = 0;
    QJsonArray trace;
    std::vector<std::unique_ptr<Ref>> objects;
    std::vector<std::unique_ptr<MediaType>> types;
    std::atomic<bool> *stop = nullptr;
    void *owner = nullptr;
    explicit State(int s) : scene(s) {}
    template <class T, class... A> T *make(A &&...a) {
        auto p = std::make_unique<T>(std::forward<A>(a)...);
        auto *raw = p.get();
        objects.push_back(std::move(p));
        return raw;
    }
    HRESULT step(const QString &name, int id = -1) {
        trace.append(QJsonObject{{"call", name}, {"id", id}});
        const QStringList failures{"none",           "graph",          "add-source",   "grabber",
                                   "null",           "grab-interface", "set-type",     "connect-source",
                                   "connect-output", "actual",         "set-callback", "run"};
        return name == failures[(scene / 32) % failures.size()] ? E_FAIL : S_OK;
    }
};
inline void Ref::add() { active->trace.append(QJsonObject{{"add", kind}, {"id", id}, {"refs", ++refs}}); }
inline void Ref::release() {
    active->trace.append(QJsonObject{{"release", kind}, {"id", id}, {"refs", --refs}});
}
inline DWORD code(const char *text) {
    DWORD v;
    memcpy(&v, text, 4);
    return v;
}
struct StreamConfig : Ref {
    explicit StreamConfig(int i) : Ref("config", i) {}
    HRESULT GetNumberOfCapabilities(int *count, int *size) {
        active->trace.append(QJsonObject{{"counts", id}});
        *count = active->scene % 37 == 0 ? -1 : active->scene % 41 == 0 ? 4097 : 3;
        *size = active->scene % 43 == 0 ? 0 : active->scene % 47 == 0 ? 65537 : 64;
        return active->scene % 53 ? S_OK : E_FAIL;
    }
    HRESULT GetStreamCaps(int i, MediaType **out, BYTE *) {
        active->trace.append(QJsonObject{{"caps", id}, {"index", i}});
        if ((active->scene + i) % 17 == 0)
            return E_FAIL;
        auto p = std::make_unique<MediaType>();
        p->id = id * 10 + i;
        p->header.biWidth = i == 0 ? 1280 : 1920;
        p->header.biHeight = active->scene % 2 ? -1080 : 1080;
        p->header.biCompression = code(i == 2 ? "YUY2" : "NV12");
        p->header.biBitCount = 12;
        p->bitmap = (active->scene + i) % 29 != 0;
        p->period = (active->scene + i) % 31 != 0;
        *out = p.get();
        active->types.push_back(std::move(p));
        return S_OK;
    }
    HRESULT SetFormat(MediaType *t) {
        active->trace.append(QJsonObject{{"set-format", t->id}, {"period", qint64(t->per)}});
        return (active->scene + t->id) % 13 ? S_OK : E_FAIL;
    }
};
struct Pin : Ref {
    explicit Pin(int i) : Ref("pin", i) {}
};
struct Filter : Ref {
    explicit Filter(int i = 0) : Ref("filter", i) {}
};
struct GrabCB : Ref {
    GrabCB() : Ref("callback", 0) {}
};
struct Callback : GrabCB {
    template <class C> Callback(C *owner, int w, int h, int stride, PixelFormat fmt, bool bottom) {
        active->trace.append(QJsonObject{{"callback", owner == active->owner},
                                         {"width", w},
                                         {"height", h},
                                         {"stride", stride},
                                         {"format", int(fmt)},
                                         {"bottom", bottom}});
        active->objects.emplace_back(this);
    }
};
struct Grab : Ref {
    Grab() : Ref("grab", 0) {}
    HRESULT SetMediaType(MediaType *t) {
        active->trace.append(QJsonObject{{"chosen", t->id}});
        return active->step("set-type");
    }
    HRESULT GetConnectedMediaType(MediaType *out) {
        HRESULT result = active->step("actual");
        if (FAILED(result))
            return result;
        out->id = 999;
        out->header.biWidth = active->scene % 7 ? 1920 : 1919;
        out->header.biHeight = active->scene % 2 ? -1080 : 1080;
        const char *names[]{"RGB3", "RGB4", "NV12", "P010", "YUY2", "UYVY", "I420", "MJPG", "NONE"};
        out->header.biCompression = code(names[active->scene % 9]);
        out->header.biBitCount = 0;
        out->bitmap = active->scene % 19 != 0;
        out->period = active->scene % 23 != 0;
        out->per = active->scene % 11 ? 41667 : 0;
        return result;
    }
    HRESULT SetOneShot(BOOL value) {
        active->trace.append(QJsonObject{{"one-shot", int(value)}});
        return E_FAIL;
    }
    HRESULT SetBufferSamples(BOOL value) {
        active->trace.append(QJsonObject{{"buffered", int(value)}});
        return E_FAIL;
    }
    HRESULT SetCallback(GrabCB *cb, long) { return active->step(cb ? "set-callback" : "clear-callback"); }
};
struct Control : Ref {
    Control() : Ref("control", 0) {}
    HRESULT Run() { return active->step("run"); }
    HRESULT Stop() { return active->step("stop"); }
};
struct MediaFilter : Ref {
    MediaFilter() : Ref("media-filter", 0) {}
    HRESULT SetSyncSource(void *) { return active->step("sync"); }
};
struct Graph : Ref {
    Graph() : Ref("graph", 0) {}
    HRESULT AddFilter(Filter *, const wchar_t *name) {
        const QString text = QString::fromWCharArray(name);
        return active->step(text == "Capture" ? "add-source" : text == "Null" ? "add-null" : "add-grabber");
    }
    HRESULT ConnectDirect(Pin *from, Pin *to, void *) {
        active->trace.append(QJsonObject{{"from", from->id}, {"to", to->id}});
        return active->step(from->id < 10 ? "connect-source" : "connect-output");
    }
};
template <class T> template <class U> HRESULT Ptr<T>::As(Ptr<U> *out) const {
    if constexpr (std::is_same_v<U, StreamConfig>) {
        active->trace.append(QJsonObject{{"query-config", value->id}});
        if ((active->scene + value->id) % 29 == 0)
            return E_NOINTERFACE;
        out->Attach(active->make<U>(value->id));
    } else if constexpr (std::is_same_v<U, Grab>) {
        HRESULT result = active->step("grab-interface");
        if (FAILED(result))
            return result;
        out->Attach(active->make<U>());
    } else if constexpr (std::is_same_v<U, Control>) {
        active->step("control-interface");
        out->Attach(active->make<U>());
    } else {
        active->step("media-filter-interface");
        out->Attach(active->make<U>());
    }
    return S_OK;
}
inline const CLSID GrabberId = {1}, NullId = {2};
inline HRESULT create(REFCLSID, void *, DWORD, Ptr<Graph> *out) {
    HRESULT result = active->step("graph");
    if (SUCCEEDED(result))
        out->Attach(active->make<Graph>());
    return result;
}
inline HRESULT create(REFCLSID id, void *, DWORD, Ptr<Filter> *out) {
    const bool grab = id == GrabberId;
    HRESULT result = active->step(grab ? "grabber" : "null");
    if (SUCCEEDED(result))
        out->Attach(active->make<Filter>(grab ? 1 : 2));
    return result;
}
inline Ptr<Filter> dsDevice(const Config &) {
    active->step("source");
    if (active->scene % 61 == 0)
        throw std::runtime_error("source fixture");
    Ptr<Filter> p;
    p.Attach(active->make<Filter>());
    return p;
}
inline std::vector<Ptr<Pin>> pins(Filter *f, PIN_DIRECTION direction) {
    active->trace.append(QJsonObject{{"pins", f->id}, {"direction", int(direction)}});
    if (active->scene % 59 == 0)
        throw 27;
    std::vector<Ptr<Pin>> list;
    const int count = f->id ? 1 : 2;
    for (int i = 0; i < count; ++i) {
        Ptr<Pin> p;
        p.Attach(active->make<Pin>(f->id * 10 + int(direction) * 5 + i));
        list.push_back(std::move(p));
    }
    return list;
}
inline BITMAPINFOHEADER *bitmap(MediaType *t) {
    active->trace.append(QJsonObject{{"bitmap", t->id}});
    return t->bitmap ? &t->header : nullptr;
}
inline REFERENCE_TIME *period(MediaType *t) {
    active->trace.append(QJsonObject{{"period", t->id}});
    return t->period ? &t->per : nullptr;
}
inline void freeType(MediaType *t, bool outer = false) {
    active->trace.append(QJsonObject{{"free-type", t->id}, {"outer", outer}});
}
inline QString fourcc(DWORD v, int bits = 0) {
    std::array<uint8_t, 4> text{};
    int n = rust::okww_rs_capture_fourcc(v, bits, text.data());
    return QString::fromLatin1(reinterpret_cast<const char *>(text.data()), n);
}
inline double rate(i64 value) { return rust::okww_rs_capture_rate(value); }
inline PixelFormat pixelFormat(const QString &text) {
    int code = rust::okww_rs_capture_format(reinterpret_cast<const uint16_t *>(text.constData()),
                                            size_t(text.size()));
    if (code < 0)
        fail("暂不支持采集格式：" + text);
    return PixelFormat(code);
}
struct Cv {
    template <class L, class D, class P> void wait_for(L &, D duration, P predicate) {
        active->trace.append(QJsonObject{
            {"wait", int(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count())},
            {"predicate", predicate()}});
        if (++active->waits >= active->scene % 4 + 1)
            *active->stop = true;
    }
};
struct CaptureState {
    Config config_ = Config::defaults();
    std::mutex mutex_;
    Cv cv_;
    std::atomic<bool> stop_{false};
    double negotiatedFps_ = 0;
};
namespace original {
struct Capture : CaptureState {
    void runDs();
};
void Capture::runDs() {
    auto source = dsDevice(config_);
    Ptr<Graph> graph;
    hr(capture_dshow_test::create(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, &graph),
       "创建采集图失败");
    hr(graph->AddFilter(source.Get(), L"Capture"), "添加采集卡失败");
    Ptr<Pin> selected;
    MediaType *chosen = nullptr;
    for (auto &p : pins(source.Get(), PINDIR_OUTPUT)) {
        Ptr<StreamConfig> cfg;
        if (FAILED(p.As(&cfg)))
            continue;
        int count = 0, size = 0;
        if (FAILED(cfg->GetNumberOfCapabilities(&count, &size)) || size <= 0 || size > 65536 || count > 4096)
            continue;
        std::vector<BYTE> cap(size);
        for (int i = 0; i < count; ++i) {
            MediaType *t = nullptr;
            if (FAILED(cfg->GetStreamCaps(i, &t, cap.data())))
                continue;
            auto b = bitmap(t);
            auto per = period(t);
            if (b && per && b->biWidth == config_.n("capture.width") &&
                std::abs(b->biHeight) == config_.n("capture.height") &&
                fourcc(b->biCompression, b->biBitCount) == config_.s("capture.fourcc")) {
                *per = llround(1e7 / config_.d("capture.fps"));
                if (SUCCEEDED(cfg->SetFormat(t))) {
                    selected = p;
                    chosen = t;
                    break;
                }
            }
            freeType(t, true);
        }
        if (selected)
            break;
    }
    if (!selected)
        fail("驱动拒绝所选格式、分辨率或帧率，请重新扫描配置");
    Ptr<Filter> grabFilter, nullFilter;
    hr(capture_dshow_test::create(GrabberId, nullptr, CLSCTX_INPROC_SERVER, &grabFilter),
       "创建原始帧采集器失败");
    hr(capture_dshow_test::create(NullId, nullptr, CLSCTX_INPROC_SERVER, &nullFilter), "创建空渲染器失败");
    Ptr<Grab> grab;
    hr(grabFilter.As(&grab), "读取原始帧接口失败");
    hr(grab->SetMediaType(chosen), "设置原始格式失败");
    freeType(chosen, true);
    graph->AddFilter(grabFilter.Get(), L"Raw frames");
    graph->AddFilter(nullFilter.Get(), L"Null");
    hr(graph->ConnectDirect(selected.Get(), pins(grabFilter.Get(), PINDIR_INPUT).at(0).Get(), nullptr),
       "连接原始采集帧失败");
    hr(graph->ConnectDirect(pins(grabFilter.Get(), PINDIR_OUTPUT).at(0).Get(),
                            pins(nullFilter.Get(), PINDIR_INPUT).at(0).Get(), nullptr),
       "连接采集输出失败");
    MediaType actual{};
    hr(grab->GetConnectedMediaType(&actual), "获取实际格式失败");
    auto b = bitmap(&actual);
    if (!b) {
        freeType(&actual);
        fail("采集卡实际格式无效");
    }
    int w = b->biWidth, h = std::abs(b->biHeight);
    if (auto p = period(&actual); p && *p > 0) {
        std::lock_guard lock(mutex_);
        negotiatedFps_ = rate(*p);
    }
    auto fmt = pixelFormat(fourcc(b->biCompression, b->biBitCount));
    int stride = fmt == PixelFormat::BGR24    ? ((w * 3 + 3) & ~3)
                 : fmt == PixelFormat::BGRA32 ? w * 4
                 : fmt == PixelFormat::YUY2 || fmt == PixelFormat::UYVY || fmt == PixelFormat::P010 ? w * 2
                                                                                                    : w;
    bool bottom = b->biHeight > 0 && (fmt == PixelFormat::BGR24 || fmt == PixelFormat::BGRA32);
    freeType(&actual);
    Ptr<GrabCB> cb;
    cb.Attach(new Callback(this, w, h, stride, fmt, bottom));
    grab->SetOneShot(FALSE);
    grab->SetBufferSamples(FALSE);
    hr(grab->SetCallback(cb.Get(), 1), "注册采集回调失败");
    Ptr<Control> control;
    graph.As(&control);
    Ptr<MediaFilter> mf;
    graph.As(&mf);
    mf->SetSyncSource(nullptr);
    hr(control->Run(), "启动采集失败（设备可能已被占用）");
    while (!stop_) {
        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(50), [&] { return stop_.load(); });
    }
    control->Stop();
    grab->SetCallback(nullptr, 1);
}
} // namespace original
namespace current {
struct Capture : CaptureState {
    void runDs();
};
struct CaptureDsRunSdk {
    Capture &c;
    Ptr<Filter> source;
    Ptr<Graph> graph;
    Ptr<Pin> selected;
    MediaType *chosen = nullptr;
    std::vector<Ptr<Pin>> pinList;
    size_t pinIndex = 0;
    Ptr<StreamConfig> cfg;
    int count = 0, size = 0;
    std::vector<BYTE> cap;
    MediaType *type = nullptr;
    BITMAPINFOHEADER *b = nullptr;
    REFERENCE_TIME *per = nullptr;
    Ptr<Filter> grabFilter, nullFilter;
    Ptr<Grab> grab;
    MediaType actual{};
    int w = 0, h = 0, stride = 0;
    PixelFormat fmt = PixelFormat::BGR24;
    bool bottom = false;
    Ptr<GrabCB> cb;
    Ptr<Control> control;
    Ptr<MediaFilter> mf;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::CaptureDsRunSource:
            source = dsDevice(c.config_);
            break;
        case rust::WorkflowOp::CaptureDsCreateGraph:
            hr(capture_dshow_test::create(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, &graph),
               "创建采集图失败");
            break;
        case rust::WorkflowOp::CaptureDsAddSource:
            hr(graph->AddFilter(source.Get(), L"Capture"), "添加采集卡失败");
            break;
        case rust::WorkflowOp::CaptureDsRunPins:
            pinList = pins(source.Get(), PINDIR_OUTPUT);
            break;
        case rust::WorkflowOp::CaptureDsRunPinCount:
            out[0] = double(pinList.size());
            break;
        case rust::WorkflowOp::CaptureDsRunSelectPin:
            pinIndex = size_t(a[0]);
            break;
        case rust::WorkflowOp::CaptureDsRunStreamConfig:
            out[0] = SUCCEEDED(pinList.at(pinIndex).As(&cfg));
            break;
        case rust::WorkflowOp::CaptureDsRunCapabilityCounts:
            count = size = 0;
            out[0] = SUCCEEDED(cfg->GetNumberOfCapabilities(&count, &size));
            out[1] = count;
            out[2] = size;
            break;
        case rust::WorkflowOp::CaptureDsRunAllocateCaps:
            cap = std::vector<BYTE>(size);
            break;
        case rust::WorkflowOp::CaptureDsRunStreamCaps:
            type = nullptr;
            out[0] = SUCCEEDED(cfg->GetStreamCaps(int(a[0]), &type, cap.data()));
            break;
        case rust::WorkflowOp::CaptureDsRunReadBitmap:
            b = bitmap(type);
            break;
        case rust::WorkflowOp::CaptureDsRunReadPeriod:
            per = period(type);
            break;
        case rust::WorkflowOp::CaptureDsRunBitmapExists:
            out[0] = b != nullptr;
            break;
        case rust::WorkflowOp::CaptureDsRunPeriodExists:
            out[0] = per != nullptr;
            break;
        case rust::WorkflowOp::CaptureDsRunWidthMatches:
            out[0] = b->biWidth == c.config_.n("capture.width");
            break;
        case rust::WorkflowOp::CaptureDsRunHeightMatches:
            out[0] = std::abs(b->biHeight) == c.config_.n("capture.height");
            break;
        case rust::WorkflowOp::CaptureDsRunFormatMatches:
            out[0] = fourcc(b->biCompression, b->biBitCount) == c.config_.s("capture.fourcc");
            break;
        case rust::WorkflowOp::CaptureDsRunRequestedRate:
            out[0] = c.config_.d("capture.fps");
            break;
        case rust::WorkflowOp::CaptureDsRunSetPeriod:
            *per = llround(a[0]);
            break;
        case rust::WorkflowOp::CaptureDsRunSetFormat:
            out[0] = SUCCEEDED(cfg->SetFormat(type));
            break;
        case rust::WorkflowOp::CaptureDsRunChoose:
            selected = pinList.at(pinIndex);
            chosen = type;
            break;
        case rust::WorkflowOp::CaptureDsRunFreeType:
            freeType(type, true);
            break;
        case rust::WorkflowOp::CaptureDsRunDropPinScope:
            cap = std::vector<BYTE>{};
            cfg.Reset();
            break;
        case rust::WorkflowOp::CaptureDsRunSelected:
            out[0] = bool(selected);
            break;
        case rust::WorkflowOp::CaptureDsRunDropPins:
            pinList = decltype(pinList){};
            break;
        case rust::WorkflowOp::CaptureDsRunFailure:
            if (a[0] == 0)
                fail("驱动拒绝所选格式、分辨率或帧率，请重新扫描配置");
            else
                fail("采集卡实际格式无效");
            break;
        case rust::WorkflowOp::CaptureDsCreateGrabber:
            hr(capture_dshow_test::create(GrabberId, nullptr, CLSCTX_INPROC_SERVER, &grabFilter),
               "创建原始帧采集器失败");
            break;
        case rust::WorkflowOp::CaptureDsCreateNull:
            hr(capture_dshow_test::create(NullId, nullptr, CLSCTX_INPROC_SERVER, &nullFilter),
               "创建空渲染器失败");
            break;
        case rust::WorkflowOp::CaptureDsGrabInterface:
            hr(grabFilter.As(&grab), "读取原始帧接口失败");
            break;
        case rust::WorkflowOp::CaptureDsGrabSetType:
            hr(grab->SetMediaType(chosen), "设置原始格式失败");
            break;
        case rust::WorkflowOp::CaptureDsFreeChosen:
            freeType(chosen, true);
            break;
        case rust::WorkflowOp::CaptureDsAddGrabber:
            graph->AddFilter(grabFilter.Get(), L"Raw frames");
            break;
        case rust::WorkflowOp::CaptureDsAddNull:
            graph->AddFilter(nullFilter.Get(), L"Null");
            break;
        case rust::WorkflowOp::CaptureDsConnectSource:
            hr(graph->ConnectDirect(selected.Get(), pins(grabFilter.Get(), PINDIR_INPUT).at(0).Get(),
                                    nullptr),
               "连接原始采集帧失败");
            break;
        case rust::WorkflowOp::CaptureDsConnectOutput:
            hr(graph->ConnectDirect(pins(grabFilter.Get(), PINDIR_OUTPUT).at(0).Get(),
                                    pins(nullFilter.Get(), PINDIR_INPUT).at(0).Get(), nullptr),
               "连接采集输出失败");
            break;
        case rust::WorkflowOp::CaptureDsConnectedType:
            hr(grab->GetConnectedMediaType(&actual), "获取实际格式失败");
            break;
        case rust::WorkflowOp::CaptureDsActualBitmap:
            b = bitmap(&actual);
            break;
        case rust::WorkflowOp::CaptureDsActualBitmapExists:
            out[0] = b != nullptr;
            break;
        case rust::WorkflowOp::CaptureDsActualDimensions:
            w = b->biWidth;
            h = std::abs(b->biHeight);
            break;
        case rust::WorkflowOp::CaptureDsActualPeriod:
            per = period(&actual);
            break;
        case rust::WorkflowOp::CaptureDsActualPeriodExists:
            out[0] = per != nullptr;
            break;
        case rust::WorkflowOp::CaptureDsActualPositivePeriod:
            out[0] = *per > 0;
            break;
        case rust::WorkflowOp::CaptureDsNegotiatedRate: {
            std::lock_guard lock(c.mutex_);
            c.negotiatedFps_ = rate(*per);
            break;
        }
        case rust::WorkflowOp::CaptureDsActualPixelFormat:
            fmt = pixelFormat(fourcc(b->biCompression, b->biBitCount));
            break;
        case rust::WorkflowOp::CaptureDsActualLayout:
            stride = rust::okww_rs_capture_stride(w, int(fmt));
            bottom = rust::okww_rs_capture_bottom(b->biHeight, int(fmt)) != 0;
            break;
        case rust::WorkflowOp::CaptureDsFreeActual:
            freeType(&actual);
            break;
        case rust::WorkflowOp::CaptureDsCreateCallback:
            cb.Attach(new Callback(&c, w, h, stride, fmt, bottom));
            break;
        case rust::WorkflowOp::CaptureDsSetOneShot:
            grab->SetOneShot(FALSE);
            break;
        case rust::WorkflowOp::CaptureDsSetBufferSamples:
            grab->SetBufferSamples(FALSE);
            break;
        case rust::WorkflowOp::CaptureDsSetCallback:
            hr(grab->SetCallback(cb.Get(), 1), "注册采集回调失败");
            break;
        case rust::WorkflowOp::CaptureDsControlInterface:
            graph.As(&control);
            break;
        case rust::WorkflowOp::CaptureDsMediaFilterInterface:
            graph.As(&mf);
            break;
        case rust::WorkflowOp::CaptureDsUnsetSyncSource:
            mf->SetSyncSource(nullptr);
            break;
        case rust::WorkflowOp::CaptureDsControlRun:
            hr(control->Run(), "启动采集失败（设备可能已被占用）");
            break;
        case rust::WorkflowOp::CaptureDsRunStopped:
            out[0] = bool(c.stop_);
            break;
        case rust::WorkflowOp::CaptureDsRunWait: {
            std::unique_lock lock(c.mutex_);
            c.cv_.wait_for(lock, std::chrono::milliseconds(50), [&] { return c.stop_.load(); });
            break;
        }
        case rust::WorkflowOp::CaptureDsControlStop:
            control->Stop();
            break;
        case rust::WorkflowOp::CaptureDsClearCallback:
            grab->SetCallback(nullptr, 1);
            break;
        default:
            fail("Rust DirectShow 采集宿主无效");
        }
    }
};
void Capture::runDs() {
    CaptureDsRunSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::CaptureRunDshow, {}, sdk);
}
} // namespace current
template <class C> QJsonObject run() {
    C capture;
    active->owner = &capture;
    active->stop = &capture.stop_;
    const double rates[]{120., 240., 59.94, 29.97};
    capture.config_.set("capture.width", active->scene % 5 ? 1920 : 1280);
    capture.config_.set("capture.fps", rates[active->scene % 4]);
    QJsonObject result;
    try {
        capture.runDs();
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    } catch (...) {
        result["error"] = "unknown";
    }
    result["negotiated"] = capture.negotiatedFps_;
    result["stopped"] = bool(capture.stop_);
    return result;
}
inline QJsonObject outcome(int scene, bool rust) {
    State state(scene);
    active = &state;
    auto result = rust ? run<current::Capture>() : run<original::Capture>();
    result["trace"] = state.trace;
    return result;
}
} // namespace okww::capture_dshow_test
