#pragma once
#include "backend.hpp"
#include "backend_pixels_abi.hpp"
#include "platform_workflow.hpp"
namespace okww::backend_lifecycle_test {
struct State {
    int scene, uuid = 0;
    bool seed = false;
    QJsonArray trace;
    explicit State(int s) : scene(s) {}
    bool failure(int index) const { return !seed && scene % 23 == index; }
    void step(const QString &name, int index = -1) {
        trace.append(QJsonObject{{"call", name}, {"index", index}});
        if (failure(index))
            throw std::runtime_error(name.toStdString());
        if (!seed && scene % 37 == index)
            throw 27;
    }
};
inline thread_local State *active = nullptr;
struct Uuid {
    int id;
    QString toString(QUuid::StringFormat) const { return QString("uuid-%1").arg(id); }
};
inline Uuid uuid() {
    active->trace.append("uuid");
    return {++active->uuid};
}
struct FakeThread {
    bool live = false;
    QString name;
    FakeThread() = default;
    template <class F, class T> FakeThread(F method, T *owner) {
        name = method == &T::serve ? "server" : "monitor";
        active->step("thread-" + name, 12);
        live = true;
        (void)owner;
    }
    FakeThread(FakeThread &&o) noexcept : live(std::exchange(o.live, false)), name(std::move(o.name)) {}
    FakeThread &operator=(FakeThread &&o) noexcept {
        live = std::exchange(o.live, false);
        name = std::move(o.name);
        return *this;
    }
    bool joinable() const {
        active->trace.append("joinable-" + name);
        return live;
    }
    void join() {
        active->trace.append("join-" + name);
        live = false;
    }
    ~FakeThread() {
        if (live)
            active->trace.append("unjoined-" + name);
    }
};
inline int WSAStartup(WORD version, WSADATA *) {
    active->trace.append(QJsonObject{{"wsa-create", int(version)}});
    return active->failure(0) ? 1 : 0;
}
inline HANDLE CreateFileMappingW(HANDLE, void *, DWORD flags, DWORD high, DWORD size, LPCWSTR name) {
    active->trace.append(QJsonObject{{"mapping", QString::fromWCharArray(name)},
                                     {"flags", qint64(flags)},
                                     {"high", qint64(high)},
                                     {"size", qint64(size)}});
    return active->failure(1) ? nullptr : reinterpret_cast<HANDLE>(UINT64_C(0x123456789abcdef1));
}
inline void *MapViewOfFile(HANDLE, DWORD flags, DWORD, DWORD, size_t size) {
    active->trace.append(QJsonObject{{"map-view", int(flags)}, {"size", qint64(size)}});
    return active->failure(2) ? nullptr : reinterpret_cast<void *>(UINT64_C(0x123456789abcdef2));
}
inline SOCKET socket(int family, int kind, int protocol) {
    active->trace.append(QJsonArray{"socket", family, kind, protocol});
    return active->failure(3) ? INVALID_SOCKET : SOCKET(UINT64_C(0x123456789abcdef3));
}
inline int bind(SOCKET value, sockaddr *address, int length) {
    const auto *ip = reinterpret_cast<sockaddr_in *>(address);
    active->trace.append(QJsonObject{{"bind", QString::number(value)},
                                     {"family", ip->sin_family},
                                     {"port", ntohs(ip->sin_port)},
                                     {"address", qint64(ntohl(ip->sin_addr.s_addr))},
                                     {"length", length}});
    return active->failure(4) ? -1 : 0;
}
inline int listen(SOCKET, int count) {
    active->trace.append(QJsonObject{{"listen", count}});
    return active->failure(5) ? -1 : 0;
}
inline int getsockname(SOCKET, sockaddr *address, int *length) {
    active->trace.append(QJsonObject{{"socket-name", *length}});
    reinterpret_cast<sockaddr_in *>(address)->sin_port = htons(23456);
    return -1;
}
inline int shutdown(SOCKET value, int how) {
    active->trace.append(QJsonObject{{"shutdown", QString::number(value)}, {"how", how}});
    return -1;
}
inline int closesocket(SOCKET value) {
    active->trace.append(QJsonObject{{"close-socket", QString::number(value)}});
    return -1;
}
inline BOOL UnmapViewOfFile(const void *value) {
    active->trace.append(QJsonObject{{"unmap", QString::number(quintptr(value))}});
    return FALSE;
}
inline BOOL CloseHandle(HANDLE value) {
    active->trace.append(QJsonObject{{"close-handle", QString::number(quintptr(value))}});
    return FALSE;
}
inline int WSACleanup() {
    active->trace.append("wsa-end");
    return -1;
}
struct CaptureDevice {
    explicit CaptureDevice(const Config &) { active->step("capture-new", 6); }
    ~CaptureDevice() { active->trace.append("capture-delete"); }
    void start() { active->step("capture-start", 7); }
    void stop() { active->trace.append("capture-stop"); }
};
struct Box {
    explicit Box(const Config &) { active->step("box-new", 8); }
    ~Box() {
        active->trace.append("box-delete");
        close();
    }
    void connect() { active->step("box-connect", 9); }
    void close() noexcept { active->trace.append("box-close"); }
};
struct Keys {
    Keys() { active->step("keys-new", 10); }
    ~Keys() { active->trace.append("keys-delete"); }
};
struct Skip {};
struct BackendState {
    mutable std::recursive_mutex mutex_;
    Config cfg_ = Config::defaults();
    std::unique_ptr<CaptureDevice> capture_;
    std::unique_ptr<Box> box_;
    std::unique_ptr<Keys> physicalKeys_;
    bool running_ = false, simulation_ = false, armed_ = false;
    QString error_;
    QJsonObject inputFault_;
    i64 captureRecoveryBegan_ = 0;
    FramePtr simulated_;
    std::atomic<bool> exit_{false};
    FakeThread monitor_, server_;
    SOCKET listen_ = INVALID_SOCKET;
    HANDLE mapping_ = nullptr;
    void *mapped_ = nullptr;
    QString token_, mappingName_;
    int port_ = 0;
    WSADATA wsa_{};
};
#define BACKEND_LIFECYCLE_FIXTURE_CLASS                                                                      \
    class Backend : public BackendState {                                                                    \
      public:                                                                                                \
        static constexpr int mappingCapacity = 64 * 1024 * 1024;                                             \
        explicit Backend(bool);                                                                              \
        explicit Backend(Skip) {}                                                                            \
        ~Backend();                                                                                          \
        void start(const Config &, bool);                                                                    \
        bool configurationMatches(const Config &) const;                                                     \
        void stop();                                                                                         \
        void serve() {}                                                                                      \
        void monitor() {};                                                                                   \
    };
namespace original {
BACKEND_LIFECYCLE_FIXTURE_CLASS
Backend::Backend(bool compatibilityChannel) {
    if (backend_lifecycle_test::WSAStartup(MAKEWORD(2, 2), &wsa_))
        fail("Windows 网络初始化失败");
    token_ = backend_lifecycle_test::uuid().toString(QUuid::WithoutBraces) +
             backend_lifecycle_test::uuid().toString(QUuid::WithoutBraces);
    if (compatibilityChannel) {
        mappingName_ = "Local\\OKWW.Native." + backend_lifecycle_test::uuid().toString(QUuid::WithoutBraces);
        mapping_ = backend_lifecycle_test::CreateFileMappingW(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, mappingCapacity,
            reinterpret_cast<LPCWSTR>(mappingName_.utf16()));
        if (!mapping_)
            fail("创建视频共享内存失败");
        mapped_ = backend_lifecycle_test::MapViewOfFile(mapping_, FILE_MAP_WRITE, 0, 0, mappingCapacity);
        if (!mapped_)
            fail("映射视频内存失败");
        listen_ = backend_lifecycle_test::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (listen_ == INVALID_SOCKET ||
            backend_lifecycle_test::bind(listen_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 ||
            backend_lifecycle_test::listen(listen_, 1) != 0)
            fail("创建本机兼容引擎通道失败");
        int length = sizeof(addr);
        backend_lifecycle_test::getsockname(listen_, reinterpret_cast<sockaddr *>(&addr), &length);
        port_ = ntohs(addr.sin_port);
        server_ = FakeThread(&Backend::serve, this);
    }
    monitor_ = FakeThread(&Backend::monitor, this);
}
Backend::~Backend() {
    exit_ = true;
    if (listen_ != INVALID_SOCKET)
        backend_lifecycle_test::shutdown(listen_, SD_BOTH);
    if (server_.joinable())
        server_.join();
    if (monitor_.joinable())
        monitor_.join();
    stop();
    if (listen_ != INVALID_SOCKET)
        backend_lifecycle_test::closesocket(listen_);
    if (mapped_)
        backend_lifecycle_test::UnmapViewOfFile(mapped_);
    if (mapping_)
        backend_lifecycle_test::CloseHandle(mapping_);
    backend_lifecycle_test::WSACleanup();
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
            capture_ = std::make_unique<CaptureDevice>(cfg_);
            capture_->start();
        }
        box_ = std::make_unique<Box>(cfg_);
        box_->connect();
        if (cfg_.s("kmbox.protocol") == "sendinput" && !cfg_.b("kmbox.dry_run"))
            physicalKeys_ = std::make_unique<Keys>();
        running_ = true;
        error_.clear();
        inputFault_ = {};
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
    captureRecoveryBegan_ = 0;
    if (box_)
        box_->close();
    box_.reset();
    physicalKeys_.reset();
    if (capture_)
        capture_->stop();
    capture_.reset();
    simulated_.reset();
}
} // namespace original
namespace current {
BACKEND_LIFECYCLE_FIXTURE_CLASS
static void backendLifecycleFailure(int kind) {
    switch (kind) {
    case 0:
        fail("Windows 网络初始化失败");
    case 1:
        fail("创建视频共享内存失败");
    case 2:
        fail("映射视频内存失败");
    case 3:
        fail("创建本机兼容引擎通道失败");
    default:
        fail("Rust 设备后台生命周期错误宿主无效");
    }
}
struct BackendLifecycleSdk {
    Backend &b;
    bool compatibility = true;
    const Config *config = nullptr;
    bool simulation = false;
    sockaddr_in address{};
    std::shared_ptr<Frame> frame;
    void operator()(rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &error) {
        switch (op) {
        case rust::WorkflowOp::BackendCreateWsa:
            out[0] = backend_lifecycle_test::WSAStartup(MAKEWORD(2, 2), &b.wsa_) != 0;
            break;
        case rust::WorkflowOp::BackendCreateToken:
            b.token_ = backend_lifecycle_test::uuid().toString(QUuid::WithoutBraces) +
                       backend_lifecycle_test::uuid().toString(QUuid::WithoutBraces);
            break;
        case rust::WorkflowOp::BackendCreateCompatibility:
            out[0] = compatibility;
            break;
        case rust::WorkflowOp::BackendCreateMappingName:
            b.mappingName_ =
                "Local\\OKWW.Native." + backend_lifecycle_test::uuid().toString(QUuid::WithoutBraces);
            break;
        case rust::WorkflowOp::BackendCreateMapping:
            b.mapping_ = backend_lifecycle_test::CreateFileMappingW(
                INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, Backend::mappingCapacity,
                reinterpret_cast<LPCWSTR>(b.mappingName_.utf16()));
            break;
        case rust::WorkflowOp::BackendLifecycleHasMapping:
            out[0] = b.mapping_ != nullptr;
            break;
        case rust::WorkflowOp::BackendCreateMapView:
            b.mapped_ = backend_lifecycle_test::MapViewOfFile(b.mapping_, FILE_MAP_WRITE, 0, 0,
                                                              Backend::mappingCapacity);
            break;
        case rust::WorkflowOp::BackendLifecycleHasMapped:
            out[0] = b.mapped_ != nullptr;
            break;
        case rust::WorkflowOp::BackendCreateSocket:
            b.listen_ = backend_lifecycle_test::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            break;
        case rust::WorkflowOp::BackendCreateAddress:
            address = {};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = 0;
            break;
        case rust::WorkflowOp::BackendLifecycleHasSocket:
            out[0] = b.listen_ != INVALID_SOCKET;
            break;
        case rust::WorkflowOp::BackendCreateBind:
            out[0] = backend_lifecycle_test::bind(b.listen_, reinterpret_cast<sockaddr *>(&address),
                                                  sizeof(address)) == 0;
            break;
        case rust::WorkflowOp::BackendCreateListen:
            out[0] = backend_lifecycle_test::listen(b.listen_, 1) == 0;
            break;
        case rust::WorkflowOp::BackendCreateSocketName: {
            int length = sizeof(address);
            backend_lifecycle_test::getsockname(b.listen_, reinterpret_cast<sockaddr *>(&address), &length);
            break;
        }
        case rust::WorkflowOp::BackendCreatePort:
            b.port_ = ntohs(address.sin_port);
            break;
        case rust::WorkflowOp::BackendCreateServerThread:
            b.server_ = FakeThread(&Backend::serve, &b);
            break;
        case rust::WorkflowOp::BackendCreateMonitorThread:
            b.monitor_ = FakeThread(&Backend::monitor, &b);
            break;
        case rust::WorkflowOp::BackendDestroySetExit:
            b.exit_ = true;
            break;
        case rust::WorkflowOp::BackendDestroyShutdown:
            backend_lifecycle_test::shutdown(b.listen_, SD_BOTH);
            break;
        case rust::WorkflowOp::BackendDestroyServerJoinable:
            out[0] = b.server_.joinable();
            break;
        case rust::WorkflowOp::BackendDestroyJoinServer:
            b.server_.join();
            break;
        case rust::WorkflowOp::BackendDestroyMonitorJoinable:
            out[0] = b.monitor_.joinable();
            break;
        case rust::WorkflowOp::BackendDestroyJoinMonitor:
            b.monitor_.join();
            break;
        case rust::WorkflowOp::BackendLifecycleStop:
            b.stop();
            break;
        case rust::WorkflowOp::BackendDestroyCloseSocket:
            backend_lifecycle_test::closesocket(b.listen_);
            break;
        case rust::WorkflowOp::BackendDestroyUnmap:
            backend_lifecycle_test::UnmapViewOfFile(b.mapped_);
            break;
        case rust::WorkflowOp::BackendDestroyCloseMapping:
            backend_lifecycle_test::CloseHandle(b.mapping_);
            break;
        case rust::WorkflowOp::BackendDestroyWsa:
            backend_lifecycle_test::WSACleanup();
            break;
        case rust::WorkflowOp::BackendStartCopyConfig:
            b.cfg_ = *config;
            break;
        case rust::WorkflowOp::BackendStartSimulation:
            out[0] = simulation;
            break;
        case rust::WorkflowOp::BackendStartForceDryRun:
            b.cfg_.set("kmbox.dry_run", true);
            break;
        case rust::WorkflowOp::BackendStartValidateCapture:
            b.cfg_.validateCapture();
            break;
        case rust::WorkflowOp::BackendStartValidateBox:
            b.cfg_.validateBox();
            break;
        case rust::WorkflowOp::BackendStartStoreSimulation:
            b.simulation_ = simulation;
            break;
        case rust::WorkflowOp::BackendSimCreate:
            frame = std::make_shared<Frame>();
            break;
        case rust::WorkflowOp::BackendSimDescribe:
            frame->width = 1280;
            frame->height = 720;
            frame->stride = 1280 * 3;
            break;
        case rust::WorkflowOp::BackendSimAllocate:
            frame->bytes.resize(frame->stride * frame->height);
            break;
        case rust::WorkflowOp::BackendSimFormat:
            frame->format = PixelFormat::BGR24;
            break;
        case rust::WorkflowOp::BackendSimFill:
            if (!rust::okww_rs_backend_simulation(frame->width, frame->height, frame->stride,
                                                  reinterpret_cast<uint8_t *>(frame->bytes.data()),
                                                  size_t(frame->bytes.size())))
                fail("Rust 演练图像接口异常");
            break;
        case rust::WorkflowOp::BackendSimStore:
            b.simulated_ = frame;
            break;
        case rust::WorkflowOp::BackendDropTempFrame:
            frame.reset();
            break;
        case rust::WorkflowOp::BackendStartCreateCapture:
            b.capture_ = std::make_unique<CaptureDevice>(b.cfg_);
            break;
        case rust::WorkflowOp::BackendStartCapture:
            b.capture_->start();
            break;
        case rust::WorkflowOp::BackendStartCreateBox:
            b.box_ = std::make_unique<Box>(b.cfg_);
            break;
        case rust::WorkflowOp::BackendStartBox:
            b.box_->connect();
            break;
        case rust::WorkflowOp::BackendStartLocalProtocol:
            out[0] = b.cfg_.s("kmbox.protocol") == "sendinput";
            break;
        case rust::WorkflowOp::BackendStartDryRun:
            out[0] = b.cfg_.b("kmbox.dry_run");
            break;
        case rust::WorkflowOp::BackendStartCreatePhysicalKeys:
            b.physicalKeys_ = std::make_unique<Keys>();
            break;
        case rust::WorkflowOp::BackendStartRunning:
            b.running_ = true;
            break;
        case rust::WorkflowOp::BackendStartClearError:
            b.error_.clear();
            break;
        case rust::WorkflowOp::BackendStartClearInputFault:
            b.inputFault_ = {};
            break;
        case rust::WorkflowOp::BackendStartStoreError:
            try {
                std::rethrow_exception(error);
            } catch (const std::exception &e) {
                b.error_ = errorText(e);
            }
            break;
        case rust::WorkflowOp::BackendStopDisarm:
            b.armed_ = false;
            break;
        case rust::WorkflowOp::BackendStopRunning:
            b.running_ = false;
            break;
        case rust::WorkflowOp::BackendStopResetRecovery:
            b.captureRecoveryBegan_ = 0;
            break;
        case rust::WorkflowOp::BackendStopHasBox:
            out[0] = bool(b.box_);
            break;
        case rust::WorkflowOp::BackendStopCloseBox:
            b.box_->close();
            break;
        case rust::WorkflowOp::BackendStopDropBox:
            b.box_.reset();
            break;
        case rust::WorkflowOp::BackendStopDropPhysicalKeys:
            b.physicalKeys_.reset();
            break;
        case rust::WorkflowOp::BackendStopHasCapture:
            out[0] = bool(b.capture_);
            break;
        case rust::WorkflowOp::BackendStopCapture:
            b.capture_->stop();
            break;
        case rust::WorkflowOp::BackendStopDropCapture:
            b.capture_.reset();
            break;
        case rust::WorkflowOp::BackendStopDropSimulated:
            b.simulated_.reset();
            break;
        case rust::WorkflowOp::BackendLifecycleFailure:
            backendLifecycleFailure(int(a[0]));
            break;
        default:
            fail("Rust 设备后台生命周期宿主无效");
        }
    }
};
struct BackendConfigSdk {
    const Backend &b;
    const Config &config;
    Config expected;
    void operator()(rust::WorkflowOp op, const double *, double *out) {
        switch (op) {
        case rust::WorkflowOp::BackendMatchCopyConfig:
            expected = config;
            break;
        case rust::WorkflowOp::BackendMatchSimulation:
            out[0] = b.simulation_;
            break;
        case rust::WorkflowOp::BackendMatchForceDryRun:
            expected.set("kmbox.dry_run", true);
            break;
        case rust::WorkflowOp::BackendMatchRunning:
            out[0] = b.running_;
            break;
        case rust::WorkflowOp::BackendMatchEqualConfig:
            out[0] = b.cfg_.json == expected.json;
            break;
        default:
            fail("Rust 设备后台配置匹配宿主无效");
        }
    }
};
Backend::Backend(bool compatibilityChannel) {
    BackendLifecycleSdk sdk{*this};
    sdk.compatibility = compatibilityChannel;
    rust::platformWorkflowRun(rust::WorkflowMethod::BackendCreate, {}, sdk);
}
Backend::~Backend() {
    BackendLifecycleSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::BackendDestroy, {}, sdk);
}
void Backend::start(const Config &cfg, bool simulation) {
    std::lock_guard lock(mutex_);
    BackendLifecycleSdk sdk{*this};
    sdk.config = &cfg;
    sdk.simulation = simulation;
    rust::platformWorkflowRun(rust::WorkflowMethod::BackendStart, {}, sdk);
}
bool Backend::configurationMatches(const Config &cfg) const {
    std::lock_guard lock(mutex_);
    BackendConfigSdk sdk{*this, cfg};
    return rust::platformWorkflowRun(rust::WorkflowMethod::BackendConfigMatches, {}, sdk)[0] != 0;
}
void Backend::stop() {
    std::lock_guard lock(mutex_);
    BackendLifecycleSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::BackendStop, {}, sdk);
}
} // namespace current
#undef BACKEND_LIFECYCLE_FIXTURE_CLASS
template <class B> QJsonObject snapshot(const B &b) {
    return {{"running", b.running_},
            {"armed", b.armed_},
            {"simulation", b.simulation_},
            {"error", b.error_},
            {"fault", b.inputFault_},
            {"recovery", QString::number(b.captureRecoveryBegan_)},
            {"capture", bool(b.capture_)},
            {"box", bool(b.box_)},
            {"physical", bool(b.physicalKeys_)},
            {"simulated", bool(b.simulated_)},
            {"pixel_hash",
             b.simulated_
                 ? QString::fromLatin1(
                       QCryptographicHash::hash(b.simulated_->bytes, QCryptographicHash::Sha256).toHex())
                 : QString{}},
            {"token", b.token_},
            {"mapping", b.mappingName_},
            {"port", b.port_}};
}
template <class B> QJsonObject run(int method) {
    QJsonObject result;
    if (method == 0) {
        try {
            B backend(active->scene % 2 == 0);
            result = snapshot(backend);
        } catch (const std::exception &e) {
            result["error"] = errorText(e);
        } catch (...) {
            result["error"] = "unknown";
        }
        return result;
    }
    B backend(Skip{});
    active->seed = true;
    backend.capture_ = std::make_unique<CaptureDevice>(backend.cfg_);
    backend.box_ = std::make_unique<Box>(backend.cfg_);
    backend.physicalKeys_ = std::make_unique<Keys>();
    auto old = std::make_shared<Frame>();
    old->bytes = QByteArray("old");
    backend.simulated_ = old;
    old.reset();
    active->seed = false;
    backend.running_ = active->scene % 5 != 0;
    backend.armed_ = true;
    backend.simulation_ = active->scene % 3 == 0;
    backend.error_ = "previous";
    backend.inputFault_ = {{"old", true}};
    backend.captureRecoveryBegan_ = 42;
    Config config = Config::defaults();
    config.set("kmbox.protocol", active->scene % 2 ? "kmbox_net" : "sendinput");
    config.set("kmbox.dry_run", active->scene % 7 == 0);
    if (active->scene % 29 == 0)
        config.set("capture.fps", 0);
    try {
        if (method == 1 || method == 2)
            backend.start(config, method == 2);
        else if (method == 3)
            backend.stop();
        else {
            backend.cfg_ = config;
            result["matches"] = backend.configurationMatches(config);
            config.set("unrelated", true);
            result["mismatch"] = backend.configurationMatches(config);
        }
    } catch (const std::exception &e) {
        result["action_error"] = errorText(e);
    } catch (...) {
        result["action_error"] = "unknown";
    }
    result["state"] = snapshot(backend);
    return result;
}
inline QJsonObject outcome(int scene, int method, bool rust) {
    State state(scene);
    active = &state;
    auto result = rust ? run<current::Backend>(method) : run<original::Backend>(method);
    result["trace"] = state.trace;
    return result;
}
} // namespace okww::backend_lifecycle_test
