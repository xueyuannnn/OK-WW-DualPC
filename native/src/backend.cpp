#include "backend.hpp"
namespace okww {
Backend::Backend(bool compatibilityChannel) {
    if (WSAStartup(MAKEWORD(2, 2), &wsa_))
        fail("Windows 网络初始化失败");
    token_ = QUuid::createUuid().toString(QUuid::WithoutBraces) +
             QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (compatibilityChannel) {
    mappingName_ = "Local\\OKWW.Native." + QUuid::createUuid().toString(QUuid::WithoutBraces);
    mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, mappingCapacity,
                                  reinterpret_cast<LPCWSTR>(mappingName_.utf16()));
    if (!mapping_)
        fail("创建视频共享内存失败");
    mapped_ = MapViewOfFile(mapping_, FILE_MAP_WRITE, 0, 0, mappingCapacity);
    if (!mapped_)
        fail("映射视频内存失败");
    listen_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (listen_ == INVALID_SOCKET || bind(listen_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 ||
        ::listen(listen_, 1) != 0)
        fail("创建本机兼容引擎通道失败");
    int length = sizeof(addr);
    getsockname(listen_, reinterpret_cast<sockaddr *>(&addr), &length);
    port_ = ntohs(addr.sin_port);
    server_ = std::thread(&Backend::serve, this);
    }
    monitor_ = std::thread(&Backend::monitor, this);
}
Backend::~Backend() {
    exit_ = true;
    if (listen_ != INVALID_SOCKET)
        shutdown(listen_, SD_BOTH);
    if (server_.joinable())
        server_.join();
    if (monitor_.joinable())
        monitor_.join();
    stop();
    if (listen_ != INVALID_SOCKET)
        closesocket(listen_);
    if (mapped_)
        UnmapViewOfFile(mapped_);
    if (mapping_)
        CloseHandle(mapping_);
    WSACleanup();
}
void Backend::start(const Config &cfg, bool simulation) {
    std::lock_guard lock(mutex_);
    stop();
    cfg_ = cfg;
    if (simulation)
        cfg_.set("kmbox.dry_run", true);
    cfg_.validateCapture();
    cfg_.validateBox();
    simulation_ = simulation;
    try {
        if (simulation) {
            auto f = std::make_shared<Frame>();
            f->width = 1280;
            f->height = 720;
            f->stride = 1280 * 3;
            f->bytes.resize(f->stride * f->height);
            f->format = PixelFormat::BGR24;
            for (int y = 0; y < f->height; ++y)
                for (int x = 0; x < f->width; ++x) {
                    auto p = reinterpret_cast<uint8_t *>(f->bytes.data()) + y * f->stride + x * 3;
                    p[0] = uint8_t(x * 255 / f->width);
                    p[1] = uint8_t(y * 255 / f->height);
                    p[2] = 80;
                }
            simulated_ = f;
        } else {
            capture_ = std::make_unique<Capture>(cfg_);
            capture_->start();
        }
        box_ = std::make_unique<Controller>(cfg_);
        box_->connect();
        running_ = true;
        error_.clear();
    } catch (const std::exception &e) {
        error_ = errorText(e);
        stop();
        throw;
    }
}
bool Backend::configurationMatches(const Config &cfg) const {
    std::lock_guard lock(mutex_);
    auto expected = cfg;
    if (simulation_)
        expected.set("kmbox.dry_run", true);
    return running_ && cfg_.json == expected.json;
}
void Backend::stop() {
    std::lock_guard lock(mutex_);
    armed_ = false;
    running_ = false;
    if (box_)
        box_->close();
    box_.reset();
    if (capture_)
        capture_->stop();
    capture_.reset();
    simulated_.reset();
}
FramePtr Backend::frameLocked() {
    if (!running_)
        return nullptr;
    if (simulation_ && simulated_) {
        auto f = std::make_shared<Frame>(*simulated_);
        f->captured = nowNs();
        f->sequence = uint64_t(f->captured / 16666667);
        return f;
    }
    auto f = capture_ ? capture_->latest() : nullptr;
    if (!f || nowNs() - f->captured > cfg_.d("capture.stale_after") * 1e9)
        return nullptr;
    return f;
}
QImage Backend::preview(QSize size, uint64_t &after) {
    FramePtr f;
    {
        std::lock_guard lock(mutex_);
        f = frameLocked();
    }
    if (!f || f->sequence == after)
        return {};
    after = f->sequence;
    return previewImage(*f, size);
}
void Backend::arm(bool enabled) {
    std::lock_guard lock(mutex_);
    if (enabled) {
        if (!frameLocked() || !box_ || !box_->connected())
            fail("请先启动采集与盒子连接线程");
        if (!box_->state()["keyboard"].toBool())
            fail(box_->state()["capability"].toString("盒子没有可用键盘路由"));
        heartbeat_ = nowNs();
    }
    armed_ = enabled;
    if (!enabled && box_)
        box_->release();
}
bool Backend::heartbeat() {
    std::lock_guard lock(mutex_);
    if (!armed_)
        return false;
    heartbeat_ = nowNs();
    return true;
}
QJsonObject Backend::testMove() {
    std::lock_guard lock(mutex_);
    if (!box_ || !box_->connected())
        fail("请先连接键鼠盒子，再测试移动");
    const int x = int(std::lround(40 * cfg_.d("kmbox.counts_per_pixel_x"))),
              y = int(std::lround(40 * cfg_.d("kmbox.counts_per_pixel_y")));
    try {
        // Serialize with task input and retain its held buttons and keys. A
        // successful test must not cancel a heavy attack or interrupt movement.
        box_->move(x, y);
    } catch (const std::exception &error) {
        armed_ = false;
        error_ = "测试移动失败：" + errorText(error);
        try {
            box_->release();
        } catch (...) {
        }
        throw;
    }
    return {{"dry_run", cfg_.b("kmbox.dry_run")}, {"armed", armed_},
            {"counts_x", x}, {"counts_y", y}};
}
FramePtr Backend::latestFrame() {
    std::lock_guard lock(mutex_);
    return frameLocked();
}
QJsonObject Backend::nativeCommand(QJsonObject command) {
    command["token"] = token_;
    return dispatch(command);
}
QJsonObject Backend::snapshot() {
    std::lock_guard lock(mutex_);
    auto f = frameLocked();
    QJsonObject actual = capture_ ? capture_->actual() : QJsonObject{};
    if (simulation_ && f)
        actual = {{"width", f->width},
                  {"height", f->height},
                  {"fourcc", "RGB3"},
                  {"backend", "演练画面"},
                  {"requested_fps", 60}};
    actual["measured_fps"] = capture_ ? capture_->fps() : simulation_ && running_ ? 60 : 0;
    QJsonObject c = box_ ? box_->state() : QJsonObject{{"connected", false}};
    return {{"running", running_},
            {"dry_run", cfg_.b("kmbox.dry_run")},
            {"capture_connected", bool(f)},
            {"armed", armed_},
            {"box", c},
            {"actual", actual},
            {"error", error_.isEmpty() && capture_ ? capture_->error() : error_},
            {"age_ms", f ? double(nowNs() - f->captured) / 1e6 : -1.0},
            {"simulation", simulation_}};
}
void Backend::monitor() {
    while (!exit_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        std::lock_guard lock(mutex_);
        if (armed_ &&
            (!frameLocked() || !box_ || !box_->connected() || nowNs() - heartbeat_ > 1500000000LL)) {
            armed_ = false;
            error_ = "任务已暂停：视频、盒子或兼容引擎连接中断";
            if (box_) {
                try {
                    box_->release();
                } catch (const std::exception &e) {
                    error_ += "；" + errorText(e);
                }
            }
        }
    }
}
QJsonObject Backend::dispatch(const QJsonObject &q) {
    std::lock_guard lock(mutex_);
    if (q["token"].toString() != token_)
        fail("兼容引擎身份校验失败");
    const auto op = q["op"].toString();
    if (op == "heartbeat") {
        heartbeat_ = nowNs();
        return snapshot();
    }
    if (op == "state" || op == "connect")
        return snapshot();
    if (op == "frame") {
        auto f = frameLocked();
        if (!f)
            return {{"available", false}};
        if (f->bytes.size() > mappingCapacity)
            fail("采集帧超出共享内存大小");
        memcpy(mapped_, f->bytes.constData(), f->bytes.size());
        return {{"available", true},
                {"size", int(f->bytes.size())},
                {"width", f->width},
                {"height", f->height},
                {"stride", f->stride},
                {"format", formatName(f->format)},
                {"bottom_up", f->bottomUp},
                {"sequence", QString::number(f->sequence)},
                {"age_ms", double(nowNs() - f->captured) / 1e6}};
    }
    if (op == "release") {
        if (box_)
            box_->release();
        return {};
    }
    if (op == "probe") {
        if (!box_)
            fail("盒子未连接");
        box_->probe();
        return snapshot();
    }
    if (!armed_ || !frameLocked() || !box_ || !box_->connected())
        fail("C++ 输入通道已暂停或设备未就绪");
    if (op == "move")
        box_->move(q["x"].toInt(), q["y"].toInt());
    else if (op == "absolute")
        box_->absolute(q["x"].toInt(), q["y"].toInt(), q["width"].toInt(), q["height"].toInt());
    else if (op == "button")
        box_->button(q["name"].toString(), q["down"].toBool());
    else if (op == "key")
        box_->key(q["code"].toInt(), q["down"].toBool());
    else if (op == "wheel")
        box_->wheel(q["steps"].toInt());
    else
        fail("未知的原生输入操作");
    return box_->state();
}
void Backend::serve() {
    while (!exit_) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(listen_, &set);
        timeval wait{0, 50000};
        if (select(0, &set, nullptr, nullptr, &wait) <= 0)
            continue;
        SOCKET client = accept(listen_, nullptr, nullptr);
        if (client == INVALID_SOCKET)
            continue;
        DWORD timeout = 100;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char *>(&timeout), sizeof(timeout));
        timeout = 1000;
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char *>(&timeout), sizeof(timeout));
        BOOL yes = TRUE;
        setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char *>(&yes), sizeof(yes));
        QByteArray buffered;
        while (!exit_) {
            char bytes[8192];
            int n = recv(client, bytes, sizeof(bytes), 0);
            if (n == 0)
                break;
            if (n < 0) {
                if (WSAGetLastError() == WSAETIMEDOUT)
                    continue;
                break;
            }
            buffered.append(bytes, n);
            if (buffered.size() > 65536)
                break;
            int newline;
            while ((newline = int(buffered.indexOf('\n'))) >= 0) {
                auto line = buffered.left(newline);
                buffered.remove(0, newline + 1);
                QJsonObject reply;
                try {
                    QJsonParseError e;
                    auto doc = QJsonDocument::fromJson(line, &e);
                    if (e.error != QJsonParseError::NoError || !doc.isObject())
                        fail("无效 IPC 消息");
                    reply = {{"ok", true}, {"result", dispatch(doc.object())}};
                } catch (const std::exception &e) {
                    reply = {{"ok", false}, {"error", errorText(e)}};
                }
                auto out = QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n';
                int sent = 0;
                while (sent < out.size()) {
                    int count = send(client, out.constData() + sent, int(out.size()) - sent, 0);
                    if (count <= 0)
                        break;
                    sent += count;
                }
                if (sent != out.size()) {
                    shutdown(client, SD_BOTH);
                    break;
                }
            }
        }
        closesocket(client);
        try {
            arm(false);
        } catch (...) {
        }
    }
}
} // namespace okww
