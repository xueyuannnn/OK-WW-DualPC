#pragma once
#include "capture.hpp"
#include "platform_workflow.hpp"
#include <dshow.h>
#include <mfapi.h>
#include <mfidl.h>
namespace okww::capture_enumeration_test {
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
    int scene;
    QJsonArray trace;
    std::vector<std::unique_ptr<Ref>> objects;
    std::array<struct Activation *, 4> sources{};
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
inline QString propertyText(int id, bool path) {
    return path ? QString("device/%1").arg(id) : QString::fromUtf16(u"采集\0设备", 5) + QString::number(id);
}
struct Bag : Ref {
    explicit Bag(int i) : Ref("bag", i) {}
    HRESULT Read(const wchar_t *key, VARIANT *value, void *) {
        const bool path = QString::fromWCharArray(key) == "DevicePath";
        active->trace.append(QJsonObject{{"bag-read", path ? "path" : "name"}, {"id", id}});
        const auto text = propertyText(id, path).toStdWString();
        if ((active->scene + id) % 7 == 0) {
            value->vt = VT_I4;
            value->lVal = 13;
        } else {
            value->vt = VT_BSTR;
            value->bstrVal = SysAllocStringLen(text.data(), UINT(text.size()));
        }
        return (active->scene + id) % 11 ? S_OK : E_FAIL;
    }
};
struct Moniker : Ref {
    explicit Moniker(int i) : Ref("moniker", i) {}
    HRESULT BindToStorage(void *, void *, Ptr<Bag> *out) {
        active->trace.append(QJsonObject{{"bind-bag", id}});
        if ((active->scene + id) % 13 == 0)
            return E_FAIL;
        out->Attach(active->make<Bag>(id));
        return S_OK;
    }
};
struct MonikerEnum : Ref {
    int index = 0;
    MonikerEnum() : Ref("monikers", 0) {}
    HRESULT Next(ULONG, Ptr<Moniker> *out, void *) {
        active->trace.append("next-moniker");
        if (index >= active->scene % 5) {
            if (active->scene % 31 == 0)
                out->Attach(active->make<Moniker>(999));
            return S_FALSE;
        }
        out->Attach(active->make<Moniker>(index++));
        return S_OK;
    }
};
struct DeviceEnum : Ref {
    DeviceEnum() : Ref("device-enumerator", 0) {}
    HRESULT CreateClassEnumerator(REFCLSID, Ptr<MonikerEnum> *out, DWORD) {
        active->trace.append("class-enumerator");
        if (active->scene % 17 == 0)
            return S_FALSE;
        out->Attach(active->make<MonikerEnum>());
        return S_OK;
    }
};
struct Pin : Ref {
    explicit Pin(int i) : Ref("pin", i) {}
    HRESULT QueryDirection(PIN_DIRECTION *out) {
        active->trace.append(QJsonObject{{"pin-direction", id}});
        *out = PIN_DIRECTION((id + active->scene) % 2);
        return active->scene % 7 ? S_OK : E_FAIL;
    }
};
struct PinEnum : Ref {
    int index = 0;
    PinEnum() : Ref("pins", 0) {}
    HRESULT Next(ULONG, Ptr<Pin> *out, void *) {
        active->trace.append("next-pin");
        if (index >= active->scene % 6)
            return S_FALSE;
        out->Attach(active->make<Pin>(index++));
        return S_OK;
    }
};
struct Filter : Ref {
    Filter() : Ref("filter", 0) {}
    HRESULT EnumPins(Ptr<PinEnum> *out) {
        active->trace.append("enum-pins");
        if (active->scene % 19 == 0)
            return E_FAIL;
        out->Attach(active->make<PinEnum>());
        return S_OK;
    }
};
struct Attributes : Ref {
    Attributes() : Ref("attributes", 0) {}
    HRESULT SetGUID(REFGUID, REFGUID) {
        active->trace.append("set-source-guid");
        return active->scene % 7 ? S_OK : E_FAIL;
    }
};
struct Activation : Ref {
    explicit Activation(int i) : Ref("activation", i) {}
    HRESULT GetAllocatedString(REFGUID key, wchar_t **out, UINT32 *length) {
        const bool path = key == MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK;
        active->trace.append(QJsonObject{{"allocated-string", path ? "path" : "name"}, {"id", id}});
        if ((active->scene + id) % 13 == 0)
            return E_FAIL;
        if ((active->scene + id) % 7 == 0) {
            *out = nullptr;
            *length = 0;
            return S_OK;
        }
        const auto text = propertyText(id, path).toStdWString();
        *length = UINT32(text.size());
        *out = static_cast<wchar_t *>(::CoTaskMemAlloc((text.size() + 1) * sizeof(wchar_t)));
        std::copy(text.begin(), text.end(), *out);
        (*out)[text.size()] = 0;
        return S_OK;
    }
};
inline HRESULT CoCreateInstance(REFCLSID, void *, DWORD, Ptr<DeviceEnum> *out) {
    active->trace.append("create-enumerator");
    if (active->scene % 23 == 0)
        return E_FAIL;
    out->Attach(active->make<DeviceEnum>());
    return S_OK;
}
inline HRESULT MFCreateAttributes(Ptr<Attributes> *out, UINT count) {
    active->trace.append(QJsonObject{{"create-attributes", int(count)}});
    if (active->scene % 29 == 0)
        return E_FAIL;
    out->Attach(active->make<Attributes>());
    return S_OK;
}
inline HRESULT MFEnumDeviceSources(Attributes *, Activation ***out, UINT32 *count) {
    active->trace.append("enumerate-mf");
    if (active->scene % 17 == 0)
        return E_FAIL;
    *count = UINT32(active->scene % 5);
    for (UINT32 i = 0; i < *count; ++i)
        active->sources[i] = active->make<Activation>(int(i));
    *out = active->sources.data();
    return S_OK;
}
inline void freeMemory(void *pointer) {
    if (pointer == active->sources.data())
        active->trace.append("free-source-array");
    else {
        active->trace.append(pointer ? "free-string" : "free-null");
        ::CoTaskMemFree(pointer);
    }
}
inline void VariantInit(VARIANT *value) {
    active->trace.append("variant-init");
    ::VariantInit(value);
}
inline HRESULT VariantClear(VARIANT *value) {
    active->trace.append(QJsonObject{{"variant-clear", int(value->vt)}});
    return ::VariantClear(value);
}
namespace original {
static std::vector<std::pair<Device, Ptr<Moniker>>> dsDevices() {
    Ptr<DeviceEnum> en;
    hr(capture_enumeration_test::CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, &en),
       "枚举采集设备失败");
    Ptr<MonikerEnum> mons;
    if (en->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &mons, 0) != S_OK)
        return {};
    std::vector<std::pair<Device, Ptr<Moniker>>> out;
    Ptr<Moniker> m;
    while (mons->Next(1, &m, nullptr) == S_OK) {
        Ptr<Bag> bag;
        hr(m->BindToStorage(nullptr, nullptr, &bag), "读取设备属性失败");
        auto prop = [&](const wchar_t *name) {
            VARIANT v;
            capture_enumeration_test::VariantInit(&v);
            QString s;
            if (SUCCEEDED(bag->Read(name, &v, nullptr)) && v.vt == VT_BSTR)
                s = QString::fromWCharArray(v.bstrVal);
            capture_enumeration_test::VariantClear(&v);
            return s;
        };
        out.push_back({{int(out.size()), prop(L"FriendlyName"), prop(L"DevicePath")}, m});
        m.Reset();
    }
    return out;
}
static std::vector<Ptr<Pin>> pins(Filter *f, PIN_DIRECTION dir) {
    Ptr<PinEnum> en;
    hr(f->EnumPins(&en), "枚举采集引脚失败");
    std::vector<Ptr<Pin>> p;
    Ptr<Pin> v;
    while (en->Next(1, &v, nullptr) == S_OK) {
        PIN_DIRECTION d;
        v->QueryDirection(&d);
        if (d == dir)
            p.push_back(v);
        v.Reset();
    }
    return p;
}
static std::vector<std::pair<Device, Ptr<Activation>>> mfDevices() {
    Ptr<Attributes> a;
    hr(capture_enumeration_test::MFCreateAttributes(&a, 1), "创建媒体属性失败");
    a->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    Activation **ptr = nullptr;
    UINT32 count = 0;
    hr(capture_enumeration_test::MFEnumDeviceSources(a.Get(), &ptr, &count), "枚举媒体设备失败");
    std::vector<std::pair<Device, Ptr<Activation>>> out;
    for (UINT32 i = 0; i < count; ++i) {
        Ptr<Activation> act;
        act.Attach(ptr[i]);
        auto get = [&](const GUID &k) {
            wchar_t *p = nullptr;
            UINT32 n = 0;
            QString r;
            if (SUCCEEDED(act->GetAllocatedString(k, &p, &n))) {
                r = QString::fromWCharArray(p, int(n));
                capture_enumeration_test::freeMemory(p);
            }
            return r;
        };
        out.push_back({{int(i), get(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME),
                        get(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK)},
                       act});
    }
    capture_enumeration_test::freeMemory(ptr);
    return out;
}
} // namespace original
namespace current {
struct CaptureDsDevicesSdk {
    Ptr<DeviceEnum> en;
    Ptr<MonikerEnum> mons;
    std::vector<std::pair<Device, Ptr<Moniker>>> values;
    Ptr<Moniker> moniker;
    Ptr<Bag> bag;
    QString name, path;
    QString property(const wchar_t *key) {
        VARIANT value;
        capture_enumeration_test::VariantInit(&value);
        QString text;
        if (SUCCEEDED(bag->Read(key, &value, nullptr)) && value.vt == VT_BSTR)
            text = QString::fromWCharArray(value.bstrVal);
        capture_enumeration_test::VariantClear(&value);
        return text;
    }
    void operator()(rust::WorkflowOp op, const double *, double *out) {
        switch (op) {
        case rust::WorkflowOp::CaptureDsCreateEnumerator:
            hr(capture_enumeration_test::CoCreateInstance(CLSID_SystemDeviceEnum, nullptr,
                                                          CLSCTX_INPROC_SERVER, &en),
               "枚举采集设备失败");
            break;
        case rust::WorkflowOp::CaptureDsClassEnumerator:
            out[0] = en->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &mons, 0) == S_OK;
            break;
        case rust::WorkflowOp::CaptureDsNextMoniker:
            out[0] = mons->Next(1, &moniker, nullptr) == S_OK;
            break;
        case rust::WorkflowOp::CaptureDsBindBag:
            hr(moniker->BindToStorage(nullptr, nullptr, &bag), "读取设备属性失败");
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
struct CapturePinsSdk {
    Filter *f;
    Ptr<PinEnum> en;
    std::vector<Ptr<Pin>> values;
    Ptr<Pin> pin;
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
struct CaptureMfDevicesSdk {
    Ptr<Attributes> attributes;
    Activation **ptr = nullptr;
    UINT32 count = 0;
    std::vector<std::pair<Device, Ptr<Activation>>> values;
    Ptr<Activation> act;
    QString name, path;
    QString property(const GUID &key) {
        wchar_t *p = nullptr;
        UINT32 n = 0;
        QString value;
        if (SUCCEEDED(act->GetAllocatedString(key, &p, &n))) {
            value = QString::fromWCharArray(p, int(n));
            capture_enumeration_test::freeMemory(p);
        }
        return value;
    }
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::CaptureMfCreateAttributes:
            hr(capture_enumeration_test::MFCreateAttributes(&attributes, 1), "创建媒体属性失败");
            break;
        case rust::WorkflowOp::CaptureMfSetVideoSource:
            attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
            break;
        case rust::WorkflowOp::CaptureMfEnumerateSources:
            hr(capture_enumeration_test::MFEnumDeviceSources(attributes.Get(), &ptr, &count),
               "枚举媒体设备失败");
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
            capture_enumeration_test::freeMemory(ptr);
            break;
        default:
            fail("Rust 媒体设备枚举宿主无效");
        }
    }
};
static std::vector<std::pair<Device, Ptr<Moniker>>> dsDevices() {
    CaptureDsDevicesSdk sdk;
    rust::platformWorkflowRun(rust::WorkflowMethod::CaptureDsDevices, {}, sdk);
    return std::move(sdk.values);
}
static std::vector<Ptr<Pin>> pins(Filter *f, PIN_DIRECTION dir) {
    CapturePinsSdk sdk{f};
    const std::array<double, 1> args{double(dir)};
    rust::platformWorkflowRun(rust::WorkflowMethod::CaptureDsPins, args, sdk);
    return std::move(sdk.values);
}
static std::vector<std::pair<Device, Ptr<Activation>>> mfDevices() {
    CaptureMfDevicesSdk sdk;
    rust::platformWorkflowRun(rust::WorkflowMethod::CaptureMfDevices, {}, sdk);
    return std::move(sdk.values);
}
} // namespace current
inline QJsonObject outcome(int scene, int method, bool rust) {
    State state(scene);
    active = &state;
    QJsonObject result;
    try {
        if (method == 0) {
            auto list = rust ? current::dsDevices() : original::dsDevices();
            QJsonArray values;
            for (const auto &[d, m] : list)
                values.append(
                    QJsonObject{{"index", d.index}, {"name", d.name}, {"path", d.path}, {"object", m->id}});
            result["devices"] = values;
        } else if (method == 1) {
            auto list = rust ? current::mfDevices() : original::mfDevices();
            QJsonArray values;
            for (const auto &[d, a] : list)
                values.append(
                    QJsonObject{{"index", d.index}, {"name", d.name}, {"path", d.path}, {"object", a->id}});
            result["devices"] = values;
        } else {
            Filter filter;
            auto list = rust ? current::pins(&filter, PIN_DIRECTION(method - 2))
                             : original::pins(&filter, PIN_DIRECTION(method - 2));
            QJsonArray values;
            for (const auto &p : list)
                values.append(p->id);
            result["pins"] = values;
        }
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    } catch (...) {
        result["error"] = "unknown";
    }
    result["trace"] = state.trace;
    return result;
}
} // namespace okww::capture_enumeration_test
