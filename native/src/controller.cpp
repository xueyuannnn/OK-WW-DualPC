#include "controller.hpp"
#include "makcu_protocol.hpp"
#include <ws2tcpip.h>
namespace okww {
static constexpr uint32_t CONNECT = 0xAF3C2828, MOVE = 0xAEDE7345, LEFT = 0x9823AE8D, RIGHT = 0x238D8212,
                          MIDDLE = 0x97A3AE8D, WHEEL = 0xFFEEAD38, KEYBOARD = 0x123C2C2F;
static void u32(QByteArray &p, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        p.append(char(v >> (i * 8)));
}
static uint32_t get32(const QByteArray &p, int n) { return qFromLittleEndian<uint32_t>(p.constData() + n); }
void Controller::noteLatency(i64 began) {
    latencyMs_.push_back((nowNs() - began) / 1e6);
    if (latencyMs_.size() > 128)
        latencyMs_.pop_front();
    std::vector<double> sorted(latencyMs_.begin(), latencyMs_.end());
    std::sort(sorted.begin(), sorted.end());
    latencyP50_ = sorted[size_t(std::ceil(.5 * sorted.size())) - 1];
    latencyP95_ = sorted[size_t(std::ceil(.95 * sorted.size())) - 1];
}
void Controller::write(const QByteArray &p) {
    if (cfg_.b("kmbox.dry_run")) {
        history_.append(p);
        while (history_.size() > 256)
            history_.removeFirst();
        return;
    }
    DWORD written = 0;
    if (device_ == INVALID_HANDLE_VALUE ||
        !WriteFile(device_, p.constData(), DWORD(p.size()), &written, nullptr) || written != p.size()) {
        connected_ = false;
        fail("盒子写入失败");
    }
}
void Controller::exchange(uint32_t command, const QByteArray &payload) {
    const auto began = nowNs();
    const auto seq = index_++;
    QByteArray p;
    u32(p, uuid_);
    u32(p, QRandomGenerator::global()->generate());
    u32(p, seq);
    u32(p, command);
    p += payload;
    if (cfg_.b("kmbox.dry_run")) {
        write(p);
        return;
    }
    if (socket_ == INVALID_SOCKET || send(socket_, p.constData(), int(p.size()), 0) != p.size()) {
        connected_ = false;
        fail("KMBox 发送失败");
    }
    const i64 deadline = nowNs() + i64(cfg_.d("kmbox.timeout") * 1e9);
    while (nowNs() < deadline) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(socket_, &fds);
        auto remaining = std::max<i64>(1, deadline - nowNs());
        timeval wait{long(remaining / 1000000000), long((remaining % 1000000000) / 1000)};
        if (select(0, &fds, nullptr, nullptr, &wait) <= 0)
            break;
        char buffer[2048];
        int count = recv(socket_, buffer, sizeof(buffer), 0);
        if (count < 16)
            continue;
        QByteArray reply(buffer, count);
        if (get32(reply, 8) == seq && get32(reply, 12) == command) {
            noteLatency(began);
            return;
        }
    }
    connected_ = false;
    fail("KMBox 未返回匹配应答；没有重复发送移动指令");
}
QString Controller::makcu(const QStringList &commands) {
    const auto began = nowNs();
    QStringList lines = commands;
    lines << "km.version()" << "";
    write(lines.join("\r\n").toLatin1());
    if (cfg_.b("kmbox.dry_run"))
        return "R:MK;M:8uf;K:8uf;C:0uf\r\nkm.MAKCU\r\n>>>";
    QByteArray data;
    const i64 deadline = nowNs() + i64(cfg_.d("kmbox.timeout") * 1e9);
    while (nowNs() < deadline && data.size() < 32768) {
        char b[1024];
        DWORD n = 0;
        if (!ReadFile(device_, b, sizeof(b), &n, nullptr))
            break;
        data.append(b, n);
        const auto text = QString::fromLatin1(data);
        const auto identity = QRegularExpression("(?:^|[\\r\\n]|>>>)\\s*km\\.MAKCU[\\r\\n]+").match(text);
        if (identity.hasMatch() && text.indexOf(">>> ", identity.capturedEnd()) >= 0) {
            if (QRegularExpression("(?:^|[\\r\\n>\\s])ERR\\b").match(text).hasMatch())
                fail("MAKCU 固件拒绝命令");
            if (legacyMakcu_)
                try {
                    checkMakcuLegacyAcknowledgements(text, commands);
                } catch (...) {
                    connected_ = false;
                    throw;
                }
            noteLatency(began);
            return text;
        }
    }
    connected_ = false;
    fail(QString("MAKCU 身份应答超时（%1 / %2）。请选择 CH343 串口并核对固件波特率；被其他软件占用时请先释放。")
             .arg(cfg_.s("kmbox.serial_port")).arg(cfg_.n("kmbox.baudrate")));
}
QJsonArray Controller::serialDevices() {
    QJsonArray out;
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &key) !=
        ERROR_SUCCESS)
        return out;
    for (DWORD i = 0;; ++i) {
        wchar_t name[512], data[512];
        DWORD n = 512, bytes = sizeof(data), type = 0;
        auto r = RegEnumValueW(key, i, name, &n, nullptr, &type, reinterpret_cast<BYTE *>(data), &bytes);
        if (r == ERROR_NO_MORE_ITEMS)
            break;
        if (r == ERROR_SUCCESS && type == REG_SZ)
            out.append(QString::fromWCharArray(data));
    }
    RegCloseKey(key);
    return out;
}
void Controller::connect() {
    std::lock_guard lock(mutex_);
    if (connected_)
        return;
    close();
    cfg_.validateBox();
    try {
        const auto protocol = cfg_.s("kmbox.protocol");
        legacyMakcu_ = false;
        keyboard_ = protocol != "makcu_serial";
        index_ = 0;
        latencyMs_.clear();
        uuid_ = cfg_.s("kmbox.uuid").toUInt(nullptr, 16);
        if (!cfg_.b("kmbox.dry_run")) {
            if (protocol == "kmbox_net") {
                socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                sockaddr_in dest{};
                dest.sin_family = AF_INET;
                dest.sin_port = htons(u_short(cfg_.n("kmbox.port")));
                InetPtonW(AF_INET, reinterpret_cast<LPCWSTR>(cfg_.s("kmbox.host").utf16()), &dest.sin_addr);
                if (socket_ == INVALID_SOCKET ||
                    ::connect(socket_, reinterpret_cast<sockaddr *>(&dest), sizeof(dest)) != 0)
                    fail("KMBox 网络连接失败");
            } else {
                const auto port = "\\\\.\\" + cfg_.s("kmbox.serial_port");
                device_ = CreateFileW(reinterpret_cast<LPCWSTR>(port.utf16()), GENERIC_READ | GENERIC_WRITE,
                                      0, nullptr, OPEN_EXISTING, 0, nullptr);
                if (device_ == INVALID_HANDLE_VALUE)
                    fail("串口打开失败或被占用");
                DCB dcb{};
                dcb.DCBlength = sizeof(dcb);
                GetCommState(device_, &dcb);
                dcb.BaudRate = cfg_.n("kmbox.baudrate");
                dcb.ByteSize = 8;
                dcb.Parity = NOPARITY;
                dcb.StopBits = ONESTOPBIT;
                dcb.fBinary = TRUE;
                dcb.fParity = FALSE;
                dcb.fOutxCtsFlow = dcb.fOutxDsrFlow = dcb.fOutX = dcb.fInX = FALSE;
                dcb.fDtrControl = DTR_CONTROL_DISABLE;
                dcb.fRtsControl = RTS_CONTROL_DISABLE;
                if (!SetCommState(device_, &dcb))
                    fail("串口参数设置失败");
                COMMTIMEOUTS t{MAXDWORD, 0, 25, 0, DWORD(cfg_.d("kmbox.timeout") * 1000)};
                SetCommTimeouts(device_, &t);
                PurgeComm(device_, PURGE_RXCLEAR | PURGE_TXCLEAR);
            }
        }
        if (protocol == "kmbox_net")
            exchange(CONNECT);
        if (protocol == "makcu_serial") {
            makcu();
            auto response = makcu({"km.device()"});
            const auto keyboard = makcuV4Keyboard(response);
            if (keyboard.has_value()) {
                keyboard_ = *keyboard;
            } else {
                const auto mouse = makcu({"km.left()"}); // Read-only discovery for 3.2 / 3.7.
                if (!makcuLegacyMouseReply(mouse))
                    fail("已识别 MAKCU，但未确认 V4 路由或 3.x 鼠标协议。请检查固件、USB1 游戏机与 USB3 鼠标连接。");
                legacyMakcu_ = true;
                keyboard_ = false;
            }
        }
        capability_ = legacyMakcu_ ? "MAKCU 3.x 鼠标兼容 · 不支持键盘，不能运行自动任务"
                                  : keyboard_ ? "键盘与鼠标可用" : "仅鼠标路由；键盘不可用，暂不能运行自动任务";
        connected_ = true;
        release();
    } catch (...) {
        close();
        throw;
    }
}
void Controller::mouse(uint32_t command, int x, int y, int wheel) {
    QByteArray payload;
    for (int value : {buttons_, x, y, wheel})
        u32(payload, uint32_t(value));
    payload.append(40, '\0');
    exchange(command, payload);
}
void Controller::keyboardReport() {
    QByteArray payload;
    payload.append(char(modifiers_)).append(char(0));
    for (auto code : keys_)
        payload.append(char(code));
    payload.append(12 - payload.size(), '\0');
    exchange(KEYBOARD, payload);
}
void Controller::move(int x, int y) {
    std::lock_guard lock(mutex_);
    if (std::abs(int64_t(x)) > 32767 || std::abs(int64_t(y)) > 32767)
        fail("移动超出 HID 范围");
    auto p = cfg_.s("kmbox.protocol");
    if (p == "makcu_serial") {
        makcu({QString("km.move(%1,%2)").arg(x).arg(y)});
        return;
    }
    mouse(MOVE, x, y);
}
void Controller::absolute(int, int, int, int) {
    fail("MAKCU 和 KMBox 使用相对定位");
}
void Controller::button(const QString &name, bool down) {
    std::lock_guard lock(mutex_);
    int i = QStringList{"left", "right", "middle", "x1", "x2"}.indexOf(name);
    if (i < 0)
        fail("未知鼠标按键");
    buttons_ = down ? buttons_ | (1 << i) : buttons_ & ~(1 << i);
    if (cfg_.s("kmbox.protocol") == "makcu_serial")
        makcu({QString("km.%1(%2)")
                   .arg((legacyMakcu_ ? QStringList{"left", "right", "middle", "ms1", "ms2"}
                                      : QStringList{"left", "right", "middle", "side1", "side2"})[i])
                   .arg(down ? 1 : 0)});
    else
        mouse(i == 0 ? LEFT : i == 2 ? MIDDLE : RIGHT);
}
void Controller::key(int code, bool down) {
    std::lock_guard lock(mutex_);
    if (!keyboard_)
        fail("盒子没有可用键盘路由");
    if (code < 1 || code > 255)
        fail("无效 HID 键码");
    bool mod = code >= 0xe0 && code <= 0xe7;
    if (mod)
        modifiers_ = down ? modifiers_ | (1 << (code - 0xe0)) : modifiers_ & ~(1 << (code - 0xe0));
    else if (down) {
        int max = cfg_.s("kmbox.protocol") == "kmbox_net" ? 10 : 6;
        if (keys_.size() >= size_t(max) && !keys_.contains(code))
            fail("同时按下的键超过盒子容量");
        keys_.insert(code);
    } else
        keys_.erase(code);
    if (cfg_.s("kmbox.protocol") == "makcu_serial")
        makcu({QString("km.%1(%2)").arg(down ? "down" : "up").arg(code)});
    else
        keyboardReport();
}
void Controller::wheel(int steps) {
    std::lock_guard lock(mutex_);
    if (std::abs(int64_t(steps)) > 10000)
        fail("滚轮步数过大");
    while (steps) {
        int n = std::clamp(steps, -127, 127);
        if (cfg_.s("kmbox.protocol") == "makcu_serial") {
            n = steps > 0 ? 1 : -1;
            makcu({QString("km.wheel(%1)").arg(n)});
        } else
            mouse(WHEEL, 0, 0, n);
        steps -= n;
    }
}
void Controller::probe() {
    std::lock_guard lock(mutex_);
    if (!connected_)
        fail("盒子未连接");
    if (cfg_.s("kmbox.protocol") == "makcu_serial")
        makcu();
    else
        move(0, 0);
}
void Controller::release() {
    std::lock_guard lock(mutex_);
    if (cfg_.s("kmbox.protocol") == "makcu_serial") {
        QStringList cmds;
        for (const auto &b : (legacyMakcu_ ? QStringList{"left", "right", "middle", "ms1", "ms2"}
                                         : QStringList{"left", "right", "middle", "side1", "side2"}))
            cmds << "km." + b + "(0)";
        for (int key : keys_)
            cmds << QString("km.up(%1)").arg(key);
        for (int i = 0; i < 8; ++i)
            if (modifiers_ & (1 << i))
                cmds << QString("km.up(%1)").arg(0xe0 + i);
        makcu(cmds);
        buttons_ = modifiers_ = 0;
        keys_.clear();
    } else {
        buttons_ = modifiers_ = 0;
        keys_.clear();
        std::exception_ptr error;
        try {
            mouse(MOVE);
        } catch (...) {
            error = std::current_exception();
        }
        try {
            keyboardReport();
        } catch (...) {
            if (!error)
                error = std::current_exception();
        }
        if (error)
            std::rethrow_exception(error);
    }
}
void Controller::close() noexcept {
    std::lock_guard lock(mutex_);
    if (connected_ || socket_ != INVALID_SOCKET || device_ != INVALID_HANDLE_VALUE) {
        try {
            release();
        } catch (...) {
        }
    }
    if (socket_ != INVALID_SOCKET)
        closesocket(socket_);
    if (device_ != INVALID_HANDLE_VALUE)
        CloseHandle(device_);
    socket_ = INVALID_SOCKET;
    device_ = INVALID_HANDLE_VALUE;
    connected_ = false;
    buttons_ = modifiers_ = 0;
    keys_.clear();
}
bool Controller::connected() {
    std::lock_guard lock(mutex_);
    return connected_;
}
QJsonObject Controller::state() {
    std::lock_guard lock(mutex_);
    QJsonArray keys;
    for (auto k : keys_)
        keys.append(k);
    return {{"connected", connected_},
            {"firmware_mode", cfg_.s("kmbox.protocol") != "makcu_serial" ? "native" : legacyMakcu_ ? "legacy3" : "v4"},
            {"keyboard", keyboard_},
            {"absolute", false},
            {"capability", capability_},
            {"buttons", buttons_},
            {"modifiers", modifiers_},
            {"keys", keys},
            {"dry_run", cfg_.b("kmbox.dry_run")},
            {"transport_samples", int(latencyMs_.size())},
            {"transport_p50_ms", latencyMs_.empty() ? QJsonValue() : QJsonValue(latencyP50_)},
            {"transport_p95_ms", latencyMs_.empty() ? QJsonValue() : QJsonValue(latencyP95_)},
            {"transport_scope", "host send to matching protocol reply; excludes game/USB consumption"}};
}
} // namespace okww
