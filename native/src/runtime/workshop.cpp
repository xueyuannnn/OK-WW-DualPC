#include "workshop.hpp"
#include "diagnostics.hpp"
#include <QProcessEnvironment>
#include <QThread>
namespace okww::runtime {
namespace {
const std::array<std::pair<const char *, double CharacterState::*>, 9> commonTimes{{
    {"last_res", &CharacterState::lastResonance}, {"last_echo", &CharacterState::lastEcho},
    {"last_liberation", &CharacterState::lastLiberation}, {"last_switch_in", &CharacterState::lastSwitchIn},
    {"last_outro", &CharacterState::lastOutro}, {"last_switch", &CharacterState::lastSwitch},
    {"last_full_con_switch", &CharacterState::lastFullConSwitch}, {"last_buff", &CharacterState::lastBuff},
    {"last_perform", &CharacterState::lastPerform}}};
double finiteNumber(QJsonValue value) {
    if (!value.isDouble() || !std::isfinite(value.toDouble())) fail("工坊状态包含无效数值");
    return value.toDouble();
}
bool stateBoolean(QJsonValue value) {
    if (!value.isBool()) fail("工坊状态包含无效开关值");
    return value.toBool();
}
double percentile(QVector<double> values, double fraction) {
    if (values.isEmpty()) return 0;
    std::sort(values.begin(), values.end());
    return values[std::min(values.size()-1, qsizetype(std::ceil(values.size()*fraction))-1)];
}
}
QJsonObject workshopCombatState(CombatHost &host) {
    const auto now = host.now();
    QJsonArray chars, freezes;
    for (auto c : host.characters()) {
        const auto &s = c->state;
        QJsonObject value{{"index", s.index}, {"current", s.current}, {"intro", s.hasIntro},
            {"sub_intro", s.hasSubDpsIntro}, {"current_con", s.currentCon}, {"check_f", c->checkFOnSwitch},
            {"buff_time", s.buffTime},
            {"role", s.role == Role::Healer ? "Healer" : s.role == Role::SubDps ? "SubDps" : "MainDps"}};
        for (const auto &[key, member] : commonTimes)
            value[key] = s.*member < 0 ? -1 : now-s.*member;
        chars.append(value);
    }
    for (const auto &f : host.freezes.entries())
        freezes.append(QJsonObject{{"age", now-f.start}, {"duration", f.duration}, {"threshold", f.threshold}});
    return {{"chars", chars}, {"freezes", freezes}, {"in_liberation", host.inLiberation},
            {"combat_age", host.combatStart < 0 ? -1 : now-host.combatStart}};
}
void applyWorkshopCombatState(CombatHost &host, const QJsonObject &state) {
    const auto party = host.characters();
    const auto values = state["chars"].toArray();
    if (values.size() != party.size()) fail("工坊队伍状态与当前队伍不一致");
    const auto now = host.now();
    QSet<int> seen;
    QVector<QPair<Character *, CharacterState>> prepared;
    QVector<bool> checks;
    for (const auto &v : values) {
        const auto value = v.toObject();
        const auto index = finiteNumber(value["index"]);
        auto it = std::find_if(party.begin(), party.end(), [&](auto c) { return c->state.index == index; });
        if (it == party.end() || seen.contains(int(index))) fail("工坊返回了不一致的角色位置");
        seen.insert(int(index));
        auto s = (*it)->state;
        const auto role = value["role"].toString();
        if (!QStringList{"MainDps", "SubDps", "Healer"}.contains(role)) fail("工坊角色定位无效");
        s.role = role == "Healer" ? Role::Healer : role == "SubDps" ? Role::SubDps : Role::MainDps;
        s.current = stateBoolean(value["current"]);
        s.hasIntro = stateBoolean(value["intro"]);
        s.hasSubDpsIntro = stateBoolean(value["sub_intro"]);
        s.currentCon = finiteNumber(value["current_con"]);
        if (s.currentCon < 0 || s.currentCon > 1) fail("工坊协奏值无效");
        s.buffTime = finiteNumber(value["buff_time"]);
        if (s.buffTime < 0 || s.buffTime > 86400) fail("工坊角色增益时间无效");
        for (const auto &[key, member] : commonTimes) {
            const auto age = finiteNumber(value[key]);
            s.*member = age < 0 ? -1 : now-age;
        }
        prepared.append({*it, s});
        checks.append(stateBoolean(value["check_f"]));
    }
    FreezeTimeline freezes;
    const auto timeline = state["freezes"].toArray();
    if (timeline.size() > 2048) fail("工坊冻结时间线过大");
    for (const auto &v : timeline) {
        const auto f = v.toObject();
        const auto age = finiteNumber(f["age"]), duration = finiteNumber(f["duration"]),
                   threshold = finiteNumber(f["threshold"]);
        if (duration < 0 || duration > 600) fail("工坊冻结时间无效");
        freezes.add(now, now-age, duration, threshold);
    }
    const auto combatAge = finiteNumber(state["combat_age"]);
    const auto inLiberation = stateBoolean(state["in_liberation"]);
    for (int i = 0; i < prepared.size(); ++i) {
        prepared[i].first->state = prepared[i].second;
        prepared[i].first->checkFOnSwitch = checks[i];
    }
    host.freezes = std::move(freezes);
    host.combatStart = combatAge < 0 ? -1 : now-combatAge;
    host.inLiberation = inLiberation;
}
void validateWorkshopHandshake(const QJsonObject &hello) {
    if (hello["event"] != "ready" || hello["protocol"].toInt() != workshopProtocolVersion ||
        hello["api"].toInt() != workshopApiVersion)
        fail("工坊桥接版本不一致，请使用同一完整包中的主程序和兼容引擎");
    const auto capabilities = hello["capabilities"].toArray();
    for (const auto &capability : {"request-identity", "combat-state-ages", "immutable-frame-cache", "bridge-metrics",
                                    "character-policy", "task-config-context"})
        if (!capabilities.contains(capability)) fail("工坊兼容引擎缺少必要接口能力，请更新完整包");
}
QString workshopRoot(const QString &root) {
    if (QFile::exists(root + "/workshop-engine/native/bridge/workshop_worker.py"))
        return root + "/workshop-engine";
    return root;
}
QString workshopPython(const QString &root) {
    const auto engine = workshopRoot(root);
    for (const auto &relative : {"/runtime/python.exe", "/.venv/Scripts/python.exe"})
        if (QFile::exists(engine + relative))
            return engine + relative;
    return {};
}
static QProcessEnvironment environment() {
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONUTF8", "1");
    env.insert("PYTHONIOENCODING", "utf-8");
    env.insert("PYTHONDONTWRITEBYTECODE", "1");
    env.remove("QT_PLUGIN_PATH");
    env.remove("QT_QPA_PLATFORM_PLUGIN_PATH");
    env.remove("PYTHONHOME");
    env.remove("PYTHONPATH");
    return env;
}
QJsonObject workshopManage(const QString &root, const QString &data, QJsonObject request) {
    const auto python = workshopPython(root), engine = workshopRoot(root);
    if (python.isEmpty())
        fail("缺少工坊 Python 兼容引擎，请使用完整包");
    QProcess process;
    process.setProcessEnvironment(environment());
    process.setWorkingDirectory(engine);
    process.start(python, {engine + "/native/bridge/workshop_manager.py", "--root", engine, "--data", data});
    if (!process.waitForStarted(10000))
        fail("工坊管理器启动失败：" + process.errorString());
    process.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    process.closeWriteChannel();
    if (!process.waitForFinished(request["op"].toString().startsWith("library") ? 180000 : 60000)) {
        process.kill();
        process.waitForFinished(3000);
        fail("工坊代码检查超时");
    }
    const auto response = QJsonDocument::fromJson(process.readAllStandardOutput().trimmed()).object();
    if (process.exitCode() || !response["ok"].toBool())
        fail(response["error"].toString("工坊管理器返回无效，请查看兼容引擎日志"));
    return response["data"].toObject();
}
struct WorkshopFileSession::State {
    QString root, data;
    QThread thread;
    QObject *worker = new QObject;
    QProcess *process = nullptr;
    QByteArray buffer, errors;
    quint64 sequence = 0;
    std::atomic<qint64> pid{0};
    State(QString root, QString data) : root(std::move(root)), data(std::move(data)) {
        worker->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
        thread.start();
    }
    void stop() {
        if (process && process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(3000);
        }
        pid = 0;
        buffer.clear();
        errors.clear();
    }
    ~State() {
        QMetaObject::invokeMethod(worker, [this] { stop(); }, Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait();
    }
    QJsonObject exchange(QJsonObject request) {
        if (!process) process = new QProcess(worker);
        if (process->state() == QProcess::NotRunning) {
            buffer.clear();
            errors.clear();
            const auto python = workshopPython(root), engine = workshopRoot(root);
            if (python.isEmpty()) fail("缺少工坊 Python 兼容引擎，请使用完整包");
            process->setProcessEnvironment(environment());
            process->setWorkingDirectory(engine);
            process->start(python, {engine + "/native/bridge/workshop_manager.py", "--root", engine,
                                    "--data", data, "--persistent"});
            if (!process->waitForStarted(10000)) fail("工坊管理器启动失败：" + process->errorString());
            pid = process->processId();
        }
        const auto id = QString::number(++sequence);
        request["request_id"] = id;
        const auto payload = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
        if (process->write(payload) != payload.size()) fail("工坊文件管理通道已断开");
        process->waitForBytesWritten(1000);
        QElapsedTimer elapsed;
        elapsed.start();
        while (elapsed.elapsed() < 60000) {
            errors += process->readAllStandardError();
            errors = errors.right(65536);
            buffer += process->readAllStandardOutput();
            if (buffer.size() > 16 * 1024 * 1024) fail("工坊管理器返回数据过大");
            if (const auto newline = buffer.indexOf('\n'); newline >= 0) {
                const auto reply = QJsonDocument::fromJson(buffer.left(newline)).object();
                buffer.remove(0, newline + 1);
                if (reply["request_id"] != id || reply["manager_protocol"].toInt() != 1)
                    fail("工坊文件管理接口不匹配，请更新完整包");
                if (!reply["ok"].toBool()) fail(reply["error"].toString("工坊文件操作失败"));
                return reply["data"].toObject();
            }
            if (process->state() == QProcess::NotRunning) fail("工坊文件管理器已退出");
            process->waitForReadyRead(15);
        }
        fail("工坊代码检查超时");
    }
};
WorkshopFileSession::WorkshopFileSession(QString root, QString data)
    : state_(std::make_unique<State>(std::move(root), std::move(data))) {}
WorkshopFileSession::~WorkshopFileSession() = default;
qint64 WorkshopFileSession::processId() const { return state_->pid.load(); }
QJsonObject WorkshopFileSession::command(QJsonObject request) {
    QJsonObject result;
    std::exception_ptr failure;
    QMetaObject::invokeMethod(state_->worker, [&] {
        try { result = state_->exchange(std::move(request)); }
        catch (...) {
            // Never retry a mutation after an uncertain reply. The next explicit
            // request gets a fresh manager, including after a timeout or crash.
            failure = std::current_exception();
            state_->stop();
        }
    }, Qt::BlockingQueuedConnection);
    if (failure) std::rethrow_exception(failure);
    return result;
}
PythonTeamMacro::PythonTeamMacro(CombatHost &host, QString root, QString data)
    : TeamMacro(host), root_(std::move(root)), data_(std::move(data)), pythonRoot_(workshopRoot(root_)) {}
PythonTeamMacro::~PythonTeamMacro() {
    stop();
}
void PythonTeamMacro::stop() noexcept {
    if (process_.state() != QProcess::NotRunning) {
        process_.kill();
        process_.waitForFinished(2000);
    }
    if (mapped_)
        UnmapViewOfFile(mapped_);
    if (mapping_)
        CloseHandle(mapping_);
    mapped_ = nullptr;
    mapping_ = nullptr;
    loaded_ = false;
    for (const auto &entry : originalPolicies_) {
        entry.character->state.role = entry.role;
        entry.character->state.buffTime = entry.buffTime;
        entry.character->checkFOnSwitch = entry.checkF;
    }
    originalPolicies_.clear();
    healerOverrides_.clear();
    shortTargetOverrides_.clear();
    buffer_.clear();
    errors_.clear();
    frameSequence_ = UINT64_MAX;
    frameSize_ = {};
    lastFrameAt_ = 0;
}
void PythonTeamMacro::write(QJsonObject value) {
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n';
    if (process_.write(bytes) != bytes.size())
        fail("工坊兼容引擎连接已断开");
    process_.waitForBytesWritten(1000);
}
QJsonObject PythonTeamMacro::rpc(QJsonObject request) {
    if (request["op"] == "frame") {
        ++frameRequests_;
        h_.refreshObservation();
        auto image = h_.frameImage();
        if (image.empty())
            return {{"available", false}};
        if (image.type() != CV_8UC3 || image.total() * 3 > 64 * 1024 * 1024)
            fail("工坊图像格式无效");
        if (!image.isContinuous())
            image = image.clone();
        const auto size = image.total() * 3;
        const auto sequence = h_.observationId();
        if (!sequence || frameSequence_ != sequence || frameSize_ != QSize(image.cols, image.rows)) {
            QElapsedTimer timer;
            timer.start();
            memcpy(mapped_, image.data, size);
            copyMs_ = timer.nsecsElapsed() / 1e6;
            ++frameCopies_;
            frameSequence_ = sequence;
            frameSize_ = {image.cols, image.rows};
        }
        lastFrameAt_ = nowNs();
        if (!telemetryClock_.isValid() || telemetryClock_.elapsed() >= 250) {
            report();
            telemetryClock_.restart();
        }
        return {{"available", true},
                {"size", double(size)},
                {"width", image.cols},
                {"height", image.rows},
                {"stride", image.cols * 3},
                {"format", "RGB3"},
                {"sequence", QString::number(sequence ? sequence : frameCopies_)}};
    }
    if (lastFrameAt_ && (request["op"] == "key" || request["op"] == "button") && request["down"].toBool()) {
        frameInputSamples_.append((nowNs()-lastFrameAt_) / 1e6);
        if (frameInputSamples_.size() > 128) frameInputSamples_.removeFirst();
    }
    return h_.workshopCommand(std::move(request));
}
QJsonObject PythonTeamMacro::receive(int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        h_.workshopCheckpoint();
        errors_ += process_.readAllStandardError();
        if (errors_.size() > 65536)
            errors_ = errors_.right(65536);
        buffer_ += process_.readAllStandardOutput();
        if (buffer_.size() > 16 * 1024 * 1024)
            fail("工坊兼容引擎返回数据过大");
        while (buffer_.contains('\n')) {
            const auto newline = buffer_.indexOf('\n');
            const auto line = buffer_.left(newline);
            buffer_.remove(0, newline + 1);
            const auto message = QJsonDocument::fromJson(line).object();
            if (message["event"] == "rpc") {
                if (loaded_ && (message["session"] != session_ || message["request_id"] != requestId_))
                    fail("已丢弃过期工坊输入请求");
                if (!loaded_ && !QStringList{"state", "heartbeat", "probe", "connect", "release"}
                        .contains(message["request"].toObject()["op"].toString()))
                    fail("工坊完成接口校验前不能发送操作");
                try {
                    write({{"ok", true}, {"data", rpc(message["request"].toObject())}});
                } catch (const Cancelled &) {
                    throw;
                } catch (const CombatEnded &) {
                    // Lifecycle interruption belongs to the host. Returning it
                    // as a Python RuntimeError lets broad script handlers swallow
                    // it and continue pressing keys on a result/revival screen.
                    throw;
                } catch (const std::exception &error) {
                    write({{"ok", false}, {"error", errorText(error)}});
                }
                continue;
            }
            if (message["event"] == "fatal")
                throw ExecutionFailure(message["error"].toString("工坊兼容引擎启动失败"),
                                       message["traceback"].toString());
            if (!message.isEmpty())
                return message;
        }
        if (process_.state() == QProcess::NotRunning)
            fail("工坊兼容引擎已退出：" + QString::fromUtf8(errors_.right(2000)));
        process_.waitForReadyRead(15);
    }
    fail("工坊连招执行超时");
}
void PythonTeamMacro::startWorker() {
    if (loaded_)
        return;
    QElapsedTimer startup;
    startup.start();
    frameCopies_ = frameRequests_ = 0;
    startupMs_ = pingMs_ = copyMs_ = 0;
    frameInputSamples_.clear();
    telemetryClock_.invalidate();
    session_ = QUuid::createUuid().toString(QUuid::Id128);
    const auto python = workshopPython(root_);
    if (python.isEmpty())
        fail("工坊 Python 运行库缺失，请使用完整包");
    mappingName_ = "Local\\OKWW-workshop-" + QUuid::createUuid().toString(QUuid::Id128);
    mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 64 * 1024 * 1024,
                                  reinterpret_cast<LPCWSTR>(mappingName_.utf16()));
    if (!mapping_)
        fail("无法创建工坊共享图像");
    mapped_ = MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, 64 * 1024 * 1024);
    if (!mapped_)
        fail("无法映射工坊共享图像");
    process_.setProcessEnvironment(environment());
    process_.setWorkingDirectory(pythonRoot_);
    process_.start(python, {pythonRoot_ + "/native/bridge/workshop_worker.py", "--root", pythonRoot_,
                            "--data", data_, "--mapping", mappingName_});
    if (!process_.waitForStarted(10000))
        fail("工坊兼容引擎启动失败");
    validateWorkshopHandshake(receive(90000));
    loaded_ = true;
    QJsonArray party;
    for (auto c : h_.characters()) {
        originalPolicies_.append({c, c->state.role, c->state.buffTime, c->checkFOnSwitch});
        party.append(QJsonObject{{"class", c->state.className},
                                 {"canonical", c->state.name},
                                 {"index", c->state.index},
                                 {"element", c->state.ringIndex},
                                 {"role", c->state.role == Role::Healer   ? "Healer"
                                          : c->state.role == Role::SubDps ? "SubDps"
                                                                          : "MainDps"},
                                 {"buff", c->state.buffTime}});
    }
    sync(call({{"op", "load"}, {"party", party}}));
    startupMs_ = startup.nsecsElapsed() / 1e6;
    pingSamples_.clear();
    for (int i = 0; i < 15; ++i) {
        QElapsedTimer ping;
        ping.start();
        call({{"op", "ping"}});
        pingSamples_.append(ping.nsecsElapsed() / 1e6);
    }
    pingMs_ = percentile(pingSamples_, .5);
    report();
}
QJsonObject PythonTeamMacro::call(QJsonObject request) {
    const bool wasBusy = h_.macroBusy;
    h_.macroBusy = true;
    request["use_liberation"] = h_.useLiberation;
    request["in_combat"] = h_.combatEngaged();
    request["has_levitator"] = h_.hasLevitator;
    request["current"] = h_.partyIndex();
    request["f_rearm"] = h_.macroRearmAt;
    request["f_age"] = h_.now() - h_.macroRearmAt;
    requestId_ = QString::number(++requestSequence_);
    request["protocol"] = workshopProtocolVersion;
    request["api"] = workshopApiVersion;
    request["session"] = session_;
    request["request_id"] = requestId_;
    request["task"] = h_.taskId();
    request["task_config"] = h_.workshopTaskConfig();
    request["host_state"] = workshopCombatState(h_);
    try {
        write(request);
        const auto response = receive(600000);
        if (response["event"] != "reply" || response["session"] != session_ || response["request_id"] != requestId_)
            fail("工坊返回了过期或不匹配的回复");
        if (!response["ok"].toBool())
            throw ExecutionFailure(response["error"].toString("工坊引擎返回无效"),
                                   response["traceback"].toString());
        h_.macroBusy = wasBusy;
        return response["data"].toObject();
    } catch (...) {
        h_.macroBusy = wasBusy;
        stop();
        try {
            h_.workshopCommand({{"op", "release"}});
        } catch (...) {
        }
        throw;
    }
}
void PythonTeamMacro::sync(const QJsonObject &state) {
    QHash<int, bool> healers, shortTargets;
    try {
        for (const auto &value : state["chars"].toArray()) {
            const auto character = value.toObject();
            if (!character["healer"].isBool() || !character["short_target"].isBool())
                fail("工坊缺少角色战斗规则");
            healers[character["index"].toInt()] = character["healer"].toBool();
            shortTargets[character["index"].toInt()] = character["short_target"].toBool();
        }
        applyWorkshopCombatState(h_, state);
    }
    catch (...) {
        stop();
        try { h_.workshopCommand({{"op", "release"}}); } catch (...) {}
        throw;
    }
    healerOverrides_ = std::move(healers);
    shortTargetOverrides_ = std::move(shortTargets);
    report();
}
void PythonTeamMacro::report() {
    h_.workshopTelemetry({{"protocol", workshopProtocolVersion}, {"api", workshopApiVersion},
        {"startup_ms", startupMs_}, {"ping_ms", pingMs_}, {"last_copy_ms", copyMs_},
        {"ping_p95_ms", percentile(pingSamples_, .95)}, {"ping_samples", pingSamples_.size()},
        {"frame_input_p50_ms", percentile(frameInputSamples_, .5)},
        {"frame_input_p95_ms", percentile(frameInputSamples_, .95)},
        {"frame_input_samples", frameInputSamples_.size()},
        {"frame_requests", double(frameRequests_)}, {"frame_copies", double(frameCopies_)}});
}
void PythonTeamMacro::configure() { startWorker(); }
void PythonTeamMacro::reset() {
    // A host-owned result/cancellation can terminate the worker mid-segment.
    // Reload policy/state before the next battle's healer selection, rather
    // than falling back to native roles until the first perform call.
    startWorker();
    sync(call({{"op", "reset"}}));
}
void PythonTeamMacro::combatEnd() {
    if (loaded_)
        sync(call({{"op", "end"}}));
}
bool PythonTeamMacro::perform(Character &entry) {
    startWorker();
    const auto result = call({{"op", "perform"}, {"index", entry.state.index}});
    sync(result);
    if (result.contains("interrupted"))
        throw CombatInterrupted(result["interrupted"].toString());
    return true;
}
std::optional<int> PythonTeamMacro::priority(const Character &c, bool intro) {
    return priority(c, intro, false);
}
std::optional<int> PythonTeamMacro::priority(const Character &c, bool intro, bool targetLowCon) {
    startWorker();
    const auto result = call({{"op", "priority"}, {"index", c.state.index}, {"intro", intro},
                              {"target_low_con", targetLowCon}});
    if (result.contains("interrupted"))
        throw CombatInterrupted(result["interrupted"].toString());
    if (!result["priority"].isDouble()) fail("工坊返回了无效的切人优先级");
    sync(result);
    return result["priority"].toInt();
}
bool PythonTeamMacro::skipsCombatCheck(const Character &c) {
    startWorker();
    const auto result = call({{"op", "policy"}, {"index", c.state.index}});
    if (result.contains("interrupted"))
        throw CombatInterrupted(result["interrupted"].toString());
    if (!result["skip_combat_check"].isBool()) fail("工坊返回了无效的战斗检测规则");
    sync(result);
    return result["skip_combat_check"].toBool();
}
} // namespace okww::runtime
