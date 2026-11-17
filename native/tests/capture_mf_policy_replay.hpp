#pragma once
#include "capture.hpp"
#include "capture_math_abi.hpp"
#include "platform_workflow.hpp"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <optional>
namespace okww::capture_mf_test {
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
};
struct State {
    int scene, fault = 0, requests = 0, arrivals = 0, published = 0;
    QJsonArray trace;
    std::vector<std::unique_ptr<Ref>> objects;
    std::array<BYTE, 128> bytes{};
    std::atomic<bool> *stop = nullptr;
    struct MfCallback *callback = nullptr;
    explicit State(int s, int f = 0) : scene(s), fault(f) {}
    template <class T, class... A> T *make(A &&...a) {
        auto p = std::make_unique<T>(std::forward<A>(a)...);
        auto *raw = p.get();
        objects.push_back(std::move(p));
        return raw;
    }
    HRESULT step(const QString &name, int id = -1) {
        trace.append(QJsonObject{{"call", name}, {"id", id}});
        const QStringList failures{"none",      "reader", "set-type",   "read-first",
                                   "read-next", "status", "contiguous", "buffer-lock"};
        return !fault && name == failures[(scene / 32) % failures.size()] ? E_FAIL : S_OK;
    }
};
inline void Ref::add() { active->trace.append(QJsonObject{{"add", kind}, {"id", id}, {"refs", ++refs}}); }
inline void Ref::release() {
    active->trace.append(QJsonObject{{"release", kind}, {"id", id}, {"refs", --refs}});
}
struct Mf {
    Mf() {
        active->trace.append("mf");
        if (active->scene % 97 == 0)
            throw std::runtime_error("mf startup");
    }
    ~Mf() { active->trace.append("mf-end"); }
};
struct Source : Ref {
    Source() : Ref("source", 0) {}
    HRESULT Shutdown() { return active->step("shutdown"); }
};
struct Media : Ref {
    explicit Media(int i) : Ref("type", i) {}
    HRESULT GetUINT32(REFGUID, UINT32 *out) {
        active->step("stride");
        const int strides[]{0, 1920, -1920, 5760, -5760};
        *out = UINT32(strides[active->scene % 5]);
        return active->scene % 13 ? S_OK : E_FAIL;
    }
};
struct Buffer : Ref {
    explicit Buffer(int i) : Ref("buffer", i) {}
    HRESULT Lock(BYTE **data, void *, DWORD *length) {
        HRESULT result = active->step("buffer-lock", id);
        *data = active->bytes.data();
        *length = DWORD(active->bytes.size());
        return result;
    }
    HRESULT Unlock() {
        active->step("buffer-unlock", id);
        if (active->fault == 3 || active->scene % 71 == 0)
            throw std::runtime_error("unlock fixture");
        return E_FAIL;
    }
};
struct Sample : Ref {
    explicit Sample(int i) : Ref("sample", i) {}
    HRESULT ConvertToContiguousBuffer(Ptr<Buffer> *out) {
        HRESULT result = active->step("contiguous", id);
        if (SUCCEEDED(result))
            out->Attach(active->make<Buffer>(id));
        return result;
    }
};
struct Cv {
    template <class L, class D> void wait_for(L &, D duration);
    void notify_one() {}
    void notify_all() {}
};
struct MfCallback : Ref {
    std::mutex m;
    Cv cv;
    Ptr<Sample> sample;
    HRESULT status = S_OK;
    DWORD flags = 0;
    bool arrived = false;
    i64 arrivedAt = 0;
    MfCallback() : Ref("callback", 0) {
        active->step("callback");
        active->callback = this;
        active->objects.emplace_back(this);
    }
};
template <class L, class D> void Cv::wait_for(L &lock, D duration) {
    active->trace.append(
        QJsonObject{{"wait", int(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count())},
                    {"locked", lock.owns_lock()}});
    auto &cb = *active->callback;
    const int index = ++active->arrivals;
    if (active->scene % 19 == 0 || index > 6) {
        *active->stop = true;
        return;
    }
    cb.status = active->step("status", index);
    const DWORD flags[]{0,
                        MF_SOURCE_READERF_ENDOFSTREAM,
                        MF_SOURCE_READERF_ERROR,
                        MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED,
                        MF_SOURCE_READERF_STREAMTICK,
                        MF_SOURCE_READERF_NEWSTREAM};
    cb.flags = index == 2 ? flags[(active->scene / 8) % 6] : 0;
    if ((active->scene + index) % 7)
        cb.sample.Attach(active->make<Sample>(index));
    cb.arrivedAt = INT64_C(0x123456789abcde00) + index;
    cb.arrived = true;
}
struct Attributes : Ref {
    Attributes() : Ref("attributes", 0) {}
    HRESULT SetUnknown(REFGUID, MfCallback *cb) {
        active->trace.append(QJsonObject{{"set-callback", cb == active->callback}});
        return E_FAIL;
    }
    HRESULT SetUINT32(REFGUID key, UINT32 value) {
        active->trace.append(
            QJsonObject{{"attribute", key == MF_LOW_LATENCY ? "low-latency" : "disable-converters"},
                        {"value", int(value)}});
        return E_FAIL;
    }
};
struct Reader : Ref {
    Reader() : Ref("reader", 0) {}
    HRESULT GetNativeMediaType(DWORD, DWORD index, Ptr<Media> *out) {
        active->trace.append(QJsonObject{{"native-type", int(index)}});
        if (index >= 3) {
            if (active->scene % 17 == 0)
                out->Attach(active->make<Media>(999));
            return E_FAIL;
        }
        out->Attach(active->make<Media>(int(index)));
        return S_OK;
    }
    HRESULT SetCurrentMediaType(DWORD, void *, Media *t) { return active->step("set-type", t->id); }
    HRESULT GetCurrentMediaType(DWORD, Ptr<Media> *out) {
        active->step("current-type");
        out->Attach(active->make<Media>(9));
        return E_FAIL;
    }
    HRESULT ReadSample(DWORD, DWORD, void *, void *, void *, void *) {
        return active->step(++active->requests == 1 ? "read-first" : "read-next");
    }
    HRESULT Flush(DWORD) { return active->step("flush"); }
};
inline Ptr<Source> mfSource(const Config &) {
    active->step("source");
    if (active->scene % 89 == 0)
        throw 27;
    Ptr<Source> p;
    p.Attach(active->make<Source>());
    return p;
}
inline HRESULT createAttributes(Ptr<Attributes> *out, UINT count) {
    active->trace.append(QJsonObject{{"attributes", int(count)}});
    out->Attach(active->make<Attributes>());
    return S_OK;
}
inline HRESULT createReader(Source *, Attributes *, Ptr<Reader> *out) {
    HRESULT result = active->step("reader");
    if (SUCCEEDED(result))
        out->Attach(active->make<Reader>());
    return result;
}
inline Mode mfMode(Media *type) {
    active->trace.append(QJsonObject{{"mode", type->id}});
    if (active->scene % 83 == 0)
        throw std::runtime_error("mode fixture");
    const char *formats[]{"RGB3", "RGB4", "NV12", "P010", "YUY2", "UYVY", "I420", "MJPG", "NONE"};
    if (type->id == 9)
        return {active->scene % 2 ? 1919 : 1920, 1080, 240., formats[active->scene % 9]};
    return {type->id == 0 ? 1280 : 1920, active->scene % 11 == 0 ? 720 : 1080,
            120. + (active->scene % 3 == 0   ? .01
                    : active->scene % 3 == 1 ? .021
                                             : 0.),
            type->id == 2 ? "YUY2" : "NV12"};
}
inline PixelFormat pixelFormat(const QString &text) {
    int code = rust::okww_rs_capture_format(reinterpret_cast<const uint16_t *>(text.constData()),
                                            size_t(text.size()));
    if (code < 0)
        fail("暂不支持采集格式：" + text);
    return PixelFormat(code);
}
struct CaptureState {
    Config config_ = Config::defaults();
    std::mutex mutex_;
    std::atomic<bool> stop_{false};
    double negotiatedFps_ = 0;
    void publish(const BYTE *data, size_t length, int w, int h, int stride, PixelFormat format, bool bottom,
                 i64 stamp) {
        active->trace.append(QJsonObject{{"publish", ++active->published},
                                         {"data", data == active->bytes.data()},
                                         {"length", int(length)},
                                         {"width", w},
                                         {"height", h},
                                         {"stride", stride},
                                         {"format", int(format)},
                                         {"bottom", bottom},
                                         {"stamp", QString::number(stamp)}});
        if (active->fault == 1 || active->fault == 3 || active->scene % 67 == 0)
            throw std::runtime_error("publish fixture");
        if (active->fault == 2 || active->scene % 73 == 0)
            throw 31;
    }
};
namespace original {
struct Capture : CaptureState {
    void runMf();
};
void Capture::runMf() {
    Mf mf;
    auto source = mfSource(config_);
    struct Shutdown {
        Source *p;
        ~Shutdown() { p->Shutdown(); }
    } shutdown{source.Get()};
    Ptr<Attributes> attr;
    capture_mf_test::createAttributes(&attr, 4);
    Ptr<MfCallback> cb;
    cb.Attach(new MfCallback);
    attr->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, cb.Get());
    attr->SetUINT32(MF_LOW_LATENCY, TRUE);
    attr->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE);
    Ptr<Reader> reader;
    hr(capture_mf_test::createReader(source.Get(), attr.Get(), &reader), "创建兼容采集接口失败");
    bool found = false;
    for (DWORD i = 0;; ++i) {
        Ptr<Media> t;
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
    Ptr<Media> t;
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
        Ptr<Buffer> buffer;
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
} // namespace original
namespace current {
struct Capture : CaptureState {
    void runMf();
};
struct CaptureMfRunSdk {
    Capture &c;
    std::optional<Mf> mf;
    Ptr<Source> source;
    struct Shutdown {
        Source *p;
        ~Shutdown() { p->Shutdown(); }
    };
    std::optional<Shutdown> shutdown;
    Ptr<Attributes> attr;
    Ptr<MfCallback> cb;
    Ptr<Reader> reader;
    Ptr<Media> nativeType;
    Mode nativeMode;
    Ptr<Media> type;
    Mode mode;
    PixelFormat fmt = PixelFormat::BGR24;
    int stride = 0;
    bool bottom = false;
    std::optional<std::unique_lock<std::mutex>> lock;
    Ptr<Sample> sample;
    HRESULT status = S_OK;
    DWORD flags = 0;
    i64 stamp = 0;
    Ptr<Buffer> buffer;
    BYTE *data = nullptr;
    DWORD length = 0;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::CaptureMfRunStart:
            mf.emplace();
            break;
        case rust::WorkflowOp::CaptureMfRunSource:
            source = mfSource(c.config_);
            break;
        case rust::WorkflowOp::CaptureMfRunShutdownGuard:
            shutdown.emplace(source.Get());
            break;
        case rust::WorkflowOp::CaptureMfRunAttributes:
            capture_mf_test::createAttributes(&attr, 4);
            break;
        case rust::WorkflowOp::CaptureMfRunCallback:
            cb.Attach(new MfCallback);
            break;
        case rust::WorkflowOp::CaptureMfRunSetCallback:
            attr->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, cb.Get());
            break;
        case rust::WorkflowOp::CaptureMfRunLowLatency:
            attr->SetUINT32(MF_LOW_LATENCY, TRUE);
            break;
        case rust::WorkflowOp::CaptureMfRunDisableConverters:
            attr->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE);
            break;
        case rust::WorkflowOp::CaptureMfRunReader:
            hr(capture_mf_test::createReader(source.Get(), attr.Get(), &reader), "创建兼容采集接口失败");
            break;
        case rust::WorkflowOp::CaptureMfRunNativeType:
            out[0] = SUCCEEDED(
                reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, DWORD(a[0]), &nativeType));
            break;
        case rust::WorkflowOp::CaptureMfRunNativeMode:
            nativeMode = mfMode(nativeType.Get());
            break;
        case rust::WorkflowOp::CaptureMfRunWidthMatches:
            out[0] = nativeMode.width == c.config_.n("capture.width");
            break;
        case rust::WorkflowOp::CaptureMfRunHeightMatches:
            out[0] = nativeMode.height == c.config_.n("capture.height");
            break;
        case rust::WorkflowOp::CaptureMfRunFormatMatches:
            out[0] = nativeMode.fourcc == c.config_.s("capture.fourcc");
            break;
        case rust::WorkflowOp::CaptureMfRunRateDifference:
            out[0] = nativeMode.fps - c.config_.d("capture.fps");
            break;
        case rust::WorkflowOp::CaptureMfRunSetType:
            hr(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, nativeType.Get()),
               "设置兼容采集格式失败");
            break;
        case rust::WorkflowOp::CaptureMfRunDropNativeMode:
            nativeMode = Mode{};
            break;
        case rust::WorkflowOp::CaptureMfRunDropNativeType:
            nativeType.Reset();
            break;
        case rust::WorkflowOp::CaptureMfRunFailure:
            if (a[0] == 0)
                fail("兼容采集不支持所选配置，请重新扫描");
            else
                fail("采集信号结束或格式已改变");
            break;
        case rust::WorkflowOp::CaptureMfRunCurrentType:
            reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type);
            break;
        case rust::WorkflowOp::CaptureMfRunCurrentMode:
            mode = mfMode(type.Get());
            break;
        case rust::WorkflowOp::CaptureMfRunNegotiatedRate: {
            std::lock_guard guard(c.mutex_);
            c.negotiatedFps_ = mode.fps;
            break;
        }
        case rust::WorkflowOp::CaptureMfRunPixelFormat:
            fmt = pixelFormat(mode.fourcc);
            break;
        case rust::WorkflowOp::CaptureMfRunReadStride: {
            UINT32 value = 0;
            type->GetUINT32(MF_MT_DEFAULT_STRIDE, &value);
            out[0] = int(value);
            break;
        }
        case rust::WorkflowOp::CaptureMfRunSetStride:
            stride = int(a[0]);
            bottom = a[1] != 0;
            break;
        case rust::WorkflowOp::CaptureMfRunDefaultStride:
            stride = rust::okww_rs_capture_stride(mode.width, int(fmt));
            break;
        case rust::WorkflowOp::CaptureMfRunReadSample:
            hr(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr),
               "读取兼容采集帧失败");
            break;
        case rust::WorkflowOp::CaptureMfRunStopped:
            out[0] = bool(c.stop_);
            break;
        case rust::WorkflowOp::CaptureMfRunLockCallback:
            lock.emplace(cb->m);
            break;
        case rust::WorkflowOp::CaptureMfRunArrived:
            out[0] = cb->arrived;
            break;
        case rust::WorkflowOp::CaptureMfRunWait:
            cb->cv.wait_for(*lock, std::chrono::milliseconds(20));
            break;
        case rust::WorkflowOp::CaptureMfRunCopySample:
            sample = cb->sample;
            break;
        case rust::WorkflowOp::CaptureMfRunCopyStatus:
            status = cb->status;
            break;
        case rust::WorkflowOp::CaptureMfRunCopyFlags:
            flags = cb->flags;
            break;
        case rust::WorkflowOp::CaptureMfRunCopyStamp:
            stamp = cb->arrivedAt;
            break;
        case rust::WorkflowOp::CaptureMfRunResetSample:
            cb->sample.Reset();
            break;
        case rust::WorkflowOp::CaptureMfRunResetArrived:
            cb->arrived = false;
            break;
        case rust::WorkflowOp::CaptureMfRunUnlockCallback:
            lock->unlock();
            break;
        case rust::WorkflowOp::CaptureMfRunCheckStatus:
            hr(status, "兼容采集失去信号");
            break;
        case rust::WorkflowOp::CaptureMfRunBadFlags:
            out[0] = (flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR |
                               MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)) != 0;
            break;
        case rust::WorkflowOp::CaptureMfRunHasSample:
            out[0] = bool(sample);
            break;
        case rust::WorkflowOp::CaptureMfRunContiguousBuffer:
            hr(sample->ConvertToContiguousBuffer(&buffer), "获取采集缓冲区失败");
            break;
        case rust::WorkflowOp::CaptureMfRunLockBuffer:
            data = nullptr;
            length = 0;
            hr(buffer->Lock(&data, nullptr, &length), "锁定采集帧失败");
            break;
        case rust::WorkflowOp::CaptureMfRunPublish:
            c.publish(data, length, mode.width, mode.height, stride, fmt, bottom, stamp);
            break;
        case rust::WorkflowOp::CaptureMfRunUnlockBuffer:
            buffer->Unlock();
            break;
        case rust::WorkflowOp::CaptureMfRunDropIteration:
            buffer.Reset();
            sample.Reset();
            lock.reset();
            break;
        case rust::WorkflowOp::CaptureMfRunFlush:
            reader->Flush(MF_SOURCE_READER_ALL_STREAMS);
            break;
        case rust::WorkflowOp::CaptureMfRunResetReader:
            reader.Reset();
            break;
        default:
            fail("Rust Media Foundation 采集宿主无效");
        }
    }
};
void Capture::runMf() {
    CaptureMfRunSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::CaptureRunMediaFoundation, {}, sdk);
}
} // namespace current
template <class C> QJsonObject run() {
    C capture;
    active->stop = &capture.stop_;
    capture.config_.set("capture.fps", 120.);
    capture.config_.set("capture.width", active->scene % 5 ? 1920 : 1280);
    QJsonObject result;
    try {
        capture.runMf();
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    } catch (...) {
        result["error"] = "unknown";
    }
    result["negotiated"] = capture.negotiatedFps_;
    result["stopped"] = bool(capture.stop_);
    return result;
}
inline QJsonObject outcome(int scene, bool rust, int fault = 0) {
    State state(scene, fault);
    active = &state;
    auto result = rust ? run<current::Capture>() : run<original::Capture>();
    result["trace"] = state.trace;
    return result;
}
} // namespace okww::capture_mf_test
