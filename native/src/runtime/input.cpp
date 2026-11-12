#include "input.hpp"
namespace okww::runtime {
QJsonObject Input::workshopCommand(QJsonObject command) {
    const auto op = command["op"].toString();
    if (!QStringList{"state", "connect", "heartbeat", "probe", "key", "button", "move", "absolute", "wheel",
                     "release"}
             .contains(op))
        fail("工坊输入请求无效");
    if (op == "release") {
        release();
        return {};
    }
    execution_.checkpoint();
    if (op == "button" && command["down"].toBool() && buttons_.isEmpty())
        settlePosition();
    const auto result = transport_(command);
    if (op == "move" || op == "absolute") {
        position_.reset();
        positionReadyAt_ = -1;
    }
    if (op == "key") {
        const auto code = command["code"].toInt();
        if (command["down"].toBool())
            keys_.insert(code);
        else
            keys_.remove(code);
    } else if (op == "button") {
        const auto name = command["name"].toString();
        if (command["down"].toBool())
            buttons_.insert(name);
        else
            buttons_.remove(name);
    }
    return result;
}
int Input::keyCode(QString key) {
    key = key.toLower();
    if (key.size() == 1) {
        auto c = key[0].unicode();
        if (c >= 'a' && c <= 'z')
            return 4 + c - 'a';
        if (c >= '1' && c <= '9')
            return 0x1d + c - '0';
        if (c == '0')
            return 0x27;
        static const QMap<QChar, int> map{{'-', 0x2d},  {'=', 0x2e}, {'[', 0x2f},  {']', 0x30},
                                          {'\\', 0x31}, {';', 0x33}, {'\'', 0x34}, {'`', 0x35},
                                          {',', 0x36},  {'.', 0x37}, {'/', 0x38}};
        if (map.contains(key[0]))
            return map[key[0]];
    }
    static const QMap<QString, int> map{
        {"enter", 0x28},     {"return", 0x28},   {"esc", 0x29},      {"escape", 0x29},   {"backspace", 0x2a},
        {"tab", 0x2b},       {"space", 0x2c},    {"capslock", 0x39}, {"insert", 0x49},   {"home", 0x4a},
        {"pageup", 0x4b},    {"page_up", 0x4b},  {"delete", 0x4c},   {"end", 0x4d},      {"pagedown", 0x4e},
        {"page_down", 0x4e}, {"right", 0x4f},    {"left", 0x50},     {"down", 0x51},     {"up", 0x52},
        {"ctrl", 0xe0},      {"lctrl", 0xe0},    {"control", 0xe0},  {"lcontrol", 0xe0}, {"ctrl_l", 0xe0},
        {"rctrl", 0xe4},     {"rcontrol", 0xe4}, {"ctrl_r", 0xe4},   {"shift", 0xe1},    {"lshift", 0xe1},
        {"shift_l", 0xe1},   {"rshift", 0xe5},   {"shift_r", 0xe5},  {"alt", 0xe2},      {"lalt", 0xe2},
        {"alt_l", 0xe2},     {"ralt", 0xe6},     {"alt_r", 0xe6},    {"win", 0xe3},      {"cmd", 0xe3}};
    if (map.contains(key))
        return map[key];
    bool ok = false;
    int n = key.mid(1).toInt(&ok);
    if (key.startsWith('f') && ok && n >= 1 && n <= 12)
        return 0x39 + n;
    fail("不支持的 USB HID 按键：" + key);
}
void Input::send(QJsonObject command) {
    execution_.checkpoint();
    transport_(std::move(command));
}
QPoint Input::screenPoint(QPointF p) const {
    const int ow = config_.n("capture.output_width"), oh = config_.n("capture.output_height");
    if (rawSize_.isEmpty() || p.x() < 0 || p.y() < 0 || p.x() >= ow || p.y() >= oh || !std::isfinite(p.x()) ||
        !std::isfinite(p.y()))
        fail("点击坐标超出识别画面");
    QRectF crop(0, 0, rawSize_.width(), rawSize_.height());
    auto c = config_.get("capture.crop").toArray();
    if (!c.isEmpty()) {
        if (c.size() != 4)
            fail("裁剪参数无效");
        crop = QRectF(c[0].toDouble(), c[1].toDouble(), c[2].toDouble(), c[3].toDouble());
        if (crop.isEmpty() || !QRectF(QPointF(0, 0), rawSize_).contains(crop))
            fail("裁剪区域超出采集画面");
    }
    const auto dw = config_.n("kmbox.desktop_width"), dh = config_.n("kmbox.desktop_height");
    if (dw <= 0 || dh <= 0)
        fail("游戏机桌面分辨率无效");
    return {std::min(dw - 1, pixelRound((crop.x() + p.x() * crop.width() / ow) * dw / rawSize_.width())),
            std::min(dh - 1, pixelRound((crop.y() + p.y() * crop.height() / oh) * dh / rawSize_.height()))};
}
void Input::keyDown(QString key) {
    const int code = keyCode(key);
    send({{"op", "key"}, {"code", code}, {"down", true}});
    keys_.insert(code);
}
void Input::keyUp(QString key) {
    const int code = keyCode(key);
    try {
        transport_({{"op", "key"}, {"code", code}, {"down", false}});
        keys_.remove(code);
    } catch (...) {
        release();
        throw;
    }
}
void Input::key(QString key, double seconds) {
    const bool held = keys_.contains(keyCode(key));
    try {
        keyDown(key);
        execution_.wait(std::max(0., seconds));
        if (!held)
            keyUp(key);
    } catch (...) {
        release();
        throw;
    }
}
void Input::buttonDown(QString button) {
    if (!QStringList{"left", "right", "middle"}.contains(button))
        fail("无效鼠标按键");
    if (buttons_.isEmpty()) settlePosition();
    send({{"op", "button"}, {"name", button}, {"down", true}});
    buttons_.insert(button);
}
void Input::settlePosition() {
    if (positionReadyAt_ >= 0)
        execution_.wait(std::max(0., positionReadyAt_ - execution_.now()));
}
void Input::buttonUp(QString button) {
    try {
        transport_({{"op", "button"}, {"name", button}, {"down", false}});
        buttons_.remove(button);
    } catch (...) {
        release();
        throw;
    }
}
void Input::counts(int x, int y, bool precise) {
    const int chunk = precise ? std::min(config_.n("kmbox.move_chunk"), 127) : config_.n("kmbox.move_chunk");
    if (chunk <= 0)
        fail("鼠标分段参数无效");
    const double interval =
        precise ? std::max(config_.d("kmbox.move_interval"), .008) : config_.d("kmbox.move_interval");
    const int steps =
        std::max(1, int((std::max(std::abs(int64_t(x)), std::abs(int64_t(y))) + chunk - 1) / chunk));
    if (steps > 100000)
        fail("鼠标移动距离过大");
    QPoint sent;
    for (int i = 1; i <= steps; ++i) {
        QPoint target(pixelRound(double(x) * i / steps), pixelRound(double(y) * i / steps));
        send({{"op", "move"}, {"x", target.x() - sent.x()}, {"y", target.y() - sent.y()}});
        sent = target;
        if (interval && i < steps)
            execution_.wait(interval);
    }
}
void Input::move(QPointF point) {
    execution_.checkpoint();
    QPoint target = screenPoint(point);
    try {
        if (absolute_)
            send({{"op", "absolute"},
                  {"x", target.x()},
                  {"y", target.y()},
                  {"width", config_.n("kmbox.desktop_width")},
                  {"height", config_.n("kmbox.desktop_height")}});
        else {
            QPoint origin;
            if (buttons_.isEmpty()) {
                send({{"op", "move"},
                      {"x", -config_.n("kmbox.home_counts")},
                      {"y", -config_.n("kmbox.home_counts")}});
                execution_.wait(config_.d("kmbox.home_wait"));
            } else {
                if (!position_)
                    fail("拖动前需要已知鼠标起点");
                origin = screenPoint(*position_);
            }
            counts(pixelRound((target.x() - origin.x()) * config_.d("kmbox.counts_per_pixel_x")),
                   pixelRound((target.y() - origin.y()) * config_.d("kmbox.counts_per_pixel_y")),
                   buttons_.isEmpty());
        }
        position_ = point;
        positionReadyAt_ = execution_.now() + config_.d("kmbox.position_settle");
    } catch (...) {
        release();
        throw;
    }
}
void Input::relative(double x, double y) {
    try {
        counts(pixelRound(x * config_.d("kmbox.counts_per_pixel_x")),
               pixelRound(y * config_.d("kmbox.counts_per_pixel_y")), false);
        position_.reset();
        positionReadyAt_ = -1;
    } catch (...) {
        release();
        throw;
    }
}
void Input::click(std::optional<QPointF> point, double seconds, QString button) {
    try {
        if (point) {
            move(*point);
        }
        buttonDown(button);
        execution_.wait(std::max(point ? .04 : .02, seconds));
        buttonUp(button);
    } catch (...) {
        release();
        throw;
    }
}
void Input::scroll(int steps, std::optional<QPointF> point) {
    if (point) {
        move(*point);
    }
    settlePosition();
    send({{"op", "wheel"}, {"steps", steps}});
}
void Input::swipe(QPointF from, QPointF to, double milliseconds, double settle) {
    if (!std::isfinite(milliseconds) || milliseconds < 0 || milliseconds > 60000)
        fail("拖动时间无效");
    try {
        move(from);
        buttonDown();
        int steps = std::max(1, pixelRound(milliseconds / 10));
        for (int i = 1; i <= steps; ++i) {
            move(from + (to - from) * (double(i) / steps));
            execution_.wait(milliseconds / steps / 1000);
        }
        buttonUp();
        execution_.wait(std::max(0., settle));
    } catch (...) {
        release();
        throw;
    }
}
void Input::text(const QString &value) {
    const QString shifted = "!@#$%^&*()_+{}|:\"~<>?", base = "1234567890-=[]\\;'`,./";
    QVector<QPair<QString, bool>> strokes;
    for (QChar c : value) {
        auto pos = shifted.indexOf(c);
        QString k = pos >= 0 ? QString(base[pos]) : QString(c.toLower());
        if (c == ' ')
            k = "space";
        else if (c == '\n')
            k = "enter";
        else if (c == '\t')
            k = "tab";
        keyCode(k);
        strokes.append({k, c.isUpper() || pos >= 0});
    }
    try {
        for (const auto &[k, shift] : strokes) {
            bool held = keys_.contains(0xe1);
            if (shift)
                keyDown("shift");
            key(k);
            if (shift && !held)
                keyUp("shift");
        }
    } catch (...) {
        release();
        throw;
    }
}
void Input::release() noexcept {
    try {
        transport_({{"op", "release"}});
    } catch (...) {
    }
    keys_.clear();
    buttons_.clear();
    position_.reset();
    positionReadyAt_ = -1;
}
} // namespace okww::runtime
