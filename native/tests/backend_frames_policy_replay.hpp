#pragma once
#include "backend.hpp"
#include "backend_state_abi.hpp"
#include "platform_workflow.hpp"
#include "rust_core.hpp"
#include <optional>
namespace okww::backend_frames_test {
struct State {
    int scene;
    i64 clock = INT64_C(0x123456789abcde00);
    QJsonArray trace;
    FramePtr frame;
    bool held = false;
    explicit State(int s) : scene(s) {
        if (s % 31 == 0)
            clock = 0;
        held = s % 2 != 0;
    }
};
inline thread_local State *active = nullptr;
inline i64 nowNs() {
    const i64 now = active->clock;
    active->clock += 1000000;
    active->trace.append(QJsonObject{{"clock", QString::number(now)}});
    return now;
}
inline QDateTime wallClock() {
    active->trace.append("wall-clock");
    return QDateTime::fromSecsSinceEpoch(1234567890);
}
struct Mutex {
    std::recursive_mutex value;
    void lock() {
        active->trace.append("lock");
        value.lock();
    }
    void unlock() {
        active->trace.append("unlock");
        value.unlock();
    }
};
struct CaptureDevice {
    bool recovering() {
        active->trace.append("capture-recovering");
        return active->scene % 13 == 0;
    }
    FramePtr latest() {
        active->trace.append("capture-latest");
        return active->scene % 17 ? active->frame : nullptr;
    }
    bool stopped() {
        active->trace.append("capture-stopped");
        return active->scene % 19 == 0;
    }
    QString error() {
        active->trace.append("capture-error");
        return active->scene % 5 ? QString{} : QString("signal fixture");
    }
    QJsonObject actual() {
        active->trace.append("capture-actual");
        if (active->scene % 43 == 0)
            throw std::runtime_error("actual fixture");
        return {{"width", 8},
                {"height", 4},
                {"backend", "DSHOW"},
                {"fourcc", "NV12"},
                {"recovery_reason", active->scene % 7 ? "" : "driver recovering"}};
    }
    double fps() {
        active->trace.append("capture-fps");
        return 120.5;
    }
};
struct Box {
    QJsonObject state() {
        active->trace.append("box-state");
        if (active->scene % 37 == 0)
            throw std::runtime_error("state fixture");
        QJsonArray keys;
        if (active->held)
            keys.append(4);
        return {{"connected", true},
                {"keys", keys},
                {"buttons", active->held ? 1 : 0},
                {"modifiers", active->held ? 8 : 0}};
    }
    void release() {
        active->trace.append("box-release");
        if (active->scene % 23 == 0)
            throw std::runtime_error("release fixture");
        if (active->scene % 41 == 0)
            throw 27;
        active->held = false;
    }
};
struct BackendState {
    mutable Mutex mutex_;
    Config cfg_ = Config::defaults();
    std::unique_ptr<CaptureDevice> capture_;
    std::unique_ptr<Box> box_;
    bool running_ = false, simulation_ = false, armed_ = true;
    QString error_;
    QJsonObject inputFault_;
    i64 heartbeat_ = 0, captureRecoveryBegan_ = 0;
    FramePtr simulated_;
};
#define BACKEND_FRAMES_FIXTURE_CLASS                                                                         \
    class Backend : public BackendState {                                                                    \
      public:                                                                                                \
        static constexpr i64 captureRecoveryTimeoutNs = 120000000000LL;                                      \
        FramePtr frameLocked();                                                                              \
        FramePtr rawFrameLocked();                                                                           \
        bool recoverCaptureLocked();                                                                         \
        void disarmFaultLocked(const QString &, const QString &);                                            \
        QImage preview(QSize, uint64_t &);                                                                   \
        QJsonObject snapshot();                                                                              \
    };
namespace original {
BACKEND_FRAMES_FIXTURE_CLASS
FramePtr Backend::frameLocked() {
    if (capture_ && capture_->recovering())
        return nullptr;
    auto f = rawFrameLocked();
    if (!f || nowNs() - f->captured > cfg_.d("capture.stale_after") * 1e9)
        return nullptr;
    captureRecoveryBegan_ = 0;
    return f;
}
FramePtr Backend::rawFrameLocked() {
    if (!running_)
        return nullptr;
    if (simulation_ && simulated_) {
        auto f = std::make_shared<Frame>(*simulated_);
        f->captured = nowNs();
        f->sequence = uint64_t(f->captured / 16666667);
        return f;
    }
    return capture_ ? capture_->latest() : nullptr;
}
bool Backend::recoverCaptureLocked() {
    // A freshness miss blocks new input, but does not mean the capture thread
    // died. Cards and desktop capture share the same bounded recovery path.
    if (!capture_ || capture_->stopped()) {
        const auto detail = capture_ ? capture_->error() : QString{};
        disarmFaultLocked("capture_failed",
                          "采集线程已停止" + (detail.isEmpty() ? QString{} : "：" + detail));
        return false;
    }
    if (!captureRecoveryBegan_)
        captureRecoveryBegan_ = nowNs();
    if (nowNs() - captureRecoveryBegan_ >= captureRecoveryTimeoutNs) {
        disarmFaultLocked("capture_recovery_timeout", "等待采集恢复超过 120 秒，仍没有新鲜画面");
        return false;
    }
    try {
        const auto state = box_->state();
        if (!state["keys"].toArray().isEmpty() || state["modifiers"].toInt() || state["buttons"].toInt())
            box_->release();
    } catch (const std::exception &error) {
        disarmFaultLocked("input_transport_error", errorText(error));
        return false;
    }
    return true;
}
void Backend::disarmFaultLocked(const QString &reason, const QString &message) {
    // Capture the first cause before releasing controls or a later frame can
    // replace the evidence. Ordinary Pause/cleanup must not erase it.
    const auto frame = rawFrameLocked();
    const auto box = box_ ? box_->state() : QJsonObject{};
    if (inputFault_.isEmpty()) {
        inputFault_ = {{"reason", reason},
                       {"timestamp", backend_frames_test::wallClock().toString(Qt::ISODateWithMs)},
                       {"heartbeat_age_ms", heartbeat_ ? double(nowNs() - heartbeat_) / 1e6 : -1.},
                       {"frame_age_ms", frame ? double(nowNs() - frame->captured) / 1e6 : -1.},
                       {"frame_sequence", frame ? QString::number(frame->sequence) : QString{}},
                       {"stale_after_ms", cfg_.d("capture.stale_after") * 1000},
                       {"recovery_elapsed_ms",
                        captureRecoveryBegan_ ? double(nowNs() - captureRecoveryBegan_) / 1e6 : 0.},
                       {"capture_error", capture_ ? capture_->error() : QString{}},
                       {"box_connected", box["connected"]},
                       {"keys", box["keys"]},
                       {"buttons", box["buttons"]},
                       {"modifiers", box["modifiers"]}};
        error_ = "任务已暂停：" + message;
    }
    armed_ = false;
    if (box_) {
        try {
            box_->release();
        } catch (const std::exception &error) {
            inputFault_["release_error"] = errorText(error);
        }
    }
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
QJsonObject Backend::snapshot() {
    std::lock_guard lock(mutex_);
    auto f = frameLocked();
    QJsonObject actual = capture_ ? capture_->actual() : QJsonObject{};
    if (capture_ && !f) {
        const bool recovering = running_ && !capture_->stopped() &&
                                inputFault_.value("reason").toString() != "capture_recovery_timeout";
        actual["recovering"] = recovering;
        if (recovering && actual["recovery_reason"].toString().isEmpty())
            actual["recovery_reason"] = "等待新采集帧，恢复后继续当前任务";
    }
    actual["recovery_elapsed_ms"] =
        captureRecoveryBegan_ ? double(nowNs() - captureRecoveryBegan_) / 1e6 : 0.;
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
            {"input_fault", inputFault_},
            {"age_ms", f ? double(nowNs() - f->captured) / 1e6 : -1.0},
            {"simulation", simulation_}};
}
} // namespace original
namespace current {
BACKEND_FRAMES_FIXTURE_CLASS
struct BackendFramesSdk {
    Backend &b;
    FramePtr frame;
    std::shared_ptr<Frame> mutableFrame;
    QJsonObject box, actual, value;
    QString detail;
    const QString *reason = nullptr;
    const QString *message = nullptr;
    QSize size;
    uint64_t *after = nullptr;
    QImage image;
    std::optional<std::unique_lock<Mutex>> lock;
    void operator()(rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &error) {
        switch (op) {
        case rust::WorkflowOp::BackendFrameHasCapture:
            out[0] = bool(b.capture_);
            break;
        case rust::WorkflowOp::BackendFrameCaptureRecovering:
            out[0] = b.capture_->recovering();
            break;
        case rust::WorkflowOp::BackendFrameReadRaw:
            frame = b.rawFrameLocked();
            break;
        case rust::WorkflowOp::BackendFramePresent:
            out[0] = bool(frame);
            break;
        case rust::WorkflowOp::BackendFrameFresh:
            out[0] =
                rust::okww_rs_frame_fresh(nowNs(), frame->captured, true, b.cfg_.d("capture.stale_after"));
            break;
        case rust::WorkflowOp::BackendFrameClear:
            frame.reset();
            break;
        case rust::WorkflowOp::BackendFrameResetRecovery:
            b.captureRecoveryBegan_ = 0;
            break;
        case rust::WorkflowOp::BackendFrameRunning:
            out[0] = b.running_;
            break;
        case rust::WorkflowOp::BackendFrameSimulation:
            out[0] = b.simulation_;
            break;
        case rust::WorkflowOp::BackendFrameHasSimulated:
            out[0] = bool(b.simulated_);
            break;
        case rust::WorkflowOp::BackendFrameCloneSimulated:
            mutableFrame = std::make_shared<Frame>(*b.simulated_);
            break;
        case rust::WorkflowOp::BackendFrameStampSimulated:
            mutableFrame->captured = nowNs();
            break;
        case rust::WorkflowOp::BackendFrameSequenceSimulated:
            mutableFrame->sequence = rust::okww_rs_backend_simulation_sequence(mutableFrame->captured);
            break;
        case rust::WorkflowOp::BackendFrameStoreSimulated:
            frame = mutableFrame;
            mutableFrame.reset();
            break;
        case rust::WorkflowOp::BackendFrameCaptureLatest:
            frame = b.capture_->latest();
            break;
        case rust::WorkflowOp::BackendFrameCaptureStopped:
            out[0] = b.capture_->stopped();
            break;
        case rust::WorkflowOp::BackendRecoveryCaptureError:
            detail = b.capture_->error();
            break;
        case rust::WorkflowOp::BackendRecoveryDisarmFailed:
            b.disarmFaultLocked("capture_failed",
                                "采集线程已停止" + (detail.isEmpty() ? QString{} : "：" + detail));
            break;
        case rust::WorkflowOp::BackendRecoveryStarted:
            out[0] = b.captureRecoveryBegan_ != 0;
            break;
        case rust::WorkflowOp::BackendRecoveryBegin:
            b.captureRecoveryBegan_ = nowNs();
            break;
        case rust::WorkflowOp::BackendRecoveryExpired:
            out[0] = rust::okww_rs_backend_recovery_timed_out(nowNs(), b.captureRecoveryBegan_);
            break;
        case rust::WorkflowOp::BackendRecoveryDisarmTimeout:
            b.disarmFaultLocked("capture_recovery_timeout", "等待采集恢复超过 120 秒，仍没有新鲜画面");
            break;
        case rust::WorkflowOp::BackendFrameBoxState:
            box = b.box_->state();
            break;
        case rust::WorkflowOp::BackendRecoveryKeysEmpty:
            out[0] = std::as_const(box)["keys"].toArray().isEmpty();
            break;
        case rust::WorkflowOp::BackendRecoveryModifiers:
            out[0] = std::as_const(box)["modifiers"].toInt();
            break;
        case rust::WorkflowOp::BackendRecoveryButtons:
            out[0] = std::as_const(box)["buttons"].toInt();
            break;
        case rust::WorkflowOp::BackendFrameBoxRelease:
            b.box_->release();
            break;
        case rust::WorkflowOp::BackendFrameDropBoxState:
            box = {};
            break;
        case rust::WorkflowOp::BackendRecoveryDisarmInputError:
            try {
                std::rethrow_exception(error);
            } catch (const std::exception &e) {
                b.disarmFaultLocked("input_transport_error", errorText(e));
            }
            break;
        case rust::WorkflowOp::BackendFrameHasBox:
            out[0] = bool(b.box_);
            break;
        case rust::WorkflowOp::BackendFaultEmpty:
            out[0] = b.inputFault_.isEmpty();
            break;
        case rust::WorkflowOp::BackendFaultRecord:
            b.inputFault_ = {{"reason", *reason},
                             {"timestamp", backend_frames_test::wallClock().toString(Qt::ISODateWithMs)},
                             {"heartbeat_age_ms", b.heartbeat_ ? double(nowNs() - b.heartbeat_) / 1e6 : -1.},
                             {"frame_age_ms", frame ? double(nowNs() - frame->captured) / 1e6 : -1.},
                             {"frame_sequence", frame ? QString::number(frame->sequence) : QString{}},
                             {"stale_after_ms", b.cfg_.d("capture.stale_after") * 1000},
                             {"recovery_elapsed_ms",
                              b.captureRecoveryBegan_ ? double(nowNs() - b.captureRecoveryBegan_) / 1e6 : 0.},
                             {"capture_error", b.capture_ ? b.capture_->error() : QString{}},
                             {"box_connected", std::as_const(box)["connected"]},
                             {"keys", std::as_const(box)["keys"]},
                             {"buttons", std::as_const(box)["buttons"]},
                             {"modifiers", std::as_const(box)["modifiers"]}};
            break;
        case rust::WorkflowOp::BackendFaultSetError:
            b.error_ = "任务已暂停：" + *message;
            break;
        case rust::WorkflowOp::BackendFaultDisarm:
            b.armed_ = false;
            break;
        case rust::WorkflowOp::BackendFaultReleaseError:
            try {
                std::rethrow_exception(error);
            } catch (const std::exception &e) {
                b.inputFault_["release_error"] = errorText(e);
            }
            break;
        case rust::WorkflowOp::BackendFrameLock:
            lock.emplace(b.mutex_);
            break;
        case rust::WorkflowOp::BackendFrameReadFresh:
            frame = b.frameLocked();
            break;
        case rust::WorkflowOp::BackendFrameUnlock:
            lock.reset();
            break;
        case rust::WorkflowOp::BackendPreviewSameSequence:
            out[0] = frame->sequence == *after;
            break;
        case rust::WorkflowOp::BackendPreviewStoreSequence:
            *after = frame->sequence;
            break;
        case rust::WorkflowOp::BackendPreviewConvert:
            image = previewImage(*frame, size);
            break;
        case rust::WorkflowOp::BackendSnapshotActual:
            actual = b.capture_->actual();
            break;
        case rust::WorkflowOp::BackendSnapshotRecoveryTimedOut:
            out[0] = b.inputFault_.value("reason").toString() == "capture_recovery_timeout";
            break;
        case rust::WorkflowOp::BackendSnapshotSetRecovering:
            actual["recovering"] = a[0] != 0;
            break;
        case rust::WorkflowOp::BackendSnapshotRecoveryReasonEmpty:
            out[0] = actual["recovery_reason"].toString().isEmpty();
            break;
        case rust::WorkflowOp::BackendSnapshotSetRecoveryReason:
            actual["recovery_reason"] = "等待新采集帧，恢复后继续当前任务";
            break;
        case rust::WorkflowOp::BackendSnapshotRecoveryElapsed:
            actual["recovery_elapsed_ms"] =
                b.captureRecoveryBegan_ ? double(nowNs() - b.captureRecoveryBegan_) / 1e6 : 0.;
            break;
        case rust::WorkflowOp::BackendSnapshotSimulated:
            actual = {{"width", frame->width},
                      {"height", frame->height},
                      {"fourcc", "RGB3"},
                      {"backend", "演练画面"},
                      {"requested_fps", 60}};
            break;
        case rust::WorkflowOp::BackendSnapshotCaptureFps:
            actual["measured_fps"] = b.capture_->fps();
            break;
        case rust::WorkflowOp::BackendSnapshotDefaultFps:
            actual["measured_fps"] = b.simulation_ && b.running_ ? 60 : 0;
            break;
        case rust::WorkflowOp::BackendSnapshotDisconnectedBox:
            box = {{"connected", false}};
            break;
        case rust::WorkflowOp::BackendSnapshotResult:
            value = {{"running", b.running_},
                     {"dry_run", b.cfg_.b("kmbox.dry_run")},
                     {"capture_connected", bool(frame)},
                     {"armed", b.armed_},
                     {"box", box},
                     {"actual", actual},
                     {"error", b.error_.isEmpty() && b.capture_ ? b.capture_->error() : b.error_},
                     {"input_fault", b.inputFault_},
                     {"age_ms", frame ? double(nowNs() - frame->captured) / 1e6 : -1.0},
                     {"simulation", b.simulation_}};
            break;
        default:
            fail("Rust 后台画面与故障宿主无效");
        }
    }
};
FramePtr Backend::frameLocked() {
    BackendFramesSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::BackendFrame, {}, sdk);
    return sdk.frame;
}
FramePtr Backend::rawFrameLocked() {
    BackendFramesSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::BackendRawFrame, {}, sdk);
    return sdk.frame;
}
bool Backend::recoverCaptureLocked() {
    BackendFramesSdk sdk{*this};
    return rust::platformWorkflowRun(rust::WorkflowMethod::BackendRecoverCapture, {}, sdk)[0] != 0;
}
void Backend::disarmFaultLocked(const QString &reason, const QString &message) {
    BackendFramesSdk sdk{*this};
    sdk.reason = &reason;
    sdk.message = &message;
    rust::platformWorkflowRun(rust::WorkflowMethod::BackendDisarmFault, {}, sdk);
}
QImage Backend::preview(QSize size, uint64_t &after) {
    BackendFramesSdk sdk{*this};
    sdk.size = size;
    sdk.after = &after;
    rust::platformWorkflowRun(rust::WorkflowMethod::BackendPreview, {}, sdk);
    return sdk.image;
}
QJsonObject Backend::snapshot() {
    std::lock_guard lock(mutex_);
    BackendFramesSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::BackendSnapshot, {}, sdk);
    return sdk.value;
}
} // namespace current
#undef BACKEND_FRAMES_FIXTURE_CLASS
inline QJsonObject frameValue(const FramePtr &f) {
    return f ? QJsonObject{{"present", true},
                           {"stamp", QString::number(f->captured)},
                           {"sequence", QString::number(f->sequence)},
                           {"width", f->width},
                           {"height", f->height},
                           {"stride", f->stride},
                           {"hash",
                            QString::fromLatin1(
                                QCryptographicHash::hash(f->bytes, QCryptographicHash::Sha256).toHex())},
                           {"refs", int(f.use_count())}}
             : QJsonObject{{"present", false}};
}
inline QJsonObject imageValue(const QImage &image) {
    return {{"width", image.width()},
            {"height", image.height()},
            {"hash", QString::fromLatin1(QCryptographicHash::hash(
                                             QByteArrayView(reinterpret_cast<const char *>(image.constBits()),
                                                            image.sizeInBytes()),
                                             QCryptographicHash::Sha256)
                                             .toHex())}};
}
template <class B> QJsonObject run(int method) {
    B b;
    const int scene = active->scene;
    b.running_ = scene % 7 != 0;
    b.simulation_ = (scene / 3) % 2 != 0;
    if (scene % 5 != 0)
        b.capture_ = std::make_unique<CaptureDevice>();
    if (method == 2 || scene % 11 != 0)
        b.box_ = std::make_unique<Box>();
    if (scene % 3 != 0)
        b.simulated_ = active->frame;
    b.error_ = scene % 2 ? "prior error" : "";
    b.inputFault_ = scene % 4 == 0
                        ? QJsonObject{{"reason", scene % 8 ? "old fault" : "capture_recovery_timeout"}}
                        : QJsonObject{};
    b.heartbeat_ = scene % 9 ? active->clock - 123456789 : 0;
    const i64 elapsed[]{0, 119999999999, 120000000000, 120000000001};
    b.captureRecoveryBegan_ = scene % 3 ? active->clock - elapsed[(scene / 7) % 4] : 0;
    b.cfg_.set("capture.stale_after", .15);
    uint64_t after = scene % 3 == 0 ? active->frame->sequence : active->frame->sequence + 1;
    QJsonObject result;
    try {
        switch (method) {
        case 0:
            result["frame"] = frameValue(b.frameLocked());
            break;
        case 1:
            result["frame"] = frameValue(b.rawFrameLocked());
            break;
        case 2:
            result["recovered"] = b.recoverCaptureLocked();
            break;
        case 3:
            b.disarmFaultLocked("test_fault", "测试故障");
            break;
        case 4:
            result["preview"] = imageValue(b.preview(QSize(13, 7), after));
            result["after"] = QString::number(after);
            break;
        case 5:
            result["snapshot"] = b.snapshot();
            break;
        }
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    } catch (...) {
        result["error"] = "unknown";
    }
    result["armed"] = b.armed_;
    result["backend_error"] = b.error_;
    result["fault"] = b.inputFault_;
    result["recovery"] = QString::number(b.captureRecoveryBegan_);
    return result;
}
inline QJsonObject outcome(int scene, int method, bool rust) {
    State state(scene);
    active = &state;
    auto frame = std::make_shared<Frame>();
    frame->width = 8;
    frame->height = 4;
    frame->stride = 24;
    frame->format = PixelFormat::BGR24;
    frame->bytes = QByteArray(96, char(scene % 256));
    const i64 ages[]{0, 150000000, 150000001, 200000000, -1000000};
    frame->captured = state.clock - ages[(scene / 5) % 5];
    frame->sequence = scene % 43 ? UINT64_C(0xf123456789abcdef) : UINT64_MAX;
    state.frame = frame;
    frame.reset();
    auto result = rust ? run<current::Backend>(method) : run<original::Backend>(method);
    result["trace"] = state.trace;
    return result;
}
} // namespace okww::backend_frames_test
