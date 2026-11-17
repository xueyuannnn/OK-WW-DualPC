#include "common.hpp"
#include "common_pixels_abi.hpp"
#include "platform_workflow.hpp"
#include <QHostAddress>
#include <new>
#include <optional>
#ifdef OKWW_FAST_PREVIEW
#include <opencv2/imgproc.hpp>
#endif
namespace okww {

namespace {
[[noreturn]] void commonConfigFailure(int code, const QString &path = {}) {
    switch (code) {
    case 0:
        fail("无法读取配置：" + path);
    case 1:
        fail("配置格式错误：" + path);
    case 2:
        fail("保存配置失败：" + path);
    }
    static const char *const errors[]{"请选择采集方式",
                                      "请选择本机截图方式，采集帧率需为 1–1000 的整数",
                                      "请选择有效的本机截图目标",
                                      "C++ 采集支持 DirectShow / Media Foundation，请重新选择后端",
                                      "采集分辨率或帧率无效",
                                      "采集卡图像格式仅支持 NV12，请重新检测设备与参数",
                                      "识别图像需要至少 1280×720 的 16:9 分辨率",
                                      "图像过期时间无效",
                                      "请选择 KMBox Net 或 SendInput",
                                      "请在高级设置中选择 SendInput 目标窗口，或切回当前前台窗口",
                                      "盒子超时、倍率或桌面尺寸无效",
                                      "定位后等待需要为 0.11 至 1 秒，界面点击按住需要为 0.1 至 0.5 秒",
                                      "请填写盒子的 IPv4 地址、端口和 8 位 UUID"};
    fail(QString::fromUtf8(errors[code - 3]));
}
struct CommonConfigSdk {
    const Config &cfg;
    Config *write = nullptr;
    const QString *path = nullptr;
    QJsonObject object, sub, incoming, box, capture, target;
    QJsonObject::const_iterator outer, inner;
    QStringList parts;
    QJsonValue value;
    static QString numberPath(int index) {
        static const QStringList paths{
            "capture.width",         "capture.height",           "capture.fps",
            "capture.output_width",  "capture.output_height",    "capture.stale_after",
            "capture.local_fps",     "kmbox.desktop_width",      "kmbox.desktop_height",
            "kmbox.timeout",         "kmbox.counts_per_pixel_x", "kmbox.counts_per_pixel_y",
            "kmbox.position_settle", "kmbox.ui_click_hold",      "kmbox.port"};
        return paths.at(index);
    }
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::CommonConfigReadIncoming:
            object = readJson(*path);
            break;
        case rust::WorkflowOp::CommonConfigIncomingBegin:
            outer = object.constBegin();
            break;
        case rust::WorkflowOp::CommonConfigIncomingMore:
            out[0] = outer != object.constEnd();
            break;
        case rust::WorkflowOp::CommonConfigIncomingObject:
            out[0] = outer.value().isObject();
            break;
        case rust::WorkflowOp::CommonConfigExistingSub:
            sub = write->json[outer.key()].toObject();
            break;
        case rust::WorkflowOp::CommonConfigIncomingSub:
            incoming = outer.value().toObject();
            break;
        case rust::WorkflowOp::CommonConfigSubBegin:
            inner = incoming.constBegin();
            break;
        case rust::WorkflowOp::CommonConfigSubMore:
            out[0] = inner != incoming.constEnd();
            break;
        case rust::WorkflowOp::CommonConfigSubAssign:
            sub[inner.key()] = inner.value();
            break;
        case rust::WorkflowOp::CommonConfigSubNext:
            ++inner;
            break;
        case rust::WorkflowOp::CommonConfigAssignSub:
            write->json[outer.key()] = sub;
            break;
        case rust::WorkflowOp::CommonConfigDropIncomingSub:
            incoming = {};
            sub = {};
            break;
        case rust::WorkflowOp::CommonConfigAssignValue:
            write->json[outer.key()] = outer.value();
            break;
        case rust::WorkflowOp::CommonConfigIncomingNext:
            ++outer;
            break;
        case rust::WorkflowOp::CommonConfigNormalize:
            write->normalizeBox();
            break;
        case rust::WorkflowOp::CommonConfigReadBox:
            box = write->json["kmbox"].toObject();
            break;
        case rust::WorkflowOp::CommonConfigRetiredProtocol:
            out[0] = box["protocol"].toString() == "makcu_serial";
            break;
        case rust::WorkflowOp::CommonConfigNetworkProtocol:
            box["protocol"] = "kmbox_net";
            break;
        case rust::WorkflowOp::CommonConfigRemoveRetiredKey: {
            static const QStringList keys{"hid_vid", "hid_pid", "hid_path", "serial_port", "baudrate"};
            box.remove(keys.at(int(a[0])));
            break;
        }
        case rust::WorkflowOp::CommonConfigStoreBox:
            write->json["kmbox"] = box;
            break;
        case rust::WorkflowOp::CommonConfigReadCapture:
            capture = write->json["capture"].toObject();
            break;
        case rust::WorkflowOp::CommonConfigRemoveAutoRegion:
            capture.remove("auto_game_region");
            break;
        case rust::WorkflowOp::CommonConfigCaptureIsDesktop:
            out[0] = capture["source"].toString() == "desktop";
            break;
        case rust::WorkflowOp::CommonConfigSetNv12:
            capture["fourcc"] = "NV12";
            break;
        case rust::WorkflowOp::CommonConfigStoreCapture:
            write->json["capture"] = capture;
            break;
        case rust::WorkflowOp::CommonConfigValidateCapture:
            cfg.validateCapture();
            break;
        case rust::WorkflowOp::CommonConfigValidateBox:
            cfg.validateBox();
            break;
        case rust::WorkflowOp::CommonConfigWrite:
            writeJson(*path, cfg.json);
            break;
        case rust::WorkflowOp::CommonConfigSplitPath:
            parts = path->split('.');
            break;
        case rust::WorkflowOp::CommonConfigPartCount:
            out[0] = double(parts.size());
            break;
        case rust::WorkflowOp::CommonConfigGetPart:
            value = value.toObject().value(parts.at(qsizetype(a[0])));
            break;
        case rust::WorkflowOp::CommonConfigSetTop:
            write->json[*path] = value;
            break;
        case rust::WorkflowOp::CommonConfigReadSetSub:
            sub = write->json[parts[0]].toObject();
            break;
        case rust::WorkflowOp::CommonConfigSetSecond:
            sub[parts[1]] = value;
            break;
        case rust::WorkflowOp::CommonConfigStoreSetSub:
            write->json[parts[0]] = sub;
            break;
        case rust::WorkflowOp::CommonConfigNumber:
            out[0] = cfg.d(numberPath(int(a[0])));
            break;
        case rust::WorkflowOp::CommonConfigInteger:
            out[0] = cfg.n(numberPath(int(a[0])));
            break;
        case rust::WorkflowOp::CommonConfigSourceDesktop:
            out[0] = cfg.s("capture.source") == "desktop";
            break;
        case rust::WorkflowOp::CommonConfigSourceCode:
            out[0] = QStringList{"card", "desktop"}.indexOf(cfg.s("capture.source"));
            break;
        case rust::WorkflowOp::CommonConfigLocalBackendCode:
            out[0] = QStringList{"DXGI", "WIN32"}.indexOf(cfg.s("capture.local_backend"));
            break;
        case rust::WorkflowOp::CommonConfigReadLocalTarget:
            target = cfg.get("capture.local_target").toObject();
            break;
        case rust::WorkflowOp::CommonConfigLocalTargetEmpty:
            out[0] = target.isEmpty();
            break;
        case rust::WorkflowOp::CommonConfigLocalTargetKind:
            out[0] = QStringList{"monitor", "window"}.indexOf(target["kind"].toString());
            break;
        case rust::WorkflowOp::CommonConfigCardBackendCode:
            out[0] = QStringList{"DSHOW", "MSMF"}.indexOf(cfg.s("capture.backend"));
            break;
        case rust::WorkflowOp::CommonConfigFormatNv12:
            out[0] = cfg.s("capture.fourcc") == "NV12";
            break;
        case rust::WorkflowOp::CommonConfigProtocolCode:
            out[0] = QStringList{"kmbox_net", "sendinput"}.indexOf(cfg.s("kmbox.protocol"));
            break;
        case rust::WorkflowOp::CommonConfigTargetEnabled:
            out[0] = cfg.b("kmbox.target_enabled");
            break;
        case rust::WorkflowOp::CommonConfigInputTargetWindow:
            out[0] = cfg.get("kmbox.target").toObject()["kind"].toString() == "window";
            break;
        case rust::WorkflowOp::CommonConfigDryRun:
            out[0] = cfg.b("kmbox.dry_run");
            break;
        case rust::WorkflowOp::CommonConfigIpv4: {
            QHostAddress address(cfg.s("kmbox.host"));
            out[0] = address.protocol() == QAbstractSocket::IPv4Protocol;
            break;
        }
        case rust::WorkflowOp::CommonConfigUuidValid:
            out[0] = QRegularExpression("^[0-9a-fA-F]{8}$").match(cfg.s("kmbox.uuid")).hasMatch();
            break;
        case rust::WorkflowOp::CommonConfigFailure:
            commonConfigFailure(int(a[0]));
            break;
        default:
            fail("Rust 配置宿主无效");
        }
    }
    void run(rust::WorkflowMethod method) { rust::platformWorkflowRun(method, {}, *this); }
};
} // namespace
QJsonObject readJson(const QString &p) {
    QFile file(p);
    QByteArray bytes;
    QJsonParseError error;
    QJsonDocument doc;
    QJsonObject result;
    rust::platformWorkflowRun(rust::WorkflowMethod::CommonReadJson, {},
                              [&](rust::WorkflowOp op, const double *a, double *out) {
                                  switch (op) {
                                  case rust::WorkflowOp::CommonJsonOpenRead:
                                      out[0] = file.open(QIODevice::ReadOnly);
                                      break;
                                  case rust::WorkflowOp::CommonJsonReadBytes:
                                      bytes = file.readAll();
                                      break;
                                  case rust::WorkflowOp::CommonJsonHasBom:
                                      out[0] = bytes.startsWith("\xef\xbb\xbf");
                                      break;
                                  case rust::WorkflowOp::CommonJsonRemoveBom:
                                      bytes.remove(0, 3);
                                      break;
                                  case rust::WorkflowOp::CommonJsonParse:
                                      doc = QJsonDocument::fromJson(bytes, &error);
                                      break;
                                  case rust::WorkflowOp::CommonJsonParseOk:
                                      out[0] = error.error == QJsonParseError::NoError;
                                      break;
                                  case rust::WorkflowOp::CommonJsonObject:
                                      out[0] = doc.isObject();
                                      break;
                                  case rust::WorkflowOp::CommonJsonResult:
                                      result = doc.object();
                                      break;
                                  case rust::WorkflowOp::CommonConfigFailure:
                                      commonConfigFailure(int(a[0]), p);
                                      break;
                                  default:
                                      fail("Rust 配置读取宿主无效");
                                  }
                              });
    return result;
}
void writeJson(const QString &p, const QJsonObject &obj) {
    std::optional<QSaveFile> file;
    QByteArray data;
    rust::platformWorkflowRun(rust::WorkflowMethod::CommonWriteJson, {},
                              [&](rust::WorkflowOp op, const double *a, double *out) {
                                  switch (op) {
                                  case rust::WorkflowOp::CommonJsonMakeDirectory:
                                      QDir().mkpath(QFileInfo(p).absolutePath());
                                      break;
                                  case rust::WorkflowOp::CommonJsonSaveFile:
                                      file.emplace(p);
                                      break;
                                  case rust::WorkflowOp::CommonJsonEncode:
                                      data = QJsonDocument(obj).toJson(QJsonDocument::Indented);
                                      break;
                                  case rust::WorkflowOp::CommonJsonOpenWrite:
                                      out[0] = file->open(QIODevice::WriteOnly);
                                      break;
                                  case rust::WorkflowOp::CommonJsonWrite:
                                      out[0] = file->write(data) == data.size();
                                      break;
                                  case rust::WorkflowOp::CommonJsonCommit:
                                      out[0] = file->commit();
                                      break;
                                  case rust::WorkflowOp::CommonConfigFailure:
                                      commonConfigFailure(int(a[0]), p);
                                      break;
                                  default:
                                      fail("Rust 配置写入宿主无效");
                                  }
                              });
}
Config Config::defaults() {
    return {{{"capture", QJsonObject{{"device_index", 0},
                                     {"device_name", ""},
                                     {"device_path", ""},
                                     {"backend", "DSHOW"},
                                     {"source", "card"},
                                     {"local_backend", "DXGI"},
                                     {"dxgi_monitor", ""},
                                     {"dxgi_follow_refresh", true},
                                     {"local_target", QJsonObject{}},
                                     {"local_fps", 60},
                                     {"width", 1920},
                                     {"height", 1080},
                                     {"fps", 60},
                                     {"fourcc", "NV12"},
                                     {"crop", QJsonValue()},
                                     {"output_width", 1920},
                                     {"output_height", 1080},
                                     {"stale_after", 0.15},
                                     {"ffmpeg_path", "ffmpeg"}}},
             {"kmbox", QJsonObject{{"protocol", "kmbox_net"},
                                   {"target_enabled", false},
                                   {"target", QJsonObject{}},
                                   {"host", ""},
                                   {"port", 0},
                                   {"uuid", ""},
                                   {"timeout", 0.3},
                                   {"dry_run", false},
                                   {"desktop_width", 1920},
                                   {"desktop_height", 1080},
                                   {"counts_per_pixel_x", 1.0},
                                   {"counts_per_pixel_y", 1.0},
                                   {"home_counts", 32767},
                                   {"home_wait", 0.05},
                                   {"move_chunk", 512},
                                   {"move_interval", 0.002},
                                   {"position_settle", 0.12},
                                   {"ui_click_hold", 0.12}}},
             {"game_language", "zh_CN"}}};
}
Config Config::load(const QString &p) {
    Config c = defaults();
    CommonConfigSdk{c, &c, &p}.run(rust::WorkflowMethod::CommonConfigLoad);
    return c;
}
void Config::normalizeBox() { CommonConfigSdk{*this, this}.run(rust::WorkflowMethod::CommonConfigNormalize); }
void Config::save(const QString &p) const {
    CommonConfigSdk{*this, nullptr, &p}.run(rust::WorkflowMethod::CommonConfigSave);
}
QJsonValue Config::get(const QString &path) const {
    CommonConfigSdk sdk{*this, nullptr, &path};
    sdk.value = QJsonValue(json);
    sdk.run(rust::WorkflowMethod::CommonConfigGet);
    return sdk.value;
}
void Config::set(const QString &path, const QJsonValue &v) {
    CommonConfigSdk sdk{*this, this, &path};
    sdk.value = v;
    sdk.run(rust::WorkflowMethod::CommonConfigSet);
}
void Config::validateCapture() const {
    CommonConfigSdk{*this}.run(rust::WorkflowMethod::CommonConfigValidateCapture);
}
void Config::validateBox() const {
    CommonConfigSdk{*this}.run(rust::WorkflowMethod::CommonConfigValidateBox);
}
QString formatName(PixelFormat f) {
    return QStringList{"RGB3", "RGB4", "NV12", "P010", "YUY2", "UYVY", "I420", "MJPG"}.at(int(f));
}
} // namespace okww
namespace okww {

namespace {
struct CommonPreviewSdk {
    const Frame &f;
    QSize target, size;
    QImage result;
#ifdef OKWW_FAST_PREVIEW
    cv::Mat bgr, resized;
#endif
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::CommonPreviewFormat:
            out[0] = int(f.format);
            break;
        case rust::WorkflowOp::CommonPreviewDimensions:
            out[0] = f.width;
            out[1] = f.height;
            out[2] = f.stride;
            break;
        case rust::WorkflowOp::CommonFrameDecodeJpeg:
            result = QImage::fromData(f.bytes, "JPG");
            break;
        case rust::WorkflowOp::CommonFrameImageNull:
            out[0] = result.isNull();
            break;
        case rust::WorkflowOp::CommonFrameSizeMatches:
            out[0] = result.size() == QSize(f.width, f.height);
            break;
        case rust::WorkflowOp::CommonFrameConvertRgb32:
            result = result.convertToFormat(QImage::Format_RGB32);
            break;
        case rust::WorkflowOp::CommonFrameCreateRgb32:
            result = QImage(f.width, f.height, QImage::Format_RGB32);
            break;
        case rust::WorkflowOp::CommonFramePixels:
            if (result.isNull())
                throw std::bad_alloc();
            if (!rust::okww_rs_frame_rgb32(reinterpret_cast<const uint8_t *>(f.bytes.constData()),
                                           size_t(f.bytes.size()), f.width, f.height, f.stride, int(f.format),
                                           f.bottomUp, result.bits(), size_t(result.sizeInBytes()),
                                           size_t(result.bytesPerLine())))
                fail("Rust 预览像素转换接口异常");
            break;
        case rust::WorkflowOp::CommonPreviewTargetEmpty:
            out[0] = target.isEmpty();
            break;
        case rust::WorkflowOp::CommonPreviewTargetSize:
            out[0] = target.width();
            out[1] = target.height();
            break;
        case rust::WorkflowOp::CommonPreviewSourceSize:
            size = {f.width, f.height};
            break;
        case rust::WorkflowOp::CommonPreviewScaleSize:
            size.scale(target, Qt::KeepAspectRatio);
            break;
        case rust::WorkflowOp::CommonPreviewScaledFrame:
            result = frameImage(f).scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            break;
        case rust::WorkflowOp::CommonPreviewFastCompiled:
#ifdef OKWW_FAST_PREVIEW
            out[0] = 1;
#else
            out[0] = 0;
#endif
            break;
#ifdef OKWW_FAST_PREVIEW
        case rust::WorkflowOp::CommonPreviewLengthValid:
            out[0] = int64_t(f.stride) * f.height * int(a[0]) / 2 <= f.bytes.size();
            break;
        case rust::WorkflowOp::CommonPreviewNv12: {
            auto *bytes = const_cast<char *>(f.bytes.constData());
            cv::cvtColorTwoPlane(
                cv::Mat(f.height, f.width, CV_8U, bytes, f.stride),
                cv::Mat(f.height / 2, f.width / 2, CV_8UC2, bytes + f.stride * f.height, f.stride), bgr,
                cv::COLOR_YUV2BGR_NV12);
            break;
        }
        case rust::WorkflowOp::CommonPreviewYuy2:
            cv::cvtColor(
                cv::Mat(f.height, f.width, CV_8UC2, const_cast<char *>(f.bytes.constData()), f.stride), bgr,
                cv::COLOR_YUV2BGR_YUY2);
            break;
        case rust::WorkflowOp::CommonPreviewUyvy:
            cv::cvtColor(
                cv::Mat(f.height, f.width, CV_8UC2, const_cast<char *>(f.bytes.constData()), f.stride), bgr,
                cv::COLOR_YUV2BGR_UYVY);
            break;
        case rust::WorkflowOp::CommonPreviewBgra:
            cv::cvtColor(
                cv::Mat(f.height, f.width, CV_8UC4, const_cast<char *>(f.bytes.constData()), f.stride), bgr,
                cv::COLOR_BGRA2BGR);
            break;
        case rust::WorkflowOp::CommonPreviewBgr:
            bgr = cv::Mat(f.height, f.width, CV_8UC3, const_cast<char *>(f.bytes.constData()), f.stride);
            break;
        case rust::WorkflowOp::CommonPreviewBottomUp:
            out[0] = f.bottomUp;
            break;
        case rust::WorkflowOp::CommonPreviewFlip: {
            cv::Mat flipped;
            cv::flip(bgr, flipped, 0);
            bgr = std::move(flipped);
            break;
        }
        case rust::WorkflowOp::CommonPreviewConvertedSizeMatches:
            out[0] = size == QSize(bgr.cols, bgr.rows);
            break;
        case rust::WorkflowOp::CommonPreviewReuseConverted:
            resized = bgr;
            break;
        case rust::WorkflowOp::CommonPreviewResize:
            cv::resize(bgr, resized, {size.width(), size.height()}, 0, 0, cv::INTER_AREA);
            break;
        case rust::WorkflowOp::CommonPreviewOriginalSizeMatches:
            out[0] = size == QSize(f.width, f.height);
            break;
        case rust::WorkflowOp::CommonPreviewCopyBorrowed:
            result =
                QImage(resized.data, resized.cols, resized.rows, int(resized.step), QImage::Format_BGR888)
                    .copy();
            break;
        case rust::WorkflowOp::CommonPreviewOwnConverted: {
            auto *owner = new cv::Mat(std::move(resized));
            result = QImage(
                owner->data, owner->cols, owner->rows, int(owner->step), QImage::Format_BGR888,
                [](void *pixels) { delete static_cast<cv::Mat *>(pixels); }, owner);
            break;
        }
#endif
        case rust::WorkflowOp::CommonPreviewFailure: {
            static const char *const errors[]{"MJPEG 解码失败", "MJPEG 图像尺寸与采集设置不符",
                                              "预览图像长度或尺寸无效"};
            fail(QString::fromUtf8(errors[int(a[0])]));
        }
        default:
            fail("Rust 图像预览宿主无效");
        }
    }
};
} // namespace
QImage frameImage(const Frame &f) {
    CommonPreviewSdk sdk{f};
    rust::platformWorkflowRun(rust::WorkflowMethod::CommonFrameImage, {}, sdk);
    return sdk.result;
}
QImage previewImage(const Frame &f, QSize target) {
    CommonPreviewSdk sdk{f, target};
    rust::platformWorkflowRun(rust::WorkflowMethod::CommonPreviewImage, {}, sdk);
    return sdk.result;
}
} // namespace okww
