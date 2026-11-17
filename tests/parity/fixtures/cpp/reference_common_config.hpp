#pragma once
#include "common.hpp"
#include <QHostAddress>
namespace okww::common_reference {
QJsonObject readJson(const QString &);
void writeJson(const QString &, const QJsonObject &);
struct Config {
    QJsonObject json;
    static Config defaults();
    static Config load(const QString &path);
    void save(const QString &path) const;
    QJsonValue get(const QString &path) const;
    QString s(const QString &p) const { return get(p).toString(); }
    double d(const QString &p) const { return get(p).toDouble(); }
    int n(const QString &p) const { return int(d(p)); }
    bool b(const QString &p) const { return get(p).toBool(); }
    void set(const QString &path, const QJsonValue &value);
    void normalizeBox();
    void validateCapture() const;
    void validateBox() const;
};
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
    auto capture = json["capture"].toObject();
    capture.remove("auto_game_region");
    if (capture["source"].toString() != "desktop")
        capture["fourcc"] = "NV12";
    json["capture"] = capture;
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
    const bool local = s("capture.source") == "desktop";
    if (!QStringList{"card", "desktop"}.contains(s("capture.source")))
        fail("请选择采集方式");
    if (local &&
        (!QStringList{"DXGI", "WIN32"}.contains(s("capture.local_backend")) ||
         !std::isfinite(d("capture.local_fps")) || d("capture.local_fps") != n("capture.local_fps") ||
         n("capture.local_fps") < 1 || n("capture.local_fps") > 1000))
        fail("请选择本机截图方式，采集帧率需为 1–1000 的整数");
    const auto localTarget = get("capture.local_target").toObject();
    if (local && s("capture.local_backend") == "WIN32" && !localTarget.isEmpty() &&
        !QStringList{"monitor", "window"}.contains(localTarget["kind"].toString()))
        fail("请选择有效的本机截图目标");
    if (!local && !QStringList{"DSHOW", "MSMF"}.contains(s("capture.backend")))
        fail("C++ 采集支持 DirectShow / Media Foundation，请重新选择后端");
    if (!local && (n("capture.width") < 2 || n("capture.height") < 2 || n("capture.width") > 8192 ||
                   n("capture.height") > 8192 || !std::isfinite(d("capture.fps")) || d("capture.fps") <= 0 ||
                   d("capture.fps") > 1000))
        fail("采集分辨率或帧率无效");
    if (!local && s("capture.fourcc") != "NV12")
        fail("采集卡图像格式仅支持 NV12，请重新检测设备与参数");
    if (n("capture.output_width") < 1280 || n("capture.output_height") < 720 ||
        n("capture.output_width") > 8192 ||
        std::abs(d("capture.output_width") / d("capture.output_height") - 16.0 / 9) > .01)
        fail("识别图像需要至少 1280×720 的 16:9 分辨率");
    if (d("capture.stale_after") <= 0 || d("capture.stale_after") > 5)
        fail("图像过期时间无效");
}
void Config::validateBox() const {
    if (!QStringList{"kmbox_net", "sendinput"}.contains(s("kmbox.protocol")))
        fail("请选择 KMBox Net 或 SendInput");
    if (s("kmbox.protocol") == "sendinput" && b("kmbox.target_enabled") &&
        get("kmbox.target").toObject()["kind"].toString() != "window")
        fail("请在高级设置中选择 SendInput 目标窗口，或切回当前前台窗口");
    if (n("kmbox.desktop_width") <= 0 || n("kmbox.desktop_height") <= 0 ||
        (s("kmbox.protocol") != "sendinput" &&
         (d("kmbox.timeout") <= 0 || d("kmbox.timeout") > 5 || d("kmbox.counts_per_pixel_x") <= 0 ||
          d("kmbox.counts_per_pixel_y") <= 0)))
        fail("盒子超时、倍率或桌面尺寸无效");
    if (!std::isfinite(d("kmbox.position_settle")) || !std::isfinite(d("kmbox.ui_click_hold")) ||
        d("kmbox.position_settle") < .11 || d("kmbox.position_settle") > 1 || d("kmbox.ui_click_hold") < .1 ||
        d("kmbox.ui_click_hold") > .5)
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

} // namespace okww::common_reference
