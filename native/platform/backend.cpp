#include "backend.hpp"
#include "backend_pixels_abi.hpp"
#include "backend_state_abi.hpp"
#include "platform_workflow.hpp"
#include "rust_core.hpp"
#include <optional>
namespace okww {

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
            out[0] = WSAStartup(MAKEWORD(2, 2), &b.wsa_) != 0;
            break;
        case rust::WorkflowOp::BackendCreateToken:
            b.token_ = QUuid::createUuid().toString(QUuid::WithoutBraces) +
                       QUuid::createUuid().toString(QUuid::WithoutBraces);
            break;
        case rust::WorkflowOp::BackendCreateCompatibility:
            out[0] = compatibility;
            break;
        case rust::WorkflowOp::BackendCreateMappingName:
            b.mappingName_ = "Local\\OKWW.Native." + QUuid::createUuid().toString(QUuid::WithoutBraces);
            break;
        case rust::WorkflowOp::BackendCreateMapping:
            b.mapping_ =
                CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, Backend::mappingCapacity,
                                   reinterpret_cast<LPCWSTR>(b.mappingName_.utf16()));
            break;
        case rust::WorkflowOp::BackendLifecycleHasMapping:
            out[0] = b.mapping_ != nullptr;
            break;
        case rust::WorkflowOp::BackendCreateMapView:
            b.mapped_ = MapViewOfFile(b.mapping_, FILE_MAP_WRITE, 0, 0, Backend::mappingCapacity);
            break;
        case rust::WorkflowOp::BackendLifecycleHasMapped:
            out[0] = b.mapped_ != nullptr;
            break;
        case rust::WorkflowOp::BackendCreateSocket:
            b.listen_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
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
            out[0] = bind(b.listen_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0;
            break;
        case rust::WorkflowOp::BackendCreateListen:
            out[0] = ::listen(b.listen_, 1) == 0;
            break;
        case rust::WorkflowOp::BackendCreateSocketName: {
            int length = sizeof(address);
            getsockname(b.listen_, reinterpret_cast<sockaddr *>(&address), &length);
            break;
        }
        case rust::WorkflowOp::BackendCreatePort:
            b.port_ = ntohs(address.sin_port);
            break;
        case rust::WorkflowOp::BackendCreateServerThread:
            b.server_ = std::thread(&Backend::serve, &b);
            break;
        case rust::WorkflowOp::BackendCreateMonitorThread:
            b.monitor_ = std::thread(&Backend::monitor, &b);
            break;
        case rust::WorkflowOp::BackendDestroySetExit:
            b.exit_ = true;
            break;
        case rust::WorkflowOp::BackendDestroyShutdown:
            shutdown(b.listen_, SD_BOTH);
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
            closesocket(b.listen_);
            break;
        case rust::WorkflowOp::BackendDestroyUnmap:
            UnmapViewOfFile(b.mapped_);
            break;
        case rust::WorkflowOp::BackendDestroyCloseMapping:
            CloseHandle(b.mapping_);
            break;
        case rust::WorkflowOp::BackendDestroyWsa:
            WSACleanup();
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
            b.capture_ = std::make_unique<Capture>(b.cfg_);
            break;
        case rust::WorkflowOp::BackendStartCapture:
            b.capture_->start();
            break;
        case rust::WorkflowOp::BackendStartCreateBox:
            b.box_ = std::make_unique<Controller>(b.cfg_);
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
            b.physicalKeys_ = std::make_unique<PhysicalKeys>();
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
    std::optional<std::unique_lock<std::recursive_mutex>> lock;
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
                             {"timestamp", QDateTime::currentDateTime().toString(Qt::ISODateWithMs)},
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

static void backendControlFailure(int kind) {
    switch (kind) {
    case 0:
        fail("请先启动采集与盒子连接线程");
    case 1:
        fail("请先连接键鼠盒子，再测试移动");
    default:
        fail("Rust 后台操作错误宿主无效");
    }
}
struct BackendControlSdk {
    Backend &b;
    bool enabled = false;
    int vk = 0, x = 0, y = 0;
    FramePtr frame;
    std::exception_ptr moveError;
    QJsonObject value;
    void operator()(rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &error) {
        switch (op) {
        case rust::WorkflowOp::BackendControlEnabled:
            out[0] = enabled;
            break;
        case rust::WorkflowOp::BackendControlReadFrame:
            frame = b.frameLocked();
            break;
        case rust::WorkflowOp::BackendControlFramePresent:
            out[0] = bool(frame);
            break;
        case rust::WorkflowOp::BackendControlDropFrame:
            frame.reset();
            break;
        case rust::WorkflowOp::BackendControlHasBox:
            out[0] = bool(b.box_);
            break;
        case rust::WorkflowOp::BackendControlBoxConnected:
            out[0] = b.box_->connected();
            break;
        case rust::WorkflowOp::BackendControlBoxKeyboard:
            out[0] = b.box_->state()["keyboard"].toBool();
            break;
        case rust::WorkflowOp::BackendControlCapabilityError:
            fail(b.box_->state()["capability"].toString("盒子没有可用键盘路由"));
            break;
        case rust::WorkflowOp::BackendControlHeartbeatNow:
            b.heartbeat_ = nowNs();
            break;
        case rust::WorkflowOp::BackendControlResetRecovery:
            b.captureRecoveryBegan_ = 0;
            break;
        case rust::WorkflowOp::BackendControlClearError:
            b.error_.clear();
            break;
        case rust::WorkflowOp::BackendControlClearFault:
            b.inputFault_ = {};
            break;
        case rust::WorkflowOp::BackendControlSetArmed:
            b.armed_ = a[0] != 0;
            break;
        case rust::WorkflowOp::BackendControlRelease:
            b.box_->release();
            break;
        case rust::WorkflowOp::BackendControlArmed:
            out[0] = b.armed_;
            break;
        case rust::WorkflowOp::BackendControlLocal:
            out[0] = b.cfg_.s("kmbox.protocol") == "sendinput";
            break;
        case rust::WorkflowOp::BackendControlLocalTestCounts:
            x = y = 40;
            break;
        case rust::WorkflowOp::BackendControlNetTestCounts:
            x = int(std::lround(40 * b.cfg_.d("kmbox.counts_per_pixel_x")));
            y = int(std::lround(40 * b.cfg_.d("kmbox.counts_per_pixel_y")));
            break;
        case rust::WorkflowOp::BackendControlMove:
            b.box_->move(x, y);
            break;
        case rust::WorkflowOp::BackendControlSaveMoveError:
            moveError = error;
            break;
        case rust::WorkflowOp::BackendControlMoveError:
            try {
                std::rethrow_exception(error);
            } catch (const std::exception &e) {
                b.error_ = "测试移动失败：" + errorText(e);
            }
            break;
        case rust::WorkflowOp::BackendControlRethrowMoveError:
            std::rethrow_exception(moveError);
            break;
        case rust::WorkflowOp::BackendControlMoveResult:
            value = {{"dry_run", b.cfg_.b("kmbox.dry_run")},
                     {"armed", b.armed_},
                     {"counts_x", x},
                     {"counts_y", y}};
            break;
        case rust::WorkflowOp::BackendControlHasPhysicalKeys:
            out[0] = bool(b.physicalKeys_);
            break;
        case rust::WorkflowOp::BackendControlPhysicalDown:
            out[0] = b.physicalKeys_->down(vk);
            break;
        case rust::WorkflowOp::BackendControlAsyncDown:
            out[0] = (GetAsyncKeyState(vk) & 0x8000) != 0;
            break;
        case rust::WorkflowOp::BackendControlPhysicalHotkey:
            out[0] = b.physicalKeys_->physicalHotkey(vk);
            break;
        case rust::WorkflowOp::BackendControlFailure:
            backendControlFailure(int(a[0]));
            break;
        default:
            fail("Rust 后台操作宿主无效");
        }
    }
};
void Backend::arm(bool enabled) {
    std::lock_guard lock(mutex_);
    BackendControlSdk sdk{*this};
    sdk.enabled = enabled;
    rust::platformWorkflowRun(rust::WorkflowMethod::BackendArm, {}, sdk);
}
bool Backend::heartbeat() {
    std::lock_guard lock(mutex_);
    BackendControlSdk sdk{*this};
    return rust::platformWorkflowRun(rust::WorkflowMethod::BackendHeartbeat, {}, sdk)[0] != 0;
}
QJsonObject Backend::testMove() {
    std::lock_guard lock(mutex_);
    BackendControlSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::BackendTestMove, {}, sdk);
    return sdk.value;
}
bool Backend::physicalKeyDown(int vk) {
    std::lock_guard lock(mutex_);
    BackendControlSdk sdk{*this};
    sdk.vk = vk;
    return rust::platformWorkflowRun(rust::WorkflowMethod::BackendPhysicalDown, {}, sdk)[0] != 0;
}
bool Backend::physicalHotkey(int vk) {
    std::lock_guard lock(mutex_);
    BackendControlSdk sdk{*this};
    sdk.vk = vk;
    return rust::platformWorkflowRun(rust::WorkflowMethod::BackendPhysicalHotkey, {}, sdk)[0] != 0;
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
    BackendFramesSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::BackendSnapshot, {}, sdk);
    return sdk.value;
}

struct BackendMonitorSdk {
    Backend &b;
    std::optional<std::unique_lock<std::recursive_mutex>> lock;
    void operator()(rust::WorkflowOp op, const double *, double *out) {
        switch (op) {
        case rust::WorkflowOp::BackendMonitorExit:
            out[0] = bool(b.exit_);
            break;
        case rust::WorkflowOp::BackendMonitorSleep:
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            break;
        case rust::WorkflowOp::BackendMonitorLock:
            lock.emplace(b.mutex_);
            break;
        case rust::WorkflowOp::BackendMonitorUnlock:
            lock.reset();
            break;
        case rust::WorkflowOp::BackendMonitorArmed:
            out[0] = b.armed_;
            break;
        case rust::WorkflowOp::BackendMonitorHasBox:
            out[0] = bool(b.box_);
            break;
        case rust::WorkflowOp::BackendMonitorBoxConnected:
            out[0] = b.box_->connected();
            break;
        case rust::WorkflowOp::BackendMonitorDisarmBox:
            b.disarmFaultLocked("box_disconnected", "键鼠盒子连接中断");
            break;
        case rust::WorkflowOp::BackendMonitorHeartbeatExpired:
            out[0] = rust::okww_rs_watchdog(nowNs(), true, true, b.heartbeat_) == 2;
            break;
        case rust::WorkflowOp::BackendMonitorDisarmHeartbeat:
            b.disarmFaultLocked("heartbeat_timeout", "任务心跳超过 1500 毫秒未更新");
            break;
        case rust::WorkflowOp::BackendMonitorFreshFrame:
            out[0] = bool(b.frameLocked());
            break;
        case rust::WorkflowOp::BackendMonitorRecover:
            b.recoverCaptureLocked();
            break;
        default:
            fail("Rust 后台监测宿主无效");
        }
    }
};
void Backend::monitor() {
    BackendMonitorSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::BackendMonitor, {}, sdk);
}

static void backendRpcFailure(int kind) {
    switch (kind) {
    case 0:
        fail("兼容引擎身份校验失败");
    case 1:
        fail("采集帧超出共享内存大小");
    case 2:
        fail("盒子未连接");
    default:
        fail("Rust 兼容引擎错误宿主无效");
    }
}
struct BackendDispatchSdk {
    Backend &b;
    const QJsonObject &q;
    QString command;
    FramePtr frame;
    QJsonObject value;
    void operator()(rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &error) {
        switch (op) {
        case rust::WorkflowOp::BackendRpcAuthenticated:
            out[0] = q["token"].toString() == b.token_;
            break;
        case rust::WorkflowOp::BackendRpcCommand:
            command = q["op"].toString();
            out[0] = QStringList{"heartbeat", "state",  "connect", "frame",    "release", "probe",
                                 "key",       "button", "move",    "absolute", "wheel"}
                         .indexOf(command);
            break;
        case rust::WorkflowOp::BackendRpcHeartbeat:
            b.heartbeat_ = nowNs();
            break;
        case rust::WorkflowOp::BackendRpcSnapshot:
            value = b.snapshot();
            break;
        case rust::WorkflowOp::BackendRpcReadFrame:
            frame = b.frameLocked();
            break;
        case rust::WorkflowOp::BackendRpcFramePresent:
            out[0] = bool(frame);
            break;
        case rust::WorkflowOp::BackendRpcFrameUnavailable:
            value = {{"available", false}};
            break;
        case rust::WorkflowOp::BackendRpcFrameFitsMapping:
            out[0] = frame->bytes.size() <= Backend::mappingCapacity;
            break;
        case rust::WorkflowOp::BackendRpcCopyFrame:
            memcpy(b.mapped_, frame->bytes.constData(), size_t(frame->bytes.size()));
            break;
        case rust::WorkflowOp::BackendRpcFrameResult:
            value = {{"available", true},
                     {"size", int(frame->bytes.size())},
                     {"width", frame->width},
                     {"height", frame->height},
                     {"stride", frame->stride},
                     {"format", formatName(frame->format)},
                     {"bottom_up", frame->bottomUp},
                     {"sequence", QString::number(frame->sequence)},
                     {"age_ms", double(nowNs() - frame->captured) / 1e6}};
            break;
        case rust::WorkflowOp::BackendRpcHasBox:
            out[0] = bool(b.box_);
            break;
        case rust::WorkflowOp::BackendRpcRelease:
            b.box_->release();
            break;
        case rust::WorkflowOp::BackendRpcProbe:
            b.box_->probe();
            break;
        case rust::WorkflowOp::BackendRpcDown:
            out[0] = q["down"].toBool();
            break;
        case rust::WorkflowOp::BackendRpcBoxConnected:
            out[0] = b.box_->connected();
            break;
        case rust::WorkflowOp::BackendRpcKeyUp:
            b.box_->key(q["code"].toInt(), false);
            break;
        case rust::WorkflowOp::BackendRpcButtonUp:
            b.box_->button(q["name"].toString(), false);
            break;
        case rust::WorkflowOp::BackendRpcBoxState:
            value = b.box_->state();
            break;
        case rust::WorkflowOp::BackendRpcArmed:
            out[0] = b.armed_;
            break;
        case rust::WorkflowOp::BackendRpcDisarmBox:
            b.disarmFaultLocked("box_disconnected", "键鼠盒子连接中断");
            break;
        case rust::WorkflowOp::BackendRpcFreshFrame:
            out[0] = bool(b.frameLocked());
            break;
        case rust::WorkflowOp::BackendRpcRecoverFrame:
            out[0] = b.recoverCaptureLocked();
            break;
        case rust::WorkflowOp::BackendRpcWaitFrame:
            throw LocalInputBlocked("等待采集画面自动恢复");
        case rust::WorkflowOp::BackendRpcPausedError:
            fail(b.error_.isEmpty() ? "C++ 输入通道已暂停，请先启动自动任务" : b.error_);
            break;
        case rust::WorkflowOp::BackendRpcMove:
            b.box_->move(q["x"].toInt(), q["y"].toInt());
            break;
        case rust::WorkflowOp::BackendRpcAbsolute:
            b.box_->absolute(q["x"].toInt(), q["y"].toInt(), q["width"].toInt(), q["height"].toInt());
            break;
        case rust::WorkflowOp::BackendRpcButton:
            b.box_->button(q["name"].toString(), q["down"].toBool());
            break;
        case rust::WorkflowOp::BackendRpcKey:
            b.box_->key(q["code"].toInt(), q["down"].toBool());
            break;
        case rust::WorkflowOp::BackendRpcWheel:
            b.box_->wheel(q["steps"].toInt());
            break;
        case rust::WorkflowOp::BackendRpcUnknownCommand:
            fail("未知的原生输入操作");
            break;
        case rust::WorkflowOp::BackendRpcErrorIsLocalBlocked:
            try {
                std::rethrow_exception(error);
            } catch (const LocalInputBlocked &) {
                out[0] = 1;
            } catch (...) {
                out[0] = 0;
            }
            break;
        case rust::WorkflowOp::BackendRpcLocalProtocol:
            out[0] = b.cfg_.s("kmbox.protocol") == "sendinput";
            break;
        case rust::WorkflowOp::BackendRpcDisarmLocalError:
            try {
                std::rethrow_exception(error);
            } catch (const std::exception &e) {
                b.disarmFaultLocked("local_input_error", "本机输入异常：" + errorText(e));
            }
            break;
        case rust::WorkflowOp::BackendRpcDisarmTransportError:
            try {
                std::rethrow_exception(error);
            } catch (const std::exception &e) {
                b.disarmFaultLocked("input_transport_error", "键鼠盒子通信失败：" + errorText(e));
            }
            break;
        case rust::WorkflowOp::BackendRpcFailure:
            backendRpcFailure(int(a[0]));
            break;
        default:
            fail("Rust 兼容引擎命令宿主无效");
        }
    }
};
QJsonObject Backend::dispatch(const QJsonObject &q) {
    std::lock_guard lock(mutex_);
    BackendDispatchSdk sdk{*this, q};
    rust::platformWorkflowRun(rust::WorkflowMethod::BackendDispatch, {}, sdk);
    return sdk.value;
}

struct BackendServeSdk {
    Backend &b;
    SOCKET client = INVALID_SOCKET;
    QByteArray buffered;
    char bytes[8192];
    int received = 0;
    QByteArray line;
    QJsonObject reply;
    QByteArray output;
    int sent = 0;
    QJsonParseError parseError;
    QJsonDocument document;
    void operator()(rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &error) {
        switch (op) {
        case rust::WorkflowOp::BackendServeExit:
            out[0] = bool(b.exit_);
            break;
        case rust::WorkflowOp::BackendServeReadable: {
            fd_set set;
            FD_ZERO(&set);
            FD_SET(b.listen_, &set);
            timeval wait{0, 50000};
            out[0] = select(0, &set, nullptr, nullptr, &wait) > 0;
            break;
        }
        case rust::WorkflowOp::BackendServeAccept:
            client = accept(b.listen_, nullptr, nullptr);
            break;
        case rust::WorkflowOp::BackendServeClientValid:
            out[0] = client != INVALID_SOCKET;
            break;
        case rust::WorkflowOp::BackendServeReceiveTimeout: {
            DWORD timeout = 100;
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char *>(&timeout), sizeof(timeout));
            break;
        }
        case rust::WorkflowOp::BackendServeSendTimeout: {
            DWORD timeout = 1000;
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char *>(&timeout), sizeof(timeout));
            break;
        }
        case rust::WorkflowOp::BackendServeNoDelay: {
            BOOL yes = TRUE;
            setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char *>(&yes), sizeof(yes));
            break;
        }
        case rust::WorkflowOp::BackendServeReceive:
            received = recv(client, bytes, sizeof(bytes), 0);
            out[0] = received;
            break;
        case rust::WorkflowOp::BackendServeTimedOut:
            out[0] = WSAGetLastError() == WSAETIMEDOUT;
            break;
        case rust::WorkflowOp::BackendServeAppendBuffer:
            buffered.append(bytes, received);
            break;
        case rust::WorkflowOp::BackendServeBufferSize:
            out[0] = double(buffered.size());
            break;
        case rust::WorkflowOp::BackendServeNewline:
            out[0] = int(buffered.indexOf('\n'));
            break;
        case rust::WorkflowOp::BackendServeReadLine:
            line = buffered.left(int(a[0]));
            break;
        case rust::WorkflowOp::BackendServeRemoveLine:
            buffered.remove(0, int(a[0]) + 1);
            break;
        case rust::WorkflowOp::BackendServeParseDocument:
            parseError = {};
            document = QJsonDocument::fromJson(line, &parseError);
            break;
        case rust::WorkflowOp::BackendServeParseValid:
            out[0] = parseError.error == QJsonParseError::NoError;
            break;
        case rust::WorkflowOp::BackendServeDocumentObject:
            out[0] = document.isObject();
            break;
        case rust::WorkflowOp::BackendServeInvalidMessage:
            fail("无效 IPC 消息");
            break;
        case rust::WorkflowOp::BackendServeSuccessReply:
            reply = {{"ok", true}, {"result", b.dispatch(document.object())}};
            break;
        case rust::WorkflowOp::BackendServeDropDocument:
            document = QJsonDocument{};
            break;
        case rust::WorkflowOp::BackendServeErrorReply:
            try {
                std::rethrow_exception(error);
            } catch (const std::exception &e) {
                reply = {{"ok", false}, {"error", errorText(e)}};
            }
            break;
        case rust::WorkflowOp::BackendServeEncodeReply:
            output = QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n';
            break;
        case rust::WorkflowOp::BackendServeResetSent:
            sent = 0;
            break;
        case rust::WorkflowOp::BackendServeHasUnsent:
            out[0] = sent < output.size();
            break;
        case rust::WorkflowOp::BackendServeSend:
            out[0] = send(client, output.constData() + sent, int(output.size()) - sent, 0);
            break;
        case rust::WorkflowOp::BackendServeAdvanceSent:
            sent += int(a[0]);
            break;
        case rust::WorkflowOp::BackendServeSendComplete:
            out[0] = sent == output.size();
            break;
        case rust::WorkflowOp::BackendServeShutdownClient:
            shutdown(client, SD_BOTH);
            break;
        case rust::WorkflowOp::BackendServeDropLineScope:
            output = QByteArray{};
            reply = QJsonObject{};
            line = QByteArray{};
            break;
        case rust::WorkflowOp::BackendServeCloseClient:
            closesocket(client);
            break;
        case rust::WorkflowOp::BackendServeDisarm:
            b.arm(false);
            break;
        case rust::WorkflowOp::BackendServeDropBuffer:
            buffered = QByteArray{};
            break;
        default:
            fail("Rust 本机通信服务宿主无效");
        }
    }
};
void Backend::serve() {
    BackendServeSdk sdk{*this};
    rust::platformWorkflowRun(rust::WorkflowMethod::BackendServe, {}, sdk);
}

} // namespace okww
