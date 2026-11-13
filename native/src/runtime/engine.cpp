#include "engine.hpp"
#include "app_info.hpp"
#include "scripts.hpp"
#include "task_registry.hpp"
#include "resource_failure.hpp"
#include "diagnostics.hpp"
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
    try {
        scripts_ = scriptCatalog(data_);
        if (QFile::exists(data_ + "/native-script-catalog.json"))
            scriptTemplates_ = readJson(data_ + "/native-script-catalog.json")["templates"].toArray();
    } catch (...) {
        // A disposable metadata cache must never prevent opening the profile.
    }
    tasks_ += businessTasks([](const QString &) -> bool { fail("仅供目录展示的任务不可执行"); });
    launchTimer_.setInterval(50);
    connect(&launchTimer_, &QTimer::timeout, this, &Engine::tryLaunch);
    QString models = root_ + "/assets/ocr/ppocrv5";
    if (!QFile::exists(models + "/det/det.onnx"))
        models = root_ + "/.venv/Lib/site-packages/onnxocr/models/ppocrv5";
    notifications_ = std::make_unique<Notifications>(models, [this](QString text) { log(text); });
    exitTimer_.setInterval(100);
    connect(&exitTimer_, &QTimer::timeout, this, [this] {
        if (!paused_) {
            exitTimer_.stop();
            return;
        }
        if (notifications_->idle() || exitWait_.elapsed() > 180000) {
            exitTimer_.stop();
            emit message({{"event", "quit_requested"}});
        }
    });
}
Engine::~Engine() {
    pause();
    if (worker_.joinable())
        worker_.join();
    if (modelWorker_.joinable())
        modelWorker_.join();
    notifications_->stop();
}
void Engine::log(QString text) { emit message({{"event", "log"}, {"text", text}}); }
void Engine::notify(Notice notice) {
    const auto config = profile_.group("Notification");
    if (config["System Notification"].toBool(true))
        emit message({{"event", "notification"},
                      {"title", notice.title},
                      {"text", notice.text},
                      {"error", notice.error}});
    notifications_->submit(config, std::move(notice));
}
void Engine::postStatus(uint64_t generation, QString current) {
    QMetaObject::invokeMethod(
        this,
        [this, generation, current] {
            if (generation_ == generation)
                emit message({{"event", "status"}, {"data", status(foreground_ ? current : QString{})}});
        },
        Qt::QueuedConnection);
}
QJsonObject Engine::catalog() const {
    auto result = profile_.catalog();
    QJsonArray tasks;
    for (auto v : result["tasks"].toArray()) {
        auto t = v.toObject();
        const auto id = t["id"].toString();
        const bool supported =
            std::any_of(tasks_.begin(), tasks_.end(), [&](const auto &item) { return item.id == id; });
        t["native_supported"] = supported;
        if (!supported)
            t["description"] = t["description"].toString() + "\n此版本不支持该任务。";
        if (!supported)
            t["name"] = t["name"].toString() + "（待迁移）";
        tasks.append(t);
    }
    for (auto value : scripts_) {
        auto script = value.toObject();
        script.remove("source");
        tasks.append(script);
    }
    result["tasks"] = tasks;
    result["scripts"] = scripts_;
    result["script_templates"] = scriptTemplates_;
    // Native rotations stay in-process. Workshop classes use an isolated
    // Python child and the same C++ capture/input ownership.
    result["teams"] = teamSwitchBusy_ ? teamSwitchSnapshot_ : teams_->catalog();
    return result;
}
QJsonObject Engine::status(QString current) const {
    QJsonArray states;
    for (auto v : profile_.catalog()["tasks"].toArray()) {
        auto t = v.toObject();
        states.append(QJsonObject{{"id", t["id"]}, {"enabled", t["enabled"]}, {"paused", bool(paused_)}});
    }
    for (auto value : scripts_) {
        const auto script = value.toObject();
        states.append(
            QJsonObject{{"id", script["id"]}, {"enabled", script["enabled"]}, {"paused", bool(paused_)}});
    }
    return {{"paused", bool(paused_)}, {"warming", bool(warming_)}, {"current", current}, {"tasks", states}};
}
void Engine::load() {
    preloadModels();
    log("C++ 执行器已就绪；Python 工坊按需加载，采集和盒子仍由 C++ 管理。");
    if (QDir(data_ + "/ok_tasks").exists() || QDir(data_ + "/ok_import").exists()) {
        loadingScripts_ = true;
        scriptCommand({{"id", 0}, {"op", "script_catalog"}});
    } else
        emit message({{"event", "ready"}, {"data", catalog()}});
}
void Engine::pause() {
    std::lock_guard lock(lifecycle_);
    pendingTask_.reset();
    launchTimer_.stop();
    gate_.stop();
    generation_ = 0;
    paused_ = true;
    foreground_ = false;
    cancelForeground_ = false;
    try {
        backend_.arm(false);
    } catch (const std::exception &e) {
        log(errorText(e));
    }
}
void Engine::start(const QString &task, QJsonObject options) {
    if (teamSwitchBusy_)
        fail("正在切换队伍代码，请等待完成后启动");
    if (scriptsBusy_)
        fail("正在更新脚本文件，请等待保存完成后启动");
    exitTimer_.stop();
    const auto limit = options["limit_seconds"].toDouble(0);
    if (!std::isfinite(limit) || limit < 0 || limit > 168 * 3600)
        fail("任务最长运行时间无效");
    const auto script = std::find_if(scripts_.begin(), scripts_.end(),
                                     [&](const auto &value) { return value.toObject()["id"] == task; });
    if (script != scripts_.end() && !script->toObject()["native_supported"].toBool())
        fail(script->toObject()["error"].toString("脚本加载失败"));
    if (!task.isEmpty() && script == scripts_.end() &&
        std::none_of(tasks_.begin(), tasks_.end(), [&](const auto &t) { return t.id == task; }))
        fail("此任务尚未完成 C++ 迁移，请使用保留的兼容版。");
    if (task.isEmpty())
        for (auto v : profile_.catalog()["tasks"].toArray()) {
            auto t = v.toObject();
            const auto id = t["id"].toString();
            if (t["trigger"].toBool() && t["enabled"].toBool() && id != "RemoteMouseResetTask" &&
                std::none_of(tasks_.begin(), tasks_.end(), [&](const auto &item) { return item.id == id; }))
                fail("已启用的“" + t["name"].toString() + "”尚未迁移，请先关闭该项再测试已迁移功能。");
        }
    pause();
    preloadModels();
    pendingTask_ = task;
    foreground_ = !task.isEmpty();
    pendingOptions_ = options;
    pendingOptions_["scripts_snapshot"] = scripts_;
    tryLaunch();
    if (pendingTask_)
        launchTimer_.start();
}
void Engine::tryLaunch() {
    if (!pendingTask_ || !workerFinished_)
        return;
    if (worker_.joinable())
        worker_.join();
    const auto task = *pendingTask_;
    pendingTask_.reset();
    launchTimer_.stop();
    paused_ = false;
    const auto epoch = gate_.start();
    generation_ = epoch;
    workerFinished_ = false;
    scriptEnabled_.clear();
    for (auto value : pendingOptions_["scripts_snapshot"].toArray()) {
        const auto script = value.toObject();
        scriptEnabled_[script["id"].toString()] =
            std::make_shared<std::atomic<bool>>(script["enabled"].toBool());
    }
    try {
        worker_ = std::thread(&Engine::run, this, task, epoch, pendingOptions_);
    } catch (...) {
        workerFinished_ = true;
        gate_.stop();
        paused_ = true;
        throw;
    }
}
void Engine::preloadModels() {
    if (modelWorker_.joinable())
        return;
    modelWorker_ = std::thread([this] {
        QElapsedTimer elapsed;
        elapsed.start();
        try {
            templates_ = std::make_unique<Templates>(root_ + "/assets/coco_annotations.json");
            QString models = root_ + "/assets/ocr/ppocrv5";
            if (!QFile::exists(models + "/det/det.onnx"))
                models = root_ + "/.venv/Lib/site-packages/onnxocr/models/ppocrv5";
            ocr_ = std::make_unique<Ocr>(models);
            echo_ = std::make_unique<EchoDetector>(root_ + "/assets/echo_model/echo.onnx");
            log(QString("识别资源后台预加载完成，用时 %1 ms；后续启动复用。").arg(elapsed.elapsed()));
        } catch (const std::exception &error) {
            modelError_ = errorText(error);
            log("识别资源准备失败：" + modelError_);
        }
        modelsReady_ = true;
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
        try {
            writeJson(data_ + "/native-last-run.json",
                      {{"task", task}, {"success", success},
                       {"error", success ? QString{} : failure},
                       {"finished", QDateTime::currentDateTime().toString(Qt::ISODate)},
                       {"diagnostic_report", diagnosticReport}, {"seconds", clock.now() - began}});
        } catch (const std::exception &e) {
            log("运行结果保存失败：" + errorText(e));
        }
    };
    EmergencyMemory emergency;
    try {
        KeepAwake awake;
        if (!awake.active())
            log("未能阻止任务运行期间自动休眠，请检查系统电源设置。");
        warming_ = true;
        postStatus(epoch, selected);
        log("正在准备任务，检查工坊代码与识别资源；可随时暂停。");
        log(emergency.available() ? "已预留 64 MiB 识别应急内存。" : "可用提交内存不足，未建立识别应急缓冲。");
        Execution preparation(gate_, epoch, clock);
        preparation.checkpoint();
        if (!selected.startsWith("script:"))
            teams_->validateEnabled();
        preparation.checkpoint();
        while (!modelsReady_)
            preparation.wait(.02);
        if (!modelError_.isEmpty())
            fail(modelError_);
        preparation.checkpoint();
        // Serialize arming with Pause so a cancelled warm-up cannot re-arm HID.
        {
            std::lock_guard lock(lifecycle_);
            preparation.checkpoint();
            backend_.arm(true);
        }
        warming_ = false;
        log(QString("任务准备完成，用时 %1 ms。").arg(qRound64((clock.now() - began) * 1000)));
        postStatus(epoch, selected);
        auto config = Config::load(data_ + "/dualpc.json");
        auto frame = backend_.latestFrame();
        if (!frame)
            fail("采集画面尚未就绪");
        std::function<void()> pollControls;
        bool taskRunning = false;
        Execution execution(gate_, epoch, clock, [&] {
            if (!backend_.heartbeat()) {
                // Explicit Pause invalidates the epoch first. A device watchdog
                // disarm while this epoch is live is an error, not user cancel.
                if (!gate_.active(epoch)) throw Cancelled();
                const auto state = backend_.snapshot();
                throw ExecutionFailure(state["error"].toString().isEmpty()
                    ? QString("采集或盒子连接已中断，自动操作已暂停") : state["error"].toString(),
                    QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Indented)));
            }
            if (taskRunning && foreground_ && cancelForeground_)
                throw Cancelled();
            if (taskRunning && pollControls)
                pollControls();
            if (limit > 0 && clock.now() - began >= limit)
                fail("任务达到设置的最长运行时间，已停止并释放按键");
        });
        Input input(
            config, {frame->width, frame->height}, backend_.snapshot()["box"].toObject()["absolute"].toBool(),
            [&](auto command) { return backend_.nativeCommand(command); }, execution);
        Templates activeTemplates(*templates_);
        activeTemplates.setCheckpoint([&] { execution.checkpoint(); });
        const auto userTemplates = data_ + "/ok_templates/coco_annotations.json";
        if (QFile::exists(userTemplates))
            activeTemplates.add(userTemplates);
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
                    return ocr_->read(image, area, threshold, true, false,
                                      [&] { execution.checkpoint(); });
                });
            });
        context.info = [&](auto key, auto value) { log(key + "：" + value); };
        context.sourceSequence = [&] { return captureSequence; };
        context.combatInfo = [this, epoch, &lastCombat](QJsonObject value) {
            lastCombat = value;
            QMetaObject::invokeMethod(this, [this, epoch, value = std::move(value)] {
                if (generation_ == epoch && !paused_)
                    emit message({{"event", "combat_info"}, {"data", value}});
            }, Qt::QueuedConnection);
        };
        context.notify = [&](QString title, QString text, bool error) { notify({title, text, {}, error}); };
        context.diagnostic = [&](QString reason, QJsonObject event) {
            event["error"] = reason;
            event["task"] = context.task;
            event["phase"] = "task_step";
            // Capture before daily recovery backs out of the failed page.
            // Diagnostic failures must not replace the task's original error.
            try {
                const auto folder = data_ + "/screenshots/errors";
                const auto filePath = folder + '/' + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") + "-step.png";
                std::vector<uchar> bytes;
                if (!context.frame.empty() && QDir().mkpath(folder) && cv::imencode(".png", context.frame, bytes)) {
                    QSaveFile file(filePath);
                    if (file.open(QIODevice::WriteOnly) && file.write(reinterpret_cast<const char *>(bytes.data()),
                            qint64(bytes.size())) == qint64(bytes.size()) && file.commit())
                        event["screenshot"] = filePath;
                }
                const auto saved = saveDiagnostic(data_, event);
                if (stepDiagnostics.size() >= 3) stepDiagnostics.removeAt(0);
                stepDiagnostics.append(saved["diagnostic_report"]);
                log("步骤识别现场：" + saved["diagnostic_report"].toString());
            } catch (const std::exception &error) {
                log("步骤诊断保存失败：" + errorText(error));
            }
        };
        context.state = taskState_;
        context.gameLanguage = config.get("game_language").toString("zh_CN");
        context.physicalFDown = [this] { return backend_.physicalKeyDown('F'); };
        auto runner = std::make_unique<TaskRunner>(context, *echo_, root_, data_, teams_);
        ScriptRuntime scripts(context, root_, data_);
        scripts.builtin = [&](const QString &id) {
            const auto definitions =
                businessTasks([&](const QString &task) { return runner->dispatch(task); });
            if (std::none_of(definitions.begin(), definitions.end(),
                             [&](const auto &task) { return task.id == id; }))
                fail("脚本请求的内置任务不存在：" + id);
            return runner->dispatch(id);
        };
        // Observe physical key edges on the execution thread, including while
        // a long macro is waiting. Scripted remote F is never seen as local F.
        pollControls = [&] { runner->pollControls(); };
        auto runnable = builtinTasks();
        runnable += businessTasks([&](const QString &id) { return runner->dispatch(id); });
        QMap<QString, QJsonObject> custom;
        for (auto value : options["scripts_snapshot"].toArray()) {
            const auto script = value.toObject();
            const auto id = script["id"].toString();
            custom[id] = script;
            log(QString("载入通用脚本：%1 · %2 · %3")
                    .arg(script["name"].toString(), script["trigger"].toBool() ? "实时触发" : "任务",
                         scriptEnabled_.value(id)->load() ? "已启用" : "未启用"));
            runnable.append(
                {id, script["trigger"].toBool(), std::max(.05, script["interval"].toDouble(.1)),
                 [&, script](TaskContext &) {
                     const bool trigger = script["trigger"].toBool();
                     const int repeat = std::clamp(script["repeat"].toInt(1), 1, 1000000);
                     const bool continuous = !trigger && script["continuous"].toBool();
                     bool handled = false;
                     for (uint64_t iteration = 0;
                          trigger ? iteration < 1 : (continuous || iteration < uint64_t(repeat));
                          ++iteration) {
                         context.execution.checkpoint();
                         if (!trigger && context.info)
                             context.info("脚本循环",
                                          QString::number(iteration + 1) +
                                              (continuous ? " / 持续" : " / " + QString::number(repeat)));
                         try {
                             const auto flag = scriptEnabled_.value(script["id"].toString());
                             handled = scripts
                                           .run(script, trigger ? std::function<bool()>(
                                                                      [flag] { return flag && flag->load(); })
                                                                : std::function<bool()>{})["handled"]
                                           .toBool() ||
                                       handled;
                         } catch (const ScriptDisabled &) {
                             if (context.info)
                                 context.info("脚本", "已停用并释放输入");
                             return false;
                         }
                     }
                     return handled;
                 }});
        }
        QMap<QString, double> next;
        const auto resumeTriggers = [&] {
            input.release();
            {
                std::lock_guard lock(lifecycle_);
                if (!gate_.active(epoch)) throw Cancelled();
                foreground_ = false;
                cancelForeground_ = false;
            }
            selected.clear();
            activeTask.clear();
            context.task.clear();
            context.taskOverrides = {};
            context.state->inCombat = false;
            // Boss callbacks and a previous workshop actor are task-local.
            runner = std::make_unique<TaskRunner>(context, *echo_, root_, data_, teams_);
            context.combatInfo({{"code_state", "pending"}, {"verified", false}});
            limit = 0;
            next.clear();
            postStatus(epoch);
        };
        const auto cancelTask = [&] {
            failure = "当前任务已停止";
            recordResult(selected, false);
            resultRecorded = true;
            resumeTriggers();
            log("当前任务已停止，实时触发继续运行。");
        };
        double lastStatus = 0;
        while (true) {
            execution.checkpoint();
            if (!selected.isEmpty() && cancelForeground_)
                cancelTask();
            context.refresh();
            bool handled = false;
            for (const auto &task : runnable) {
                if (!selected.isEmpty() && task.id != selected)
                    continue;
                if (selected.isEmpty() && !task.trigger)
                    continue;
                if (selected.isEmpty() &&
                    (custom.contains(task.id)
                         ? !custom[task.id]["trigger"].toBool() || !scriptEnabled_.value(task.id)->load()
                         : !profile_.value(task.id, "_enabled", false).toBool()))
                    continue;
                if (execution.now() < next.value(task.id, 0))
                    continue;
                context.task = task.id;
                activeTask = task.id;
                phase = "task";
                taskRunning = true;
                try {
                    handled = task.step(context);
                } catch (const Cancelled &) {
                    if (!gate_.active(epoch) || !cancelForeground_)
                        throw;
                    taskRunning = false;
                    cancelTask();
                    break;
                } catch (const CombatEnded &error) {
                    if (task.id != "AutoCombatTask" || profile_.value(task.id, "_enabled", true).toBool())
                        throw;
                    input.release();
                    log(errorText(error));
                    handled = false;
                }
                taskRunning = false;
                next[task.id] = execution.now() + task.interval;
                if (!task.trigger) {
                    execution.checkpoint();
                    input.release();
                    exitAfter = options["exit_after"].toBool() ||
                                (!custom.contains(task.id) &&
                                 profile_.value(task.id, "Exit After Task", false).toBool());
                    recordResult(selected, true);
                    resultRecorded = true;
                    emit message({{"event", "task_done"}, {"task", selected}});
                    QString name = selected;
                    for (auto value : profile_.catalog()["tasks"].toArray())
                        if (value.toObject()["id"] == selected)
                            name = value.toObject()["name"].toString();
                    notify({product::name + " 任务完成", name, {}});
                    if (exitAfter) {
                        completed = true;
                        gate_.stop();
                        break;
                    }
                    resumeTriggers();
                    log("当前任务已完成，实时触发继续运行。");
                    break;
                }
                if (handled)
                    break;
            }
            if (execution.now() - lastStatus > .25) {
                postStatus(epoch, selected);
                lastStatus = execution.now();
            }
            const double interval =
                std::clamp(profile_.value("Basic Options", "Trigger Interval", 1).toDouble(1), 1., 60000.) /
                1000.;
            execution.wait(interval);
        }
    } catch (const Cancelled &) {
        emergency.release();
        failure = "已暂停或取消";
    } catch (const std::exception &e) {
        emergency.release();
        const bool lowMemory = memoryFailure(e);
        failure = taskFailureText(e);
        if (lowMemory)
            log("内存不足的原始错误：" + errorText(e));
        // Stop accepting input before allocating a screenshot or notifying UI.
        {
            std::lock_guard lock(lifecycle_);
            gate_.stop();
            paused_ = true;
            warming_ = false;
            try { backend_.arm(false); }
            catch (const std::exception &releaseError) { log(errorText(releaseError)); }
        }
        QJsonObject errorEvent{{"event", "error"}, {"error", failure}, {"task", activeTask},
                               {"selected_task", selected}, {"phase", phase}, {"combat", lastCombat},
                               {"devices", backend_.snapshot()}, {"elapsed_seconds", clock.now() - began}};
        errorEvent["step_diagnostics"] = stepDiagnostics;
        if (const auto bridgeError = dynamic_cast<const ExecutionFailure *>(&e))
            errorEvent["traceback"] = bridgeError->details;
        else
            errorEvent["exception_type"] = QString::fromLatin1(typeid(e).name());
        Notice errorNotice{product::name + " 任务停止", failure, {}, true};
        try {
            // Error screenshots need another full image and PNG buffer. Avoid
            // making allocation failures worse while reporting low memory.
            if (auto last = lowMemory ? FramePtr{} : backend_.latestFrame()) {
                auto picture = inferenceImage(*last, Config::load(data_ + "/dualpc.json"));
                std::vector<uchar> bytes;
                if (cv::imencode(".png", picture, bytes)) {
                    QByteArray png(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size()));
                    const auto folder = data_ + "/screenshots/errors";
                    QDir().mkpath(folder);
                    const auto filePath =
                        folder + '/' + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") + ".png";
                    QSaveFile file(filePath);
                    if (file.open(QIODevice::WriteOnly) && file.write(png) == png.size() && file.commit()) {
                        log("错误现场已保存：" + filePath);
                        errorEvent["screenshot"] = filePath;
                    }
                    errorNotice.images << png;
                }
            }
        } catch (const std::exception &captureError) {
            log("错误现场保存失败：" + errorText(captureError));
            errorEvent["screenshot_error"] = errorText(captureError);
        }
        errorEvent = saveDiagnostic(data_, errorEvent);
        diagnosticReport = errorEvent["diagnostic_report"].toString();
        log("任务异常，自动操作已暂停。诊断记录：" + diagnosticReport);
        emit message(errorEvent);
        notify(std::move(errorNotice));
        // Keep the diagnostic available until the user explicitly resumes/exits.
        exitAfter = false;
    }
    {
        std::lock_guard lock(lifecycle_);
        gate_.stop();
        paused_ = true;
        warming_ = false;
        try {
            backend_.arm(false);
        } catch (const std::exception &e) {
            log(errorText(e));
        }
    }
    postStatus(epoch);
    if (!resultRecorded)
        recordResult(selected, completed);
    if (exitAfter)
        QMetaObject::invokeMethod(
            this,
            [this, epoch] {
                if (generation_ != epoch || pendingTask_)
                    return;
                log("任务结束，正在等待通知队列处理后退出。");
                exitWait_.start();
                exitTimer_.start();
            },
            Qt::QueuedConnection);
    workerFinished_ = true;
}
void Engine::scriptCommand(QJsonObject request) {
    const QJsonValue id = request.value("id");
    try {
        if (scriptsBusy_) {
            QTimer::singleShot(50, this, [this, request] { scriptCommand(request); });
            return;
        }
        const bool liveToggle = request["op"] == "script_configure" && request["key"] == "_enabled";
        const bool triggerSettings = request["op"] == "script_configure" &&
            std::any_of(scripts_.begin(), scripts_.end(), [&](const auto &value) {
                const auto entry = value.toObject();
                const auto fields = entry["fields"].toArray();
                return entry["id"] == request["script"] && entry["trigger"].toBool() &&
                    std::any_of(fields.begin(), fields.end(), [&](auto field) {
                        return field.toObject()["key"] == request["key"];
                    });
            });
        if ((!paused_ || !workerFinished_ || pendingTask_) && !liveToggle && !triggerSettings)
            fail("请先暂停任务，再修改或重新载入脚本");
        if (request["op"] == "script_configure") {
            const auto ident = request["script"].toString(), key = request["key"].toString();
            const auto value = request["value"];
            auto state = QFile::exists(data_ + "/native-scripts.json")
                             ? readJson(data_ + "/native-scripts.json")
                             : QJsonObject{};
            auto settings = state[ident].toObject();
            bool found = false;
            auto changed = scripts_;
            for (int i = 0; i < changed.size(); ++i) {
                auto entry = changed[i].toObject();
                if (entry["id"] != ident)
                    continue;
                found = true;
                if (key == "_enabled" || key == "_continuous") {
                    if (!value.isBool())
                        fail("脚本开关需要布尔值");
                    const auto property = key == "_enabled" ? "enabled" : "continuous";
                    settings[property] = value;
                    entry[property] = value;
                    if (key == "_enabled" && scriptEnabled_.contains(ident))
                        scriptEnabled_.value(ident)->store(value.toBool());
                } else if (key == "_repeat") {
                    if (!value.isDouble() || value.toDouble() != value.toInt() || value.toInt() < 1 ||
                        value.toInt() > 1000000)
                        fail("循环次数应在 1 至 1000000 之间");
                    settings["repeat"] = value;
                    entry["repeat"] = value;
                } else {
                    auto fields = entry["fields"].toArray();
                    bool fieldFound = false;
                    for (int f = 0; f < fields.size(); ++f) {
                        auto field = fields[f].toObject();
                        if (field["key"] != key)
                            continue;
                        fieldFound = true;
                        field["value"] = value;
                        fields[f] = field;
                    }
                    if (!fieldFound)
                        fail("脚本参数不存在");
                    auto values = settings["values"].toObject();
                    values[key] = value;
                    settings["values"] = values;
                    entry["fields"] = fields;
                }
                changed[i] = entry;
            }
            if (!found)
                fail("脚本已不存在");
            state[ident] = settings;
            writeJson(data_ + "/native-scripts.json", state);
            scripts_ = changed;
            writeJson(data_ + "/native-script-catalog.json",
                      {{"scripts", scripts_}, {"templates", scriptTemplates_}});
            const auto result = catalog();
            emit message({{"event", "catalog_changed"}, {"data", result}});
            emit message({{"id", id}, {"ok", true}, {"data", result}});
            return;
        }
        scriptsBusy_ = true;
        auto command = request;
        command["op"] = request["op"].toString().mid(7);
        auto watcher = new QFutureWatcher<QJsonObject>(this);
        connect(watcher, &QFutureWatcher<QJsonObject>::finished, this, [this, watcher, id] {
            const auto response = watcher->result();
            watcher->deleteLater();
            scriptsBusy_ = false;
            if (response["ok"].toBool()) {
                scripts_ = response["data"].toObject()["scripts"].toArray();
                scriptTemplates_ = response["data"].toObject()["templates"].toArray();
                auto result = catalog();
                result["selected_script"] = response["data"].toObject()["selected"];
                result["snippet"] = response["data"].toObject()["snippet"];
                emit message({{"event", "catalog_changed"}, {"data", result}});
                emit message({{"id", id}, {"ok", true}, {"data", result}});
            } else
                emit message({{"id", id}, {"ok", false}, {"error", response["error"]}});
            // A queued UI start must not race the initial script-catalog scan.
            if (loadingScripts_) {
                loadingScripts_ = false;
                emit message({{"event", "ready"}, {"data", catalog()}});
            }
        });
        watcher->setFuture(QtConcurrent::run([root = root_, data = data_, command] {
            try {
                return QJsonObject{{"ok", true}, {"data", scriptManage(root, data, command)}};
            } catch (const std::exception &error) {
                return QJsonObject{{"ok", false}, {"error", errorText(error)}};
            }
        }));
    } catch (const std::exception &error) {
        emit message({{"id", id}, {"ok", false}, {"error", errorText(error)}});
    }
}
void Engine::invalidateParty() {
    // Configuration mutations run only after the old combat worker exits.
    // Keep account/login state, but discard combat context and old UI identity.
    taskState_->inCombat = false;
    taskState_->retryingStep = false;
    ++taskState_->combatEnableRevision;
    emit message({{"event", "party_invalidated"}});
}
void Engine::switchTeamCode(QJsonObject request) {
    if (teamSwitchBusy_) fail("正在切换队伍代码，请等待完成");
    if (!paused_ || !workerFinished_ || pendingTask_)
        fail("请先暂停任务，再修改队伍。");
    if (worker_.joinable()) worker_.join();
    teamSwitchSnapshot_ = teams_->catalog();
    teamSwitchBusy_ = true;
    auto watcher = new QFutureWatcher<QJsonObject>(this);
    connect(watcher, &QFutureWatcher<QJsonObject>::finished, this, [this, watcher, request] {
        auto reply = watcher->result();
        watcher->deleteLater();
        teamSwitchBusy_ = false;
        // A batch failure can still have changed some teams. Re-detect on the
        // next start in either case; TaskRunner/CombatSession are recreated.
        invalidateParty();
        reply["id"] = request["id"];
        try {
            auto data = catalog();
            data["team_switch_ms"] = reply.take("elapsed_ms");
            // A failed batch can have switched other valid teams; show disk state.
            emit message({{"event", "catalog_changed"}, {"data", data}});
            reply["data"] = data;
        } catch (const std::exception &error) {
            reply["ok"] = false;
            reply["error"] = errorText(error);
        }
        emit message(reply);
    });
    watcher->setFuture(QtConcurrent::run([teams = teams_, request] {
        QElapsedTimer elapsed;
        elapsed.start();
        QJsonObject result;
        try {
            if (request["op"] == "team_mode")
                teams->setMode(request["team"].toArray(), request["mode"].toString());
            else teams->setAllModes(request["mode"].toString());
            result["ok"] = true;
        } catch (const std::exception &error) {
            result["ok"] = false;
            result["error"] = errorText(error);
        }
        result["elapsed_ms"] = double(elapsed.elapsed());
        return result;
    }));
}
void Engine::command(const QJsonObject &request) {
    QJsonObject reply{{"id", request["id"]}};
    try {
        const auto op = request["op"].toString();
        QJsonObject result;
        if (op == "team_mode" || op == "team_all_mode") {
            switchTeamCode(request);
            return;
        }
        if (op.startsWith("script_")) {
            scriptCommand(request);
            return;
        }
        if (op == "catalog")
            result = catalog();
        else if (op == "pause" || op == "shutdown") {
            pause();
            result = status();
        } else if (op == "stop_task") {
            // Explicit global pause/error must never be undone by a late task
            // button. Cancelling a foreground task hands control to triggers.
            if (pendingTask_ && !pendingTask_->isEmpty()) {
                pendingTask_ = QString{};
                pendingOptions_["exit_after"] = false;
                pendingOptions_["limit_seconds"] = 0;
                foreground_ = false;
            } else if (foreground_ && !paused_)
                cancelForeground_ = true;
            result = status();
        } else if (op == "start") {
            start(request["task"].toString(), request);
            result = status(request["task"].toString());
        } else if (op == "task_config" || op == "global_config") {
            if (request["task"].toString().startsWith("script:")) {
                auto q = request;
                q["op"] = "script_configure";
                q["script"] = request["task"];
                scriptCommand(q);
                return;
            }
            profile_.set(request[op == "task_config" ? "task" : "group"].toString(),
                         request["key"].toString(), request["value"]);
            result = catalog();
        } else if (op == "reset_config") {
            if (request["group"].toString().startsWith("script:")) {
                auto q = request;
                q["op"] = "script_reset";
                q["script"] = request["group"];
                scriptCommand(q);
                return;
            }
            profile_.reset(request["group"].toString());
            result = catalog();
        } else if (op == "trigger") {
            const auto id = request["task"].toString();
            if (id.startsWith("script:")) {
                auto q = request;
                q["op"] = "script_configure";
                q["script"] = id;
                q["key"] = "_enabled";
                q["value"] = request["enabled"];
                scriptCommand(q);
                return;
            }
            if (request["enabled"].toBool() &&
                std::none_of(tasks_.begin(), tasks_.end(), [&](const auto &t) { return t.id == id; }))
                fail("此实时任务尚未迁移，当前不能启用。");
            const bool wasEnabled = profile_.value(id, "_enabled", false).toBool();
            profile_.set(id, "_enabled", request["enabled"]);
            if (id == "AutoCombatTask" && !wasEnabled && request["enabled"].toBool())
                ++taskState_->combatEnableRevision;
            result = status();
        } else if (op.startsWith("team_")) {
            if (teamSwitchBusy_) fail("正在切换队伍代码，请等待完成");
            const auto team = request["team"].toArray();
            if (op == "team_code")
                result = teams_->code(team, request["member"].toString());
            else if (op == "team_editor")
                result = teams_->editor(team, request["member"].toString(), request["source"].toString(),
                                        request["view"].toString("active"));
            else if (op == "team_export")
                teams_->exportTeam(team, request["path"].toString());
            else {
                if (!paused_ || !workerFinished_ || pendingTask_)
                    fail("请先暂停任务，再修改队伍。");
                if (worker_.joinable())
                    worker_.join();
                if (op == "team_create")
                    teams_->createTeam(team);
                else if (op == "team_delete")
                    teams_->removeTeam(team);
                else if (op == "team_save")
                    teams_->saveDraft(team, request["member"].toString(), request["code"].toString());
                else if (op == "team_import")
                    teams_->importTeam(request["path"].toString());
                else
                    fail("未知队伍操作");
                invalidateParty();
                result = catalog();
            }
        } else
            fail("未知原生执行器操作：" + op);
        reply["ok"] = true;
        reply["data"] = result;
    } catch (const std::exception &e) {
        reply["ok"] = false;
        reply["error"] = errorText(e);
    }
    emit message(reply);
}
} // namespace okww::runtime
