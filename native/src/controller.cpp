#include "controller.hpp"
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
void Controller::recordPacket(const QByteArray &p) {
    history_.append(p);
    while (history_.size() > 256)
        history_.removeFirst();
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
        recordPacket(p);
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

void Controller::connect() {
    std::lock_guard lock(mutex_);
    if (connected_)
        return;
    close();
    cfg_.validateBox();
    try {
        index_ = 0;
        latencyMs_.clear();
        uuid_ = cfg_.s("kmbox.uuid").toUInt(nullptr, 16);
        if (!cfg_.b("kmbox.dry_run")) {
            socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            sockaddr_in dest{};
            dest.sin_family = AF_INET;
            dest.sin_port = htons(u_short(cfg_.n("kmbox.port")));
            InetPtonW(AF_INET, reinterpret_cast<LPCWSTR>(cfg_.s("kmbox.host").utf16()), &dest.sin_addr);
            if (socket_ == INVALID_SOCKET ||
                ::connect(socket_, reinterpret_cast<sockaddr *>(&dest), sizeof(dest)) != 0)
                fail("KMBox 网络连接失败");
        }
        exchange(CONNECT);
        capability_ = "键盘与鼠标可用";
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
    mouse(MOVE, x, y);
}

void Controller::absolute(int, int, int, int) {
    fail("KMBox Net 使用相对定位");
}
void Controller::button(const QString &name, bool down) {
    std::lock_guard lock(mutex_);
    int i = QStringList{"left", "right", "middle", "x1", "x2"}.indexOf(name);
    if (i < 0)
        fail("未知鼠标按键");
    buttons_ = down ? buttons_ | (1 << i) : buttons_ & ~(1 << i);
    mouse(i == 0 ? LEFT : i == 2 ? MIDDLE : RIGHT);
}

void Controller::key(int code, bool down) {
    std::lock_guard lock(mutex_);
    if (code < 1 || code > 255)
        fail("无效 HID 键码");
    bool mod = code >= 0xe0 && code <= 0xe7;
    if (mod)
        modifiers_ = down ? modifiers_ | (1 << (code - 0xe0)) : modifiers_ & ~(1 << (code - 0xe0));
    else if (down) {
        if (keys_.size() >= 10 && !keys_.contains(code))
            fail("同时按下的键超过盒子容量");
        keys_.insert(code);
    } else
        keys_.erase(code);
    keyboardReport();
}

void Controller::wheel(int steps) {
    std::lock_guard lock(mutex_);
    if (std::abs(int64_t(steps)) > 10000)
        fail("滚轮步数过大");
    while (steps) {
        const int n = std::clamp(steps, -127, 127);
        mouse(WHEEL, 0, 0, n);
        steps -= n;
    }
}

void Controller::probe() {
    std::lock_guard lock(mutex_);
    if (!connected_)
        fail("盒子未连接");
    move(0, 0);
}

void Controller::release() {
    std::lock_guard lock(mutex_);
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

void Controller::close() noexcept {
    std::lock_guard lock(mutex_);
    if (connected_ || socket_ != INVALID_SOCKET) {
        try {
            release();
        } catch (...) {
        }
    }
    if (socket_ != INVALID_SOCKET)
        closesocket(socket_);
    socket_ = INVALID_SOCKET;
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
            {"keyboard", true},
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
