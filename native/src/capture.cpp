#include "capture.hpp"
#include "capture_math_abi.hpp"
#include "platform_workflow.hpp"
#include <optional>

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
            CoTaskMemFree(t->pbFormat);
            break;
        case rust::WorkflowOp::CaptureTypeHasUnknown:
            out[0] = t->pUnk != nullptr;
            break;
        case rust::WorkflowOp::CaptureTypeReleaseUnknown:
            t->pUnk->Release();
            break;
        case rust::WorkflowOp::CaptureTypeFreeOuter:
            CoTaskMemFree(t);
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

struct CaptureDsDevicesSdk {
    ComPtr<ICreateDevEnum> en;
    ComPtr<IEnumMoniker> mons;
    std::vector<std::pair<Device, ComPtr<IMoniker>>> values;
    ComPtr<IMoniker> moniker;
    ComPtr<IPropertyBag> bag;
    QString name, path;
    QString property(const wchar_t *key) {
        VARIANT value;
        VariantInit(&value);
        QString text;
        if (SUCCEEDED(bag->Read(key, &value, nullptr)) && value.vt == VT_BSTR)
            text = QString::fromWCharArray(value.bstrVal);
        VariantClear(&value);
        return text;
    }
    void operator()(rust::WorkflowOp op, const double *, double *out) {
        switch (op) {
        case rust::WorkflowOp::CaptureDsCreateEnumerator:
            hr(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&en)),
               "枚举采集设备失败");
            break;
        case rust::WorkflowOp::CaptureDsClassEnumerator:
            out[0] = en->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &mons, 0) == S_OK;
            break;
        case rust::WorkflowOp::CaptureDsNextMoniker:
            out[0] = mons->Next(1, &moniker, nullptr) == S_OK;
            break;
        case rust::WorkflowOp::CaptureDsBindBag:
            hr(moniker->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(&bag)), "读取设备属性失败");
            break;
        case rust::WorkflowOp::CaptureDsFriendlyName:
            name = property(L"FriendlyName");
            break;
        case rust::WorkflowOp::CaptureDsDevicePath:
            path = property(L"DevicePath");
            break;
        case rust::WorkflowOp::CaptureDsAppendDevice:
            values.push_back({{int(values.size()), std::move(name), std::move(path)}, moniker});
            break;
        case rust::WorkflowOp::CaptureDsResetMoniker:
            moniker.Reset();
            break;
        case rust::WorkflowOp::CaptureDsResetBag:
            bag.Reset();
            break;
        default:
            fail("Rust DirectShow 设备枚举宿主无效");
        }
    }
};
static std::vector<std::pair<Device, ComPtr<IMoniker>>> dsDevices() {
    CaptureDsDevicesSdk sdk;
    rust::platformWorkflowRun(rust::WorkflowMethod::CaptureDsDevices, {}, sdk);
    return std::move(sdk.values);
}

struct CaptureDsDeviceSdk {
    const Config &c;
    std::vector<std::pair<Device, ComPtr<IMoniker>>> list;
    std::vector<ComPtr<IMoniker>> matches;
    ComPtr<IBaseFilter> filter;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::CaptureDsDeviceList:
            list = dsDevices();
            break;

        case rust::WorkflowOp::CaptureDeviceListCount:
            out[0] = double(list.size());
            break;
        case rust::WorkflowOp::CaptureDevicePathEmpty:
            out[0] = c.s("capture.device_path").isEmpty();
            break;
        case rust::WorkflowOp::CaptureDeviceNameEmpty:
            out[0] = c.s("capture.device_name").isEmpty();
            break;
        case rust::WorkflowOp::CaptureDevicePathMatches:
            out[0] =
                list.at(size_t(a[0])).first.path.compare(c.s("capture.device_path"), Qt::CaseInsensitive) ==
                0;
            break;
        case rust::WorkflowOp::CaptureDeviceNameMatches:
            out[0] = list.at(size_t(a[0])).first.name == c.s("capture.device_name");
            break;
        case rust::WorkflowOp::CaptureDeviceIndexMatches:
            out[0] = list.at(size_t(a[0])).first.index == c.n("capture.device_index");
            break;

        case rust::WorkflowOp::CaptureDsAppendMatch:
            matches.push_back(list.at(size_t(a[0])).second);
            break;
        case rust::WorkflowOp::CaptureDsMatchCount:
            out[0] = double(matches.size());
            break;
        case rust::WorkflowOp::CaptureDsBindDevice:
            hr(matches.at(0)->BindToObject(nullptr, nullptr, IID_PPV_ARGS(&filter)), "打开采集卡失败");
            break;
        case rust::WorkflowOp::CaptureDeviceFailure:
            fail("采集卡未找到或名称重复，请重新扫描设备");
            break;
        default:
            fail("Rust DirectShow 设备选择宿主无效");
        }
    }
};
static ComPtr<IBaseFilter> dsDevice(const Config &c) {
    CaptureDsDeviceSdk sdk{c};
    rust::platformWorkflowRun(rust::WorkflowMethod::CaptureDsDevice, {}, sdk);
    return std::move(sdk.filter);
}

struct CapturePinsSdk {
    IBaseFilter *f;
    ComPtr<IEnumPins> en;
    std::vector<ComPtr<IPin>> values;
    ComPtr<IPin> pin;
    PIN_DIRECTION direction;
    void operator()(rust::WorkflowOp op, const double *, double *out) {
        switch (op) {
        case rust::WorkflowOp::CaptureDsEnumeratePins:
            hr(f->EnumPins(&en), "枚举采集引脚失败");
            break;
        case rust::WorkflowOp::CaptureDsNextPin:
            out[0] = en->Next(1, &pin, nullptr) == S_OK;
            break;
        case rust::WorkflowOp::CaptureDsPinDirection:
            pin->QueryDirection(&direction);
            out[0] = direction;
            break;
        case rust::WorkflowOp::CaptureDsAppendPin:
            values.push_back(pin);
            break;
        case rust::WorkflowOp::CaptureDsResetPin:
            pin.Reset();
            break;
        default:
            fail("Rust 采集引脚宿主无效");
        }
    }
};
static std::vector<ComPtr<IPin>> pins(IBaseFilter *f, PIN_DIRECTION dir) {
    CapturePinsSdk sdk{f};
    const std::array<double, 1> args{double(dir)};
    rust::platformWorkflowRun(rust::WorkflowMethod::CaptureDsPins, args, sdk);
    return std::move(sdk.values);
}

struct MfInit {
    MfInit() { hr(MFStartup(MF_VERSION, MFSTARTUP_LITE), "初始化媒体采集失败"); }
    ~MfInit() { MFShutdown(); }
};

struct CaptureMfDevicesSdk {
    ComPtr<IMFAttributes> attributes;
    IMFActivate **ptr = nullptr;
    UINT32 count = 0;
    std::vector<std::pair<Device, ComPtr<IMFActivate>>> values;
    ComPtr<IMFActivate> act;
    QString name, path;
    QString property(const GUID &key) {
        wchar_t *p = nullptr;
        UINT32 n = 0;
        QString value;
        if (SUCCEEDED(act->GetAllocatedString(key, &p, &n))) {
            value = QString::fromWCharArray(p, int(n));
            CoTaskMemFree(p);
        }
        return value;
    }
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::CaptureMfCreateAttributes:
            hr(MFCreateAttributes(&attributes, 1), "创建媒体属性失败");
            break;
        case rust::WorkflowOp::CaptureMfSetVideoSource:
            attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
            break;
        case rust::WorkflowOp::CaptureMfEnumerateSources:
            hr(MFEnumDeviceSources(attributes.Get(), &ptr, &count), "枚举媒体设备失败");
            break;
        case rust::WorkflowOp::CaptureMfSourceCount:
            out[0] = count;
            break;
        case rust::WorkflowOp::CaptureMfAttachSource:
            act.Attach(ptr[size_t(a[0])]);
            break;
        case rust::WorkflowOp::CaptureMfFriendlyName:
            name = property(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME);
            break;
        case rust::WorkflowOp::CaptureMfDevicePath:
            path = property(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);
            break;
        case rust::WorkflowOp::CaptureMfAppendDevice:
            values.push_back({{int(UINT32(a[0])), std::move(name), std::move(path)}, act});
            break;
        case rust::WorkflowOp::CaptureMfResetActivation:
            act.Reset();
            break;
        case rust::WorkflowOp::CaptureMfFreeSourceArray:
            CoTaskMemFree(ptr);
            break;
        default:
            fail("Rust 媒体设备枚举宿主无效");
        }
    }
};
static std::vector<std::pair<Device, ComPtr<IMFActivate>>> mfDevices() {
    CaptureMfDevicesSdk sdk;
    rust::platformWorkflowRun(rust::WorkflowMethod::CaptureMfDevices, {}, sdk);
    return std::move(sdk.values);
}

struct CaptureMfSourceSdk {
    const Config &c;
    std::vector<std::pair<Device, ComPtr<IMFActivate>>> list;
    ComPtr<IMFMediaSource> source;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::CaptureMfDeviceList:
            list = mfDevices();
            break;

        case rust::WorkflowOp::CaptureDeviceListCount:
            out[0] = double(list.size());
            break;
        case rust::WorkflowOp::CaptureDevicePathEmpty:
            out[0] = c.s("capture.device_path").isEmpty();
            break;
        case rust::WorkflowOp::CaptureDeviceNameEmpty:
            out[0] = c.s("capture.device_name").isEmpty();
            break;
        case rust::WorkflowOp::CaptureDevicePathMatches:
            out[0] =
                list.at(size_t(a[0])).first.path.compare(c.s("capture.device_path"), Qt::CaseInsensitive) ==
                0;
            break;
        case rust::WorkflowOp::CaptureDeviceNameMatches:
            out[0] = list.at(size_t(a[0])).first.name == c.s("capture.device_name");
            break;
        case rust::WorkflowOp::CaptureDeviceIndexMatches:
            out[0] = list.at(size_t(a[0])).first.index == c.n("capture.device_index");
            break;

        case rust::WorkflowOp::CaptureMfActivateSource:
            hr(list.at(size_t(a[0])).second->ActivateObject(IID_PPV_ARGS(&source)), "打开采集设备失败");
            break;
        case rust::WorkflowOp::CaptureDeviceFailure:
            fail("采集设备不存在，请重新扫描");
            break;
        default:
            fail("Rust 媒体设备选择宿主无效");
        }
    }
};
static ComPtr<IMFMediaSource> mfSource(const Config &c) {
    CaptureMfSourceSdk sdk{c};
    rust::platformWorkflowRun(rust::WorkflowMethod::CaptureMfSource, {}, sdk);
    return std::move(sdk.source);
}

struct CaptureMfModeSdk {
    IMFMediaType *t;
    UINT32 w = 0, h = 0, num = 0, den = 1;
    GUID subtype{};
    Mode value;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::CaptureMfReadSize:
            MFGetAttributeSize(t, MF_MT_FRAME_SIZE, &w, &h);
            break;
        case rust::WorkflowOp::CaptureMfReadRate:
            MFGetAttributeRatio(t, MF_MT_FRAME_RATE, &num, &den);
            break;
        case rust::WorkflowOp::CaptureMfReadSubtype:
            t->GetGUID(MF_MT_SUBTYPE, &subtype);
            break;
        case rust::WorkflowOp::CaptureMfSubtypeKind:
            out[0] = subtype == MFVideoFormat_RGB32 ? 0 : subtype == MFVideoFormat_RGB24 ? 1 : 2;
            break;
        case rust::WorkflowOp::CaptureMfRgb32Format:
            value.fourcc = "RGB4";
            break;
        case rust::WorkflowOp::CaptureMfRgb24Format:
            value.fourcc = "RGB3";
            break;
        case rust::WorkflowOp::CaptureMfFourccFormat:
            value.fourcc = fourcc(subtype.Data1);
            break;
        case rust::WorkflowOp::CaptureMfCopyDimensions:
            value.width = int(w);
            value.height = int(h);
            break;
        case rust::WorkflowOp::CaptureMfRateRatio:
            out[0] = num;
            out[1] = den;
            break;
        case rust::WorkflowOp::CaptureMfSetModeRate:
            value.fps = a[0];
            break;
        default:
            fail("Rust 媒体采集格式宿主无效");
        }
    }
};
static Mode mfMode(IMFMediaType *t) {
    CaptureMfModeSdk sdk{t};
    rust::platformWorkflowRun(rust::WorkflowMethod::CaptureMfMode, {}, sdk);
    return std::move(sdk.value);
}

struct CaptureDevicesSdk {
    const QString &backend;
    std::vector<Device> values;
    std::optional<MfInit> mf;
    std::vector<std::pair<Device, ComPtr<IMoniker>>> dsList;
    std::vector<std::pair<Device, ComPtr<IMFActivate>>> mfList;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::CaptureDevicesDshow:
            out[0] = backend == "DSHOW";
            break;
        case rust::WorkflowOp::CaptureDevicesDsList:
            dsList = dsDevices();
            break;
        case rust::WorkflowOp::CaptureDevicesAppendDs:
            values.push_back(dsList.at(size_t(a[0])).first);
            break;
        case rust::WorkflowOp::CaptureDevicesDropDs:
            dsList = decltype(dsList){};
            break;
        case rust::WorkflowOp::CaptureDevicesStartMf:
            mf.emplace();
            break;
        case rust::WorkflowOp::CaptureDevicesMfList:
            mfList = mfDevices();
            break;
        case rust::WorkflowOp::CaptureDevicesAppendMf:
            values.push_back(mfList.at(size_t(a[0])).first);
            break;
        case rust::WorkflowOp::CaptureDevicesDropMf:
            mfList = decltype(mfList){};
            break;
        case rust::WorkflowOp::CaptureDevicesStopMf:
            mf.reset();
            break;
        case rust::WorkflowOp::CaptureDeviceListCount:
            out[0] = double(dsList.size() + mfList.size());
            break;
        default:
            fail("Rust 视频设备列表宿主无效");
        }
    }
};
std::vector<Device> devices(const QString &backend) {
    ComInit com;
    CaptureDevicesSdk sdk{backend};
    rust::platformWorkflowRun(rust::WorkflowMethod::CaptureDevices, {}, sdk);
    return std::move(sdk.values);
}

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
    ComPtr<IBaseFilter> filter;
    ComPtr<IAMVideoControl> vc;
    std::vector<ComPtr<IPin>> pinList;
    IPin *pin = nullptr;
    size_t pinIndex = 0;
    ComPtr<IAMStreamConfig> cfg;
    int count = 0, size = 0;
    std::vector<BYTE> cap;
    AM_MEDIA_TYPE *type = nullptr;
    BITMAPINFOHEADER *b = nullptr;
    REFERENCE_TIME *per = nullptr;
    std::optional<MfInit> mf;
    ComPtr<IMFMediaSource> source;
    ComPtr<IMFSourceReader> reader;
    ComPtr<IMFMediaType> media;
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
            CoTaskMemFree(rates);
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
            hr(MFCreateSourceReaderFromMediaSource(source.Get(), nullptr, &reader), "读取采集能力失败");
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
    ComInit com;
    CaptureModesSdk sdk{c};
    rust::platformWorkflowRun(rust::WorkflowMethod::CaptureModes, {}, sdk);
    return std::move(sdk.out);
}

struct CaptureCapabilitiesSdk {
    const Config &config;
    const std::function<std::vector<Device>(const QString &)> &enumerateDevices;
    const std::function<std::vector<Mode>(const Config &)> &enumerateModes;
    std::vector<Device> deviceList;
    QJsonArray results;
    QJsonObject entry;
    std::optional<Config> selected;
    QJsonArray parameters;
    std::vector<Mode> modeList;
    void operator()(rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &error) {
        switch (op) {
        case rust::WorkflowOp::CaptureCapabilitiesDevices:
            deviceList = enumerateDevices(config.s("capture.backend"));
            break;
        case rust::WorkflowOp::CaptureCapabilitiesDeviceCount:
            out[0] = double(deviceList.size());
            break;
        case rust::WorkflowOp::CaptureCapabilitiesEntry: {
            const auto &device = deviceList.at(size_t(a[0]));
            entry = {{"name", device.name}, {"path", device.path}, {"index", device.index}};
            break;
        }
        case rust::WorkflowOp::CaptureCapabilitiesConfig:
            selected.emplace(config);
            break;
        case rust::WorkflowOp::CaptureCapabilitiesSetName:
            selected->set("capture.device_name", deviceList.at(size_t(a[0])).name);
            break;
        case rust::WorkflowOp::CaptureCapabilitiesSetPath:
            selected->set("capture.device_path", deviceList.at(size_t(a[0])).path);
            break;
        case rust::WorkflowOp::CaptureCapabilitiesSetIndex:
            selected->set("capture.device_index", deviceList.at(size_t(a[0])).index);
            break;
        case rust::WorkflowOp::CaptureCapabilitiesModes:
            modeList = enumerateModes(*selected);
            break;
        case rust::WorkflowOp::CaptureCapabilitiesModeCount:
            out[0] = double(modeList.size());
            break;
        case rust::WorkflowOp::CaptureCapabilitiesSupported:
            out[0] = supported(modeList.at(size_t(a[0])).fourcc);
            break;
        case rust::WorkflowOp::CaptureCapabilitiesAppendMode:
            parameters.append(modeList.at(size_t(a[0])).json());
            break;
        case rust::WorkflowOp::CaptureCapabilitiesDropModes:
            modeList = std::vector<Mode>{};
            break;
        case rust::WorkflowOp::CaptureCapabilitiesError:
            try {
                std::rethrow_exception(error);
            } catch (const std::exception &e) {
                entry["error"] = errorText(e);
            }
            break;
        case rust::WorkflowOp::CaptureCapabilitiesSetModes:
            entry["modes"] = parameters;
            break;
        case rust::WorkflowOp::CaptureCapabilitiesAppendEntry:
            results.append(entry);
            break;
        case rust::WorkflowOp::CaptureCapabilitiesDropEntry:
            parameters = {};
            selected.reset();
            entry = {};
            break;
        case rust::WorkflowOp::CaptureCapabilitiesDropDevices:
            deviceList = std::vector<Device>{};
            break;
        default:
            fail("Rust 采集设备参数宿主无效");
        }
    }
};
QJsonArray captureCapabilities(const Config &config,
                               const std::function<std::vector<Device>(const QString &)> &enumerateDevices,
                               const std::function<std::vector<Mode>(const Config &)> &enumerateModes) {
    CaptureCapabilitiesSdk sdk{config, enumerateDevices, enumerateModes};
    rust::platformWorkflowRun(rust::WorkflowMethod::CaptureCapabilities, {}, sdk);
    return sdk.results;
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

struct CaptureFlowSdk {
    Capture &c;
    const uint8_t *data = nullptr;
    size_t length = 0;
    int w = 0, h = 0, stride = 0;
    PixelFormat fmt = PixelFormat::BGR24;
    bool bottom = false;
    i64 stamp = 0;
    uint64_t after = 0;
    QString text;
    QJsonObject state;
    FramePtr output;
    std::shared_ptr<Frame> frame;
    std::optional<std::unique_lock<std::mutex>> lock;
    std::optional<ComInit> com;
    void operator()(rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &error) {
        switch (op) {
        case rust::WorkflowOp::CaptureStopped:
            out[0] = c.stop_.load();
            break;
        case rust::WorkflowOp::CapturePayloadState:
            out[0] = rust::okww_rs_capture_payload(length, stride, h, int(fmt));
            break;
        case rust::WorkflowOp::CaptureLock:
            lock.emplace(c.mutex_);
            break;
        case rust::WorkflowOp::CaptureUnlock:
            lock.reset();
            break;
        case rust::WorkflowOp::CaptureSetShortError:
            c.error_ = "采集驱动返回了不完整图像";
            break;
        case rust::WorkflowOp::CaptureNotifyAll:
            c.cv_.notify_all();
            break;
        case rust::WorkflowOp::CaptureHasLatest:
            out[0] = bool(c.latest_);
            break;
        case rust::WorkflowOp::CaptureLatestDimensions:
            out[0] = c.latest_->width;
            out[1] = c.latest_->height;
            out[2] = c.latest_->stride;
            break;
        case rust::WorkflowOp::CaptureLatestFormat:
            out[0] = int(c.latest_->format);
            break;
        case rust::WorkflowOp::CaptureLatestBottom:
            out[0] = c.latest_->bottomUp;
            break;
        case rust::WorkflowOp::CaptureLatestLengthMatches:
            out[0] = c.latest_->bytes.size() == qsizetype(length);
            break;
        case rust::WorkflowOp::CaptureContentsMatch:
            out[0] = memcmp(c.latest_->bytes.constData(), data, length) == 0;
            break;
        case rust::WorkflowOp::CaptureCopyLatest:
            frame = std::make_shared<Frame>(*c.latest_);
            break;
        case rust::WorkflowOp::CaptureSetCopiedStamp:
            frame->captured = stamp;
            break;
        case rust::WorkflowOp::CaptureMoveLatest:
            c.latest_ = std::move(frame);
            break;
        case rust::WorkflowOp::CaptureIncrementUnchanged:
            ++c.unchangedPolls_;
            break;
        case rust::WorkflowOp::CapturePoolSize:
            out[0] = double(c.pool_.size());
            break;
        case rust::WorkflowOp::CapturePoolPresent:
            out[0] = bool(c.pool_.at(size_t(a[0])));
            break;
        case rust::WorkflowOp::CapturePoolReferences:
            out[0] = double(c.pool_.at(size_t(a[0])).use_count());
            break;
        case rust::WorkflowOp::CaptureCreatePoolFrame:
            c.pool_.at(size_t(a[0])) = std::make_shared<Frame>();
            break;
        case rust::WorkflowOp::CaptureUsePoolFrame:
            frame = c.pool_.at(size_t(a[0]));
            break;
        case rust::WorkflowOp::CaptureHasPublishFrame:
            out[0] = bool(frame);
            break;
        case rust::WorkflowOp::CaptureResizeBytes:
            frame->bytes.resize(qsizetype(length));
            break;
        case rust::WorkflowOp::CaptureCopyBytes:
            memcpy(frame->bytes.data(), data, length);
            break;
        case rust::WorkflowOp::CaptureAssignFrameFields:
            frame->width = w;
            frame->height = h;
            frame->stride = stride;
            frame->format = fmt;
            frame->bottomUp = bottom;
            frame->captured = stamp;
            break;
        case rust::WorkflowOp::CaptureIncrementSequence:
            frame->sequence = ++c.seq_;
            break;
        case rust::WorkflowOp::CapturePublishFrame:
            c.latest_ = frame;
            break;
        case rust::WorkflowOp::CaptureShortErrorActive:
            out[0] = c.error_ == "采集驱动返回了不完整图像";
            break;
        case rust::WorkflowOp::CaptureClearError:
            c.error_.clear();
            break;
        case rust::WorkflowOp::CaptureAppendArrival:
            c.arrivals_.push_back(stamp);
            break;
        case rust::WorkflowOp::CaptureArrivalCount:
            out[0] = double(c.arrivals_.size());
            break;
        case rust::WorkflowOp::CapturePopArrival:
            c.arrivals_.pop_front();
            break;
        case rust::WorkflowOp::CaptureSetReady:
            c.ready_ = true;
            break;
        case rust::WorkflowOp::CaptureSourceDesktop:
            out[0] = c.config_.s("capture.source") == "desktop";
            break;
        case rust::WorkflowOp::CaptureThreadJoinable:
            out[0] = c.thread_.joinable();
            break;
        case rust::WorkflowOp::CaptureValidate:
            c.config_.validateCapture();
            break;
        case rust::WorkflowOp::CaptureCreateThread:
            c.thread_ = std::thread(&Capture::run, &c);
            break;
        case rust::WorkflowOp::CaptureWaitStart:
            out[0] = c.cv_.wait_for(*lock, std::chrono::seconds(int(a[0])),
                                    [this] { return run(rust::WorkflowMethod::CaptureStartReady)[0] != 0; });
            break;
        case rust::WorkflowOp::CaptureHasError:
            out[0] = !c.error_.isEmpty();
            break;
        case rust::WorkflowOp::CaptureMarkStopped:
            c.stop_ = true;
            break;
        case rust::WorkflowOp::CaptureJoinThread:
            c.thread_.join();
            break;
        case rust::WorkflowOp::CaptureCopyError:
            text = c.error_;
            break;
        case rust::WorkflowOp::CaptureWriteRecovery:
            c.desktopRecovery_ = std::move(text);
            break;
        case rust::WorkflowOp::CaptureRecoveryActive:
            out[0] = !c.desktopRecovery_.isEmpty();
            break;
        case rust::WorkflowOp::CaptureCopyLatestOutput:
            output = c.latest_;
            break;
        case rust::WorkflowOp::CaptureRecoveryState:
            state = {{"recovering", !c.desktopRecovery_.isEmpty()}, {"recovery_reason", c.desktopRecovery_}};
            break;
        case rust::WorkflowOp::CaptureBasicState:
            state = {{"width", c.latest_->width},
                     {"height", c.latest_->height},
                     {"fourcc", formatName(c.latest_->format)}};
            break;
        case rust::WorkflowOp::CaptureLocalBackendState:
            state["backend"] = c.config_.s("capture.local_backend");
            break;
        case rust::WorkflowOp::CaptureCardBackendState:
            state["backend"] = c.config_.s("capture.backend");
            break;
        case rust::WorkflowOp::CaptureIsDxgi:
            out[0] = c.config_.s("capture.local_backend") == "DXGI";
            break;
        case rust::WorkflowOp::CaptureNegotiatedRequest:
            state["requested_fps"] = c.negotiatedFps_;
            break;
        case rust::WorkflowOp::CaptureLocalRequest:
            state["requested_fps"] = c.config_.d("capture.local_fps");
            break;
        case rust::WorkflowOp::CaptureCardRequest:
            state["requested_fps"] = c.config_.d("capture.fps");
            break;
        case rust::WorkflowOp::CaptureRemainingState:
            state["negotiated_fps"] = c.negotiatedFps_;
            state["unchanged_polls"] = double(c.unchangedPolls_);
            state["recovering"] = !c.desktopRecovery_.isEmpty();
            state["recovery_reason"] = c.desktopRecovery_;
            break;
        case rust::WorkflowOp::CaptureArrivalRecent:
            out[0] = rust::okww_rs_capture_recent(nowNs(), c.arrivals_.back());
            break;
        case rust::WorkflowOp::CaptureFpsValue:
            out[0] = rust::okww_rs_capture_fps(c.arrivals_.size(), c.arrivals_.back(), c.arrivals_.front());
            break;
        case rust::WorkflowOp::CaptureWaitNext:
            c.cv_.wait_for(*lock, std::chrono::milliseconds(int(a[0])),
                           [this] { return run(rust::WorkflowMethod::CaptureNextReady)[0] != 0; });
            break;
        case rust::WorkflowOp::CaptureNewSequence:
            out[0] = c.latest_->sequence != after;
            break;
        case rust::WorkflowOp::CaptureCreateCom:
            com.emplace();
            break;
        case rust::WorkflowOp::CaptureCheckCom:
            hr(com->result, "初始化采集 COM 失败");
            break;
        case rust::WorkflowOp::CaptureDescribeThread:
            SetThreadDescription(GetCurrentThread(), L"OKWW Capture");
            break;
        case rust::WorkflowOp::CaptureIsDshow:
            out[0] = c.config_.s("capture.backend") == "DSHOW";
            break;
        case rust::WorkflowOp::CaptureRunDesktop:
            c.runDesktop();
            break;
        case rust::WorkflowOp::CaptureRunDs:
            c.runDs();
            break;
        case rust::WorkflowOp::CaptureRunMf:
            c.runMf();
            break;
        case rust::WorkflowOp::CaptureDropCom:
            com.reset();
            break;
        case rust::WorkflowOp::CaptureCaughtError:
            try {
                std::rethrow_exception(error);
            } catch (const std::exception &e) {
                c.error_ = errorText(e);
            }
            break;
        case rust::WorkflowOp::CaptureReady:
            out[0] = c.ready_;
            break;
        case rust::WorkflowOp::CaptureFlowFailure:
            switch (int(a[0])) {
            case 0:
                fail("采集线程已经启动");
            case 1:
                fail("视频采集启动超时");
            case 2:
                fail(c.error_);
            default:
                fail("Rust 采集流程参数无效");
            }
            break;
        default:
            fail("Rust 采集帧与线程宿主无效");
        }
    }
    std::array<double, 3> run(rust::WorkflowMethod method, std::span<const double> args = {}) {
        return rust::platformWorkflowRun(method, args, *this);
    }
};
void Capture::publish(const uint8_t *data, size_t length, int w, int h, int stride, PixelFormat fmt,
                      bool bottom, i64 stamp, bool compareContents) {
    CaptureFlowSdk sdk{*this};
    sdk.data = data;
    sdk.length = length;
    sdk.w = w;
    sdk.h = h;
    sdk.stride = stride;
    sdk.fmt = fmt;
    sdk.bottom = bottom;
    sdk.stamp = stamp;
    const std::array<double, 6> args{double(w),        double(h),      double(stride),
                                     double(int(fmt)), double(bottom), double(compareContents)};
    sdk.run(rust::WorkflowMethod::CapturePublish, args);
}
void Capture::verifyUnchangedDesktop(i64 stamp) {
    CaptureFlowSdk sdk{*this};
    sdk.stamp = stamp;
    sdk.run(rust::WorkflowMethod::CaptureVerifyUnchanged);
}
void Capture::start() { CaptureFlowSdk{*this}.run(rust::WorkflowMethod::CaptureStart); }
void Capture::stop() { CaptureFlowSdk{*this}.run(rust::WorkflowMethod::CaptureStop); }
QString Capture::error() {
    CaptureFlowSdk sdk{*this};
    sdk.run(rust::WorkflowMethod::CaptureError);
    return sdk.text;
}
void Capture::desktopRecovery(QString reason) {
    CaptureFlowSdk sdk{*this};
    sdk.text = std::move(reason);
    sdk.run(rust::WorkflowMethod::CaptureDesktopRecovery);
}
bool Capture::recovering() {
    return CaptureFlowSdk{*this}.run(rust::WorkflowMethod::CaptureRecovering)[0] != 0;
}
FramePtr Capture::latest() {
    CaptureFlowSdk sdk{*this};
    sdk.run(rust::WorkflowMethod::CaptureLatest);
    return sdk.output;
}
QJsonObject Capture::actual() {
    CaptureFlowSdk sdk{*this};
    sdk.run(rust::WorkflowMethod::CaptureActual);
    return sdk.state;
}
double Capture::fps() { return CaptureFlowSdk{*this}.run(rust::WorkflowMethod::CaptureFps)[0]; }
FramePtr Capture::next(uint64_t after) {
    CaptureFlowSdk sdk{*this};
    sdk.after = after;
    sdk.run(rust::WorkflowMethod::CaptureNext);
    return sdk.output;
}
void Capture::run() { CaptureFlowSdk{*this}.run(rust::WorkflowMethod::CaptureRun); }

struct CaptureDsRunSdk {
    Capture &c;
    ComPtr<IBaseFilter> source;
    ComPtr<IGraphBuilder> graph;
    ComPtr<IPin> selected;
    AM_MEDIA_TYPE *chosen = nullptr;
    std::vector<ComPtr<IPin>> pinList;
    size_t pinIndex = 0;
    ComPtr<IAMStreamConfig> cfg;
    int count = 0, size = 0;
    std::vector<BYTE> cap;
    AM_MEDIA_TYPE *type = nullptr;
    BITMAPINFOHEADER *b = nullptr;
    REFERENCE_TIME *per = nullptr;
    ComPtr<IBaseFilter> grabFilter, nullFilter;
    ComPtr<Grab> grab;
    AM_MEDIA_TYPE actual{};
    int w = 0, h = 0, stride = 0;
    PixelFormat fmt = PixelFormat::BGR24;
    bool bottom = false;
    ComPtr<GrabCB> cb;
    ComPtr<IMediaControl> control;
    ComPtr<IMediaFilter> mf;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::CaptureDsRunSource:
            source = dsDevice(c.config_);
            break;
        case rust::WorkflowOp::CaptureDsCreateGraph:
            hr(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&graph)),
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
            hr(CoCreateInstance(GrabberId, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&grabFilter)),
               "创建原始帧采集器失败");
            break;
        case rust::WorkflowOp::CaptureDsCreateNull:
            hr(CoCreateInstance(NullId, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&nullFilter)),
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

struct CaptureMfRunSdk {
    Capture &c;
    std::optional<MfInit> mf;
    ComPtr<IMFMediaSource> source;
    struct Shutdown {
        IMFMediaSource *p;
        ~Shutdown() { p->Shutdown(); }
    };
    std::optional<Shutdown> shutdown;
    ComPtr<IMFAttributes> attr;
    ComPtr<MfCallback> cb;
    ComPtr<IMFSourceReader> reader;
    ComPtr<IMFMediaType> nativeType;
    Mode nativeMode;
    ComPtr<IMFMediaType> type;
    Mode mode;
    PixelFormat fmt = PixelFormat::BGR24;
    int stride = 0;
    bool bottom = false;
    std::optional<std::unique_lock<std::mutex>> lock;
    ComPtr<IMFSample> sample;
    HRESULT status = S_OK;
    DWORD flags = 0;
    i64 stamp = 0;
    ComPtr<IMFMediaBuffer> buffer;
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
            MFCreateAttributes(&attr, 4);
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
            hr(MFCreateSourceReaderFromMediaSource(source.Get(), attr.Get(), &reader),
               "创建兼容采集接口失败");
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

} // namespace okww
