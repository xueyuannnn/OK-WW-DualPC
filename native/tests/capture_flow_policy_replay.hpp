#pragma once
#include "capture.hpp"
#include "capture_math_abi.hpp"
#include "platform_workflow.hpp"
#include <optional>
namespace okww::capture_flow_test {
struct State;
inline thread_local State *active = nullptr;
struct Mutex {
    QJsonArray *trace;
    bool owned = false;
    void lock() {
        if (owned)
            throw std::runtime_error("double lock");
        trace->append("lock");
        owned = true;
    }
    void unlock() {
        trace->append("unlock");
        owned = false;
    }
};
struct Thread {
    QJsonArray *trace;
    bool live = false;
    bool joinable() {
        trace->append("joinable");
        return live;
    }
    void join() {
        trace->append("join");
        live = false;
    }
};
struct Condition {
    State &owner;
    void notify_all();
    template <class Duration, class Predicate>
    bool wait_for(std::unique_lock<Mutex> &lock, Duration duration, Predicate predicate);
};
struct State {
    int scene, allocations = 0;
    QJsonArray trace;
    Config config_ = Config::defaults();
    Thread thread_{&trace};
    Mutex mutex_{&trace};
    Condition cv_{*this};
    std::array<std::shared_ptr<Frame>, 6> pool_{};
    FramePtr latest_;
    QString error_, desktopRecovery_;
    bool ready_ = false;
    std::atomic<bool> stop_{false};
    uint64_t seq_ = uint64_t(1) << 54, unchangedPolls_ = uint64_t(1) << 54;
    double negotiatedFps_ = 240.;
    std::deque<i64> arrivals_;
    std::vector<FramePtr> pins;
    explicit State(int s) : scene(s) {
        active = this;
        stop_ = s % 11 == 0;
        thread_.live = s % 7 == 0;
        ready_ = s % 3 == 0;
        config_.set("capture.source", s % 3 == 0 ? "desktop" : "card");
        config_.set("capture.backend", s % 2 ? "DSHOW" : "MSMF");
        config_.set("capture.local_backend", s % 2 ? "DXGI" : "WIN32");
        if (s % 41 == 0)
            config_.set("capture.fps", 0);
        if (s % 13 == 0)
            error_ = "ordinary fixture";
        else if (s % 17 == 0)
            error_ = "采集驱动返回了不完整图像";
        if (s % 19 == 0)
            desktopRecovery_ = "recovering fixture";
        for (int i = 0; i < int(pool_.size()); ++i) {
            if ((s + i) % 3 == 0 || s % 5 == 0) {
                pool_[i] = std::make_shared<Frame>();
                pool_[i]->bytes = QByteArray(24, '\x80');
                pool_[i]->width = 4;
                pool_[i]->height = 4;
                pool_[i]->stride = 4;
                pool_[i]->format = PixelFormat::NV12;
                pool_[i]->sequence = seq_ + i;
                pool_[i]->captured = 1000 + i;
                if (s % 5 == 0)
                    pins.push_back(pool_[i]);
            }
        }
        if (s % 4 == 0)
            latest_ = pool_[0] ? pool_[0] : std::make_shared<Frame>();
        if (s % 6 == 0) {
            auto f = std::make_shared<Frame>();
            f->bytes = QByteArray(24, '\x80');
            f->width = 4;
            f->height = 4;
            f->stride = 4;
            f->format = PixelFormat::NV12;
            f->sequence = seq_ + 7;
            f->captured = 2000;
            latest_ = f;
            pins.push_back(f);
        }
        for (int i = 0, n = s % 8 == 0 ? 241 : s % 4; i < n; ++i)
            arrivals_.push_back(100000000000000000LL + s - 10000000LL * (n - i));
    }
    void startWorker() {
        trace.append("create-thread");
        thread_.live = true;
        if (scene % 29 == 0)
            throw std::runtime_error("thread fixture");
    }
    void runDesktop() {
        trace.append("run-desktop");
        body();
    }
    void runDs() {
        trace.append("run-dshow");
        body();
    }
    void runMf() {
        trace.append("run-msmf");
        body();
    }
    void body() {
        if (scene % 23 == 0)
            throw 27;
        if (scene % 5 == 0)
            throw std::runtime_error("capture fixture");
    }
};
inline i64 clock() {
    active->trace.append("clock");
    return 100000000000000000LL + active->scene;
}
template <class... Args> std::shared_ptr<Frame> allocateFrame(Args &&...args) {
    active->trace.append("allocate-frame");
    if (active->scene % 37 == 0 && ++active->allocations == 1)
        throw std::bad_alloc();
    return std::make_shared<Frame>(std::forward<Args>(args)...);
}
inline void Condition::notify_all() { owner.trace.append("notify-all"); }
template <class Duration, class Predicate>
bool Condition::wait_for(std::unique_lock<Mutex> &lock, Duration duration, Predicate predicate) {
    owner.trace.append(QJsonObject{
        {"wait-ms", double(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count())}});
    if (!lock.owns_lock())
        throw std::runtime_error("unlocked wait");
    return predicate();
}
struct Com {
    HRESULT result = S_OK;
    Com() {
        active->trace.append("com-create");
        if (active->scene % 43 == 0)
            throw std::runtime_error("com fixture");
        if (active->scene % 17 == 0)
            result = E_FAIL;
    }
    ~Com() { active->trace.append("com-destroy"); }
};
inline HANDLE GetCurrentThread() {
    active->trace.append("thread-handle");
    return reinterpret_cast<HANDLE>(uintptr_t(1));
}
inline HRESULT SetThreadDescription(HANDLE, PCWSTR name) {
    active->trace.append(QJsonObject{{"thread-name", QString::fromWCharArray(name)}});
    return S_OK;
}
struct Original : State {
    using State::State;
    void publish(const uint8_t *, size_t, int, int, int, PixelFormat, bool, i64, bool = false);
    void verifyUnchangedDesktop(i64);
    void start();
    void stop();
    QString error();
    void desktopRecovery(QString);
    bool recovering();
    FramePtr latest();
    QJsonObject actual();
    double fps();
    FramePtr next(uint64_t);
    void run();
};
void Original::publish(const uint8_t *data, size_t length, int w, int h, int stride, PixelFormat fmt,
                       bool bottom, i64 stamp, bool compareContents) {
    if (stop_)
        return;
    size_t expected = size_t(stride) * h;
    if (fmt == PixelFormat::NV12 || fmt == PixelFormat::I420 || fmt == PixelFormat::P010)
        expected = expected * 3 / 2;
    if (fmt != PixelFormat::MJPG && length < expected) {
        std::lock_guard lock(mutex_);
        error_ = "采集驱动返回了不完整图像";
        cv_.notify_all();
        return;
    }
    if (length > 256 * 1024 * 1024)
        return;
    if (compareContents) {
        std::lock_guard lock(mutex_);
        if (latest_ && latest_->width == w && latest_->height == h && latest_->stride == stride &&
            latest_->format == fmt && latest_->bottomUp == bottom &&
            latest_->bytes.size() == qsizetype(length) &&
            memcmp(latest_->bytes.constData(), data, length) == 0) {
            auto verified = capture_flow_test::allocateFrame(*latest_);
            verified->captured = stamp;
            latest_ = std::move(verified);
            ++unchangedPolls_;
            return;
        }
    }
    std::shared_ptr<Frame> f;
    {
        std::lock_guard lock(mutex_);
        for (auto &slot : pool_)
            if (!slot || slot.use_count() == 1) {
                if (!slot)
                    slot = capture_flow_test::allocateFrame();
                f = slot;
                break;
            }
    }
    if (!f)
        return;
    f->bytes.resize(qsizetype(length));
    memcpy(f->bytes.data(), data, length);
    f->width = w;
    f->height = h;
    f->stride = stride;
    f->format = fmt;
    f->bottomUp = bottom;
    f->captured = stamp;
    {
        std::lock_guard lock(mutex_);
        f->sequence = ++seq_;
        latest_ = f;
        if (error_ == "采集驱动返回了不完整图像")
            error_.clear();
        arrivals_.push_back(stamp);
        if (arrivals_.size() > 240)
            arrivals_.pop_front();
        ready_ = true;
    }
    cv_.notify_all();
}
void Original::verifyUnchangedDesktop(i64 stamp) {
    std::lock_guard lock(mutex_);
    if (stop_ || !latest_ || config_.s("capture.source") != "desktop")
        return;
    // A successful desktop poll renews liveness, not image identity. QByteArray
    // shares immutable pixels; existing readers retain their original stamp.
    auto verified = capture_flow_test::allocateFrame(*latest_);
    verified->captured = stamp;
    latest_ = std::move(verified);
    ++unchangedPolls_;
}
void Original::start() {
    if (thread_.joinable())
        fail("采集线程已经启动");
    config_.validateCapture(); // Reject quarantined modes before opening the device or creating a thread.
    startWorker();
    std::unique_lock lock(mutex_);
    if (!cv_.wait_for(lock, std::chrono::seconds(8), [&] { return ready_ || !error_.isEmpty(); }))
        fail("视频采集启动超时");
    if (!error_.isEmpty())
        fail(error_);
}
void Original::stop() {
    stop_ = true;
    cv_.notify_all();
    if (thread_.joinable())
        thread_.join();
}
QString Original::error() {
    std::lock_guard lock(mutex_);
    return error_;
}
void Original::desktopRecovery(QString reason) {
    std::lock_guard lock(mutex_);
    desktopRecovery_ = std::move(reason);
}
bool Original::recovering() {
    std::lock_guard lock(mutex_);
    return !desktopRecovery_.isEmpty();
}
FramePtr Original::latest() {
    std::lock_guard lock(mutex_);
    return !stop_ ? latest_ : nullptr;
}
QJsonObject Original::actual() {
    std::lock_guard lock(mutex_);
    if (!latest_)
        return {{"recovering", !desktopRecovery_.isEmpty()}, {"recovery_reason", desktopRecovery_}};
    return {{"width", latest_->width},
            {"height", latest_->height},
            {"fourcc", formatName(latest_->format)},
            {"backend", config_.s("capture.source") == "desktop" ? config_.s("capture.local_backend")
                                                                 : config_.s("capture.backend")},
            {"requested_fps",
             config_.s("capture.source") == "desktop"
                 ? (config_.s("capture.local_backend") == "DXGI" ? negotiatedFps_
                                                                 : config_.d("capture.local_fps"))
                 : config_.d("capture.fps")},
            {"negotiated_fps", negotiatedFps_},
            {"unchanged_polls", double(unchangedPolls_)},
            {"recovering", !desktopRecovery_.isEmpty()},
            {"recovery_reason", desktopRecovery_}};
}
double Original::fps() {
    std::lock_guard lock(mutex_);
    return arrivals_.size() > 1 && capture_flow_test::clock() - arrivals_.back() < 1000000000
               ? double(arrivals_.size() - 1) * 1e9 / (arrivals_.back() - arrivals_.front())
               : 0;
}
FramePtr Original::next(uint64_t after) {
    std::unique_lock lock(mutex_);
    cv_.wait_for(lock, std::chrono::milliseconds(20),
                 [&] { return stop_ || !error_.isEmpty() || (latest_ && latest_->sequence != after); });
    if (!error_.isEmpty())
        fail(error_);
    return !stop_ && latest_ && latest_->sequence != after ? latest_ : nullptr;
}
void Original::run() {
    try {
        Com com;
        hr(com.result, "初始化采集 COM 失败");
        capture_flow_test::SetThreadDescription(capture_flow_test::GetCurrentThread(), L"OKWW Capture");
        if (config_.s("capture.source") == "desktop")
            runDesktop();
        else if (config_.s("capture.backend") == "DSHOW")
            runDs();
        else
            runMf();
    } catch (const std::exception &e) {
        std::lock_guard lock(mutex_);
        error_ = errorText(e);
    }
    stop_ = true;
    cv_.notify_all();
}

struct CaptureFlowSdk {
    State &c;
    const uint8_t *data = nullptr;
    size_t length = 0;
    int w = 0, h = 0, stride = 0;
    PixelFormat fmt = PixelFormat::BGR24;
    bool bottom = false;
    i64 stamp = 0;
    uint64_t after = 0;
    QString text;
    QJsonObject state;
    FramePtr output;
    std::shared_ptr<Frame> frame;
    std::optional<std::unique_lock<Mutex>> lock;
    std::optional<Com> com;
    void operator()(rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &error) {
        switch (op) {
        case rust::WorkflowOp::CaptureStopped:
            out[0] = c.stop_.load();
            break;
        case rust::WorkflowOp::CapturePayloadState:
            out[0] = rust::okww_rs_capture_payload(length, stride, h, int(fmt));
            break;
        case rust::WorkflowOp::CaptureLock:
            lock.emplace(c.mutex_);
            break;
        case rust::WorkflowOp::CaptureUnlock:
            lock.reset();
            break;
        case rust::WorkflowOp::CaptureSetShortError:
            c.error_ = "采集驱动返回了不完整图像";
            break;
        case rust::WorkflowOp::CaptureNotifyAll:
            c.cv_.notify_all();
            break;
        case rust::WorkflowOp::CaptureHasLatest:
            out[0] = bool(c.latest_);
            break;
        case rust::WorkflowOp::CaptureLatestDimensions:
            out[0] = c.latest_->width;
            out[1] = c.latest_->height;
            out[2] = c.latest_->stride;
            break;
        case rust::WorkflowOp::CaptureLatestFormat:
            out[0] = int(c.latest_->format);
            break;
        case rust::WorkflowOp::CaptureLatestBottom:
            out[0] = c.latest_->bottomUp;
            break;
        case rust::WorkflowOp::CaptureLatestLengthMatches:
            out[0] = c.latest_->bytes.size() == qsizetype(length);
            break;
        case rust::WorkflowOp::CaptureContentsMatch:
            out[0] = memcmp(c.latest_->bytes.constData(), data, length) == 0;
            break;
        case rust::WorkflowOp::CaptureCopyLatest:
            frame = capture_flow_test::allocateFrame(*c.latest_);
            break;
        case rust::WorkflowOp::CaptureSetCopiedStamp:
            frame->captured = stamp;
            break;
        case rust::WorkflowOp::CaptureMoveLatest:
            c.latest_ = std::move(frame);
            break;
        case rust::WorkflowOp::CaptureIncrementUnchanged:
            ++c.unchangedPolls_;
            break;
        case rust::WorkflowOp::CapturePoolSize:
            out[0] = double(c.pool_.size());
            break;
        case rust::WorkflowOp::CapturePoolPresent:
            out[0] = bool(c.pool_.at(size_t(a[0])));
            break;
        case rust::WorkflowOp::CapturePoolReferences:
            out[0] = double(c.pool_.at(size_t(a[0])).use_count());
            break;
        case rust::WorkflowOp::CaptureCreatePoolFrame:
            c.pool_.at(size_t(a[0])) = capture_flow_test::allocateFrame();
            break;
        case rust::WorkflowOp::CaptureUsePoolFrame:
            frame = c.pool_.at(size_t(a[0]));
            break;
        case rust::WorkflowOp::CaptureHasPublishFrame:
            out[0] = bool(frame);
            break;
        case rust::WorkflowOp::CaptureResizeBytes:
            frame->bytes.resize(qsizetype(length));
            break;
        case rust::WorkflowOp::CaptureCopyBytes:
            memcpy(frame->bytes.data(), data, length);
            break;
        case rust::WorkflowOp::CaptureAssignFrameFields:
            frame->width = w;
            frame->height = h;
            frame->stride = stride;
            frame->format = fmt;
            frame->bottomUp = bottom;
            frame->captured = stamp;
            break;
        case rust::WorkflowOp::CaptureIncrementSequence:
            frame->sequence = ++c.seq_;
            break;
        case rust::WorkflowOp::CapturePublishFrame:
            c.latest_ = frame;
            break;
        case rust::WorkflowOp::CaptureShortErrorActive:
            out[0] = c.error_ == "采集驱动返回了不完整图像";
            break;
        case rust::WorkflowOp::CaptureClearError:
            c.error_.clear();
            break;
        case rust::WorkflowOp::CaptureAppendArrival:
            c.arrivals_.push_back(stamp);
            break;
        case rust::WorkflowOp::CaptureArrivalCount:
            out[0] = double(c.arrivals_.size());
            break;
        case rust::WorkflowOp::CapturePopArrival:
            c.arrivals_.pop_front();
            break;
        case rust::WorkflowOp::CaptureSetReady:
            c.ready_ = true;
            break;
        case rust::WorkflowOp::CaptureSourceDesktop:
            out[0] = c.config_.s("capture.source") == "desktop";
            break;
        case rust::WorkflowOp::CaptureThreadJoinable:
            out[0] = c.thread_.joinable();
            break;
        case rust::WorkflowOp::CaptureValidate:
            c.config_.validateCapture();
            break;
        case rust::WorkflowOp::CaptureCreateThread:
            c.startWorker();
            break;
        case rust::WorkflowOp::CaptureWaitStart:
            out[0] = c.cv_.wait_for(*lock, std::chrono::seconds(int(a[0])),
                                    [this] { return run(rust::WorkflowMethod::CaptureStartReady)[0] != 0; });
            break;
        case rust::WorkflowOp::CaptureHasError:
            out[0] = !c.error_.isEmpty();
            break;
        case rust::WorkflowOp::CaptureMarkStopped:
            c.stop_ = true;
            break;
        case rust::WorkflowOp::CaptureJoinThread:
            c.thread_.join();
            break;
        case rust::WorkflowOp::CaptureCopyError:
            text = c.error_;
            break;
        case rust::WorkflowOp::CaptureWriteRecovery:
            c.desktopRecovery_ = std::move(text);
            break;
        case rust::WorkflowOp::CaptureRecoveryActive:
            out[0] = !c.desktopRecovery_.isEmpty();
            break;
        case rust::WorkflowOp::CaptureCopyLatestOutput:
            output = c.latest_;
            break;
        case rust::WorkflowOp::CaptureRecoveryState:
            state = {{"recovering", !c.desktopRecovery_.isEmpty()}, {"recovery_reason", c.desktopRecovery_}};
            break;
        case rust::WorkflowOp::CaptureBasicState:
            state = {{"width", c.latest_->width},
                     {"height", c.latest_->height},
                     {"fourcc", formatName(c.latest_->format)}};
            break;
        case rust::WorkflowOp::CaptureLocalBackendState:
            state["backend"] = c.config_.s("capture.local_backend");
            break;
        case rust::WorkflowOp::CaptureCardBackendState:
            state["backend"] = c.config_.s("capture.backend");
            break;
        case rust::WorkflowOp::CaptureIsDxgi:
            out[0] = c.config_.s("capture.local_backend") == "DXGI";
            break;
        case rust::WorkflowOp::CaptureNegotiatedRequest:
            state["requested_fps"] = c.negotiatedFps_;
            break;
        case rust::WorkflowOp::CaptureLocalRequest:
            state["requested_fps"] = c.config_.d("capture.local_fps");
            break;
        case rust::WorkflowOp::CaptureCardRequest:
            state["requested_fps"] = c.config_.d("capture.fps");
            break;
        case rust::WorkflowOp::CaptureRemainingState:
            state["negotiated_fps"] = c.negotiatedFps_;
            state["unchanged_polls"] = double(c.unchangedPolls_);
            state["recovering"] = !c.desktopRecovery_.isEmpty();
            state["recovery_reason"] = c.desktopRecovery_;
            break;
        case rust::WorkflowOp::CaptureArrivalRecent:
            out[0] = rust::okww_rs_capture_recent(capture_flow_test::clock(), c.arrivals_.back());
            break;
        case rust::WorkflowOp::CaptureFpsValue:
            out[0] = rust::okww_rs_capture_fps(c.arrivals_.size(), c.arrivals_.back(), c.arrivals_.front());
            break;
        case rust::WorkflowOp::CaptureWaitNext:
            c.cv_.wait_for(*lock, std::chrono::milliseconds(int(a[0])),
                           [this] { return run(rust::WorkflowMethod::CaptureNextReady)[0] != 0; });
            break;
        case rust::WorkflowOp::CaptureNewSequence:
            out[0] = c.latest_->sequence != after;
            break;
        case rust::WorkflowOp::CaptureCreateCom:
            com.emplace();
            break;
        case rust::WorkflowOp::CaptureCheckCom:
            hr(com->result, "初始化采集 COM 失败");
            break;
        case rust::WorkflowOp::CaptureDescribeThread:
            capture_flow_test::SetThreadDescription(capture_flow_test::GetCurrentThread(), L"OKWW Capture");
            break;
        case rust::WorkflowOp::CaptureIsDshow:
            out[0] = c.config_.s("capture.backend") == "DSHOW";
            break;
        case rust::WorkflowOp::CaptureRunDesktop:
            c.runDesktop();
            break;
        case rust::WorkflowOp::CaptureRunDs:
            c.runDs();
            break;
        case rust::WorkflowOp::CaptureRunMf:
            c.runMf();
            break;
        case rust::WorkflowOp::CaptureDropCom:
            com.reset();
            break;
        case rust::WorkflowOp::CaptureCaughtError:
            try {
                std::rethrow_exception(error);
            } catch (const std::exception &e) {
                c.error_ = errorText(e);
            }
            break;
        case rust::WorkflowOp::CaptureReady:
            out[0] = c.ready_;
            break;
        case rust::WorkflowOp::CaptureFlowFailure:
            switch (int(a[0])) {
            case 0:
                fail("采集线程已经启动");
            case 1:
                fail("视频采集启动超时");
            case 2:
                fail(c.error_);
            default:
                fail("Rust 采集流程参数无效");
            }
            break;
        default:
            fail("Rust 采集帧与线程宿主无效");
        }
    }
    std::array<double, 3> run(rust::WorkflowMethod method, std::span<const double> args = {}) {
        return rust::platformWorkflowRun(method, args, *this);
    }
};

struct Current : State {
    using State::State;
    void publish(const uint8_t *, size_t, int, int, int, PixelFormat, bool, i64, bool = false);
    void verifyUnchangedDesktop(i64);
    void start();
    void stop();
    QString error();
    void desktopRecovery(QString);
    bool recovering();
    FramePtr latest();
    QJsonObject actual();
    double fps();
    FramePtr next(uint64_t);
    void run();
};
void Current::publish(const uint8_t *data, size_t length, int w, int h, int stride, PixelFormat fmt,
                      bool bottom, i64 stamp, bool compareContents) {
    CaptureFlowSdk sdk{*this};
    sdk.data = data;
    sdk.length = length;
    sdk.w = w;
    sdk.h = h;
    sdk.stride = stride;
    sdk.fmt = fmt;
    sdk.bottom = bottom;
    sdk.stamp = stamp;
    const std::array<double, 6> args{double(w),        double(h),      double(stride),
                                     double(int(fmt)), double(bottom), double(compareContents)};
    sdk.run(rust::WorkflowMethod::CapturePublish, args);
}
void Current::verifyUnchangedDesktop(i64 stamp) {
    CaptureFlowSdk sdk{*this};
    sdk.stamp = stamp;
    sdk.run(rust::WorkflowMethod::CaptureVerifyUnchanged);
}
void Current::start() { CaptureFlowSdk{*this}.run(rust::WorkflowMethod::CaptureStart); }
void Current::stop() { CaptureFlowSdk{*this}.run(rust::WorkflowMethod::CaptureStop); }
QString Current::error() {
    CaptureFlowSdk sdk{*this};
    sdk.run(rust::WorkflowMethod::CaptureError);
    return sdk.text;
}
void Current::desktopRecovery(QString reason) {
    CaptureFlowSdk sdk{*this};
    sdk.text = std::move(reason);
    sdk.run(rust::WorkflowMethod::CaptureDesktopRecovery);
}
bool Current::recovering() {
    return CaptureFlowSdk{*this}.run(rust::WorkflowMethod::CaptureRecovering)[0] != 0;
}
FramePtr Current::latest() {
    CaptureFlowSdk sdk{*this};
    sdk.run(rust::WorkflowMethod::CaptureLatest);
    return sdk.output;
}
QJsonObject Current::actual() {
    CaptureFlowSdk sdk{*this};
    sdk.run(rust::WorkflowMethod::CaptureActual);
    return sdk.state;
}
double Current::fps() { return CaptureFlowSdk{*this}.run(rust::WorkflowMethod::CaptureFps)[0]; }
FramePtr Current::next(uint64_t after) {
    CaptureFlowSdk sdk{*this};
    sdk.after = after;
    sdk.run(rust::WorkflowMethod::CaptureNext);
    return sdk.output;
}
void Current::run() { CaptureFlowSdk{*this}.run(rust::WorkflowMethod::CaptureRun); }

inline QJsonValue frameState(const FramePtr &frame) {
    if (!frame)
        return {};
    return QJsonObject{{"size", QJsonArray{frame->width, frame->height, frame->stride}},
                       {"format", int(frame->format)},
                       {"bottom", frame->bottomUp},
                       {"sequence", QString::number(frame->sequence)},
                       {"stamp", QString::number(frame->captured)},
                       {"bytes", QString::fromLatin1(frame->bytes.toHex())}};
}
inline QJsonObject snapshot(const State &s) {
    QJsonArray pool, pins, arrivals;
    for (const auto &f : s.pool_)
        pool.append(frameState(f));
    for (const auto &f : s.pins)
        pins.append(frameState(f));
    for (auto t : s.arrivals_)
        arrivals.append(QString::number(t));
    return {{"stopped", s.stop_.load()},
            {"ready", s.ready_},
            {"error", s.error_},
            {"recovery", s.desktopRecovery_},
            {"latest", frameState(s.latest_)},
            {"pool", pool},
            {"pins", pins},
            {"arrivals", arrivals},
            {"sequence", QString::number(s.seq_)},
            {"unchanged", QString::number(s.unchangedPolls_)},
            {"thread", s.thread_.live},
            {"locked", s.mutex_.owned}};
}
template <class Capture> QJsonObject outcome(int scene, int method) {
    Capture object(scene);
    QJsonObject result;
    try {
        switch (method) {
        case 0: {
            const QByteArray bytes(96, '\x80');
            const int formats[]{0, 1, 2, 3, 4, 5, 6, 7};
            const int format = scene % 6 == 0 ? 2 : formats[scene % 8];
            const int stride = format == 0                                   ? 12
                               : format == 1                                 ? 16
                               : (format == 3 || format == 4 || format == 5) ? 8
                                                                             : 4;
            const size_t length = scene % 31 == 0   ? size_t(0x100000001ULL)
                                  : scene % 23 == 0 ? 1
                                  : scene % 6 == 0  ? 24
                                                    : 96;
            object.publish(reinterpret_cast<const uint8_t *>(bytes.constData()), length, 4, 4, stride,
                           PixelFormat(format), false, 100000000000000001LL + scene, scene % 2 == 0);
            break;
        }
        case 1:
            object.verifyUnchangedDesktop(100000000000000001LL + scene);
            break;
        case 2:
            object.start();
            break;
        case 3:
            object.stop();
            break;
        case 4:
            result["return"] = object.error();
            break;
        case 5:
            object.desktopRecovery(scene % 2 ? QString("next recovery") : QString{});
            break;
        case 6:
            result["return"] = object.recovering();
            break;
        case 7:
            result["return"] = frameState(object.latest());
            break;
        case 8:
            result["return"] = object.actual();
            break;
        case 9:
            result["return"] = object.fps();
            break;
        case 10:
            result["return"] = frameState(object.next((uint64_t(1) << 54) + (scene % 3 ? 7 : 8)));
            break;
        case 11:
            object.run();
            break;
        }
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    } catch (...) {
        result["error"] = "unknown";
    }
    result["state"] = snapshot(object);
    result["trace"] = object.trace;
    return result;
}
} // namespace okww::capture_flow_test
