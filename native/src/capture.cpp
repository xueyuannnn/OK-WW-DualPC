#include "capture.hpp"

#include <deque>
#include <dshow.h>
#include <dvdmedia.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace okww {
static const CLSID GrabberId = {0xc1f400a0, 0x3f08, 0x11d3, {0x9f, 0x0b, 0x00, 0x60, 0x08, 0x03, 0x9e, 0x37}};
static const CLSID NullId = {0xc1f400a4, 0x3f08, 0x11d3, {0x9f, 0x0b, 0x00, 0x60, 0x08, 0x03, 0x9e, 0x37}};
MIDL_INTERFACE("0579154A-2B53-4994-B0D0-E773148EFF85") GrabCB : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SampleCB(double, IMediaSample *) = 0;
    virtual HRESULT STDMETHODCALLTYPE BufferCB(double, BYTE *, long) = 0;
};
MIDL_INTERFACE("6B652FFF-11FE-4FCE-92AD-0266B5D7C78F") Grab : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(long *, long *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(IMediaSample **) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCallback(GrabCB *, long) = 0;
};
static void freeType(AM_MEDIA_TYPE *t, bool outer = false) {
    if (!t)
        return;
    if (t->cbFormat)
        CoTaskMemFree(t->pbFormat);
    if (t->pUnk)
        t->pUnk->Release();
    if (outer)
        CoTaskMemFree(t);
}
static BITMAPINFOHEADER *bitmap(AM_MEDIA_TYPE *t) {
    if (t->formattype == FORMAT_VideoInfo && t->cbFormat >= sizeof(VIDEOINFOHEADER))
        return &reinterpret_cast<VIDEOINFOHEADER *>(t->pbFormat)->bmiHeader;
    if (t->formattype == FORMAT_VideoInfo2 && t->cbFormat >= sizeof(VIDEOINFOHEADER2))
        return &reinterpret_cast<VIDEOINFOHEADER2 *>(t->pbFormat)->bmiHeader;
    return nullptr;
}
static REFERENCE_TIME *period(AM_MEDIA_TYPE *t) {
    if (t->formattype == FORMAT_VideoInfo)
        return &reinterpret_cast<VIDEOINFOHEADER *>(t->pbFormat)->AvgTimePerFrame;
    if (t->formattype == FORMAT_VideoInfo2)
        return &reinterpret_cast<VIDEOINFOHEADER2 *>(t->pbFormat)->AvgTimePerFrame;
    return nullptr;
}
static QString fourcc(DWORD v, int bits = 0) {
    char b[5]{};
    memcpy(b, &v, 4);
    bool ascii = true;
    for (int i = 0; i < 4; ++i)
        ascii &= b[i] >= 32 && b[i] < 127;
    if (ascii)
        return QString::fromLatin1(b, 4);
    return bits == 32 ? "RGB4" : bits == 24 ? "RGB3" : "";
}
static bool supported(const QString &f) {
    return QStringList{"NV12", "YUY2", "UYVY", "I420", "IYUV", "RGB3", "RGB4", "MJPG"}.contains(f);
}
static PixelFormat pixelFormat(const QString &s) {
    if (s == "NV12")
        return PixelFormat::NV12;
    if (s == "P010")
        return PixelFormat::P010;
    if (s == "YUY2")
        return PixelFormat::YUY2;
    if (s == "UYVY")
        return PixelFormat::UYVY;
    if (s == "I420" || s == "IYUV")
        return PixelFormat::I420;
    if (s == "RGB4")
        return PixelFormat::BGRA32;
    if (s == "MJPG")
        return PixelFormat::MJPG;
    if (s == "RGB3")
        return PixelFormat::BGR24;
    fail("暂不支持采集格式：" + s);
    return PixelFormat::BGR24;
}
static double rate(i64 period) {
    double v = 1e7 / period, rounded = std::round(v);
    return rounded > 0 && std::abs(period - 1e7 / rounded) <= 1 ? rounded : std::round(v * 1000) / 1000;
}
static std::vector<std::pair<Device, ComPtr<IMoniker>>> dsDevices() {
    ComPtr<ICreateDevEnum> en;
    hr(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&en)),
       "枚举采集设备失败");
    ComPtr<IEnumMoniker> mons;
    if (en->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &mons, 0) != S_OK)
        return {};
    std::vector<std::pair<Device, ComPtr<IMoniker>>> out;
    ComPtr<IMoniker> m;
    while (mons->Next(1, &m, nullptr) == S_OK) {
        ComPtr<IPropertyBag> bag;
        hr(m->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(&bag)), "读取设备属性失败");
        auto prop = [&](const wchar_t *name) {
            VARIANT v;
            VariantInit(&v);
            QString s;
            if (SUCCEEDED(bag->Read(name, &v, nullptr)) && v.vt == VT_BSTR)
                s = QString::fromWCharArray(v.bstrVal);
            VariantClear(&v);
            return s;
        };
        out.push_back({{int(out.size()), prop(L"FriendlyName"), prop(L"DevicePath")}, m});
        m.Reset();
    }
    return out;
}
static ComPtr<IBaseFilter> dsDevice(const Config &c) {
    auto list = dsDevices();
    std::vector<ComPtr<IMoniker>> matches;
    for (const auto &[d, m] : list) {
        bool match = !c.s("capture.device_path").isEmpty()
                         ? d.path.compare(c.s("capture.device_path"), Qt::CaseInsensitive) == 0
                     : !c.s("capture.device_name").isEmpty() ? d.name == c.s("capture.device_name")
                                                             : d.index == c.n("capture.device_index");
        if (match)
            matches.push_back(m);
    }
    if (matches.size() != 1)
        fail("采集卡未找到或名称重复，请重新扫描设备");
    ComPtr<IBaseFilter> filter;
    hr(matches[0]->BindToObject(nullptr, nullptr, IID_PPV_ARGS(&filter)), "打开采集卡失败");
    return filter;
}
static std::vector<ComPtr<IPin>> pins(IBaseFilter *f, PIN_DIRECTION dir) {
    ComPtr<IEnumPins> en;
    hr(f->EnumPins(&en), "枚举采集引脚失败");
    std::vector<ComPtr<IPin>> p;
    ComPtr<IPin> v;
    while (en->Next(1, &v, nullptr) == S_OK) {
        PIN_DIRECTION d;
        v->QueryDirection(&d);
        if (d == dir)
            p.push_back(v);
        v.Reset();
    }
    return p;
}
struct MfInit {
    MfInit() { hr(MFStartup(MF_VERSION, MFSTARTUP_LITE), "初始化媒体采集失败"); }
    ~MfInit() { MFShutdown(); }
};
static std::vector<std::pair<Device, ComPtr<IMFActivate>>> mfDevices() {
    ComPtr<IMFAttributes> a;
    hr(MFCreateAttributes(&a, 1), "创建媒体属性失败");
    a->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate **ptr = nullptr;
    UINT32 count = 0;
    hr(MFEnumDeviceSources(a.Get(), &ptr, &count), "枚举媒体设备失败");
    std::vector<std::pair<Device, ComPtr<IMFActivate>>> out;
    for (UINT32 i = 0; i < count; ++i) {
        ComPtr<IMFActivate> act;
        act.Attach(ptr[i]);
        auto get = [&](const GUID &k) {
            wchar_t *p = nullptr;
            UINT32 n = 0;
            QString r;
            if (SUCCEEDED(act->GetAllocatedString(k, &p, &n))) {
                r = QString::fromWCharArray(p, int(n));
                CoTaskMemFree(p);
            }
            return r;
        };
        out.push_back({{int(i), get(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME),
                        get(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK)},
                       act});
    }
    CoTaskMemFree(ptr);
    return out;
}
static ComPtr<IMFMediaSource> mfSource(const Config &c) {
    auto list = mfDevices();
    for (auto &[d, a] : list) {
        if ((!c.s("capture.device_path").isEmpty() &&
             d.path.compare(c.s("capture.device_path"), Qt::CaseInsensitive) == 0) ||
            (c.s("capture.device_path").isEmpty() &&
             (!c.s("capture.device_name").isEmpty() ? d.name == c.s("capture.device_name")
                                                    : d.index == c.n("capture.device_index")))) {
            ComPtr<IMFMediaSource> source;
            hr(a->ActivateObject(IID_PPV_ARGS(&source)), "打开采集设备失败");
            return source;
        }
    }
    fail("采集设备不存在，请重新扫描");
    return {};
}
static Mode mfMode(IMFMediaType *t) {
    UINT32 w = 0, h = 0, num = 0, den = 1;
    GUID sub{};
    MFGetAttributeSize(t, MF_MT_FRAME_SIZE, &w, &h);
    MFGetAttributeRatio(t, MF_MT_FRAME_RATE, &num, &den);
    t->GetGUID(MF_MT_SUBTYPE, &sub);
    QString fmt = sub == MFVideoFormat_RGB32   ? "RGB4"
                  : sub == MFVideoFormat_RGB24 ? "RGB3"
                                               : fourcc(sub.Data1);
    return {int(w), int(h), den ? double(num) / den : 0, fmt};
}
std::vector<Device> devices(const QString &backend) {
    ComInit com;
    std::vector<Device> out;
    if (backend == "DSHOW") {
        for (auto &p : dsDevices())
            out.push_back(p.first);
    } else {
        MfInit mf;
        for (auto &p : mfDevices())
            out.push_back(p.first);
    }
    return out;
}
std::vector<Mode> modes(const Config &c) {
    ComInit com;
    std::vector<Mode> out;
    if (c.s("capture.backend") == "DSHOW") {
        auto f = dsDevice(c);
        ComPtr<IAMVideoControl> vc;
        f.As(&vc);
        for (auto &p : pins(f.Get(), PINDIR_OUTPUT)) {
            ComPtr<IAMStreamConfig> cfg;
            if (FAILED(p.As(&cfg)))
                continue;
            int count = 0, size = 0;
            if (FAILED(cfg->GetNumberOfCapabilities(&count, &size)) || count < 0 || count > 4096 ||
                size < 1 || size > 65536)
                continue;
            std::vector<BYTE> cap(size);
            for (int i = 0; i < count; ++i) {
                AM_MEDIA_TYPE *t = nullptr;
                if (FAILED(cfg->GetStreamCaps(i, &t, cap.data())))
                    continue;
                auto b = bitmap(t);
                auto per = period(t);
                if (b && per && *per > 0) {
                    Mode m{b->biWidth, std::abs(b->biHeight), rate(*per),
                           fourcc(b->biCompression, b->biBitCount)};
                    if (supported(m.fourcc)) {
                        out.push_back(m);
                        if (vc) {
                            long countRates = 0;
                            LONGLONG *rates = nullptr;
                            SIZE sz{m.width, m.height};
                            if (SUCCEEDED(vc->GetFrameRateList(p.Get(), i, sz, &countRates, &rates)) &&
                                rates && countRates <= 4096) {
                                for (int j = 0; j < countRates; ++j)
                                    if (rates[j] > 0) {
                                        m.fps = rate(rates[j]);
                                        out.push_back(m);
                                    }
                            }
                            CoTaskMemFree(rates);
                        }
                    }
                }
                freeType(t, true);
            }
        }
    } else {
        MfInit mf;
        auto source = mfSource(c);
        ComPtr<IMFSourceReader> reader;
        hr(MFCreateSourceReaderFromMediaSource(source.Get(), nullptr, &reader), "读取采集能力失败");
        for (DWORD i = 0;; ++i) {
            ComPtr<IMFMediaType> t;
            if (FAILED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &t)))
                break;
            auto m = mfMode(t.Get());
            if (supported(m.fourcc) && m.fps > 0)
                out.push_back(m);
        }
        reader.Reset();
        source->Shutdown();
    }
    std::sort(out.begin(), out.end(), [](const Mode &a, const Mode &b) {
        return std::tie(a.width, a.height, a.fourcc, a.fps) < std::tie(b.width, b.height, b.fourcc, b.fps);
    });
    out.erase(std::unique(out.begin(), out.end(),
                          [](const Mode &a, const Mode &b) {
                              return a.width == b.width && a.height == b.height && a.fourcc == b.fourcc &&
                                     std::abs(a.fps - b.fps) < .001;
                          }),
              out.end());
    return out;
}
QJsonArray captureCapabilities(
    const Config &config,
    const std::function<std::vector<Device>(const QString &)> &enumerateDevices,
    const std::function<std::vector<Mode>(const Config &)> &enumerateModes) {
    QJsonArray results;
    for (const auto &device : enumerateDevices(config.s("capture.backend"))) {
        QJsonObject entry{{"name", device.name}, {"path", device.path}, {"index", device.index}};
        auto selected = config;
        selected.set("capture.device_name", device.name);
        selected.set("capture.device_path", device.path);
        selected.set("capture.device_index", device.index);
        QJsonArray parameters;
        try {
            for (const auto &mode : enumerateModes(selected))
                parameters.append(mode.json());
        } catch (const std::exception &error) {
            entry["error"] = errorText(error);
        }
        entry["modes"] = parameters;
        results.append(entry);
    }
    return results;
}

class Callback : public GrabCB {
    std::atomic<ULONG> refs{1};

  public:
    Capture *capture;
    int w, h, stride;
    PixelFormat fmt;
    bool bottom;
    Callback(Capture *c, int ww, int hh, int st, PixelFormat f, bool b)
        : capture(c), w(ww), h(hh), stride(st), fmt(f), bottom(b) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **p) override {
        if (!p)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(GrabCB)) {
            *p = static_cast<GrabCB *>(this);
            AddRef();
            return S_OK;
        }
        *p = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        auto n = --refs;
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE SampleCB(double, IMediaSample *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE BufferCB(double, BYTE *p, long n) override {
        try {
            capture->publish(p, size_t(std::max(0L, n)), w, h, stride, fmt, bottom, nowNs());
        } catch (...) {
            return E_FAIL;
        }
        return S_OK;
    }
};
void Capture::publish(const uint8_t *data, size_t length, int w, int h, int stride, PixelFormat fmt,
                      bool bottom, i64 stamp) {
    if (stop_)
        return;
    size_t expected = size_t(stride) * h;
    if (fmt == PixelFormat::NV12 || fmt == PixelFormat::I420 || fmt == PixelFormat::P010)
        expected = expected * 3 / 2;
    if (fmt != PixelFormat::MJPG && length < expected) {
        std::lock_guard lock(mutex_);
        error_ = "采集驱动返回了不完整图像";
        cv_.notify_all();
        return;
    }
    if (length > 256 * 1024 * 1024)
        return;
    std::shared_ptr<Frame> f;
    {
        std::lock_guard lock(mutex_);
        for (auto &slot : pool_)
            if (!slot || slot.use_count() == 1) {
                if (!slot)
                    slot = std::make_shared<Frame>();
                f = slot;
                break;
            }
    }
    if (!f)
        return;
    f->bytes.resize(qsizetype(length));
    memcpy(f->bytes.data(), data, length);
    f->width = w;
    f->height = h;
    f->stride = stride;
    f->format = fmt;
    f->bottomUp = bottom;
    f->captured = stamp;
    {
        std::lock_guard lock(mutex_);
        f->sequence = ++seq_;
        latest_ = f;
        if (error_ == "采集驱动返回了不完整图像")
            error_.clear();
        arrivals_.push_back(stamp);
        if (arrivals_.size() > 240)
            arrivals_.pop_front();
        ready_ = true;
    }
    cv_.notify_all();
}
void Capture::start() {
    if (thread_.joinable())
        fail("采集线程已经启动");
    config_.validateCapture(); // Reject quarantined modes before opening the device or creating a thread.
    thread_ = std::thread(&Capture::run, this);
    std::unique_lock lock(mutex_);
    if (!cv_.wait_for(lock, std::chrono::seconds(8), [&] { return ready_ || !error_.isEmpty(); }))
        fail("采集卡启动超时");
    if (!error_.isEmpty())
        fail(error_);
}
void Capture::stop() {
    stop_ = true;
    cv_.notify_all();
    if (thread_.joinable())
        thread_.join();
}
QString Capture::error() {
    std::lock_guard lock(mutex_);
    return error_;
}
FramePtr Capture::latest() {
    std::lock_guard lock(mutex_);
    return !stop_ ? latest_ : nullptr;
}
QJsonObject Capture::actual() {
    std::lock_guard lock(mutex_);
    if (!latest_)
        return {};
    return {{"width", latest_->width},
            {"height", latest_->height},
            {"fourcc", formatName(latest_->format)},
            {"backend", config_.s("capture.backend")},
            {"requested_fps", config_.d("capture.fps")},
            {"negotiated_fps", negotiatedFps_}};
}
double Capture::fps() {
    std::lock_guard lock(mutex_);
    return arrivals_.size() > 1 ? double(arrivals_.size() - 1) * 1e9 / (arrivals_.back() - arrivals_.front())
                                : 0;
}
FramePtr Capture::next(uint64_t after) {
    std::unique_lock lock(mutex_);
    cv_.wait_for(lock, std::chrono::milliseconds(20),
                 [&] { return stop_ || !error_.isEmpty() || (latest_ && latest_->sequence != after); });
    if (!error_.isEmpty())
        fail(error_);
    return !stop_ && latest_ && latest_->sequence != after ? latest_ : nullptr;
}
void Capture::run() {
    try {
        ComInit com;
        hr(com.result, "初始化采集 COM 失败");
        SetThreadDescription(GetCurrentThread(), L"OKWW Capture");
        if (config_.s("capture.backend") == "DSHOW")
            runDs();
        else
            runMf();
    } catch (const std::exception &e) {
        std::lock_guard lock(mutex_);
        error_ = errorText(e);
    }
    stop_ = true;
    cv_.notify_all();
}
void Capture::runDs() {
    auto source = dsDevice(config_);
    ComPtr<IGraphBuilder> graph;
    hr(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&graph)),
       "创建采集图失败");
    hr(graph->AddFilter(source.Get(), L"Capture"), "添加采集卡失败");
    ComPtr<IPin> selected;
    AM_MEDIA_TYPE *chosen = nullptr;
    for (auto &p : pins(source.Get(), PINDIR_OUTPUT)) {
        ComPtr<IAMStreamConfig> cfg;
        if (FAILED(p.As(&cfg)))
            continue;
        int count = 0, size = 0;
        if (FAILED(cfg->GetNumberOfCapabilities(&count, &size)) || size <= 0 || size > 65536 || count > 4096)
            continue;
        std::vector<BYTE> cap(size);
        for (int i = 0; i < count; ++i) {
            AM_MEDIA_TYPE *t = nullptr;
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
    ComPtr<IBaseFilter> grabFilter, nullFilter;
    hr(CoCreateInstance(GrabberId, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&grabFilter)),
       "创建原始帧采集器失败");
    hr(CoCreateInstance(NullId, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&nullFilter)),
       "创建空渲染器失败");
    ComPtr<Grab> grab;
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
    AM_MEDIA_TYPE actual{};
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
    ComPtr<GrabCB> cb;
    cb.Attach(new Callback(this, w, h, stride, fmt, bottom));
    grab->SetOneShot(FALSE);
    grab->SetBufferSamples(FALSE);
    hr(grab->SetCallback(cb.Get(), 1), "注册采集回调失败");
    ComPtr<IMediaControl> control;
    graph.As(&control);
    ComPtr<IMediaFilter> mf;
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
class MfCallback : public IMFSourceReaderCallback {
    std::atomic<ULONG> refs{1};

  public:
    std::mutex m;
    std::condition_variable cv;
    ComPtr<IMFSample> sample;
    HRESULT status = S_OK;
    DWORD flags = 0;
    bool arrived = false;
    i64 arrivedAt = 0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **p) override {
        if (!p)
            return E_POINTER;
        if (id == IID_IUnknown || id == __uuidof(IMFSourceReaderCallback)) {
            *p = this;
            AddRef();
            return S_OK;
        }
        *p = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        auto n = --refs;
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE OnReadSample(HRESULT s, DWORD, DWORD f, LONGLONG, IMFSample *p) override {
        {
            std::lock_guard lock(m);
            status = s;
            flags = f;
            sample = p;
            arrived = true;
            arrivedAt = nowNs();
        }
        cv.notify_one();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnEvent(DWORD, IMFMediaEvent *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnFlush(DWORD) override {
        cv.notify_all();
        return S_OK;
    }
};
void Capture::runMf() {
    MfInit mf;
    auto source = mfSource(config_);
    struct Shutdown {
        IMFMediaSource *p;
        ~Shutdown() { p->Shutdown(); }
    } shutdown{source.Get()};
    ComPtr<IMFAttributes> attr;
    MFCreateAttributes(&attr, 4);
    ComPtr<MfCallback> cb;
    cb.Attach(new MfCallback);
    attr->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, cb.Get());
    attr->SetUINT32(MF_LOW_LATENCY, TRUE);
    attr->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE);
    ComPtr<IMFSourceReader> reader;
    hr(MFCreateSourceReaderFromMediaSource(source.Get(), attr.Get(), &reader), "创建兼容采集接口失败");
    bool found = false;
    for (DWORD i = 0;; ++i) {
        ComPtr<IMFMediaType> t;
        if (FAILED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &t)))
            break;
        auto m = mfMode(t.Get());
        if (m.width == config_.n("capture.width") && m.height == config_.n("capture.height") &&
            m.fourcc == config_.s("capture.fourcc") && std::abs(m.fps - config_.d("capture.fps")) < .02) {
            hr(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, t.Get()),
               "设置兼容采集格式失败");
            found = true;
            break;
        }
    }
    if (!found)
        fail("兼容采集不支持所选配置，请重新扫描");
    ComPtr<IMFMediaType> t;
    reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &t);
    auto mode = mfMode(t.Get());
    {
        std::lock_guard lock(mutex_);
        negotiatedFps_ = mode.fps;
    }
    auto fmt = pixelFormat(mode.fourcc);
    UINT32 st = 0;
    t->GetUINT32(MF_MT_DEFAULT_STRIDE, &st);
    int stride = int(st);
    bool bottom = stride < 0;
    stride = std::abs(stride);
    if (!stride)
        stride = fmt == PixelFormat::BGR24    ? (mode.width * 3 + 3) & ~3
                 : fmt == PixelFormat::BGRA32 ? mode.width * 4
                 : fmt == PixelFormat::YUY2 || fmt == PixelFormat::UYVY || fmt == PixelFormat::P010
                     ? mode.width * 2
                     : mode.width;
    hr(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr),
       "读取兼容采集帧失败");
    while (!stop_) {
        std::unique_lock lock(cb->m);
        while (!cb->arrived && !stop_)
            cb->cv.wait_for(lock, std::chrono::milliseconds(20));
        if (stop_)
            break;
        auto sample = cb->sample;
        auto status = cb->status;
        auto flags = cb->flags;
        const i64 stamp = cb->arrivedAt;
        cb->sample.Reset();
        cb->arrived = false;
        lock.unlock();
        hr(status, "兼容采集失去信号");
        if (flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR |
                     MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED))
            fail("采集信号结束或格式已改变");
        // Let the device acquire its next frame while this one is copied.
        // There is still only one outstanding read and one latest frame.
        hr(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr),
           "读取兼容采集帧失败");
        if (!sample)
            continue;
        ComPtr<IMFMediaBuffer> buffer;
        hr(sample->ConvertToContiguousBuffer(&buffer), "获取采集缓冲区失败");
        BYTE *p = nullptr;
        DWORD n = 0;
        hr(buffer->Lock(&p, nullptr, &n), "锁定采集帧失败");
        try {
            publish(p, n, mode.width, mode.height, stride, fmt, bottom, stamp);
        } catch (...) {
            buffer->Unlock();
            throw;
        }
        buffer->Unlock();
    }
    reader->Flush(MF_SOURCE_READER_ALL_STREAMS);
    reader.Reset();
}
} // namespace okww
