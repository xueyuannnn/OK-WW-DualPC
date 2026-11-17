#pragma once
#include "capture.hpp"
#include "capture_math_abi.hpp"
#include "platform_workflow.hpp"
#include <dshow.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <optional>
namespace okww::capture_modes_test {
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
    int id;
    BITMAPINFOHEADER header{};
    REFERENCE_TIME per = 83333;
    bool bitmap = true, period = true;
};
struct State {
    int scene;
    QJsonArray trace;
    std::vector<std::unique_ptr<Ref>> objects;
    std::vector<std::unique_ptr<MediaType>> types;
    std::array<LONGLONG, 5> rates{83333, 41667, -1, 0, 41667};
    explicit State(int s) : scene(s) {}
    template <class T, class... A> T *make(A &&...a) {
        auto p = std::make_unique<T>(std::forward<A>(a)...);
        auto *raw = p.get();
        objects.push_back(std::move(p));
        return raw;
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
        const int counts[]{-1, 0, 1, 4, 4097, 2, 3, 5};
        const int sizes[]{0, 1, 64, 65536, 65537};
        *count = counts[(active->scene + id) % 8];
        *size = sizes[(active->scene / 8 + id) % 5];
        return (active->scene + id) % 31 ? S_OK : E_FAIL;
    }
    HRESULT GetStreamCaps(int i, MediaType **out, BYTE *) {
        active->trace.append(QJsonObject{{"caps", id}, {"index", i}});
        if ((active->scene + id + i) % 7 == 0)
            return E_FAIL;
        auto p = std::make_unique<MediaType>();
        p->id = id * 10 + i;
        p->header.biWidth = (i % 3 ? 1920 : 1280);
        p->header.biHeight = (i % 2 ? 1080 : -1080);
        p->header.biCompression = code((i + active->scene) % 4 ? "NV12" : "YUY2");
        p->header.biBitCount = 12;
        p->per = (i + active->scene) % 11 ? 83333 : 0;
        p->bitmap = (i + active->scene) % 13 != 0;
        p->period = (i + active->scene) % 17 != 0;
        *out = p.get();
        active->types.push_back(std::move(p));
        return S_OK;
    }
};
struct Pin : Ref {
    explicit Pin(int i) : Ref("pin", i) {}
};
struct VideoControl : Ref {
    VideoControl() : Ref("video-control", 0) {}
    HRESULT GetFrameRateList(Pin *p, int index, SIZE size, long *count, LONGLONG **out) {
        active->trace.append(QJsonObject{
            {"rates", p->id}, {"index", index}, {"width", int(size.cx)}, {"height", int(size.cy)}});
        const long counts[]{-1, 0, 1, 5, 4097};
        *count = counts[(active->scene + index) % 5];
        *out = active->scene % 9 ? active->rates.data() : nullptr;
        return (active->scene + index) % 11 ? S_OK : E_FAIL;
    }
};
struct Filter : Ref {
    Filter() : Ref("filter", 0) {}
};
struct Source : Ref {
    Source() : Ref("source", 0) {}
    HRESULT Shutdown() {
        active->trace.append("shutdown");
        return S_OK;
    }
};
struct Media : Ref {
    explicit Media(int i) : Ref("type", i) {}
};
struct Reader : Ref {
    Reader() : Ref("reader", 0) {}
    HRESULT GetNativeMediaType(DWORD, DWORD index, Ptr<Media> *out) {
        active->trace.append(QJsonObject{{"native-type", int(index)}});
        if (index >= UINT32(active->scene % 7)) {
            if (active->scene % 23 == 0)
                out->Attach(active->make<Media>(999));
            return E_FAIL;
        }
        out->Attach(active->make<Media>(int(index)));
        return S_OK;
    }
};
template <class T> template <class U> HRESULT Ptr<T>::As(Ptr<U> *out) const {
    const QString target = std::is_same_v<U, VideoControl> ? "video-control" : "config";
    active->trace.append(QJsonObject{{"query", value->kind}, {"id", value->id}, {"target", target}});
    if constexpr (std::is_same_v<U, VideoControl>) {
        if (active->scene % 7 == 0)
            return E_NOINTERFACE;
        out->Attach(active->make<U>());
    } else {
        if ((active->scene + value->id) % 11 == 0)
            return E_NOINTERFACE;
        out->Attach(active->make<U>(value->id));
    }
    return S_OK;
}
struct Com {
    Com() { active->trace.append("com"); }
    ~Com() { active->trace.append("com-end"); }
};
struct Mf {
    Mf() {
        active->trace.append("mf");
        if (active->scene % 47 == 0)
            throw std::runtime_error("mf startup");
    }
    ~Mf() { active->trace.append("mf-end"); }
};
inline Ptr<Filter> dsDevice(const Config &) {
    active->trace.append("ds-source");
    if (active->scene % 43 == 0)
        throw std::runtime_error("source fixture");
    Ptr<Filter> p;
    p.Attach(active->make<Filter>());
    return p;
}
inline std::vector<Ptr<Pin>> pins(Filter *, PIN_DIRECTION direction) {
    active->trace.append(QJsonObject{{"pins", int(direction)}});
    if (active->scene % 41 == 0)
        throw 27;
    std::vector<Ptr<Pin>> out;
    for (int i = 0; i < active->scene % 4; ++i) {
        Ptr<Pin> p;
        p.Attach(active->make<Pin>(i));
        out.push_back(std::move(p));
    }
    return out;
}
inline Ptr<Source> mfSource(const Config &) {
    active->trace.append("mf-source");
    if (active->scene % 43 == 0)
        throw std::runtime_error("source fixture");
    Ptr<Source> p;
    p.Attach(active->make<Source>());
    return p;
}
inline HRESULT createReader(Source *, void *, Ptr<Reader> *out) {
    active->trace.append("create-reader");
    if (active->scene % 37 == 0)
        return E_FAIL;
    out->Attach(active->make<Reader>());
    return S_OK;
}
inline Mode mfMode(Media *type) {
    active->trace.append(QJsonObject{{"mf-mode", type->id}});
    if ((active->scene + type->id) % 29 == 0)
        throw std::runtime_error("mode fixture");
    const double rates[]{0, 120, 120.0004, 240, -1};
    return {type->id % 3 ? 1920 : 1280, 1080, rates[(active->scene + type->id) % 5],
            type->id % 4 ? "NV12" : "RGB4"};
}
inline BITMAPINFOHEADER *bitmap(MediaType *t) {
    active->trace.append(QJsonObject{{"bitmap", t->id}});
    return t->bitmap ? &t->header : nullptr;
}
inline REFERENCE_TIME *period(MediaType *t) {
    active->trace.append(QJsonObject{{"period", t->id}});
    return t->period ? &t->per : nullptr;
}
inline void freeType(MediaType *t, bool) { active->trace.append(QJsonObject{{"free-type", t->id}}); }
inline void freeRates(void *p) { active->trace.append(p ? "free-rates" : "free-null-rates"); }
inline QString fourcc(DWORD v, int bits = 0) {
    std::array<uint8_t, 4> text{};
    int n = rust::okww_rs_capture_fourcc(v, bits, text.data());
    return QString::fromLatin1(reinterpret_cast<const char *>(text.data()), n);
}
inline double rate(i64 value) { return rust::okww_rs_capture_rate(value); }
inline bool supported(const QString &text) { return text == "NV12"; }
namespace original {
std::vector<Mode> modes(const Config &c) {
    Com com;
    std::vector<Mode> out;
    if (c.s("capture.backend") == "DSHOW") {
        auto f = dsDevice(c);
        Ptr<VideoControl> vc;
        f.As(&vc);
        for (auto &p : pins(f.Get(), PINDIR_OUTPUT)) {
            Ptr<StreamConfig> cfg;
            if (FAILED(p.As(&cfg)))
                continue;
            int count = 0, size = 0;
            if (FAILED(cfg->GetNumberOfCapabilities(&count, &size)) || count < 0 || count > 4096 ||
                size < 1 || size > 65536)
                continue;
            std::vector<BYTE> cap(size);
            for (int i = 0; i < count; ++i) {
                MediaType *t = nullptr;
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
                            capture_modes_test::freeRates(rates);
                        }
                    }
                }
                freeType(t, true);
            }
        }
    } else {
        Mf mf;
        auto source = mfSource(c);
        Ptr<Reader> reader;
        hr(capture_modes_test::createReader(source.Get(), nullptr, &reader), "读取采集能力失败");
        for (DWORD i = 0;; ++i) {
            Ptr<Media> t;
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
} // namespace original
namespace current {
struct CaptureModesSameSdk {
    const Mode &a;
    const Mode &b;
    void operator()(rust::WorkflowOp op, const double *, double *out) {
        switch (op) {
        case rust::WorkflowOp::CaptureModesSameWidth:
            out[0] = a.width == b.width;
            break;
        case rust::WorkflowOp::CaptureModesSameHeight:
            out[0] = a.height == b.height;
            break;
        case rust::WorkflowOp::CaptureModesSameFormat:
            out[0] = a.fourcc == b.fourcc;
            break;
        case rust::WorkflowOp::CaptureModesRateDifference:
            out[0] = a.fps - b.fps;
            break;
        default:
            fail("Rust 采集参数去重宿主无效");
        }
    }
};
struct CaptureModesSdk {
    const Config &c;
    std::vector<Mode> out;
    Ptr<Filter> filter;
    Ptr<VideoControl> vc;
    std::vector<Ptr<Pin>> pinList;
    Pin *pin = nullptr;
    size_t pinIndex = 0;
    Ptr<StreamConfig> cfg;
    int count = 0, size = 0;
    std::vector<BYTE> cap;
    MediaType *type = nullptr;
    BITMAPINFOHEADER *b = nullptr;
    REFERENCE_TIME *per = nullptr;
    std::optional<Mf> mf;
    Ptr<Source> source;
    Ptr<Reader> reader;
    Ptr<Media> media;
    Mode mode;
    long countRates = 0;
    LONGLONG *rates = nullptr;
    void operator()(rust::WorkflowOp op, const double *a, double *o) {
        switch (op) {
        case rust::WorkflowOp::CaptureModesDshow:
            o[0] = c.s("capture.backend") == "DSHOW";
            break;
        case rust::WorkflowOp::CaptureModesDsSource:
            filter = dsDevice(c);
            break;
        case rust::WorkflowOp::CaptureModesVideoControl:
            filter.As(&vc);
            break;
        case rust::WorkflowOp::CaptureModesPins:
            pinList = pins(filter.Get(), PINDIR_OUTPUT);
            break;
        case rust::WorkflowOp::CaptureModesPinCount:
            o[0] = double(pinList.size());
            break;
        case rust::WorkflowOp::CaptureModesSelectPin:
            pinIndex = size_t(a[0]);
            pin = pinList.at(pinIndex).Get();
            break;
        case rust::WorkflowOp::CaptureModesStreamConfig:
            o[0] = SUCCEEDED(pinList.at(pinIndex).As(&cfg));
            break;
        case rust::WorkflowOp::CaptureModesCapabilityCounts:
            count = size = 0;
            o[0] = SUCCEEDED(cfg->GetNumberOfCapabilities(&count, &size));
            o[1] = count;
            o[2] = size;
            break;
        case rust::WorkflowOp::CaptureModesAllocateCaps:
            cap = std::vector<BYTE>(size);
            break;
        case rust::WorkflowOp::CaptureModesStreamCaps:
            type = nullptr;
            o[0] = SUCCEEDED(cfg->GetStreamCaps(int(a[0]), &type, cap.data()));
            break;
        case rust::WorkflowOp::CaptureModesReadBitmap:
            b = bitmap(type);
            break;
        case rust::WorkflowOp::CaptureModesReadPeriod:
            per = period(type);
            break;
        case rust::WorkflowOp::CaptureModesBitmapExists:
            o[0] = b != nullptr;
            break;
        case rust::WorkflowOp::CaptureModesPeriodExists:
            o[0] = per != nullptr;
            break;
        case rust::WorkflowOp::CaptureModesPositivePeriod:
            o[0] = *per > 0;
            break;
        case rust::WorkflowOp::CaptureModesDsMode:
            mode = {b->biWidth, std::abs(b->biHeight), rate(*per), fourcc(b->biCompression, b->biBitCount)};
            break;
        case rust::WorkflowOp::CaptureModesSupported:
            o[0] = supported(mode.fourcc);
            break;
        case rust::WorkflowOp::CaptureModesAppend:
            out.push_back(mode);
            break;
        case rust::WorkflowOp::CaptureModesHasVideoControl:
            o[0] = bool(vc);
            break;
        case rust::WorkflowOp::CaptureModesInitRates:
            countRates = 0;
            rates = nullptr;
            break;
        case rust::WorkflowOp::CaptureModesFrameRates: {
            SIZE sz{mode.width, mode.height};
            o[0] = SUCCEEDED(vc->GetFrameRateList(pin, int(a[0]), sz, &countRates, &rates));
            break;
        }
        case rust::WorkflowOp::CaptureModesRatesExist:
            o[0] = rates != nullptr;
            break;
        case rust::WorkflowOp::CaptureModesRateCount:
            o[0] = countRates;
            break;
        case rust::WorkflowOp::CaptureModesRatePositive:
            o[0] = rates[int(a[0])] > 0;
            break;
        case rust::WorkflowOp::CaptureModesSetRate:
            mode.fps = rate(rates[int(a[0])]);
            break;
        case rust::WorkflowOp::CaptureModesFreeRates:
            capture_modes_test::freeRates(rates);
            break;
        case rust::WorkflowOp::CaptureModesDropMode:
            mode = Mode{};
            break;
        case rust::WorkflowOp::CaptureModesFreeType:
            freeType(type, true);
            break;
        case rust::WorkflowOp::CaptureModesDropPinScope:
            cap = std::vector<BYTE>{};
            cfg.Reset();
            break;
        case rust::WorkflowOp::CaptureModesDropDs:
            pinList = decltype(pinList){};
            vc.Reset();
            filter.Reset();
            break;
        case rust::WorkflowOp::CaptureModesStartMf:
            mf.emplace();
            break;
        case rust::WorkflowOp::CaptureModesMfSource:
            source = mfSource(c);
            break;
        case rust::WorkflowOp::CaptureModesMfReader:
            hr(capture_modes_test::createReader(source.Get(), nullptr, &reader), "读取采集能力失败");
            break;
        case rust::WorkflowOp::CaptureModesMfType:
            o[0] = SUCCEEDED(
                reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, DWORD(a[0]), &media));
            break;
        case rust::WorkflowOp::CaptureModesMfMode:
            mode = mfMode(media.Get());
            break;
        case rust::WorkflowOp::CaptureModesModeRate:
            o[0] = mode.fps;
            break;
        case rust::WorkflowOp::CaptureModesDropMfType:
            media.Reset();
            break;
        case rust::WorkflowOp::CaptureModesResetReader:
            reader.Reset();
            break;
        case rust::WorkflowOp::CaptureModesShutdownSource:
            source->Shutdown();
            break;
        case rust::WorkflowOp::CaptureModesDropMf:
            source.Reset();
            mf.reset();
            break;
        case rust::WorkflowOp::CaptureModesSort:
            std::sort(out.begin(), out.end(), [](const Mode &a, const Mode &b) {
                return std::tie(a.width, a.height, a.fourcc, a.fps) <
                       std::tie(b.width, b.height, b.fourcc, b.fps);
            });
            break;
        case rust::WorkflowOp::CaptureModesUnique:
            out.erase(std::unique(out.begin(), out.end(),
                                  [](const Mode &a, const Mode &b) {
                                      CaptureModesSameSdk sdk{a, b};
                                      return rust::platformWorkflowRun(rust::WorkflowMethod::CaptureModesSame,
                                                                       {}, sdk)[0] != 0;
                                  }),
                      out.end());
            break;
        default:
            fail("Rust 采集参数枚举宿主无效");
        }
    }
};
std::vector<Mode> modes(const Config &c) {
    Com com;
    CaptureModesSdk sdk{c};
    rust::platformWorkflowRun(rust::WorkflowMethod::CaptureModes, {}, sdk);
    return std::move(sdk.out);
}
} // namespace current
inline QJsonObject outcome(int scene, bool ds, bool rust) {
    State state(scene);
    active = &state;
    QJsonObject result;
    Config config = Config::defaults();
    config.set("capture.backend", ds ? "DSHOW" : "MSMF");
    try {
        const auto values = rust ? current::modes(config) : original::modes(config);
        QJsonArray modes;
        for (const auto &m : values)
            modes.append(m.json());
        result["modes"] = modes;
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    } catch (...) {
        result["error"] = "unknown";
    }
    result["trace"] = state.trace;
    return result;
}
} // namespace okww::capture_modes_test
