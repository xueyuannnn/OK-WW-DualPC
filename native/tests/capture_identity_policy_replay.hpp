#pragma once
#include "capture.hpp"
#include "capture_math_abi.hpp"
#include "platform_workflow.hpp"
#include <mfapi.h>
#include <mfidl.h>
#include <optional>
namespace okww::capture_identity_test {
struct State {
    int scene;
    QJsonArray trace;
    explicit State(int s) : scene(s) {}
};
inline thread_local State *active = nullptr;
template <class T> struct Ptr {
    std::shared_ptr<T> pointer;
    T *operator->() const { return pointer.get(); }
    T *Get() const { return pointer.get(); }
    Ptr *operator&() { return this; }
    explicit operator bool() const { return bool(pointer); }
};
struct Filter {
    int id;
};
struct Source {
    int id;
};
struct Moniker {
    int id;
    HRESULT BindToObject(void *, void *, Ptr<Filter> *out) {
        active->trace.append(QJsonObject{{"bind", id}});
        if (active->scene % 29 == 0)
            return E_FAIL;
        out->pointer = std::make_shared<Filter>(Filter{id});
        return S_OK;
    }
};
struct Activation {
    int id;
    HRESULT ActivateObject(Ptr<Source> *out) {
        active->trace.append(QJsonObject{{"activate", id}});
        if (active->scene % 13 == 0)
            return E_FAIL;
        out->pointer = std::make_shared<Source>(Source{id});
        return S_OK;
    }
};
inline std::vector<std::pair<Device, Ptr<Moniker>>> dsDevices() {
    active->trace.append("ds-devices");
    if (active->scene % 23 == 0)
        throw std::runtime_error("ds enumeration fixture");
    return {{{0, "采集卡 A", "PATH/A"}, {std::make_shared<Moniker>(Moniker{0})}},
            {{1, "采集卡 A", "path/a"}, {std::make_shared<Moniker>(Moniker{1})}},
            {{2, "采集卡 B", "path/b"}, {std::make_shared<Moniker>(Moniker{2})}}};
}
inline std::vector<std::pair<Device, Ptr<Activation>>> mfDevices() {
    active->trace.append("mf-devices");
    if (active->scene % 17 == 0)
        throw 27;
    return {{{0, "采集卡 A", "PATH/A"}, {std::make_shared<Activation>(Activation{0})}},
            {{1, "采集卡 A", "path/a"}, {std::make_shared<Activation>(Activation{1})}},
            {{2, "采集卡 B", "path/b"}, {std::make_shared<Activation>(Activation{2})}}};
}
struct Com {
    Com() { active->trace.append("com"); }
    ~Com() { active->trace.append("com-end"); }
};
struct Mf {
    Mf() {
        active->trace.append("mf");
        if (active->scene % 31 == 0)
            throw std::runtime_error("mf startup fixture");
    }
    ~Mf() { active->trace.append("mf-end"); }
};
struct Media {
    HRESULT GetGUID(REFGUID, GUID *sub) {
        active->trace.append("subtype");
        if (active->scene % 19 == 0)
            return E_FAIL;
        *sub = active->scene % 3 == 0   ? MFVideoFormat_RGB32
               : active->scene % 3 == 1 ? MFVideoFormat_RGB24
                                        : MFVideoFormat_NV12;
        return S_OK;
    }
};
inline HRESULT MFGetAttributeSize(Media *, REFGUID, UINT32 *w, UINT32 *h) {
    active->trace.append("size");
    if (active->scene % 13 == 0)
        return E_FAIL;
    *w = active->scene % 7 == 0 ? 0xfffffffe : 1920;
    *h = active->scene % 11 == 0 ? 0xffffffff : 1080;
    return S_OK;
}
inline HRESULT MFGetAttributeRatio(Media *, REFGUID, UINT32 *num, UINT32 *den) {
    active->trace.append("rate");
    if (active->scene % 17 == 0)
        return E_FAIL;
    *num = active->scene % 5 == 0 ? 0xffffffff : 240000;
    *den = active->scene % 7 == 0 ? 0 : 1001;
    return S_OK;
}
inline QString fourcc(DWORD v, int bits = 0) {
    std::array<uint8_t, 4> text{};
    int n = rust::okww_rs_capture_fourcc(v, bits, text.data());
    return QString::fromLatin1(reinterpret_cast<const char *>(text.data()), n);
}
namespace original {
static Ptr<Filter> dsDevice(const Config &c) {
    auto list = dsDevices();
    std::vector<Ptr<Moniker>> matches;
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
    Ptr<Filter> filter;
    hr(matches[0]->BindToObject(nullptr, nullptr, &filter), "打开采集卡失败");
    return filter;
}
static Ptr<Source> mfSource(const Config &c) {
    auto list = mfDevices();
    for (auto &[d, a] : list) {
        if ((!c.s("capture.device_path").isEmpty() &&
             d.path.compare(c.s("capture.device_path"), Qt::CaseInsensitive) == 0) ||
            (c.s("capture.device_path").isEmpty() &&
             (!c.s("capture.device_name").isEmpty() ? d.name == c.s("capture.device_name")
                                                    : d.index == c.n("capture.device_index")))) {
            Ptr<Source> source;
            hr(a->ActivateObject(&source), "打开采集设备失败");
            return source;
        }
    }
    fail("采集设备不存在，请重新扫描");
    return {};
}
static Mode mfMode(Media *t) {
    UINT32 w = 0, h = 0, num = 0, den = 1;
    GUID sub{};
    capture_identity_test::MFGetAttributeSize(t, MF_MT_FRAME_SIZE, &w, &h);
    capture_identity_test::MFGetAttributeRatio(t, MF_MT_FRAME_RATE, &num, &den);
    t->GetGUID(MF_MT_SUBTYPE, &sub);
    QString fmt = sub == MFVideoFormat_RGB32   ? "RGB4"
                  : sub == MFVideoFormat_RGB24 ? "RGB3"
                                               : fourcc(sub.Data1);
    return {int(w), int(h), den ? double(num) / den : 0, fmt};
}
std::vector<Device> devices(const QString &backend) {
    Com com;
    std::vector<Device> out;
    if (backend == "DSHOW") {
        for (auto &p : dsDevices())
            out.push_back(p.first);
    } else {
        Mf mf;
        for (auto &p : mfDevices())
            out.push_back(p.first);
    }
    return out;
}
} // namespace original
namespace current {
struct CaptureDsDeviceSdk {
    const Config &c;
    std::vector<std::pair<Device, Ptr<Moniker>>> list;
    std::vector<Ptr<Moniker>> matches;
    Ptr<Filter> filter;
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
            hr(matches.at(0)->BindToObject(nullptr, nullptr, &filter), "打开采集卡失败");
            break;
        case rust::WorkflowOp::CaptureDeviceFailure:
            fail("采集卡未找到或名称重复，请重新扫描设备");
            break;
        default:
            fail("Rust DirectShow 设备选择宿主无效");
        }
    }
};
struct CaptureMfSourceSdk {
    const Config &c;
    std::vector<std::pair<Device, Ptr<Activation>>> list;
    Ptr<Source> source;
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
            hr(list.at(size_t(a[0])).second->ActivateObject(&source), "打开采集设备失败");
            break;
        case rust::WorkflowOp::CaptureDeviceFailure:
            fail("采集设备不存在，请重新扫描");
            break;
        default:
            fail("Rust 媒体设备选择宿主无效");
        }
    }
};
struct CaptureMfModeSdk {
    Media *t;
    UINT32 w = 0, h = 0, num = 0, den = 1;
    GUID subtype{};
    Mode value;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::CaptureMfReadSize:
            capture_identity_test::MFGetAttributeSize(t, MF_MT_FRAME_SIZE, &w, &h);
            break;
        case rust::WorkflowOp::CaptureMfReadRate:
            capture_identity_test::MFGetAttributeRatio(t, MF_MT_FRAME_RATE, &num, &den);
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
struct CaptureDevicesSdk {
    const QString &backend;
    std::vector<Device> values;
    std::optional<Mf> mf;
    std::vector<std::pair<Device, Ptr<Moniker>>> dsList;
    std::vector<std::pair<Device, Ptr<Activation>>> mfList;
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
static Ptr<Filter> dsDevice(const Config &c) {
    CaptureDsDeviceSdk sdk{c};
    rust::platformWorkflowRun(rust::WorkflowMethod::CaptureDsDevice, {}, sdk);
    return std::move(sdk.filter);
}
static Ptr<Source> mfSource(const Config &c) {
    CaptureMfSourceSdk sdk{c};
    rust::platformWorkflowRun(rust::WorkflowMethod::CaptureMfSource, {}, sdk);
    return std::move(sdk.source);
}
static Mode mfMode(Media *t) {
    CaptureMfModeSdk sdk{t};
    rust::platformWorkflowRun(rust::WorkflowMethod::CaptureMfMode, {}, sdk);
    return std::move(sdk.value);
}
std::vector<Device> devices(const QString &backend) {
    Com com;
    CaptureDevicesSdk sdk{backend};
    rust::platformWorkflowRun(rust::WorkflowMethod::CaptureDevices, {}, sdk);
    return std::move(sdk.values);
}
} // namespace current
inline QJsonObject outcome(int scene, int method, bool rust) {
    State state(scene);
    active = &state;
    QJsonObject result;
    Config config = Config::defaults();
    const QStringList paths{"", "PATH/A", "path/b", "missing"}, names{"", "采集卡 A", "采集卡 B", "missing"};
    config.set("capture.device_path", paths[scene % 4]);
    config.set("capture.device_name", names[(scene / 4) % 4]);
    config.set("capture.device_index", scene % 5 - 1);
    try {
        if (method == 0) {
            auto filter = rust ? current::dsDevice(config) : original::dsDevice(config);
            result["id"] = filter->id;
        } else if (method == 1) {
            auto source = rust ? current::mfSource(config) : original::mfSource(config);
            result["id"] = source->id;
        } else if (method == 2) {
            Media media;
            result["mode"] = rust ? current::mfMode(&media).json() : original::mfMode(&media).json();
        } else {
            const QString backend = method == 3 ? "DSHOW" : method == 4 ? "MSMF" : "other";
            auto list = rust ? current::devices(backend) : original::devices(backend);
            QJsonArray devices;
            for (const auto &d : list)
                devices.append(QJsonObject{{"index", d.index}, {"name", d.name}, {"path", d.path}});
            result["devices"] = devices;
        }
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    } catch (...) {
        result["error"] = "unknown";
    }
    result["trace"] = state.trace;
    return result;
}
} // namespace okww::capture_identity_test
