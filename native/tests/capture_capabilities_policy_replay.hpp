#pragma once
#include "capture.hpp"
#include "capture_math_abi.hpp"
#include "platform_workflow.hpp"
#include <optional>
namespace okww::capture_capabilities_test {
inline bool supported(const QString &name) {
    return rust::okww_rs_capture_format(reinterpret_cast<const uint16_t *>(name.constData()),
                                        size_t(name.size())) == int(PixelFormat::NV12);
}
namespace original {
QJsonArray captureCapabilities(const Config &config,
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
                if (supported(mode.fourcc))
                    parameters.append(mode.json());
        } catch (const std::exception &error) {
            entry["error"] = errorText(error);
        }
        entry["modes"] = parameters;
        results.append(entry);
    }
    return results;
}

} // namespace original
namespace current {
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

} // namespace current
inline QJsonObject outcome(int scene, bool rust) {
    QJsonArray trace;
    QJsonObject result;
    Config config = Config::defaults();
    config.set("capture.backend", scene % 2 ? "DSHOW" : "MSMF");
    config.set("capture.extra", QJsonObject{{"retained", scene}});
    const std::function<std::vector<Device>(const QString &)> devices = [&](const QString &backend) {
        trace.append(QJsonObject{{"devices", backend}});
        if (scene % 17 == 0)
            throw 27;
        if (scene % 13 == 0)
            throw std::runtime_error("enumeration fixture");
        std::vector<Device> out;
        for (int i = 0; i < scene % 6; ++i)
            out.push_back({i - 1, i % 2 ? QString("采集设备") : QString{}, QString("device/%1").arg(i)});
        return out;
    };
    const std::function<std::vector<Mode>(const Config &)> modes = [&](const Config &selected) {
        trace.append(QJsonObject{{"modes", selected.json}});
        const auto index = selected.n("capture.device_index");
        if (scene % 19 == 0 && index == 0)
            throw 27;
        if (scene % 11 == 0 && index < 1)
            throw std::runtime_error("device busy fixture");
        const QStringList formats{"NV12", "RGB4", "P010", "nv12", "NV12", QString{}, "IYUV"};
        std::vector<Mode> out;
        for (int i = 0; i < (scene + index + 7) % 9; ++i)
            out.push_back({1920 - i, 1080 + i, i % 3 ? 240. : 0., formats[i % formats.size()]});
        return out;
    };
    try {
        result["entries"] = rust ? current::captureCapabilities(config, devices, modes)
                                 : original::captureCapabilities(config, devices, modes);
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    } catch (...) {
        result["error"] = "unknown";
    }
    result["trace"] = trace;
    return result;
}
} // namespace okww::capture_capabilities_test
