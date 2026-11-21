#include "engine.hpp"
#include "app_info.hpp"
#include "diagnostics.hpp"
#include "engine_abi.hpp"
#include "resource_failure.hpp"
#include "scripts.hpp"
#include "task_registry.hpp"
#include <QtConcurrent>
#include <opencv2/imgcodecs.hpp>

namespace okww::runtime {
namespace {
// Thread-scoped, like the original task executor. Restore the previous request
// on every exit path; this never changes the user's power-plan settings.
class KeepAwake {
    EXECUTION_STATE previous_;

  public:
    KeepAwake()
        : previous_(SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED)) {}
    ~KeepAwake() {
        if (previous_)
            SetThreadExecutionState(previous_ | ES_CONTINUOUS);
    }
    bool active() const { return previous_ != 0; }
};
} // namespace
Engine::Engine(Backend &backend, QString root, QString data, QObject *parent)
    : QObject(parent), backend_(backend), root_(std::move(root)), data_(std::move(data)),
      profile_(data_ + "/configs-dualpc", root_ + "/native/resources/catalog.json"),
      teams_(std::make_shared<TeamStore>(root_, data_)), tasks_(builtinTasks()) {
    QString models;
    rust::engineRun(rust::EngineMethod::Initialize, [&](rust::EngineOp op, const double *a, double *out,
                                                        const std::exception_ptr &error) {
        switch (op) {
        case rust::EngineOp::ReadInitialScripts: {
            scripts_ = scriptCatalog(data_);
            break;
        }
        case rust::EngineOp::ScriptCatalogExists: {
            out[0] = QFile::exists(data_ + "/native-script-catalog.json");
            break;
        }
        case rust::EngineOp::ReadInitialTemplates: {
            scriptTemplates_ = readJson(data_ + "/native-script-catalog.json")["templates"].toArray();
            break;
        }
        case rust::EngineOp::AppendBusinessTasks: {
            tasks_ += businessTasks([](const QString &) -> bool { fail("仅供目录展示的任务不可执行"); });
            break;
        }
        case rust::EngineOp::BindLaunchTimer: {
            launchTimer_.setInterval(int(a[0]));
            connect(&launchTimer_, &QTimer::timeout, this, &Engine::tryLaunch);
            break;
        }
        case rust::EngineOp::DefaultModelPath: {
            models = root_ + "/assets/ocr/ppocrv5";
            break;
        }
        case rust::EngineOp::ModelExists: {
            out[0] = QFile::exists(models + "/det/det.onnx");
            break;
        }
        case rust::EngineOp::FallbackModelPath: {
            models = root_ + "/.venv/Lib/site-packages/onnxocr/models/ppocrv5";
            break;
        }
        case rust::EngineOp::CreateNotifications: {
            notifications_ = std::make_unique<Notifications>(models, [this](QString text) { log(text); });
            break;
        }
        case rust::EngineOp::BindExitTimer: {
            exitTimer_.setInterval(int(a[0]));
            connect(&exitTimer_, &QTimer::timeout, this, [this] {
                rust::engineRun(
                    rust::EngineMethod::ExitPoll,
                    [&](rust::EngineOp op, const double *a, double *out, const std::exception_ptr &error) {
                        switch (op) {
                        case rust::EngineOp::Paused: {
                            out[0] = paused_;
                            break;
                        }
                        case rust::EngineOp::ExitTimerStop: {
                            exitTimer_.stop();
                            break;
                        }
                        case rust::EngineOp::NotificationsIdle: {
                            out[0] = notifications_->idle();
                            break;
                        }
                        case rust::EngineOp::ExitElapsed: {
                            out[0] = double(exitWait_.elapsed());
                            break;
                        }
                        case rust::EngineOp::EmitQuit: {
                            emit message({{"event", "quit_requested"}});
                            break;
                        }
                        default:
                            fail("Rust ExitPoll 宿主操作无效");
                        }
                    });
            });
            break;
        }
        default:
            fail("Rust Initialize 宿主操作无效");
        }
    });
}
Engine::~Engine() {
    rust::engineRun(rust::EngineMethod::Destroy,
                    [&](rust::EngineOp op, const double *, double *out, const std::exception_ptr &) {
                        switch (op) {
                        case rust::EngineOp::Pause:
                            pause();
                            break;
                        case rust::EngineOp::WorkerJoinable:
                            out[0] = worker_.joinable();
                            break;
                        case rust::EngineOp::JoinWorker:
                            worker_.join();
                            break;
                        case rust::EngineOp::ModelJoinable:
                            out[0] = modelWorker_.joinable();
                            break;
                        case rust::EngineOp::JoinModel:
                            modelWorker_.join();
                            break;
                        case rust::EngineOp::NotificationsStop:
                            notifications_->stop();
                            break;
                        default:
                            fail("Rust 执行器退出宿主操作无效");
                        }
                    });
}
bool Engine::idle() const {
    return rust::engineRun(rust::EngineMethod::Idle, [&](rust::EngineOp op, const double *a, double *out,
                                                         const std::exception_ptr &error) {
               switch (op) {
               case rust::EngineOp::Paused: {
                   out[0] = paused_;
                   break;
               }
               case rust::EngineOp::WorkerFinished: {
                   out[0] = workerFinished_;
                   break;
               }
               case rust::EngineOp::TeamBusy: {
                   out[0] = teamSwitchBusy_;
                   break;
               }
               default:
                   fail("Rust Idle 宿主操作无效");
               }
           }) != 0;
}
void Engine::log(QString text) { emit message({{"event", "log"}, {"text", text}}); }
void Engine::notify(Notice notice) {
    QJsonObject config;
    rust::engineRun(rust::EngineMethod::Notify,
                    [&](rust::EngineOp op, const double *, double *out, const std::exception_ptr &) {
                        switch (op) {
                        case rust::EngineOp::LoadNotificationConfig:
                            config = profile_.group("Notification");
                            break;
                        case rust::EngineOp::SystemNotification:
                            out[0] = config["System Notification"].toBool(true);
                            break;
                        case rust::EngineOp::EmitNotification:
                            emit message({{"event", "notification"},
                                          {"title", notice.title},
                                          {"text", notice.text},
                                          {"error", notice.error}});
                            break;
                        case rust::EngineOp::SubmitNotification:
                            notifications_->submit(config, std::move(notice));
                            break;
                        default:
                            fail("Rust 通知宿主操作无效");
                        }
                    });
}
void Engine::postStatus(uint64_t generation, QString current) {
    QMetaObject::invokeMethod(
        this,
        [this, generation, current] {
            QJsonObject result;
            rust::engineRun(rust::EngineMethod::PostStatus,
                            [&](rust::EngineOp op, const double *a, double *out, const std::exception_ptr &) {
                                switch (op) {
                                case rust::EngineOp::GenerationMatches:
                                    out[0] = generation_ == generation;
                                    break;
                                case rust::EngineOp::Foreground:
                                    out[0] = bool(foreground_);
                                    break;
                                case rust::EngineOp::BuildStatus:
                                    result = status(a[0] != 0 ? current : QString{});
                                    break;
                                case rust::EngineOp::EmitStatus:
                                    emit message({{"event", "status"}, {"data", result}});
                                    break;
                                default:
                                    fail("Rust 状态发布宿主操作无效");
                                }
                            });
        },
        Qt::QueuedConnection);
}
QJsonObject Engine::catalog() const {
    QJsonObject result, entry, script;
    QJsonArray input, tasks;
    const TaskDefinition *native = nullptr;
    rust::engineRun(rust::EngineMethod::Catalog,
                    [&](rust::EngineOp op, const double *a, double *out, const std::exception_ptr &) {
                        switch (op) {
                        case rust::EngineOp::CatalogProfileLoad:
                            result = profile_.catalog();
                            input = result["tasks"].toArray();
                            break;
                        case rust::EngineOp::CatalogCount:
                            out[0] = double(input.size());
                            break;
                        case rust::EngineOp::SelectCatalog:
                            entry = input.at(int(a[0])).toObject();
                            break;
                        case rust::EngineOp::NativeCount:
                            out[0] = double(tasks_.size());
                            break;
                        case rust::EngineOp::NativeSelect:
                            native = &tasks_.at(int(a[0]));
                            break;
                        case rust::EngineOp::NativeMatches:
                            out[0] = native->id == entry["id"].toString();
                            break;
                        case rust::EngineOp::CatalogSetSupported:
                            entry["native_supported"] = a[0] != 0;
                            break;
                        case rust::EngineOp::CatalogUnsupportedDescription:
                            entry["description"] = entry["description"].toString() + "\n此版本不支持该任务。";
                            break;
                        case rust::EngineOp::CatalogUnsupportedName:
                            entry["name"] = entry["name"].toString() + "（暂不支持）";
                            break;
                        case rust::EngineOp::CatalogAppendTask:
                            tasks.append(entry);
                            break;
                        case rust::EngineOp::ScriptsCount:
                            out[0] = double(scripts_.size());
                            break;
                        case rust::EngineOp::SelectScriptEntry:
                            script = scripts_.at(int(a[0])).toObject();
                            break;
                        case rust::EngineOp::ScriptRemoveSource:
                            script.remove("source");
                            break;
                        case rust::EngineOp::CatalogAppendScript:
                            tasks.append(script);
                            break;
                        case rust::EngineOp::CatalogStoreTasks:
                            result["tasks"] = tasks;
                            break;
                        case rust::EngineOp::CatalogStoreScripts:
                            result["scripts"] = scripts_;
                            break;
                        case rust::EngineOp::CatalogStoreTemplates:
                            result["script_templates"] = scriptTemplates_;
                            break;
                        case rust::EngineOp::TeamBusy:
                            out[0] = teamSwitchBusy_;
                            break;
                        case rust::EngineOp::CatalogSnapshotTeams:
                            result["teams"] = teamSwitchSnapshot_;
                            break;
                        case rust::EngineOp::CatalogLiveTeams:
                            result["teams"] = teams_->catalog();
                            break;
                        default:
                            fail("Rust 任务目录宿主操作无效");
                        }
                    });
    return result;
}
QJsonObject Engine::status(QString current) const {
    QJsonArray input, states;
    QJsonObject entry, script, result;
    rust::engineRun(rust::EngineMethod::Status, [&](rust::EngineOp op, const double *a, double *out,
                                                    const std::exception_ptr &) {
        switch (op) {
        case rust::EngineOp::ProfileCatalogLoad:
            input = profile_.catalog()["tasks"].toArray();
            break;
        case rust::EngineOp::CatalogCount:
            out[0] = double(input.size());
            break;
        case rust::EngineOp::SelectCatalog:
            entry = input.at(int(a[0])).toObject();
            break;
        case rust::EngineOp::AppendCatalogState:
            states.append(
                QJsonObject{{"id", entry["id"]}, {"enabled", entry["enabled"]}, {"paused", bool(paused_)}});
            break;
        case rust::EngineOp::ScriptsCount:
            out[0] = double(scripts_.size());
            break;
        case rust::EngineOp::SelectScriptEntry:
            script = scripts_.at(int(a[0])).toObject();
            break;
        case rust::EngineOp::AppendScriptState:
            states.append(QJsonObject{{"id", std::as_const(script)["id"]},
                                      {"enabled", std::as_const(script)["enabled"]},
                                      {"paused", bool(paused_)}});
            break;
        case rust::EngineOp::BuildStatusResult:
            result = {{"paused", bool(paused_)},
                      {"warming", bool(warming_)},
                      {"current", current},
                      {"tasks", states}};
            break;
        default:
            fail("Rust 执行器状态宿主操作无效");
        }
    });
    return result;
}
void Engine::load() {
    rust::engineRun(rust::EngineMethod::Load,
                    [&](rust::EngineOp op, const double *a, double *out, const std::exception_ptr &) {
                        switch (op) {
                        case rust::EngineOp::Preload:
                            preloadModels();
                            break;
                        case rust::EngineOp::LogReady:
                            log("C++ 执行器已就绪；Python 工坊按需加载，采集和盒子仍由 C++ 管理。");
                            break;
                        case rust::EngineOp::TasksDirectory:
                            out[0] = QDir(data_ + "/ok_tasks").exists();
                            break;
                        case rust::EngineOp::ImportsDirectory:
                            out[0] = QDir(data_ + "/ok_import").exists();
                            break;
                        case rust::EngineOp::SetLoadingScripts:
                            loadingScripts_ = a[0] != 0;
                            break;
                        case rust::EngineOp::ScanScripts:
                            scriptCommand({{"id", 0}, {"op", "script_catalog"}});
                            break;
                        case rust::EngineOp::EmitReady:
                            emit message({{"event", "ready"}, {"data", catalog()}});
                            break;
                        default:
                            fail("Rust 执行器载入宿主操作无效");
                        }
                    });
}
void Engine::pause() {
    std::unique_lock<std::mutex> lock;
    rust::engineRun(rust::EngineMethod::Pause,
                    [&](rust::EngineOp op, const double *a, double *out, const std::exception_ptr &error) {
                        switch (op) {
                        case rust::EngineOp::LockLifecycle:
                            lock = std::unique_lock(lifecycle_);
                            break;
                        case rust::EngineOp::UnlockLifecycle:
                            lock.unlock();
                            break;
                        case rust::EngineOp::ClearPending:
                            pendingTask_.reset();
                            break;
                        case rust::EngineOp::LaunchTimerStop:
                            launchTimer_.stop();
                            break;
                        case rust::EngineOp::StopGate:
                            gate_.stop();
                            break;
                        case rust::EngineOp::GenerationZero:
                            generation_ = 0;
                            break;
                        case rust::EngineOp::SetPaused:
                            paused_ = a[0] != 0;
                            break;
                        case rust::EngineOp::SetForeground:
                            foreground_ = a[0] != 0;
                            break;
                        case rust::EngineOp::SetCancelForeground:
                            cancelForeground_ = a[0] != 0;
                            break;
                        case rust::EngineOp::BackendArm:
                            backend_.arm(a[0] != 0);
                            break;
                        case rust::EngineOp::LogException:
                            try {
                                std::rethrow_exception(error);
                            } catch (const std::exception &failure) {
                                log(errorText(failure));
                            }
                            break;
                        default:
                            fail("Rust 暂停宿主操作无效");
                        }
                    });
}
void Engine::start(const QString &task, QJsonObject options) {
    QJsonObject scriptEntry, catalogEntry;
    QJsonArray catalog;
    const TaskDefinition *native = nullptr;
    rust::engineRun(rust::EngineMethod::Start,
                    [&](rust::EngineOp op, const double *a, double *out, const std::exception_ptr &) {
                        switch (op) {
                        case rust::EngineOp::TeamBusy:
                            out[0] = teamSwitchBusy_;
                            break;
                        case rust::EngineOp::ScriptsBusy:
                            out[0] = scriptsBusy_;
                            break;
                        case rust::EngineOp::ExitTimerStop:
                            exitTimer_.stop();
                            break;
                        case rust::EngineOp::LimitOption:
                            out[0] = options["limit_seconds"].toDouble(0);
                            break;
                        case rust::EngineOp::ScriptsCount:
                            out[0] = double(scripts_.size());
                            break;
                        case rust::EngineOp::SelectScriptEntry:
                            scriptEntry = scripts_.at(int(a[0])).toObject();
                            break;
                        case rust::EngineOp::ScriptMatches:
                            out[0] = scriptEntry["id"] == task;
                            break;
                        case rust::EngineOp::ScriptSupported:
                            out[0] = scriptEntry["native_supported"].toBool();
                            break;
                        case rust::EngineOp::HasRequestedTask:
                            out[0] = !task.isEmpty();
                            break;
                        case rust::EngineOp::NativeCount:
                            out[0] = double(tasks_.size());
                            break;
                        case rust::EngineOp::NativeSelect:
                            native = &tasks_.at(int(a[0]));
                            break;
                        case rust::EngineOp::NativeMatches:
                            out[0] = native->id == (a[0] == 0 ? task : catalogEntry["id"].toString());
                            break;
                        case rust::EngineOp::ProfileCatalogLoad:
                            catalog = profile_.catalog()["tasks"].toArray();
                            break;
                        case rust::EngineOp::CatalogCount:
                            out[0] = double(catalog.size());
                            break;
                        case rust::EngineOp::SelectCatalog:
                            catalogEntry = catalog.at(int(a[0])).toObject();
                            break;
                        case rust::EngineOp::CatalogTrigger:
                            out[0] = catalogEntry["trigger"].toBool();
                            break;
                        case rust::EngineOp::CatalogEnabled:
                            out[0] = catalogEntry["enabled"].toBool();
                            break;
                        case rust::EngineOp::CatalogRemoteReset:
                            out[0] = catalogEntry["id"].toString() == "RemoteMouseResetTask";
                            break;
                        case rust::EngineOp::Error:
                            switch (int(a[0])) {
                            case 0:
                                fail("正在切换队伍代码，请等待完成后启动");
                            case 1:
                                fail("正在更新脚本文件，请等待保存完成后启动");
                            case 2:
                                fail("任务最长运行时间无效");
                            case 3:
                                fail(scriptEntry["error"].toString("脚本加载失败"));
                            case 4:
                                fail("当前引擎暂不支持此任务。");
                            case 5:
                                fail("已启用的“" + catalogEntry["name"].toString() +
                                     "”暂不支持，请先关闭该项再启动。");
                            default:
                                fail("Rust 启动错误类型无效");
                            }
                        case rust::EngineOp::Pause:
                            pause();
                            break;
                        case rust::EngineOp::Preload:
                            preloadModels();
                            break;
                        case rust::EngineOp::PendingTaskSet:
                            pendingTask_ = task;
                            break;
                        case rust::EngineOp::SetForeground:
                            foreground_ = a[0] != 0;
                            break;
                        case rust::EngineOp::PendingOptionsSet:
                            pendingOptions_ = options;
                            break;
                        case rust::EngineOp::PendingSnapshotSet:
                            pendingOptions_["scripts_snapshot"] = scripts_;
                            break;
                        case rust::EngineOp::TryLaunch:
                            tryLaunch();
                            break;
                        case rust::EngineOp::HasPending:
                            out[0] = bool(pendingTask_);
                            break;
                        case rust::EngineOp::LaunchTimerStart:
                            launchTimer_.start();
                            break;
                        default:
                            fail("Rust 启动宿主操作无效");
                        }
                    });
}
void Engine::tryLaunch() {
    QString task;
    uint64_t epoch = 0;
    QJsonArray scripts;
    QJsonObject script;
    rust::engineRun(rust::EngineMethod::TryLaunch,
                    [&](rust::EngineOp op, const double *a, double *out, const std::exception_ptr &) {
                        switch (op) {
                        case rust::EngineOp::HasPending:
                            out[0] = bool(pendingTask_);
                            break;
                        case rust::EngineOp::WorkerFinished:
                            out[0] = bool(workerFinished_);
                            break;
                        case rust::EngineOp::WorkerJoinable:
                            out[0] = worker_.joinable();
                            break;
                        case rust::EngineOp::JoinWorker:
                            worker_.join();
                            break;
                        case rust::EngineOp::CopyLaunchTask:
                            task = *pendingTask_;
                            break;
                        case rust::EngineOp::ClearPending:
                            pendingTask_.reset();
                            break;
                        case rust::EngineOp::LaunchTimerStop:
                            launchTimer_.stop();
                            break;
                        case rust::EngineOp::SetPaused:
                            paused_ = a[0] != 0;
                            break;
                        case rust::EngineOp::StartGate:
                            epoch = gate_.start();
                            break;
                        case rust::EngineOp::GenerationEpoch:
                            generation_ = epoch;
                            break;
                        case rust::EngineOp::SetWorkerFinished:
                            workerFinished_ = a[0] != 0;
                            break;
                        case rust::EngineOp::ScriptFlagsClear:
                            scriptEnabled_.clear();
                            break;
                        case rust::EngineOp::PendingScriptsCount:
                            scripts = pendingOptions_["scripts_snapshot"].toArray();
                            out[0] = double(scripts.size());
                            break;
                        case rust::EngineOp::PendingSelectScript:
                            script = scripts.at(int(a[0])).toObject();
                            break;
                        case rust::EngineOp::StoreScriptFlag:
                            scriptEnabled_[script["id"].toString()] =
                                std::make_shared<std::atomic<bool>>(script["enabled"].toBool());
                            break;
                        case rust::EngineOp::SpawnWorker:
                            worker_ = std::thread(&Engine::run, this, task, epoch, pendingOptions_);
                            break;
                        case rust::EngineOp::StopGate:
                            gate_.stop();
                            break;
                        default:
                            fail("Rust 线程启动宿主操作无效");
                        }
                    });
}
void Engine::preloadModels() {
    rust::engineRun(rust::EngineMethod::BeginPreload, [&](rust::EngineOp op, const double *, double *out,
                                                          const std::exception_ptr &) {
        switch (op) {
        case rust::EngineOp::ModelJoinable:
            out[0] = modelWorker_.joinable();
            break;
        case rust::EngineOp::SpawnModel:
            modelWorker_ = std::thread([this] {
                QElapsedTimer elapsed;
                QString models;
                rust::engineRun(rust::EngineMethod::LoadModels, [&](rust::EngineOp operation, const double *a,
                                                                    double *out,
                                                                    const std::exception_ptr &error) {
                    switch (operation) {
                    case rust::EngineOp::StartModelTimer:
                        elapsed.start();
                        break;
                    case rust::EngineOp::CreateTemplates:
                        templates_ = std::make_unique<Templates>(root_ + "/assets/coco_annotations.json");
                        break;
                    case rust::EngineOp::DefaultModelPath:
                        models = root_ + "/assets/ocr/ppocrv6-small";
                        if (!QFile::exists(models + "/det/det.onnx"))
                            models = root_ + "/assets/ocr/ppocrv5";
                        break;
                    case rust::EngineOp::ModelExists:
                        out[0] = QFile::exists(models + "/det/det.onnx");
                        break;
                    case rust::EngineOp::FallbackModelPath:
                        models = root_ + "/.venv/Lib/site-packages/onnxocr/models/ppocrv5";
                        break;
                    case rust::EngineOp::CreateOcr:
                        ocr_ = std::make_unique<Ocr>(models);
                        break;
                    case rust::EngineOp::CreateEcho:
                        echo_ = std::make_unique<EchoDetector>(root_ + "/assets/echo_model/echo.onnx");
                        break;
                    case rust::EngineOp::LogModelsReady:
                        log(QString("识别资源后台预加载完成，用时 %1 ms；后续启动复用。")
                                .arg(elapsed.elapsed()));
                        break;
                    case rust::EngineOp::SetModelError:
                        try {
                            std::rethrow_exception(error);
                        } catch (const std::exception &failure) {
                            modelError_ = errorText(failure);
                        }
                        break;
                    case rust::EngineOp::LogModelError:
                        log("识别资源准备失败：" + modelError_);
                        break;
                    case rust::EngineOp::SetModelsReady:
                        modelsReady_ = a[0] != 0;
                        break;
                    default:
                        fail("Rust 识别资源宿主操作无效");
                    }
                });
            });
            break;
        default:
            fail("Rust 资源预载入宿主操作无效");
        }
    });
}
void Engine::run(QString selected, uint64_t epoch, QJsonObject options) {
    SteadyClock clock;
    const auto began = clock.now();
    auto limit = options["limit_seconds"].toDouble();
    QString failure;
    QString activeTask = selected, phase = "preparation", diagnosticReport;
    QJsonObject lastCombat;
    QJsonArray stepDiagnostics;
    bool completed = false, exitAfter = false, resultRecorded = false;
    const auto recordResult = [&](const QString &task, bool success) {
        rust::engineRun(rust::EngineMethod::RecordResult, [&](rust::EngineOp op, const double *a, double *out,
                                                              const std::exception_ptr &error) {
            switch (op) {
            case rust::EngineOp::WriteRunResult: {
                writeJson(data_ + "/native-last-run.json",
                          {{"task", task},
                           {"success", success},
                           {"error", success ? QString{} : failure},
                           {"finished", QDateTime::currentDateTime().toString(Qt::ISODate)},
                           {"diagnostic_report", diagnosticReport},
                           {"seconds", clock.now() - began}});
                break;
            }
            case rust::EngineOp::LogResultFailure: {
                try {
                    std::rethrow_exception(error);
                } catch (const std::exception &e) {
                    log("运行结果保存失败：" + errorText(e));
                }
                break;
            }
            default:
                fail("Rust RecordResult 宿主操作无效");
            }
        });
    };
    EmergencyMemory emergency;
    auto executeSession = [&] {
        KeepAwake awake;
        std::optional<Execution> preparation;
        std::unique_lock<std::mutex> preparationLock;
        rust::engineRun(rust::EngineMethod::Prepare, [&](rust::EngineOp op, const double *a, double *out,
                                                         const std::exception_ptr &error) {
            switch (op) {
            case rust::EngineOp::AwakeActive: {
                out[0] = awake.active();
                break;
            }
            case rust::EngineOp::LogAwakeFailure: {
                log("未能阻止任务运行期间自动休眠，请检查系统电源设置。");
                break;
            }
            case rust::EngineOp::SetWarming: {
                warming_ = a[0] != 0;
                break;
            }
            case rust::EngineOp::PostStatusSelected: {
                postStatus(epoch, selected);
                break;
            }
            case rust::EngineOp::LogPreparation: {
                log("正在准备任务，检查工坊代码与识别资源；可随时暂停。");
                break;
            }
            case rust::EngineOp::EmergencyAvailable: {
                out[0] = emergency.available();
                break;
            }
            case rust::EngineOp::LogEmergency: {
                log(a[0] != 0 ? "已预留 64 MiB 识别应急内存。" : "可用提交内存不足，未建立识别应急缓冲。");
                break;
            }
            case rust::EngineOp::CreatePreparation: {
                preparation.emplace(gate_, epoch, clock);
                break;
            }
            case rust::EngineOp::Checkpoint: {
                preparation->checkpoint();
                break;
            }
            case rust::EngineOp::SelectedIsScript: {
                out[0] = selected.startsWith("script:");
                break;
            }
            case rust::EngineOp::ValidateTeams: {
                teams_->validateEnabled();
                break;
            }
            case rust::EngineOp::ModelsReady: {
                out[0] = modelsReady_;
                break;
            }
            case rust::EngineOp::Wait: {
                preparation->wait(a[0]);
                break;
            }
            case rust::EngineOp::ModelErrorEmpty: {
                out[0] = modelError_.isEmpty();
                break;
            }
            case rust::EngineOp::ThrowModelError: {
                fail(modelError_);
                break;
            }
            case rust::EngineOp::LockLifecycle: {
                preparationLock = std::unique_lock(lifecycle_);
                break;
            }
            case rust::EngineOp::UnlockLifecycle: {
                preparationLock.unlock();
                break;
            }
            case rust::EngineOp::BackendArm: {
                backend_.arm(a[0] != 0);
                break;
            }
            case rust::EngineOp::LogPreparationDone: {
                log(QString("任务准备完成，用时 %1 ms。").arg(qRound64((clock.now() - began) * 1000)));
                break;
            }
            default:
                fail("Rust Prepare 宿主操作无效");
            }
        });
        auto config = Config::load(data_ + "/dualpc.json");
        auto frame = backend_.latestFrame();
        rust::engineRun(rust::EngineMethod::RequireFrame, [&](rust::EngineOp op, const double *a, double *out,
                                                              const std::exception_ptr &error) {
            switch (op) {
            case rust::EngineOp::FrameAvailable: {
                out[0] = bool(frame);
                break;
            }
            case rust::EngineOp::Error: {
                fail("采集画面尚未就绪");
                break;
            }
            default:
                fail("Rust RequireFrame 宿主操作无效");
            }
        });
        std::function<void()> pollControls;
        bool taskRunning = false;
        Execution execution(gate_, epoch, clock, [&] {
            QJsonObject state;
            rust::engineRun(rust::EngineMethod::Health, [&](rust::EngineOp op, const double *a, double *out,
                                                            const std::exception_ptr &error) {
                switch (op) {
                case rust::EngineOp::Heartbeat: {
                    out[0] = backend_.heartbeat();
                    break;
                }
                case rust::EngineOp::GateActive: {
                    out[0] = gate_.active(epoch);
                    break;
                }
                case rust::EngineOp::ThrowCancelled: {
                    throw Cancelled();
                    break;
                }
                case rust::EngineOp::ReadDeviceState: {
                    state = backend_.snapshot();
                    break;
                }
                case rust::EngineOp::DeviceErrorEmpty: {
                    out[0] = std::as_const(state)["error"].toString().isEmpty();
                    break;
                }
                case rust::EngineOp::CaptureConnected: {
                    out[0] = std::as_const(state)["capture_connected"].toBool();
                    break;
                }
                case rust::EngineOp::BoxConnected: {
                    out[0] = std::as_const(state)["box"].toObject()["connected"].toBool();
                    break;
                }
                case rust::EngineOp::ThrowInputFailure: {
                    const QString labels[]{"自动输入通道已暂停，采集与输入设备仍保持连接，请重新开始任务",
                                           "视频采集或键鼠输入未就绪，自动操作已暂停",
                                           std::as_const(state)["error"].toString()};
                    throw ExecutionFailure(labels[int(a[0])], QString::fromUtf8(QJsonDocument(state).toJson(
                                                                  QJsonDocument::Indented)));
                    break;
                }
                case rust::EngineOp::TaskRunning: {
                    out[0] = taskRunning;
                    break;
                }
                case rust::EngineOp::Foreground: {
                    out[0] = foreground_;
                    break;
                }
                case rust::EngineOp::CancelForeground: {
                    out[0] = cancelForeground_;
                    break;
                }
                case rust::EngineOp::HasControlPoll: {
                    out[0] = bool(pollControls);
                    break;
                }
                case rust::EngineOp::PollControls: {
                    pollControls();
                    break;
                }
                case rust::EngineOp::LimitSeconds: {
                    out[0] = limit;
                    break;
                }
                case rust::EngineOp::Now: {
                    out[0] = clock.now();
                    break;
                }
                case rust::EngineOp::RunBegan: {
                    out[0] = began;
                    break;
                }
                case rust::EngineOp::Error: {
                    fail("任务达到设置的最长运行时间，已停止并释放按键");
                    break;
                }
                default:
                    fail("Rust Health 宿主操作无效");
                }
            });
        });
        Input input(
            config, {frame->width, frame->height}, backend_.snapshot()["box"].toObject()["absolute"].toBool(),
            [&](auto command) { return backend_.nativeCommand(command); }, execution);
        Templates activeTemplates(*templates_);
        activeTemplates.setCheckpoint([&] { execution.checkpoint(); });
        const auto userTemplates = data_ + "/ok_templates/coco_annotations.json";
        rust::engineRun(rust::EngineMethod::AddTemplates, [&](rust::EngineOp op, const double *a, double *out,
                                                              const std::exception_ptr &error) {
            switch (op) {
            case rust::EngineOp::UserTemplatesExist: {
                out[0] = QFile::exists(userTemplates);
                break;
            }
            case rust::EngineOp::AddUserTemplates: {
                activeTemplates.add(userTemplates);
                break;
            }
            default:
                fail("Rust AddTemplates 宿主操作无效");
            }
        });
        InferenceFrames inference(config);
        uint64_t captureSequence = 0;
        const auto retryRecognition = [&](auto &&read) {
            return emergency.read(read, [&] {
                execution.checkpoint();
                log("识别遇到内存不足：已释放 64 MiB 应急缓冲，仅重试本次识别，不重发输入指令。");
            });
        };
        TaskContext context(
            execution, input, activeTemplates, profile_,
            [&] {
                auto latest = backend_.latestFrame();
                captureSequence = latest ? latest->sequence : 0;
                return retryRecognition([&] { return inference.get(latest); });
            },
            [&](const auto &image, auto area, double threshold) {
                return retryRecognition([&] {
                    return ocr_->read(image, area, threshold, true, false, [&] { execution.checkpoint(); });
                });
            });
        QJsonObject progressState;
        context.streamedCapture = config.s("capture.source") == "desktop";
        double lastProgress = -1;
        const auto publishProgress = [&](QJsonObject patch) {
            QString task;
            QJsonValue previousStage;
            QJsonObject updated;
            rust::engineRun(rust::EngineMethod::Progress, [&](rust::EngineOp op, const double *a, double *out,
                                                              const std::exception_ptr &error) {
                switch (op) {
                case rust::EngineOp::SelectedEmpty: {
                    out[0] = selected.isEmpty();
                    break;
                }
                case rust::EngineOp::SelectProgressTask: {
                    task = a[0] != 0 ? context.task : selected;
                    break;
                }
                case rust::EngineOp::ProgressTaskMatches: {
                    out[0] = progressState["task"] == task;
                    break;
                }
                case rust::EngineOp::ResetProgressTask: {
                    progressState = {{"task", task}};
                    break;
                }
                case rust::EngineOp::InitializeProgressPatch: {
                    previousStage = progressState["stage"];
                    updated = progressState;
                    break;
                }
                case rust::EngineOp::ProgressPatchCount: {
                    out[0] = double(patch.size());
                    break;
                }
                case rust::EngineOp::MergeProgressEntry: {
                    const auto it = std::next(patch.constBegin(), int(a[0]));
                    updated[it.key()] = it.value();
                    break;
                }
                case rust::EngineOp::ProgressUnchanged: {
                    out[0] = updated == progressState;
                    break;
                }
                case rust::EngineOp::CommitProgress: {
                    progressState = updated;
                    break;
                }
                case rust::EngineOp::PatchContainsCurrent: {
                    out[0] = patch.contains("current");
                    break;
                }
                case rust::EngineOp::ProgressTotal: {
                    out[0] = updated["total"].toDouble(-1);
                    break;
                }
                case rust::EngineOp::ProgressCurrent: {
                    out[0] = updated["current"].toDouble(-1);
                    break;
                }
                case rust::EngineOp::PatchContainsFlow: {
                    out[0] = patch.contains("flow");
                    break;
                }
                case rust::EngineOp::ProgressStageUnchanged: {
                    out[0] = previousStage == updated["stage"];
                    break;
                }
                case rust::EngineOp::Now: {
                    out[0] = execution.now();
                    break;
                }
                case rust::EngineOp::LastProgress: {
                    out[0] = lastProgress;
                    break;
                }
                case rust::EngineOp::StoreProgressTime: {
                    lastProgress = a[0];
                    break;
                }
                case rust::EngineOp::QueueProgress: {
                    QMetaObject::invokeMethod(
                        this,
                        [this, epoch, updated] {
                            rust::engineRun(rust::EngineMethod::ProgressDelivery,
                                            [&](rust::EngineOp op, const double *a, double *out,
                                                const std::exception_ptr &error) {
                                                switch (op) {
                                                case rust::EngineOp::GenerationMatches: {
                                                    out[0] = generation_ == epoch;
                                                    break;
                                                }
                                                case rust::EngineOp::Paused: {
                                                    out[0] = paused_;
                                                    break;
                                                }
                                                case rust::EngineOp::GateActive: {
                                                    out[0] = gate_.active(epoch);
                                                    break;
                                                }
                                                case rust::EngineOp::EmitProgress: {
                                                    emit message(
                                                        {{"event", "task_progress"}, {"data", updated}});
                                                    break;
                                                }
                                                default:
                                                    fail("Rust ProgressDelivery 宿主操作无效");
                                                }
                                            });
                        },
                        Qt::QueuedConnection);
                    break;
                }
                default:
                    fail("Rust Progress 宿主操作无效");
                }
            });
        };
        context.progress = publishProgress;
        context.info = [&](auto key, auto value) {
            rust::engineRun(
                rust::EngineMethod::InfoProgress,
                [&](rust::EngineOp op, const double *a, double *out, const std::exception_ptr &error) {
                    switch (op) {
                    case rust::EngineOp::LogInfo: {
                        log(key + "：" + value);
                        break;
                    }
                    case rust::EngineOp::InfoIsStage: {
                        out[0] = QStringList{"当前任务", "current task", "stage", "当前阶段"}.contains(key);
                        break;
                    }
                    case rust::EngineOp::PublishStageInfo: {
                        publishProgress(
                            {{"stage", value.left(160)}, {"current", -1}, {"total", -1}, {"detail", ""}});
                        break;
                    }
                    case rust::EngineOp::PublishDetailInfo: {
                        publishProgress({{"detail", (key + "：" + value).left(500)}});
                        break;
                    }
                    default:
                        fail("Rust InfoProgress 宿主操作无效");
                    }
                });
        };
        context.sourceSequence = [&] { return captureSequence; };
        context.sourceRecovering = [&] {
            return backend_.snapshot()["actual"].toObject()["recovering"].toBool();
        };
        input.recovery = [&](QString text) {
            log(text);
            publishProgress({{"detail", text}});
        };
        context.combatInfo = [this, epoch, &lastCombat](QJsonObject value) {
            lastCombat = value;
            QMetaObject::invokeMethod(
                this,
                [this, epoch, value = std::move(value)] {
                    rust::engineRun(rust::EngineMethod::CombatDelivery,
                                    [&](rust::EngineOp op, const double *a, double *out,
                                        const std::exception_ptr &error) {
                                        switch (op) {
                                        case rust::EngineOp::GenerationMatches: {
                                            out[0] = generation_ == epoch;
                                            break;
                                        }
                                        case rust::EngineOp::Paused: {
                                            out[0] = paused_;
                                            break;
                                        }
                                        case rust::EngineOp::EmitCombat: {
                                            emit message({{"event", "combat_info"}, {"data", value}});
                                            break;
                                        }
                                        default:
                                            fail("Rust CombatDelivery 宿主操作无效");
                                        }
                                    });
                },
                Qt::QueuedConnection);
        };
        context.notify = [&](QString title, QString text, bool error) { notify({title, text, {}, error}); };
        context.diagnostic = [&](QString reason, QJsonObject event) {
            event["error"] = reason;
            event["task"] = context.task;
            event["phase"] = "task_step";
            QString folder, filePath;
            std::vector<uchar> bytes;
            std::optional<QSaveFile> file;
            QJsonObject saved;
            rust::engineRun(
                rust::EngineMethod::StepDiagnostic,
                [&](rust::EngineOp op, const double *a, double *out, const std::exception_ptr &error) {
                    switch (op) {
                    case rust::EngineOp::BuildStepPaths: {
                        folder = data_ + "/screenshots/errors";
                        filePath = folder + '/' +
                                   QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") + "-step.png";
                        break;
                    }
                    case rust::EngineOp::StepFrameEmpty: {
                        out[0] = context.frame.empty();
                        break;
                    }
                    case rust::EngineOp::CreateScreenshotDirectory: {
                        out[0] = QDir().mkpath(folder);
                        break;
                    }
                    case rust::EngineOp::EncodeStepImage: {
                        out[0] = cv::imencode(".png", context.frame, bytes);
                        break;
                    }
                    case rust::EngineOp::OpenScreenshot: {
                        file.emplace(filePath);
                        out[0] = file->open(QIODevice::WriteOnly);
                        break;
                    }
                    case rust::EngineOp::WriteScreenshot: {
                        out[0] = file->write(reinterpret_cast<const char *>(bytes.data()),
                                             qint64(bytes.size())) == qint64(bytes.size());
                        break;
                    }
                    case rust::EngineOp::CommitScreenshot: {
                        out[0] = file->commit();
                        break;
                    }
                    case rust::EngineOp::SetScreenshotPath: {
                        event["screenshot"] = filePath;
                        break;
                    }
                    case rust::EngineOp::DisposeScreenshot: {
                        file.reset();
                        break;
                    }
                    case rust::EngineOp::SaveStepDiagnostic: {
                        saved = saveDiagnostic(data_, event);
                        break;
                    }
                    case rust::EngineOp::StepDiagnosticCount: {
                        out[0] = double(stepDiagnostics.size());
                        break;
                    }
                    case rust::EngineOp::RemoveFirstStepDiagnostic: {
                        stepDiagnostics.removeAt(0);
                        break;
                    }
                    case rust::EngineOp::AppendStepDiagnostic: {
                        stepDiagnostics.append(std::as_const(saved)["diagnostic_report"]);
                        break;
                    }
                    case rust::EngineOp::LogStepDiagnostic: {
                        log("步骤识别现场：" + std::as_const(saved)["diagnostic_report"].toString());
                        break;
                    }
                    case rust::EngineOp::LogStepFailure: {
                        try {
                            std::rethrow_exception(error);
                        } catch (const std::exception &e) {
                            log("步骤诊断保存失败：" + errorText(e));
                        }
                        break;
                    }
                    default:
                        fail("Rust StepDiagnostic 宿主操作无效");
                    }
                });
        };
        context.state = taskState_;
        context.gameLanguage = config.get("game_language").toString("zh_CN");
        context.physicalFDown = [this] { return backend_.physicalKeyDown('F'); };
        auto runner = std::make_unique<TaskRunner>(context, *echo_, root_, data_, teams_);
        ScriptRuntime scripts(context, root_, data_);
        scripts.builtin = [&](const QString &id) {
            const auto definitions =
                businessTasks([&](const QString &task) { return runner->dispatch(task); });
            const TaskDefinition *definition = nullptr;
            return rust::engineRun(
                       rust::EngineMethod::BuiltinDispatch,
                       [&](rust::EngineOp op, const double *a, double *out, const std::exception_ptr &error) {
                           switch (op) {
                           case rust::EngineOp::NativeCount: {
                               out[0] = double(definitions.size());
                               break;
                           }
                           case rust::EngineOp::NativeSelect: {
                               definition = &definitions.at(int(a[0]));
                               break;
                           }
                           case rust::EngineOp::NativeMatches: {
                               out[0] = definition->id == id;
                               break;
                           }
                           case rust::EngineOp::Error: {
                               fail("脚本请求的内置任务不存在：" + id);
                               break;
                           }
                           case rust::EngineOp::DispatchBuiltin: {
                               out[0] = runner->dispatch(id);
                               break;
                           }
                           default:
                               fail("Rust BuiltinDispatch 宿主操作无效");
                           }
                       }) != 0;
        };
        // Observe physical key edges on the execution thread, including while
        // a long macro is waiting. Scripted remote F is never seen as local F.
        pollControls = [&] { runner->pollControls(); };
        auto runnable = builtinTasks();
        runnable += businessTasks([&](const QString &id) { return runner->dispatch(id); });
        QMap<QString, QJsonObject> custom;
        QJsonObject snapshotScript;
        rust::engineRun(rust::EngineMethod::RegisterScripts, [&](rust::EngineOp op, const double *a,
                                                                 double *out,
                                                                 const std::exception_ptr &error) {
            switch (op) {
            case rust::EngineOp::SnapshotScriptCount: {
                out[0] = double(options["scripts_snapshot"].toArray().size());
                break;
            }
            case rust::EngineOp::SelectSnapshotScript: {
                snapshotScript = options["scripts_snapshot"].toArray().at(int(a[0])).toObject();
                break;
            }
            case rust::EngineOp::StoreCustomScript: {
                const auto script = snapshotScript;
                custom[script["id"].toString()] = script;
                break;
            }
            case rust::EngineOp::LogScriptLoaded: {
                const auto script = snapshotScript;
                const auto id = script["id"].toString();
                log(QString("载入通用脚本：%1 · %2 · %3")
                        .arg(script["name"].toString(), script["trigger"].toBool() ? "实时触发" : "任务",
                             scriptEnabled_.value(id)->load() ? "已启用" : "未启用"));
                break;
            }
            case rust::EngineOp::ScriptTrigger: {
                out[0] = std::as_const(snapshotScript)["trigger"].toBool();
                break;
            }
            case rust::EngineOp::ScriptInterval: {
                out[0] = std::as_const(snapshotScript)["interval"].toDouble(.1);
                break;
            }
            case rust::EngineOp::AppendRunnableScript: {
                const auto script = snapshotScript;
                const auto id = script["id"].toString();
                runnable.append(
                    {id, a[0] != 0, a[1], [&, script](TaskContext &) {
                         return rust::engineRun(
                                    rust::EngineMethod::ScriptIterations,
                                    [&](rust::EngineOp op, const double *a, double *out,
                                        const std::exception_ptr &) {
                                        switch (op) {
                                        case rust::EngineOp::Checkpoint:
                                            context.execution.checkpoint();
                                            break;
                                        case rust::EngineOp::ScriptTrigger:
                                            out[0] = script["trigger"].toBool();
                                            break;
                                        case rust::EngineOp::ScriptRepeat:
                                            out[0] = script["repeat"].toInt(1);
                                            break;
                                        case rust::EngineOp::ScriptContinuous:
                                            out[0] = script["continuous"].toBool();
                                            break;
                                        case rust::EngineOp::HasInfo:
                                            out[0] = bool(context.info);
                                            break;
                                        case rust::EngineOp::ScriptProgress: {
                                            const auto next = (uint64_t(a[4]) << 32) | uint64_t(a[3]);
                                            context.reportProgress("执行脚本", a[0], a[2] != 0 ? -1. : a[1],
                                                                   "轮",
                                                                   QString("第 %1 轮%2")
                                                                       .arg(qulonglong(next))
                                                                       .arg(a[2] != 0 ? " · 持续循环" : ""));
                                            break;
                                        }
                                        case rust::EngineOp::ScriptInfo: {
                                            const auto next = (uint64_t(a[1]) << 32) | uint64_t(a[0]);
                                            context.info("脚本循环",
                                                         QString::number(qulonglong(next)) +
                                                             (a[2] != 0
                                                                  ? " / 持续"
                                                                  : " / " + QString::number(int(a[3]))));
                                            break;
                                        }
                                        case rust::EngineOp::ScriptRun: {
                                            const auto flag = scriptEnabled_.value(script["id"].toString());
                                            out[0] = scripts
                                                         .run(script,
                                                              a[0] != 0 ? std::function<bool()>([flag] {
                                                                  return flag && flag->load();
                                                              })
                                                                        : std::function<bool()>{})["handled"]
                                                         .toBool();
                                            break;
                                        }
                                        case rust::EngineOp::ScriptDisabledInfo:
                                            context.info("脚本", "已停用并释放输入");
                                            break;
                                        case rust::EngineOp::ScriptComplete:
                                            context.reportProgress("脚本执行完成", a[0], a[0], "轮");
                                            break;
                                        default:
                                            fail("Rust 脚本循环宿主操作无效");
                                        }
                                    }) != 0;
                     }});
                break;
            }
            default:
                fail("Rust RegisterScripts 宿主操作无效");
            }
        });
        QMap<QString, double> next;
        const TaskDefinition *activeDefinition = nullptr;
        QString completionName;
        QJsonArray completionCatalog;
        QJsonObject completionEntry;
        std::unique_lock<std::mutex> resumeLock;
        rust::engineRun(rust::EngineMethod::Run, [&](rust::EngineOp op, const double *a, double *out,
                                                     const std::exception_ptr &error) {
            switch (op) {
            case rust::EngineOp::Checkpoint:
                execution.checkpoint();
                break;
            case rust::EngineOp::HasSelected:
                out[0] = !selected.isEmpty();
                break;
            case rust::EngineOp::CancelForeground:
                out[0] = bool(cancelForeground_);
                break;
            case rust::EngineOp::Refresh:
                context.refresh();
                break;
            case rust::EngineOp::TaskCount:
                out[0] = double(runnable.size());
                break;
            case rust::EngineOp::SelectTask:
                activeDefinition = &runnable.at(int(a[0]));
                break;
            case rust::EngineOp::TaskSelected:
                out[0] = activeDefinition->id == selected;
                break;
            case rust::EngineOp::TaskTrigger:
                out[0] = activeDefinition->trigger;
                break;
            case rust::EngineOp::TaskCustom:
                out[0] = custom.contains(activeDefinition->id);
                break;
            case rust::EngineOp::CustomTrigger:
                out[0] = custom[activeDefinition->id]["trigger"].toBool();
                break;
            case rust::EngineOp::ScriptEnabled:
                out[0] = scriptEnabled_.value(activeDefinition->id)->load();
                break;
            case rust::EngineOp::ProfileEnabled:
                out[0] = profile_.value(activeDefinition->id, "_enabled", a[0] != 0).toBool();
                break;
            case rust::EngineOp::Now:
                out[0] = execution.now();
                break;
            case rust::EngineOp::NextTime:
                out[0] = next.value(activeDefinition->id, 0);
                break;
            case rust::EngineOp::SetTaskContext:
                context.task = activeDefinition->id;
                activeTask = activeDefinition->id;
                phase = "task";
                break;
            case rust::EngineOp::SetTaskRunning:
                taskRunning = a[0] != 0;
                break;
            case rust::EngineOp::RunTask:
                out[0] = activeDefinition->step(context);
                break;
            case rust::EngineOp::GateActive:
                out[0] = gate_.active(epoch);
                break;
            case rust::EngineOp::TaskAutoCombat:
                out[0] = activeDefinition->id == "AutoCombatTask";
                break;
            case rust::EngineOp::ReleaseInput:
                input.release();
                break;
            case rust::EngineOp::LogException:
                try {
                    std::rethrow_exception(error);
                } catch (const std::exception &failure) {
                    log(errorText(failure));
                }
                break;
            case rust::EngineOp::SetNextTime:
                next[activeDefinition->id] = a[0];
                break;
            case rust::EngineOp::TaskInterval:
                out[0] = activeDefinition->interval;
                break;
            case rust::EngineOp::ExitOption:
                out[0] = options["exit_after"].toBool();
                break;
            case rust::EngineOp::ExitProfile:
                out[0] = profile_.value(activeDefinition->id, "Exit After Task", false).toBool();
                break;
            case rust::EngineOp::SetExitAfter:
                exitAfter = a[0] != 0;
                break;
            case rust::EngineOp::RecordResult:
                recordResult(selected, a[0] != 0);
                break;
            case rust::EngineOp::SetResultRecorded:
                resultRecorded = a[0] != 0;
                break;
            case rust::EngineOp::TaskDone:
                emit message({{"event", "task_done"}, {"task", selected}});
                break;
            case rust::EngineOp::CompletionNameReset:
                completionName = selected;
                completionCatalog = profile_.catalog()["tasks"].toArray();
                break;
            case rust::EngineOp::CatalogCount:
                out[0] = double(completionCatalog.size());
                break;
            case rust::EngineOp::SelectCatalog:
                completionEntry = completionCatalog.at(int(a[0])).toObject();
                break;
            case rust::EngineOp::CatalogSelected:
                out[0] = completionEntry["id"] == selected;
                break;
            case rust::EngineOp::CompletionNameSet:
                completionName = completionEntry["name"].toString();
                break;
            case rust::EngineOp::NotifyDone:
                notify({product::name + " 任务完成", completionName, {}});
                break;
            case rust::EngineOp::SetCompleted:
                completed = a[0] != 0;
                break;
            case rust::EngineOp::StopGate:
                gate_.stop();
                break;
            case rust::EngineOp::PostStatusSelected:
                postStatus(epoch, selected);
                break;
            case rust::EngineOp::TriggerInterval:
                out[0] = profile_.value("Basic Options", "Trigger Interval", 1).toDouble(1);
                break;
            case rust::EngineOp::Wait:
                execution.wait(a[0]);
                break;
            case rust::EngineOp::LockLifecycle:
                resumeLock = std::unique_lock(lifecycle_);
                break;
            case rust::EngineOp::UnlockLifecycle:
                resumeLock.unlock();
                break;
            case rust::EngineOp::SetForeground:
                foreground_ = a[0] != 0;
                break;
            case rust::EngineOp::SetCancelForeground:
                cancelForeground_ = a[0] != 0;
                break;
            case rust::EngineOp::ClearSelected:
                selected.clear();
                break;
            case rust::EngineOp::ClearActiveTask:
                activeTask.clear();
                break;
            case rust::EngineOp::ClearContextTask:
                context.task.clear();
                break;
            case rust::EngineOp::ClearOverrides:
                context.taskOverrides = {};
                break;
            case rust::EngineOp::ClearContextCombat:
                context.state->inCombat = false;
                break;
            case rust::EngineOp::ResetRunner:
                runner = std::make_unique<TaskRunner>(context, *echo_, root_, data_, teams_);
                break;
            case rust::EngineOp::CombatPending:
                context.combatInfo({{"code_state", "pending"}, {"verified", false}});
                break;
            case rust::EngineOp::ClearLimit:
                limit = 0;
                break;
            case rust::EngineOp::ClearNext:
                next.clear();
                break;
            case rust::EngineOp::PostStatus:
                postStatus(epoch);
                break;
            case rust::EngineOp::SetStoppedFailure:
                failure = "当前任务已停止";
                break;
            case rust::EngineOp::LogStopped:
                log("当前任务已停止，实时触发继续运行。");
                break;
            case rust::EngineOp::LogCompleted:
                log("当前任务已完成，实时触发继续运行。");
                break;
            case rust::EngineOp::ThrowCancelled:
                throw Cancelled();
            default:
                fail("Rust 执行器宿主操作无效");
            }
        });
    };
    std::exception_ptr runError;
    bool lowMemory = false;
    QJsonObject errorEvent;
    std::optional<Notice> errorNotice;
    std::unique_lock<std::mutex> cleanupLock;
    rust::engineRun(rust::EngineMethod::SessionLifetime, [&](rust::EngineOp op, const double *a, double *out,
                                                             const std::exception_ptr &error) {
        switch (op) {
        case rust::EngineOp::ExecuteSession: {
            executeSession();
            break;
        }
        case rust::EngineOp::ReleaseEmergency: {
            emergency.release();
            break;
        }
        case rust::EngineOp::SetCancelledFailure: {
            failure = "已暂停或取消";
            break;
        }
        case rust::EngineOp::ClassifyFailure: {
            runError = error;
            try {
                std::rethrow_exception(runError);
            } catch (const std::exception &e) {
                lowMemory = memoryFailure(e);
                out[0] = lowMemory;
            }
            break;
        }
        case rust::EngineOp::SetTaskFailure: {
            try {
                std::rethrow_exception(runError);
            } catch (const std::exception &e) {
                failure = taskFailureText(e);
            }
            break;
        }
        case rust::EngineOp::LogLowMemoryError: {
            try {
                std::rethrow_exception(runError);
            } catch (const std::exception &e) {
                log("内存不足的原始错误：" + errorText(e));
            }
            break;
        }
        case rust::EngineOp::LockLifecycle: {
            cleanupLock = std::unique_lock(lifecycle_);
            break;
        }
        case rust::EngineOp::UnlockLifecycle: {
            cleanupLock.unlock();
            break;
        }
        case rust::EngineOp::StopGate: {
            gate_.stop();
            break;
        }
        case rust::EngineOp::SetPaused: {
            paused_ = a[0] != 0;
            break;
        }
        case rust::EngineOp::SetWarming: {
            warming_ = a[0] != 0;
            break;
        }
        case rust::EngineOp::BackendArm: {
            backend_.arm(a[0] != 0);
            break;
        }
        case rust::EngineOp::LogException: {
            try {
                std::rethrow_exception(error);
            } catch (const std::exception &e) {
                log(errorText(e));
            }
            break;
        }
        case rust::EngineOp::BuildErrorEvent: {
            errorEvent = {{"event", "error"},
                          {"error", failure},
                          {"task", activeTask},
                          {"selected_task", selected},
                          {"phase", phase},
                          {"combat", lastCombat},
                          {"devices", backend_.snapshot()},
                          {"elapsed_seconds", clock.now() - began}};
            errorEvent["step_diagnostics"] = stepDiagnostics;
            break;
        }
        case rust::EngineOp::IsExecutionFailure: {
            try {
                std::rethrow_exception(runError);
            } catch (const std::exception &e) {
                out[0] = dynamic_cast<const ExecutionFailure *>(&e) != nullptr;
            }
            break;
        }
        case rust::EngineOp::SetErrorTraceback: {
            try {
                std::rethrow_exception(runError);
            } catch (const ExecutionFailure &e) {
                errorEvent["traceback"] = e.details;
            }
            break;
        }
        case rust::EngineOp::SetErrorType: {
            try {
                std::rethrow_exception(runError);
            } catch (const std::exception &e) {
                errorEvent["exception_type"] = QString::fromLatin1(typeid(e).name());
            }
            break;
        }
        case rust::EngineOp::CreateErrorNotice: {
            errorNotice.emplace(Notice{product::name + " 任务停止", failure, {}, true});
            break;
        }
        case rust::EngineOp::CaptureErrorScreenshot: {
            FramePtr last;
            cv::Mat picture;
            std::vector<uchar> bytes;
            QByteArray png;
            QString filePath;
            std::optional<QSaveFile> file;
            rust::engineRun(rust::EngineMethod::ErrorScreenshot, [&](rust::EngineOp op, const double *a,
                                                                     double *out,
                                                                     const std::exception_ptr &error) {
                switch (op) {
                case rust::EngineOp::LowMemory: {
                    out[0] = lowMemory;
                    break;
                }
                case rust::EngineOp::ReadErrorFrame: {
                    last = backend_.latestFrame();
                    break;
                }
                case rust::EngineOp::ErrorFrameAvailable: {
                    out[0] = bool(last);
                    break;
                }
                case rust::EngineOp::PrepareErrorImage: {
                    picture = inferenceImage(*last, Config::load(data_ + "/dualpc.json"));
                    break;
                }
                case rust::EngineOp::EncodeErrorImage: {
                    out[0] = cv::imencode(".png", picture, bytes);
                    break;
                }
                case rust::EngineOp::BuildErrorScreenshot: {
                    png = QByteArray(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size()));
                    const auto folder = data_ + "/screenshots/errors";
                    QDir().mkpath(folder);
                    filePath =
                        folder + '/' + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") + ".png";
                    break;
                }
                case rust::EngineOp::OpenScreenshot: {
                    file.emplace(filePath);
                    out[0] = file->open(QIODevice::WriteOnly);
                    break;
                }
                case rust::EngineOp::WriteScreenshot: {
                    out[0] = file->write(png) == png.size();
                    break;
                }
                case rust::EngineOp::CommitScreenshot: {
                    out[0] = file->commit();
                    break;
                }
                case rust::EngineOp::LogScreenshot: {
                    log("错误现场已保存：" + filePath);
                    break;
                }
                case rust::EngineOp::SetScreenshotPath: {
                    errorEvent["screenshot"] = filePath;
                    break;
                }
                case rust::EngineOp::DisposeScreenshot: {
                    file.reset();
                    break;
                }
                case rust::EngineOp::AppendNoticeImage: {
                    errorNotice->images << png;
                    break;
                }
                default:
                    fail("Rust ErrorScreenshot 宿主操作无效");
                }
            });
            break;
        }
        case rust::EngineOp::LogScreenshotFailure: {
            try {
                std::rethrow_exception(error);
            } catch (const std::exception &e) {
                log("错误现场保存失败：" + errorText(e));
            }
            break;
        }
        case rust::EngineOp::SetScreenshotError: {
            try {
                std::rethrow_exception(error);
            } catch (const std::exception &e) {
                errorEvent["screenshot_error"] = errorText(e);
            }
            break;
        }
        case rust::EngineOp::SaveErrorDiagnostic: {
            errorEvent = saveDiagnostic(data_, errorEvent);
            break;
        }
        case rust::EngineOp::AssignDiagnosticReport: {
            diagnosticReport = errorEvent["diagnostic_report"].toString();
            break;
        }
        case rust::EngineOp::LogDiagnosticReport: {
            log("任务异常，自动操作已暂停。诊断记录：" + diagnosticReport);
            break;
        }
        case rust::EngineOp::EmitErrorEvent: {
            emit message(errorEvent);
            break;
        }
        case rust::EngineOp::NotifyError: {
            notify(std::move(*errorNotice));
            break;
        }
        case rust::EngineOp::SetExitAfterFalse: {
            exitAfter = false;
            break;
        }
        case rust::EngineOp::PostStatus: {
            postStatus(epoch);
            break;
        }
        case rust::EngineOp::ResultRecorded: {
            out[0] = resultRecorded;
            break;
        }
        case rust::EngineOp::RecordSelectedResult: {
            recordResult(selected, completed);
            break;
        }
        case rust::EngineOp::ExitAfter: {
            out[0] = exitAfter;
            break;
        }
        case rust::EngineOp::QueueExit: {
            QMetaObject::invokeMethod(
                this,
                [this, epoch] {
                    rust::engineRun(rust::EngineMethod::ScheduleExit,
                                    [&](rust::EngineOp op, const double *a, double *out,
                                        const std::exception_ptr &error) {
                                        switch (op) {
                                        case rust::EngineOp::GenerationMatches: {
                                            out[0] = generation_ == epoch;
                                            break;
                                        }
                                        case rust::EngineOp::HasPending: {
                                            out[0] = bool(pendingTask_);
                                            break;
                                        }
                                        case rust::EngineOp::LogWaitingExit: {
                                            log("任务结束，正在等待通知队列处理后退出。");
                                            break;
                                        }
                                        case rust::EngineOp::StartExitWait: {
                                            exitWait_.start();
                                            break;
                                        }
                                        case rust::EngineOp::StartExitTimer: {
                                            exitTimer_.start();
                                            break;
                                        }
                                        default:
                                            fail("Rust ScheduleExit 宿主操作无效");
                                        }
                                    });
                },
                Qt::QueuedConnection);
            break;
        }
        case rust::EngineOp::SetWorkerFinished: {
            workerFinished_ = a[0] != 0;
            break;
        }
        default:
            fail("Rust SessionLifetime 宿主操作无效");
        }
    });
}

void Engine::scriptCommand(QJsonObject request) {
    const QJsonValue id = request.value("id");
    QJsonObject entry, field, state, settings, result, reply, command;
    QJsonArray fields, changed;
    QString ident, key;
    QJsonValue value;
    bool configuring = false;
    rust::engineRun(rust::EngineMethod::ScriptCommand, [&](rust::EngineOp op, const double *a, double *out,
                                                           const std::exception_ptr &error) {
        switch (op) {
        case rust::EngineOp::ScriptsBusy:
            out[0] = scriptsBusy_;
            break;
        case rust::EngineOp::ScheduleScriptRetry:
            QTimer::singleShot(50, this, [this, request] { scriptCommand(request); });
            break;
        case rust::EngineOp::RequestConfigure:
            out[0] = request["op"] == "script_configure";
            break;
        case rust::EngineOp::RequestKeyKind: {
            const auto bytes = request["key"].toString().toUtf8();
            out[0] = rust::okww_rs_engine_script_key(reinterpret_cast<const uint8_t *>(bytes.constData()),
                                                     size_t(bytes.size()));
            break;
        }
        case rust::EngineOp::ScriptsCount:
            out[0] = double(scripts_.size());
            break;
        case rust::EngineOp::SelectScriptEntry:
            entry = scripts_.at(int(a[0])).toObject();
            break;
        case rust::EngineOp::LoadEntryFields:
            fields = entry["fields"].toArray();
            break;
        case rust::EngineOp::ScriptValueMatches:
            out[0] = std::as_const(entry)["id"] == request["script"];
            break;
        case rust::EngineOp::ScriptTrigger:
            out[0] = std::as_const(entry)["trigger"].toBool();
            break;
        case rust::EngineOp::ScriptFieldsCount:
            out[0] = double(fields.size());
            break;
        case rust::EngineOp::SelectScriptField:
            field = fields.at(int(a[0])).toObject();
            break;
        case rust::EngineOp::ScriptFieldMatches:
            out[0] = field["key"] == (configuring ? QJsonValue(key) : request["key"]);
            break;
        case rust::EngineOp::Paused:
            out[0] = paused_;
            break;
        case rust::EngineOp::WorkerFinished:
            out[0] = workerFinished_;
            break;
        case rust::EngineOp::HasPending:
            out[0] = bool(pendingTask_);
            break;
        case rust::EngineOp::ScriptStateExists:
            ident = request["script"].toString();
            key = request["key"].toString();
            value = request["value"];
            out[0] = QFile::exists(data_ + "/native-scripts.json");
            break;
        case rust::EngineOp::ReadScriptState:
            state = readJson(data_ + "/native-scripts.json");
            break;
        case rust::EngineOp::EmptyScriptState:
            state = {};
            break;
        case rust::EngineOp::InitializeScriptChanges:
            settings = state[ident].toObject();
            changed = scripts_;
            configuring = true;
            break;
        case rust::EngineOp::ChangedScriptCount:
            out[0] = double(changed.size());
            break;
        case rust::EngineOp::SelectChangedScript:
            entry = changed.at(int(a[0])).toObject();
            break;
        case rust::EngineOp::ScriptIdentMatches:
            out[0] = entry["id"] == ident;
            break;
        case rust::EngineOp::RequestValueBool:
            out[0] = value.isBool();
            break;
        case rust::EngineOp::SetScriptBoolean: {
            const auto property = a[0] == 0 ? "enabled" : "continuous";
            settings[property] = value;
            entry[property] = value;
            break;
        }
        case rust::EngineOp::ScriptFlagExists:
            out[0] = scriptEnabled_.contains(ident);
            break;
        case rust::EngineOp::StoreScriptFlagValue:
            scriptEnabled_.value(ident)->store(value.toBool());
            break;
        case rust::EngineOp::RequestValueNumber:
            out[0] = value.isDouble();
            break;
        case rust::EngineOp::RequestValueDouble:
            out[0] = value.toDouble();
            break;
        case rust::EngineOp::RequestValueInt:
            out[0] = value.toInt();
            break;
        case rust::EngineOp::SetScriptRepeat:
            settings["repeat"] = value;
            entry["repeat"] = value;
            break;
        case rust::EngineOp::SetFieldValue:
            field["value"] = value;
            break;
        case rust::EngineOp::StoreField:
            fields[int(a[0])] = field;
            break;
        case rust::EngineOp::StoreScriptValues: {
            auto values = settings["values"].toObject();
            values[key] = value;
            settings["values"] = values;
            entry["fields"] = fields;
            break;
        }
        case rust::EngineOp::StoreChangedScript:
            changed[int(a[0])] = entry;
            break;
        case rust::EngineOp::StoreScriptSettings:
            state[ident] = settings;
            break;
        case rust::EngineOp::WriteScriptState:
            writeJson(data_ + "/native-scripts.json", state);
            break;
        case rust::EngineOp::CommitChangedScripts:
            scripts_ = changed;
            break;
        case rust::EngineOp::WriteScriptCatalog:
            writeJson(data_ + "/native-script-catalog.json",
                      {{"scripts", scripts_}, {"templates", scriptTemplates_}});
            break;
        case rust::EngineOp::ResultCatalog:
            result = catalog();
            break;
        case rust::EngineOp::EmitCatalogChanged:
            emit message({{"event", "catalog_changed"}, {"data", result}});
            break;
        case rust::EngineOp::ReplySuccess:
            reply = {{"id", id}, {"ok", true}, {"data", result}};
            break;
        case rust::EngineOp::ReplyError:
            try {
                std::rethrow_exception(error);
            } catch (const std::exception &e) {
                reply = {{"id", id}, {"ok", false}, {"error", errorText(e)}};
            }
            break;
        case rust::EngineOp::EmitReply:
            emit message(reply);
            break;
        case rust::EngineOp::SetScriptsBusy:
            scriptsBusy_ = a[0] != 0;
            break;
        case rust::EngineOp::BuildScriptManageCommand:
            command = request;
            command["op"] = request["op"].toString().mid(7);
            break;
        case rust::EngineOp::SpawnScriptManage: {
            auto watcher = new QFutureWatcher<QJsonObject>(this);
            connect(watcher, &QFutureWatcher<QJsonObject>::finished, this, [this, watcher, id] {
                const auto response = watcher->result();
                QJsonObject result, reply;
                rust::engineRun(
                    rust::EngineMethod::ScriptReply,
                    [&](rust::EngineOp action, const double *args, double *out, const std::exception_ptr &) {
                        switch (action) {
                        case rust::EngineOp::DeleteWatcher:
                            watcher->deleteLater();
                            break;
                        case rust::EngineOp::SetScriptsBusy:
                            scriptsBusy_ = args[0] != 0;
                            break;
                        case rust::EngineOp::ResponseOk:
                            out[0] = response["ok"].toBool();
                            break;
                        case rust::EngineOp::AdoptResponseScripts:
                            scripts_ = response["data"].toObject()["scripts"].toArray();
                            break;
                        case rust::EngineOp::AdoptResponseTemplates:
                            scriptTemplates_ = response["data"].toObject()["templates"].toArray();
                            break;
                        case rust::EngineOp::ResultCatalog:
                            result = catalog();
                            break;
                        case rust::EngineOp::ResultSelectedScript:
                            result["selected_script"] = response["data"].toObject()["selected"];
                            break;
                        case rust::EngineOp::ResultSnippet:
                            result["snippet"] = response["data"].toObject()["snippet"];
                            break;
                        case rust::EngineOp::EmitCatalogChanged:
                            emit message({{"event", "catalog_changed"}, {"data", result}});
                            break;
                        case rust::EngineOp::ReplySuccess:
                            reply = {{"id", id}, {"ok", true}, {"data", result}};
                            break;
                        case rust::EngineOp::EmitReply:
                            emit message(reply);
                            break;
                        case rust::EngineOp::ReplyResponseError:
                            emit message({{"id", id}, {"ok", false}, {"error", response["error"]}});
                            break;
                        case rust::EngineOp::LoadingScripts:
                            out[0] = loadingScripts_;
                            break;
                        case rust::EngineOp::SetLoadingScripts:
                            loadingScripts_ = args[0] != 0;
                            break;
                        case rust::EngineOp::EmitReady:
                            emit message({{"event", "ready"}, {"data", catalog()}});
                            break;
                        default:
                            fail("Rust 脚本完成宿主操作无效");
                        }
                    });
            });
            watcher->setFuture(QtConcurrent::run([root = root_, data = data_, command] {
                QJsonObject reply;
                rust::engineRun(
                    rust::EngineMethod::ScriptManage,
                    [&](rust::EngineOp action, const double *, double *, const std::exception_ptr &error) {
                        switch (action) {
                        case rust::EngineOp::ScriptManage:
                            reply = {{"ok", true}, {"data", scriptManage(root, data, command)}};
                            break;
                        case rust::EngineOp::ReplyError:
                            try {
                                std::rethrow_exception(error);
                            } catch (const std::exception &e) {
                                reply = {{"ok", false}, {"error", errorText(e)}};
                            }
                            break;
                        default:
                            fail("Rust 脚本管理宿主操作无效");
                        }
                    });
                return reply;
            }));
            break;
        }
        case rust::EngineOp::Error:
            switch (int(a[0])) {
            case 11:
                fail("请先暂停任务，再修改或重新载入脚本");
            case 12:
                fail("脚本开关需要布尔值");
            case 13:
                fail("循环次数应在 1 至 1000000 之间");
            case 14:
                fail("脚本参数不存在");
            case 15:
                fail("脚本已不存在");
            default:
                fail("Rust 脚本错误标识无效");
            }
        default:
            fail("Rust 脚本配置宿主操作无效");
        }
    });
}
void Engine::invalidateParty() {
    rust::engineRun(rust::EngineMethod::InvalidateParty,
                    [&](rust::EngineOp op, const double *a, double *out, const std::exception_ptr &error) {
                        switch (op) {
                        case rust::EngineOp::SetTaskInCombat: {
                            taskState_->inCombat = a[0] != 0;
                            break;
                        }
                        case rust::EngineOp::SetRetryingStep: {
                            taskState_->retryingStep = a[0] != 0;
                            break;
                        }
                        case rust::EngineOp::IncrementCombatRevision: {
                            ++taskState_->combatEnableRevision;
                            break;
                        }
                        case rust::EngineOp::EmitPartyInvalidated: {
                            emit message({{"event", "party_invalidated"}});
                            break;
                        }
                        default:
                            fail("Rust InvalidateParty 宿主操作无效");
                        }
                    });
}
void Engine::switchTeamCode(QJsonObject request) {
    rust::engineRun(rust::EngineMethod::SwitchTeam, [&](rust::EngineOp op, const double *a, double *out,
                                                        const std::exception_ptr &error) {
        switch (op) {
        case rust::EngineOp::TeamBusy: {
            out[0] = teamSwitchBusy_;
            break;
        }
        case rust::EngineOp::Paused: {
            out[0] = paused_;
            break;
        }
        case rust::EngineOp::WorkerFinished: {
            out[0] = workerFinished_;
            break;
        }
        case rust::EngineOp::HasPending: {
            out[0] = bool(pendingTask_);
            break;
        }
        case rust::EngineOp::WorkerJoinable: {
            out[0] = worker_.joinable();
            break;
        }
        case rust::EngineOp::JoinWorker: {
            worker_.join();
            break;
        }
        case rust::EngineOp::SnapshotTeams: {
            teamSwitchSnapshot_ = teams_->catalog();
            break;
        }
        case rust::EngineOp::SetTeamBusy: {
            teamSwitchBusy_ = a[0] != 0;
            break;
        }
        case rust::EngineOp::SpawnTeamSwitch: {
            auto watcher = new QFutureWatcher<QJsonObject>(this);
            connect(watcher, &QFutureWatcher<QJsonObject>::finished, this, [this, watcher, request] {
                auto reply = watcher->result();
                QJsonObject data;
                rust::engineRun(
                    rust::EngineMethod::TeamReply,
                    [&](rust::EngineOp op, const double *a, double *out, const std::exception_ptr &error) {
                        switch (op) {
                        case rust::EngineOp::DeleteWatcher: {
                            watcher->deleteLater();
                            break;
                        }
                        case rust::EngineOp::SetTeamBusy: {
                            teamSwitchBusy_ = a[0] != 0;
                            break;
                        }
                        case rust::EngineOp::InvalidateParty: {
                            invalidateParty();
                            break;
                        }
                        case rust::EngineOp::ReplyRequestId: {
                            reply["id"] = request["id"];
                            break;
                        }
                        case rust::EngineOp::ResultCatalog: {
                            data = catalog();
                            break;
                        }
                        case rust::EngineOp::ResultTeamElapsed: {
                            data["team_switch_ms"] = reply.take("elapsed_ms");
                            break;
                        }
                        case rust::EngineOp::EmitCatalogChanged: {
                            emit message({{"event", "catalog_changed"}, {"data", data}});
                            break;
                        }
                        case rust::EngineOp::ReplyData: {
                            reply["data"] = data;
                            break;
                        }
                        case rust::EngineOp::ReplyError: {
                            try {
                                std::rethrow_exception(error);
                            } catch (const std::exception &e) {
                                reply["ok"] = false;
                                reply["error"] = errorText(e);
                            }
                            break;
                        }
                        case rust::EngineOp::EmitReply: {
                            emit message(reply);
                            break;
                        }
                        default:
                            fail("Rust TeamReply 宿主操作无效");
                        }
                    });
            });
            watcher->setFuture(QtConcurrent::run([teams = teams_, request] {
                QElapsedTimer elapsed;
                QJsonObject reply;
                rust::engineRun(
                    rust::EngineMethod::TeamWork,
                    [&](rust::EngineOp op, const double *a, double *out, const std::exception_ptr &error) {
                        switch (op) {
                        case rust::EngineOp::StartTeamTimer: {
                            elapsed.start();
                            break;
                        }
                        case rust::EngineOp::RequestCommand: {
                            const auto bytes = request["op"].toString().toUtf8();
                            out[0] = rust::okww_rs_engine_command_kind(
                                reinterpret_cast<const uint8_t *>(bytes.constData()), size_t(bytes.size()));
                            break;
                        }
                        case rust::EngineOp::SetTeamMode: {
                            teams->setMode(request["team"].toArray(), request["mode"].toString());
                            break;
                        }
                        case rust::EngineOp::SetAllTeamModes: {
                            teams->setAllModes(request["mode"].toString());
                            break;
                        }
                        case rust::EngineOp::ReplyOk: {
                            reply["ok"] = true;
                            break;
                        }
                        case rust::EngineOp::ReplyError: {
                            try {
                                std::rethrow_exception(error);
                            } catch (const std::exception &e) {
                                reply["ok"] = false;
                                reply["error"] = errorText(e);
                            }
                            break;
                        }
                        case rust::EngineOp::ReplyElapsed: {
                            reply["elapsed_ms"] = double(elapsed.elapsed());
                            break;
                        }
                        default:
                            fail("Rust TeamWork 宿主操作无效");
                        }
                    });
                return reply;
            }));
            break;
        }
        case rust::EngineOp::Error: {
            switch (int(a[0])) {
            case 6:
                fail("正在切换队伍代码，请等待完成");
            case 7:
                fail("请先暂停任务，再修改队伍。");
            default:
                fail("Rust 队伍错误标识无效");
            }
            break;
        }
        default:
            fail("Rust SwitchTeam 宿主操作无效");
        }
    });
}
void Engine::command(const QJsonObject &request) {
    QJsonObject reply{{"id", request["id"]}}, result;
    const TaskDefinition *native = nullptr;
    rust::engineRun(rust::EngineMethod::Command, [&](rust::EngineOp op, const double *a, double *out,
                                                     const std::exception_ptr &error) {
        switch (op) {
        case rust::EngineOp::RequestCommand: {
            const auto value = request["op"].toString().toUtf8();
            out[0] = rust::okww_rs_engine_command_kind(reinterpret_cast<const uint8_t *>(value.constData()),
                                                       size_t(value.size()));
            break;
        }
        case rust::EngineOp::SwitchTeam:
            switchTeamCode(request);
            break;
        case rust::EngineOp::ScriptCommand:
            scriptCommand(request);
            break;
        case rust::EngineOp::ResultCatalog:
            result = catalog();
            break;
        case rust::EngineOp::ResultStatus:
            result = status(a[0] != 0 ? request["task"].toString() : QString{});
            break;
        case rust::EngineOp::Pause:
            pause();
            break;
        case rust::EngineOp::PendingTaskNonempty:
            out[0] = pendingTask_ && !pendingTask_->isEmpty();
            break;
        case rust::EngineOp::PendingTaskEmpty:
            pendingTask_ = QString{};
            break;
        case rust::EngineOp::PendingExitFalse:
            pendingOptions_["exit_after"] = false;
            break;
        case rust::EngineOp::PendingLimitZero:
            pendingOptions_["limit_seconds"] = 0;
            break;
        case rust::EngineOp::SetForeground:
            foreground_ = a[0] != 0;
            break;
        case rust::EngineOp::SetCancelForeground:
            cancelForeground_ = a[0] != 0;
            break;
        case rust::EngineOp::Foreground:
            out[0] = foreground_;
            break;
        case rust::EngineOp::Paused:
            out[0] = paused_;
            break;
        case rust::EngineOp::StartRequest:
            start(request["task"].toString(), request);
            break;
        case rust::EngineOp::TaskIsScript:
            out[0] = request["task"].toString().startsWith("script:");
            break;
        case rust::EngineOp::GroupIsScript:
            out[0] = request["group"].toString().startsWith("script:");
            break;
        case rust::EngineOp::ScriptConfigureTask: {
            auto q = request;
            q["op"] = "script_configure";
            q["script"] = request["task"];
            scriptCommand(q);
            break;
        }
        case rust::EngineOp::ScriptResetGroup: {
            auto q = request;
            q["op"] = "script_reset";
            q["script"] = request["group"];
            scriptCommand(q);
            break;
        }
        case rust::EngineOp::ScriptToggleTask: {
            auto q = request;
            q["op"] = "script_configure";
            q["script"] = request["task"].toString();
            q["key"] = "_enabled";
            q["value"] = request["enabled"];
            scriptCommand(q);
            break;
        }
        case rust::EngineOp::SetProfile:
            profile_.set(request[a[0] != 0 ? "task" : "group"].toString(), request["key"].toString(),
                         request["value"]);
            break;
        case rust::EngineOp::ResetProfile:
            profile_.reset(request["group"].toString());
            break;
        case rust::EngineOp::RequestEnabled:
            out[0] = request["enabled"].toBool();
            break;
        case rust::EngineOp::NativeCount:
            out[0] = double(tasks_.size());
            break;
        case rust::EngineOp::NativeSelect:
            native = &tasks_.at(int(a[0]));
            break;
        case rust::EngineOp::NativeMatches:
            out[0] = native->id == request["task"].toString();
            break;
        case rust::EngineOp::ProfileWasEnabled:
            out[0] = profile_.value(request["task"].toString(), "_enabled", false).toBool();
            break;
        case rust::EngineOp::SetTrigger:
            profile_.set(request["task"].toString(), "_enabled", request["enabled"]);
            break;
        case rust::EngineOp::RequestAutoCombat:
            out[0] = request["task"].toString() == "AutoCombatTask";
            break;
        case rust::EngineOp::IncrementCombatRevision:
            ++taskState_->combatEnableRevision;
            break;
        case rust::EngineOp::TeamBusy:
            out[0] = teamSwitchBusy_;
            break;
        case rust::EngineOp::WorkerFinished:
            out[0] = workerFinished_;
            break;
        case rust::EngineOp::HasPending:
            out[0] = bool(pendingTask_);
            break;
        case rust::EngineOp::WorkerJoinable:
            out[0] = worker_.joinable();
            break;
        case rust::EngineOp::JoinWorker:
            worker_.join();
            break;
        case rust::EngineOp::TeamCode:
            result = teams_->code(request["team"].toArray(), request["member"].toString());
            break;
        case rust::EngineOp::TeamEditor:
            result = teams_->editor(request["team"].toArray(), request["member"].toString(),
                                    request["source"].toString(), request["view"].toString("active"));
            break;
        case rust::EngineOp::TeamExport:
            teams_->exportTeam(request["team"].toArray(), request["path"].toString());
            break;
        case rust::EngineOp::TeamCreate:
            teams_->createTeam(request["team"].toArray());
            break;
        case rust::EngineOp::TeamDelete:
            teams_->removeTeam(request["team"].toArray());
            break;
        case rust::EngineOp::TeamSave:
            teams_->saveDraft(request["team"].toArray(), request["member"].toString(),
                              request["code"].toString());
            break;
        case rust::EngineOp::TeamImport:
            teams_->importTeam(request["path"].toString());
            break;
        case rust::EngineOp::InvalidateParty:
            invalidateParty();
            break;
        case rust::EngineOp::ReplySuccess:
            reply["ok"] = true;
            reply["data"] = result;
            break;
        case rust::EngineOp::ReplyError:
            try {
                std::rethrow_exception(error);
            } catch (const std::exception &e) {
                reply["ok"] = false;
                reply["error"] = errorText(e);
            }
            break;
        case rust::EngineOp::EmitReply:
            emit message(reply);
            break;
        case rust::EngineOp::Error:
            switch (int(a[0])) {
            case 6:
                fail("正在切换队伍代码，请等待完成");
            case 7:
                fail("请先暂停任务，再修改队伍。");
            case 8:
                fail("当前引擎暂不支持此实时任务。");
            case 9:
                fail("未知队伍操作");
            case 10:
                fail("未知原生执行器操作：" + request["op"].toString());
            default:
                fail("Rust 命令错误标识无效");
            }
        default:
            fail("Rust 命令宿主操作无效");
        }
    });
}
} // namespace okww::runtime
