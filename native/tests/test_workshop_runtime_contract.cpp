#include "reference_workshop_runtime.hpp"
#include "team_replay.hpp"
#include <QCryptographicHash>
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
namespace okww::runtime {
struct WorkshopContractAccess {
    template <class T> static QJsonObject rpc(T &macro, QJsonObject request) { return macro.rpc(request); }
    template <class T> static QJsonObject call(T &macro, QJsonObject request) { return macro.call(request); }
    template <class T> static void sync(T &macro, QJsonObject state) { macro.sync(state); }
    template <class T> static void invalidateTimer(T &macro) { macro.telemetryClock_.invalidate(); }
    template <class T> static void map(T &macro) {
        macro.mapping_ =
            CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 64 * 1024 * 1024, nullptr);
        if (!macro.mapping_)
            fail("Test mapping failed");
        macro.mapped_ = MapViewOfFile(macro.mapping_, FILE_MAP_ALL_ACCESS, 0, 0, 64 * 1024 * 1024);
        if (!macro.mapped_)
            fail("Test image map failed");
    }
    template <class T> static QString pixels(T &macro, int size) {
        return QString::fromLatin1(
            QCryptographicHash::hash(QByteArray(static_cast<const char *>(macro.mapped_), size),
                                     QCryptographicHash::Sha256)
                .toHex());
    }
    template <class T> static void seed(T &macro, CombatHost &host) {
        macro.loaded_ = true;
        macro.session_ = "offline-session";
        macro.requestSequence_ = UINT64_MAX - 1;
        for (auto *c : host.characters())
            macro.originalPolicies_.append({c, c->state.role, c->state.buffTime, c->checkFOnSwitch});
    }
    template <class T> static QJsonObject dump(T &macro) {
        QJsonObject healers, shorts;
        for (auto it = macro.healerOverrides_.cbegin(); it != macro.healerOverrides_.cend(); ++it)
            healers[QString::number(it.key())] = it.value();
        for (auto it = macro.shortTargetOverrides_.cbegin(); it != macro.shortTargetOverrides_.cend(); ++it)
            shorts[QString::number(it.key())] = it.value();
        return {{"loaded", macro.loaded_},
                {"copies", double(macro.frameCopies_)},
                {"requests", double(macro.frameRequests_)},
                {"sequence", QString::number(macro.frameSequence_)},
                {"size", QJsonArray{macro.frameSize_.width(), macro.frameSize_.height()}},
                {"input_samples", macro.frameInputSamples_.size()},
                {"original_policies", macro.originalPolicies_.size()},
                {"healers", healers},
                {"shorts", shorts},
                {"request_sequence", QString::number(macro.requestSequence_)}};
    }
    template <class T>
    static void process(T &macro, const QString &python, const QString &script, int scene) {
        macro.process_.start(python, {script, QString::number(scene)});
        if (!macro.process_.waitForStarted(10000))
            fail("Fixture Python failed to start");
    }
};
} // namespace okww::runtime
namespace {
struct BridgeHost final : test::TeamReplay {
    int scene, frame = 0;
    QJsonArray trace;
    explicit BridgeHost(int s)
        : TeamReplay({{"names", QJsonArray{"Baizhi", "Mortefi", "Verina"}}}), scene(s) {}
    void refreshObservation() override {
        trace.append("refresh");
        if (scene % 43 == 0)
            throw CombatEnded("fixture-result");
    }
    cv::Mat frameImage() override {
        trace.append("image");
        if (scene % 47 == 0)
            throw Cancelled();
        if (frame % 5 == 4)
            return {};
        cv::Mat image(16 + frame / 5, 32, frame % 7 == 6 ? CV_8UC4 : CV_8UC3,
                      cv::Scalar(37 + frame, 71, 91, 255));
        return frame % 3 == 0 ? image(cv::Rect(1, 1, 29, 14)) : image;
    }
    uint64_t observationId() const override { return frame % 4 ? UINT64_C(9007199254740993) + frame / 3 : 0; }
    QJsonObject workshopCommand(QJsonObject q) override {
        trace.append(QJsonObject{{"input", q}});
        if (scene % 41 == 0 && q["down"].toBool())
            fail("fixture-input-failed");
        return {{"reply", q}};
    }
    void workshopTelemetry(QJsonObject q) override {
        for (const auto *key : {"startup_ms", "ping_ms", "last_copy_ms", "ping_p95_ms", "frame_input_p50_ms",
                                "frame_input_p95_ms"})
            q.remove(key);
        trace.append(QJsonObject{{"telemetry", q}});
    }
    int partyIndex() override {
        if (scene % 13 == 0)
            throw CombatInterrupted("fixture-party-interrupted");
        return TeamReplay::partyIndex();
    }
    QString taskId() const override { return scene % 2 ? "FarmEchoTask" : "AutoCombatTask"; }
    QJsonObject workshopTaskConfig() const override {
        return {{"Repeat Farm Count", 123}, {"fixture", scene}};
    }
};
QJsonObject errorOutcome(const std::function<QJsonValue()> &run) {
    try {
        return {{"result", run()}};
    } catch (const ExecutionFailure &e) {
        return {{"error", errorText(e)}, {"traceback", e.details}};
    } catch (const CombatInterrupted &e) {
        return {{"interrupted", errorText(e)}};
    } catch (const CombatEnded &e) {
        return {{"ended", errorText(e)}};
    } catch (const Cancelled &e) {
        return {{"cancelled", errorText(e)}};
    } catch (const std::exception &e) {
        return {{"error", errorText(e)}};
    }
}
QJsonObject partySnapshot(BridgeHost &host) {
    auto result = workshopCombatState(host);
    QJsonArray roles;
    for (auto *c : host.characters())
        roles.append(QJsonArray{int(c->state.role), c->state.buffTime, c->checkFOnSwitch});
    result["raw_roles"] = roles;
    return result;
}
template <class T> QJsonObject rpcReplay(int scene) {
    BridgeHost host(scene);
    QJsonArray results;
    {
        T macro(host, "root", "data");
        WorkshopContractAccess::map(macro);
        for (int i = 0; i < 18; ++i) {
            host.frame = i;
            WorkshopContractAccess::invalidateTimer(macro);
            auto request = i % 4 == 3
                               ? QJsonObject{{"op", i % 2 ? "key" : "button"}, {"down", true}, {"code", 8}}
                               : QJsonObject{{"op", "frame"}};
            auto result = errorOutcome([&] { return WorkshopContractAccess::rpc(macro, request); });
            if (result["result"].toObject()["available"].toBool())
                result["pixels"] =
                    WorkshopContractAccess::pixels(macro, result["result"].toObject()["size"].toInt());
            result["state"] = WorkshopContractAccess::dump(macro);
            results.append(result);
        }
    }
    return {{"results", results}, {"trace", host.trace}, {"party", partySnapshot(host)}};
}
template <class T> QJsonObject syncReplay(int scene) {
    BridgeHost host(scene);
    QJsonObject result;
    {
        T macro(host, "root", "data");
        WorkshopContractAccess::seed(macro, host);
        auto value = workshopCombatState(host);
        auto chars = value["chars"].toArray();
        for (int i = 0; i < chars.size(); ++i) {
            auto c = chars[i].toObject();
            c["healer"] = i == scene % 3;
            c["short_target"] = i != scene % 3;
            c["buff_time"] = 25 + scene;
            c["role"] = "SubDps";
            chars[i] = c;
        }
        if (scene % 7 == 0) {
            auto c = chars[scene % 3].toObject();
            c.remove("healer");
            chars[scene % 3] = c;
        }
        if (scene % 11 == 0) {
            auto c = chars[scene % 3].toObject();
            c["short_target"] = 1;
            chars[scene % 3] = c;
        }
        if (scene % 13 == 0) {
            auto c = chars[scene % 3].toObject();
            c["current_con"] = 1.01;
            chars[scene % 3] = c;
        }
        value["chars"] = chars;
        result = errorOutcome([&] {
            WorkshopContractAccess::sync(macro, value);
            return QJsonValue(true);
        });
        result["state"] = WorkshopContractAccess::dump(macro);
        result["party"] = partySnapshot(host);
    }
    result["after"] = partySnapshot(host);
    result["trace"] = host.trace;
    return result;
}
const char *controlWorker = R"py(import sys,json
s=int(sys.argv[1])
for line in sys.stdin:
 q=json.loads(line)
 state=q.get('host_state',{})
 for c in state.get('chars',[]):
  c['healer']=c['index']==s%3
  c['short_target']=c['index']!=s%3
  c['role']='SubDps'
  c['buff_time']=25
 state['priority']=s-30
 state['skip_combat_check']=bool(s%2)
 state['context']=q
 if s%17==0: state['interrupted']='fixture-host-interrupted'
 if s%19==0: state['priority']='bad'
 if s%23==0: state['skip_combat_check']=1
 reply={'event':'reply','session':q.get('session'),'request_id':q.get('request_id'),'ok':True,'data':state}
 if s%7==0:reply['session']='stale-session'
 if s%11==0:reply.update(ok=False,error='fixture-worker-failed',traceback='fixture-stack')
 print(json.dumps(reply),flush=True)
)py";
template <class T> QJsonObject controlReplay(int scene, const QString &python, const QString &script) {
    BridgeHost host(scene);
    host.macroBusy = scene % 2;
    host.useLiberation = scene % 3;
    host.hasLevitator = scene % 5;
    host.macroRearmAt = 95.25;
    QJsonObject result;
    {
        T macro(host, "root", "data");
        WorkshopContractAccess::seed(macro, host);
        WorkshopContractAccess::process(macro, python, script, scene);
        result = errorOutcome([&]() -> QJsonValue {
            switch (scene % 6) {
            case 0:
                macro.reset();
                return true;
            case 1:
                macro.combatEnd();
                return true;
            case 2:
                return macro.perform(host.entry());
            case 3:
                return *macro.priority(host.entry(), scene % 2, scene % 3);
            case 4:
                return macro.skipsCombatCheck(host.entry());
            default:
                return WorkshopContractAccess::call(macro, {{"op", "custom"}});
            }
        });
        result["state"] = WorkshopContractAccess::dump(macro);
        result["party"] = partySnapshot(host);
        result["busy"] = host.macroBusy;
    }
    result["after"] = partySnapshot(host);
    result["trace"] = host.trace;
    return result;
}
} // namespace
class WorkshopRuntimeContracts final : public QObject {
    Q_OBJECT
  private slots:
    void rpc_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 256; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void rpc() {
        QFETCH(int, scene);
        QCOMPARE(rpcReplay<PythonTeamMacro>(scene),
                 rpcReplay<workshop_runtime_reference::OriginalPythonTeamMacro>(scene));
    }
    void sync_data() { rpc_data(); }
    void sync() {
        QFETCH(int, scene);
        QCOMPARE(syncReplay<PythonTeamMacro>(scene),
                 syncReplay<workshop_runtime_reference::OriginalPythonTeamMacro>(scene));
    }
    void control_data() {
        QTest::addColumn<int>("scene");
        for (int i = 1; i < 65; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void control() {
        QFETCH(int, scene);
        QTemporaryDir fixture;
        const auto python = workshopPython(OKWW_SOURCE_ROOT);
        QVERIFY(!python.isEmpty());
        const auto path = fixture.filePath("worker.py");
        QFile code(path);
        QVERIFY(code.open(QIODevice::WriteOnly));
        code.write(controlWorker);
        code.close();
        QCOMPARE(controlReplay<PythonTeamMacro>(scene, python, path),
                 controlReplay<workshop_runtime_reference::OriginalPythonTeamMacro>(scene, python, path));
    }
};
QTEST_GUILESS_MAIN(WorkshopRuntimeContracts)
#include "test_workshop_runtime_contract.moc"
