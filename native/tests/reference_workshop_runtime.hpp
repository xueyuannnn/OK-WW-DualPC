#pragma once
#include "runtime/diagnostics.hpp"
#include "runtime/workshop.hpp"
namespace okww::runtime::workshop_runtime_reference {
class OriginalPythonTeamMacro final : public TeamMacro {
  public:
    QString root_, data_, pythonRoot_;
    QProcess process_;
    QByteArray buffer_, errors_;
    HANDLE mapping_ = nullptr;
    void *mapped_ = nullptr;
    QString mappingName_;
    QString session_, requestId_;
    uint64_t requestSequence_ = 0, frameSequence_ = UINT64_MAX, frameCopies_ = 0, frameRequests_ = 0;
    QSize frameSize_;
    double startupMs_ = 0, pingMs_ = 0, copyMs_ = 0;
    i64 lastFrameAt_ = 0;
    QVector<double> pingSamples_, frameInputSamples_;
    QElapsedTimer telemetryClock_;
    bool loaded_ = false;
    struct NativePolicy {
        Character *character;
        Role role;
        double buffTime;
        bool checkF;
    };
    QVector<NativePolicy> originalPolicies_;
    QHash<int, bool> healerOverrides_;
    QHash<int, bool> shortTargetOverrides_;
    void stop() noexcept;
    void startWorker();
    void write(QJsonObject value);
    QJsonObject receive(int timeoutMs);
    QJsonObject call(QJsonObject request);
    QJsonObject rpc(QJsonObject request);
    void sync(const QJsonObject &state);
    void report();

  public:
    OriginalPythonTeamMacro(CombatHost &host, QString root, QString data);
    ~OriginalPythonTeamMacro() override;
    void configure() override;
    void reset() override;
    void combatEnd() override;
    bool perform(Character &entry) override;
    std::optional<int> priority(const Character &c, bool intro) override;
    std::optional<int> priority(const Character &c, bool intro, bool targetLowCon);
    bool skipsCombatCheck(const Character &c);
    bool usesShortTarget(const Character &c) const {
        return shortTargetOverrides_.value(c.state.index, false);
    }
    bool isHealer(const Character &c) const {
        return healerOverrides_.value(c.state.index, c.state.role == Role::Healer);
    }
};
double percentile(QVector<double> values, double fraction) {
    if (values.isEmpty())
        return 0;
    std::sort(values.begin(), values.end());
    return values[std::min(values.size() - 1, qsizetype(std::ceil(values.size() * fraction)) - 1)];
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
OriginalPythonTeamMacro::OriginalPythonTeamMacro(CombatHost &host, QString root, QString data)
    : TeamMacro(host), root_(std::move(root)), data_(std::move(data)), pythonRoot_(workshopRoot(root_)) {}
OriginalPythonTeamMacro::~OriginalPythonTeamMacro() { stop(); }
void OriginalPythonTeamMacro::stop() noexcept {
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
void OriginalPythonTeamMacro::write(QJsonObject value) {
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n';
    if (process_.write(bytes) != bytes.size())
        fail("工坊兼容引擎连接已断开");
    process_.waitForBytesWritten(1000);
}
QJsonObject OriginalPythonTeamMacro::rpc(QJsonObject request) {
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
        frameInputSamples_.append((nowNs() - lastFrameAt_) / 1e6);
        if (frameInputSamples_.size() > 128)
            frameInputSamples_.removeFirst();
    }
    return h_.workshopCommand(std::move(request));
}
QJsonObject OriginalPythonTeamMacro::receive(int timeoutMs) {
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
                if (!loaded_ && !QStringList{"state", "heartbeat", "probe", "connect", "release"}.contains(
                                    message["request"].toObject()["op"].toString()))
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
void OriginalPythonTeamMacro::startWorker() {
    if (loaded_)
        return;
    try {
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
    } catch (...) {
        stop();
        throw;
    }
}
QJsonObject OriginalPythonTeamMacro::call(QJsonObject request) {
    const bool wasBusy = h_.macroBusy;
    h_.macroBusy = true;
    try {
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
        write(request);
        const auto response = receive(600000);
        if (response["event"] != "reply" || response["session"] != session_ ||
            response["request_id"] != requestId_)
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
void OriginalPythonTeamMacro::sync(const QJsonObject &state) {
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
    } catch (...) {
        stop();
        try {
            h_.workshopCommand({{"op", "release"}});
        } catch (...) {
        }
        throw;
    }
    healerOverrides_ = std::move(healers);
    shortTargetOverrides_ = std::move(shortTargets);
    report();
}
void OriginalPythonTeamMacro::report() {
    h_.workshopTelemetry({{"protocol", workshopProtocolVersion},
                          {"api", workshopApiVersion},
                          {"startup_ms", startupMs_},
                          {"ping_ms", pingMs_},
                          {"last_copy_ms", copyMs_},
                          {"ping_p95_ms", percentile(pingSamples_, .95)},
                          {"ping_samples", pingSamples_.size()},
                          {"frame_input_p50_ms", percentile(frameInputSamples_, .5)},
                          {"frame_input_p95_ms", percentile(frameInputSamples_, .95)},
                          {"frame_input_samples", frameInputSamples_.size()},
                          {"frame_requests", double(frameRequests_)},
                          {"frame_copies", double(frameCopies_)}});
}
void OriginalPythonTeamMacro::configure() { startWorker(); }
void OriginalPythonTeamMacro::reset() {
    // A host-owned result/cancellation can terminate the worker mid-segment.
    // Reload policy/state before the next battle's healer selection, rather
    // than falling back to native roles until the first perform call.
    startWorker();
    sync(call({{"op", "reset"}}));
}
void OriginalPythonTeamMacro::combatEnd() {
    if (loaded_)
        sync(call({{"op", "end"}}));
}
bool OriginalPythonTeamMacro::perform(Character &entry) {
    startWorker();
    const auto result = call({{"op", "perform"}, {"index", entry.state.index}});
    sync(result);
    if (result.contains("interrupted"))
        throw CombatInterrupted(result["interrupted"].toString());
    return true;
}
std::optional<int> OriginalPythonTeamMacro::priority(const Character &c, bool intro) {
    return priority(c, intro, false);
}
std::optional<int> OriginalPythonTeamMacro::priority(const Character &c, bool intro, bool targetLowCon) {
    startWorker();
    const auto result = call(
        {{"op", "priority"}, {"index", c.state.index}, {"intro", intro}, {"target_low_con", targetLowCon}});
    if (result.contains("interrupted"))
        throw CombatInterrupted(result["interrupted"].toString());
    if (!result["priority"].isDouble())
        fail("工坊返回了无效的切人优先级");
    sync(result);
    return result["priority"].toInt();
}
bool OriginalPythonTeamMacro::skipsCombatCheck(const Character &c) {
    startWorker();
    const auto result = call({{"op", "policy"}, {"index", c.state.index}});
    if (result.contains("interrupted"))
        throw CombatInterrupted(result["interrupted"].toString());
    if (!result["skip_combat_check"].isBool())
        fail("工坊返回了无效的战斗检测规则");
    sync(result);
    return result["skip_combat_check"].toBool();
}

} // namespace okww::runtime::workshop_runtime_reference
