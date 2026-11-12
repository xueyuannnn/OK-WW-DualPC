#include "common.hpp"
#include <QHostAddress>
#ifdef OKWW_FAST_PREVIEW
#include <opencv2/imgproc.hpp>
#endif
namespace okww {
QJsonObject readJson(const QString &p) {
    QFile file(p);
    if (!file.open(QIODevice::ReadOnly))
        fail("无法读取配置：" + p);
    auto bytes = file.readAll();
    if (bytes.startsWith("\xef\xbb\xbf"))
        bytes.remove(0, 3);
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        fail("配置格式错误：" + p);
    return doc.object();
}
void writeJson(const QString &p, const QJsonObject &obj) {
    QDir().mkpath(QFileInfo(p).absolutePath());
    QSaveFile file(p);
    const auto data = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        fail("保存配置失败：" + p);
}
Config Config::defaults() {
    return {{{"capture", QJsonObject{{"device_index", 0},
                                     {"device_name", ""},
                                     {"device_path", ""},
                                     {"backend", "DSHOW"},
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
    const auto obj = readJson(p);
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (it.value().isObject()) {
            auto sub = c.json[it.key()].toObject();
            const auto incoming = it.value().toObject();
            for (auto j = incoming.begin(); j != incoming.end(); ++j)
                sub[j.key()] = j.value();
            c.json[it.key()] = sub;
        } else
            c.json[it.key()] = it.value();
    }
    c.normalizeBox();
    return c;
}
void Config::normalizeBox() {
    auto box = json["kmbox"].toObject();
    // Restore the retained network settings when opening a retired serial profile.
    if (box["protocol"].toString() == "makcu_serial")
        box["protocol"] = "kmbox_net";
    for (const auto &key : {"hid_vid", "hid_pid", "hid_path", "serial_port", "baudrate"})
        box.remove(key);
    json["kmbox"] = box;
}
void Config::save(const QString &p) const {
    validateCapture();
    validateBox();
    writeJson(p, json);
}
QJsonValue Config::get(const QString &path) const {
    QJsonValue v(json);
    for (const auto &part : path.split('.'))
        v = v.toObject().value(part);
    return v;
}
void Config::set(const QString &path, const QJsonValue &v) {
    auto keys = path.split('.');
    if (keys.size() == 1) {
        json[path] = v;
        return;
    }
    auto sub = json[keys[0]].toObject();
    sub[keys[1]] = v;
    json[keys[0]] = sub;
}
void Config::validateCapture() const {
    if (s("capture.fourcc") == "P010")
        fail("P010 尚未完成驱动稳定性验证，请选择 NV12 或其他 8 位格式");
    if (!QStringList{"DSHOW", "MSMF"}.contains(s("capture.backend")))
        fail("C++ 采集支持 DirectShow / Media Foundation，请重新选择后端");
    if (n("capture.width") < 2 || n("capture.height") < 2 || n("capture.width") > 8192 ||
        n("capture.height") > 8192 || !std::isfinite(d("capture.fps")) || d("capture.fps") <= 0 ||
        d("capture.fps") > 1000)
        fail("采集分辨率或帧率无效");
    if (!QStringList{"NV12", "P010", "YUY2", "UYVY", "I420", "IYUV", "RGB3", "RGB4", "MJPG"}.contains(
            s("capture.fourcc")))
        fail("请选择受支持的图像格式");
    if (n("capture.output_width") < 1280 || n("capture.output_height") < 720 ||
        n("capture.output_width") > 8192 ||
        std::abs(d("capture.output_width") / d("capture.output_height") - 16.0 / 9) > .01)
        fail("识别图像需要至少 1280×720 的 16:9 分辨率");
    if (d("capture.stale_after") <= 0 || d("capture.stale_after") > 5)
        fail("图像过期时间无效");
}
void Config::validateBox() const {
    if (s("kmbox.protocol") != "kmbox_net")
        fail("当前版本仅支持 KMBox Net，请检查盒子配置");
    if (d("kmbox.timeout") <= 0 || d("kmbox.timeout") > 5 || d("kmbox.counts_per_pixel_x") <= 0 ||
        d("kmbox.counts_per_pixel_y") <= 0 || n("kmbox.desktop_width") <= 0 || n("kmbox.desktop_height") <= 0)
        fail("盒子超时、倍率或桌面尺寸无效");
    if (!std::isfinite(d("kmbox.position_settle")) || !std::isfinite(d("kmbox.ui_click_hold")) ||
        d("kmbox.position_settle") < .11 || d("kmbox.position_settle") > 1 ||
        d("kmbox.ui_click_hold") < .1 || d("kmbox.ui_click_hold") > .5)
        fail("定位后等待需要为 0.11 至 1 秒，界面点击按住需要为 0.1 至 0.5 秒");
    if (b("kmbox.dry_run"))
        return;
    if (s("kmbox.protocol") == "kmbox_net") {
        QHostAddress addr(s("kmbox.host"));
        if (addr.protocol() != QAbstractSocket::IPv4Protocol || n("kmbox.port") < 1 ||
            n("kmbox.port") > 65535 ||
            !QRegularExpression("^[0-9a-fA-F]{8}$").match(s("kmbox.uuid")).hasMatch())
            fail("请填写盒子的 IPv4 地址、端口和 8 位 UUID");
    }
}
QString formatName(PixelFormat f) {
    return QStringList{"RGB3", "RGB4", "NV12", "P010", "YUY2", "UYVY", "I420", "MJPG"}.at(int(f));
}
} // namespace okww
namespace okww {
static void pixel(const Frame &f, int x, int y, uint8_t *bgr) {
    auto *p = reinterpret_cast<const uint8_t *>(f.bytes.constData());
    int row = f.bottomUp ? f.height - 1 - y : y;
    if (f.format == PixelFormat::BGR24 || f.format == PixelFormat::BGRA32) {
        p += row * f.stride + x * (f.format == PixelFormat::BGR24 ? 3 : 4);
        memcpy(bgr, p, 3);
        return;
    }
    if (f.format == PixelFormat::P010) {
        auto row = reinterpret_cast<const uint16_t *>(p + y * f.stride);
        auto uv = reinterpret_cast<const uint16_t *>(p + f.stride * f.height + (y / 2) * f.stride);
        int yy = std::max(0, int(row[x] >> 6) - 64) * 1220542;
        int u = int(uv[x & ~1] >> 6) - 512, v = int(uv[(x & ~1) + 1] >> 6) - 512;
        auto clip = [](int64_t value) {
            return uint8_t(std::clamp<int64_t>((value + (1 << 21)) >> 22, 0, 255));
        };
        bgr[0] = clip(int64_t(yy) + int64_t(2116026) * u);
        bgr[1] = clip(int64_t(yy) - int64_t(409993) * u - int64_t(852492) * v);
        bgr[2] = clip(int64_t(yy) + int64_t(1673527) * v);
        return;
    }
    int Y = 0, U = 128, V = 128;
    if (f.format == PixelFormat::NV12) {
        Y = p[y * f.stride + x];
        auto uv = p + f.stride * f.height + (y / 2) * f.stride + (x & ~1);
        U = uv[0];
        V = uv[1];
    } else if (f.format == PixelFormat::I420) {
        Y = p[y * f.stride + x];
        auto uv = p + f.stride * f.height;
        U = uv[(y / 2) * (f.stride / 2) + x / 2];
        V = uv[f.stride * f.height / 4 + (y / 2) * (f.stride / 2) + x / 2];
    } else {
        p += y * f.stride + (x & ~1) * 2;
        bool u = f.format == PixelFormat::UYVY;
        Y = p[(x & 1) * 2 + (u ? 1 : 0)];
        U = p[u ? 0 : 1];
        V = p[u ? 2 : 3];
    }
    int yy = std::max(0, Y - 16) * 1220542;
    U -= 128;
    V -= 128;
    auto clip = [](int v) { return uint8_t(std::clamp((v + (1 << 19)) >> 20, 0, 255)); };
    bgr[0] = clip(yy + 2116026 * U);
    bgr[1] = clip(yy - 409993 * U - 852492 * V);
    bgr[2] = clip(yy + 1673527 * V);
}
QImage frameImage(const Frame &f) {
    if (f.format == PixelFormat::MJPG) {
        auto im = QImage::fromData(f.bytes, "JPG");
        if (im.isNull())
            fail("MJPEG 解码失败");
        if (im.size() != QSize(f.width, f.height))
            fail("MJPEG 图像尺寸与采集设置不符");
        return im.convertToFormat(QImage::Format_RGB32);
    }
    QImage im(f.width, f.height, QImage::Format_RGB32);
    for (int y = 0; y < f.height; ++y) {
        auto p = im.scanLine(y);
        for (int x = 0; x < f.width; ++x) {
            pixel(f, x, y, p + 4 * x);
            p[4 * x + 3] = 255;
        }
    }
    return im;
}
QImage previewImage(const Frame &f, QSize target) {
    if (f.width <= 0 || f.height <= 0 || target.isEmpty())
        return {};
    QSize size(f.width, f.height);
    if (size.width() > target.width() || size.height() > target.height())
        size.scale(target, Qt::KeepAspectRatio);
    if (f.format == PixelFormat::MJPG)
        return frameImage(f).scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
#ifdef OKWW_FAST_PREVIEW
    if (f.width > 0 && f.height > 0 && f.stride > 0 &&
        (f.format == PixelFormat::NV12 || f.format == PixelFormat::YUY2 ||
         f.format == PixelFormat::UYVY || f.format == PixelFormat::BGR24 || f.format == PixelFormat::BGRA32)) {
        const bool planar = f.format == PixelFormat::NV12;
        const int channels = f.format == PixelFormat::BGRA32 ? 4 : f.format == PixelFormat::BGR24 ? 3 : planar ? 1 : 2;
        if (int64_t(f.stride) * f.height * (planar ? 3 : 2) / 2 > f.bytes.size() || f.stride < f.width * channels ||
            ((planar || channels == 2) && f.width % 2) || (planar && f.height % 2))
            fail("预览图像长度或尺寸无效");
        auto bytes = const_cast<char *>(f.bytes.constData());
        cv::Mat bgr;
        if (planar)
            cv::cvtColorTwoPlane(cv::Mat(f.height, f.width, CV_8U, bytes, f.stride),
                                cv::Mat(f.height / 2, f.width / 2, CV_8UC2, bytes + f.stride * f.height, f.stride),
                                bgr, cv::COLOR_YUV2BGR_NV12);
        else if (channels == 2)
            cv::cvtColor(cv::Mat(f.height, f.width, CV_8UC2, bytes, f.stride), bgr,
                         f.format == PixelFormat::YUY2 ? cv::COLOR_YUV2BGR_YUY2 : cv::COLOR_YUV2BGR_UYVY);
        else if (channels == 4)
            cv::cvtColor(cv::Mat(f.height, f.width, CV_8UC4, bytes, f.stride), bgr, cv::COLOR_BGRA2BGR);
        else
            bgr = cv::Mat(f.height, f.width, CV_8UC3, bytes, f.stride);
        if (f.bottomUp && channels >= 3) {
            cv::Mat flipped;
            cv::flip(bgr, flipped, 0);
            bgr = std::move(flipped);
        }
        cv::Mat resized;
        if (size == QSize(bgr.cols, bgr.rows))
            resized = bgr;
        else
            // Integrate source pixels so thin text and lines survive reduction.
            cv::resize(bgr, resized, {size.width(), size.height()}, 0, 0, cv::INTER_AREA);
        // Converted/resized matrices own their pixels. Keep that allocation
        // alive with the QImage instead of copying the entire RGB image again.
        // Native BGR at full size still borrows the reusable capture buffer.
        if (channels == 3 && !f.bottomUp && size == QSize(f.width, f.height))
            return QImage(resized.data, resized.cols, resized.rows, int(resized.step),
                          QImage::Format_BGR888).copy();
        auto owner = new cv::Mat(std::move(resized));
        return QImage(owner->data, owner->cols, owner->rows, int(owner->step), QImage::Format_BGR888,
                      [](void *pixels) { delete static_cast<cv::Mat *>(pixels); }, owner);
    }
#endif
    return frameImage(f).scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

} // namespace okww
