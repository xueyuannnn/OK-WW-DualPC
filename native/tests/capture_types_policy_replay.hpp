#pragma once
#include "capture.hpp"
#include "capture_math_abi.hpp"
#include "platform_workflow.hpp"
#include <bit>
#include <dshow.h>
#include <dvdmedia.h>
namespace okww::capture_types_test {
struct State {
    QJsonArray trace;
    AM_MEDIA_TYPE *media = nullptr;
    BYTE *data = nullptr;
    int failure = 0;
};
inline thread_local State *active = nullptr;
inline void freeMemory(void *pointer) {
    active->trace.append(pointer == nullptr         ? "free-null"
                         : pointer == active->media ? "free-outer"
                         : pointer == active->data  ? "free-format"
                                                    : "free-other");
}
struct Unknown : IUnknown {
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override {
        active->trace.append("release-unknown");
        if (active->failure == 1)
            throw std::runtime_error("release fixture");
        if (active->failure == 2)
            throw 27;
        return 1;
    }
};
namespace original {
static void freeType(AM_MEDIA_TYPE *t, bool outer = false) {
    if (!t)
        return;
    if (t->cbFormat)
        capture_types_test::freeMemory(t->pbFormat);
    if (t->pUnk)
        t->pUnk->Release();
    if (outer)
        capture_types_test::freeMemory(t);
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
static bool supported(const QString &f) { return f == "NV12"; }
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

} // namespace original
namespace current {
struct CaptureTypeSdk {
    AM_MEDIA_TYPE *t;
    BITMAPINFOHEADER *bitmapValue = nullptr;
    REFERENCE_TIME *periodValue = nullptr;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::CaptureTypeExists:
            out[0] = t != nullptr;
            break;
        case rust::WorkflowOp::CaptureTypeHasFormat:
            out[0] = t->cbFormat != 0;
            break;
        case rust::WorkflowOp::CaptureTypeFreeFormat:
            capture_types_test::freeMemory(t->pbFormat);
            break;
        case rust::WorkflowOp::CaptureTypeHasUnknown:
            out[0] = t->pUnk != nullptr;
            break;
        case rust::WorkflowOp::CaptureTypeReleaseUnknown:
            t->pUnk->Release();
            break;
        case rust::WorkflowOp::CaptureTypeFreeOuter:
            capture_types_test::freeMemory(t);
            break;
        case rust::WorkflowOp::CaptureTypeFormat:
            out[0] = t->formattype == FORMAT_VideoInfo ? 0 : t->formattype == FORMAT_VideoInfo2 ? 1 : -1;
            break;
        case rust::WorkflowOp::CaptureTypeBytes:
            out[0] = t->cbFormat;
            break;
        case rust::WorkflowOp::CaptureTypeMinimum: {
            static const size_t sizes[]{sizeof(VIDEOINFOHEADER), sizeof(VIDEOINFOHEADER2)};
            out[0] = double(sizes[int(a[0])]);
            break;
        }
        case rust::WorkflowOp::CaptureTypeBitmap:
            switch (int(a[0])) {
            case 0:
                bitmapValue = &reinterpret_cast<VIDEOINFOHEADER *>(t->pbFormat)->bmiHeader;
                break;
            case 1:
                bitmapValue = &reinterpret_cast<VIDEOINFOHEADER2 *>(t->pbFormat)->bmiHeader;
                break;
            }
            break;
        case rust::WorkflowOp::CaptureTypePeriod:
            switch (int(a[0])) {
            case 0:
                periodValue = &reinterpret_cast<VIDEOINFOHEADER *>(t->pbFormat)->AvgTimePerFrame;
                break;
            case 1:
                periodValue = &reinterpret_cast<VIDEOINFOHEADER2 *>(t->pbFormat)->AvgTimePerFrame;
                break;
            }
            break;
        default:
            fail("Rust 采集媒体类型宿主无效");
        }
    }
};
static void freeType(AM_MEDIA_TYPE *t, bool outer = false) {
    CaptureTypeSdk sdk{t};
    const std::array<double, 1> a{double(outer)};
    rust::platformWorkflowRun(rust::WorkflowMethod::CaptureFreeType, a, sdk);
}
static BITMAPINFOHEADER *bitmap(AM_MEDIA_TYPE *t) {
    CaptureTypeSdk sdk{t};
    rust::platformWorkflowRun(rust::WorkflowMethod::CaptureBitmap, {}, sdk);
    return sdk.bitmapValue;
}
static REFERENCE_TIME *period(AM_MEDIA_TYPE *t) {
    CaptureTypeSdk sdk{t};
    rust::platformWorkflowRun(rust::WorkflowMethod::CapturePeriod, {}, sdk);
    return sdk.periodValue;
}
static QString fourcc(DWORD value, int bits = 0) {
    std::array<uint8_t, 4> text{};
    const auto length = rust::okww_rs_capture_fourcc(value, bits, text.data());
    return QString::fromLatin1(reinterpret_cast<const char *>(text.data()), length);
}
static bool supported(const QString &format) {
    return rust::okww_rs_capture_format(reinterpret_cast<const uint16_t *>(format.constData()),
                                        size_t(format.size())) == int(PixelFormat::NV12);
}
static PixelFormat pixelFormat(const QString &format) {
    const auto code = rust::okww_rs_capture_format(reinterpret_cast<const uint16_t *>(format.constData()),
                                                   size_t(format.size()));
    if (code < 0)
        fail("暂不支持采集格式：" + format);
    return PixelFormat(code);
}
static double rate(i64 period) { return rust::okww_rs_capture_rate(period); }

} // namespace current
inline QJsonObject mediaOutcome(int scene, bool rust) {
    State state;
    active = &state;
    AM_MEDIA_TYPE media{};
    alignas(VIDEOINFOHEADER2) std::array<BYTE, 512> data{};
    Unknown unknown;
    const int format = scene % 3;
    const size_t minimum = format == 0 ? sizeof(VIDEOINFOHEADER) : sizeof(VIDEOINFOHEADER2);
    const DWORD sizes[]{0, 1, DWORD(minimum - 1), DWORD(minimum), DWORD(minimum + 1), 4096};
    media.formattype = format == 0 ? FORMAT_VideoInfo : format == 1 ? FORMAT_VideoInfo2 : GUID_NULL;
    media.cbFormat = sizes[(scene / 3) % 6];
    media.pbFormat = format == 2 && scene % 7 == 0 ? nullptr : data.data();
    if (scene % 2)
        media.pUnk = &unknown;
    state.media = &media;
    state.data = data.data();
    state.failure = scene % 11 == 0 ? 1 : scene % 13 == 0 ? 2 : 0;
    QJsonObject result;
    auto *header = rust ? current::bitmap(&media) : original::bitmap(&media);
    auto *period = rust ? current::period(&media) : original::period(&media);
    result["bitmap"] =
        header ? QJsonValue(double(reinterpret_cast<BYTE *>(header) - data.data())) : QJsonValue{};
    result["period"] =
        period ? QJsonValue(double(reinterpret_cast<BYTE *>(period) - data.data())) : QJsonValue{};
    try {
        if (rust)
            current::freeType(scene % 17 == 0 ? nullptr : &media, scene % 5 != 0);
        else
            original::freeType(scene % 17 == 0 ? nullptr : &media, scene % 5 != 0);
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    } catch (...) {
        result["error"] = "unknown";
    }
    result["trace"] = state.trace;
    return result;
}
inline QJsonObject formatOutcome(const QString &format, bool rust) {
    QJsonObject result{{"supported", rust ? current::supported(format) : original::supported(format)}};
    try {
        result["format"] = int(rust ? current::pixelFormat(format) : original::pixelFormat(format));
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    }
    return result;
}
} // namespace okww::capture_types_test
