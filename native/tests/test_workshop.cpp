#include "runtime/workshop.hpp"
#include "runtime/team_store.hpp"
#include "team_replay.hpp"
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
class WorkshopHost final : public test::TeamReplay {
  public:
    QElapsedTimer clock;
    int cancelAfter = 0;
    QSet<int> keys;
    int buttons = 0;
    QJsonArray input;
    QJsonObject telemetry;
    QString contextTask = "AutoCombatTask";
    QString taskId() const override { return contextTask; }
    void workshopTelemetry(QJsonObject value) override { telemetry = std::move(value); }
    WorkshopHost() : TeamReplay({{"names", QJsonArray{"Baizhi", "Mortefi", "Verina"}}}) { clock.start(); }
    void preciseWait(double seconds) override {
        if (cancelAfter && clock.elapsed() >= cancelAfter)
            throw Cancelled();
        TeamReplay::preciseWait(seconds);
    }
    QJsonObject workshopCommand(QJsonObject q) override {
        const auto op = q["op"].toString();
        const auto state = [&] {
            QJsonArray a;
            for (auto k : keys)
                a.append(k);
            return QJsonObject{
                {"connected", true}, {"keyboard", true}, {"keys", a}, {"modifiers", 0}, {"buttons", buttons}};
        };
        if (op == "state" || op == "connect" || op == "heartbeat" || op == "probe")
            return {
                {"capture_connected", true}, {"armed", true}, {"box", state()}, {"actual", QJsonObject{}}};
        input.append(q);
        if (op == "key") {
            if (q["down"].toBool())
                keys.insert(q["code"].toInt());
            else
                keys.remove(q["code"].toInt());
        } else if (op == "button") {
            const int bit = q["name"] == "left" ? 1 : q["name"] == "right" ? 2 : 4;
            if (q["down"].toBool())
                buttons |= bit;
            else
                buttons &= ~bit;
        } else if (op == "release") {
            keys.clear();
            buttons = 0;
        }
        return state();
    }
};
class WorkshopTests final : public QObject {
    Q_OBJECT
    const QString root = qEnvironmentVariable("OKWW_WORKSHOP_TEST_ROOT", OKWW_SOURCE_ROOT);
    const QJsonArray team{"Baizhi", "Mortefi", "Verina"};
    QString source(const QString &name, bool stall = false) {
        return "from src.char." + name + " import " + name + " as Native\nimport time\nclass " + name +
               "(Native):\n"
               "    def get_switch_priority(self, *args, **kwargs):\n        return 333\n"
               "    def do_perform(self):\n        self.task.send_key('e', down_time=.02)\n        "
               "self.task.mouse_down()\n" +
               (stall ? QString("        time.sleep(10)\n") : QString("        self.task.mouse_up()\n"));
    }
    void seed(const QString &data) {
        auto config = Config::defaults();
        config.set("kmbox.dry_run", true);
        config.save(data + "/dualpc.json");
        const auto folder = data + "/configs-dualpc/custom_teams/Baizhi__Mortefi__Verina";
        QDir().mkpath(folder);
        writeJson(folder + "/team.json", {{"name", "offline workshop"},
                                          {"description", "new Python class"},
                                          {"author", "test"},
                                          {"version", "1.0"},
                                          {"team", team},
                                          {"code_mode", "import"}});
        for (auto member : team)
            for (const auto &suffix : {".py", ".import.py"}) {
                QFile file(folder + '/' + member.toString() + suffix);
                QVERIFY(file.open(QIODevice::WriteOnly));
                file.write(source(member.toString()).toUtf8());
            }
    }
  private slots:
    void commonCombatStateTransfersBuffsSwitchesAndFreezeAgesAtomically() {
        WorkshopHost host;
        auto chars = host.characters();
        chars[0]->state.lastBuff = host.now()-3;
        chars[0]->state.lastSwitch = host.now()-.2;
        chars[0]->state.lastFullConSwitch = host.now()-1;
        chars[0]->state.hasIntro = true;
        host.freezes.add(host.now(), host.now()-2, .9, -100);
        auto state = workshopCombatState(host);
        chars[0]->state.lastBuff = -1;
        chars[0]->state.hasIntro = false;
        host.freezes.clear();
        applyWorkshopCombatState(host, state);
        QCOMPARE(chars[0]->state.lastBuff, host.now()-3);
        QCOMPARE(chars[0]->state.lastSwitch, host.now()-.2);
        QCOMPARE(chars[0]->state.lastFullConSwitch, host.now()-1);
        QVERIFY(chars[0]->state.hasIntro);
        QCOMPARE(host.freezes.entries().size(), 1);
        auto bad = state;
        auto values = bad["chars"].toArray();
        values[1] = values[0];
        bad["chars"] = values;
        QVERIFY_EXCEPTION_THROWN(applyWorkshopCombatState(host, bad), std::runtime_error);
        QCOMPARE(workshopCombatState(host), state);
    }
    void protocolRejectsVersionAndCapabilityMismatchBeforeRotations() {
        QJsonObject hello{{"event", "ready"}, {"protocol", 2}, {"api", 1},
            {"capabilities", QJsonArray{"request-identity", "combat-state-ages", "immutable-frame-cache", "bridge-metrics"}}};
        validateWorkshopHandshake(hello);
        hello["protocol"] = 1;
        QVERIFY_EXCEPTION_THROWN(validateWorkshopHandshake(hello), std::runtime_error);
        hello["protocol"] = 2;
        hello["capabilities"] = QJsonArray{};
        QVERIFY_EXCEPTION_THROWN(validateWorkshopHandshake(hello), std::runtime_error);
    }
    void initTestCase() {
        if (workshopPython(root).isEmpty())
            QSKIP("Workshop Python SDK absent in source-only build");
    }
    void fileManagerReusesItsProcessButReloadsEditedHelpersAndRollsBackFailures() {
        QTemporaryDir data;
        seed(data.path());
        const auto folder = data.path()+"/configs-dualpc/custom_teams/Baizhi__Mortefi__Verina";
        WorkshopFileSession session(root, data.path());
        QElapsedTimer clock;
        clock.start();
        session.command({{"op", "mode"}, {"team", team}, {"mode", "builtin"}});
        const auto cold = clock.elapsed();
        const auto pid = session.processId();
        QVERIFY(pid > 0);
        QVector<qint64> warm;
        for (int n = 0; n < 6; ++n) {
            clock.restart();
            session.command({{"op", "mode"}, {"team", team}, {"mode", n % 2 ? "builtin" : "import"}});
            warm.append(clock.elapsed());
            QCOMPARE(session.processId(), pid);
        }
        auto write = [&](const QString &name, const QString &text) {
            QFile file(folder+'/'+name);
            if (!file.open(QIODevice::WriteOnly)) return false;
            return file.write(text.toUtf8()) == text.toUtf8().size();
        };
        const auto helperSource = "import sys\nfrom pathlib import Path\nsys.path.insert(0, str(Path(__file__).parent))\n"
            "import switch_helper\nassert switch_helper.value == 1, 'edited helper was reloaded'\n" + source("Baizhi");
        QVERIFY(write("switch_helper.py", "value = 1\n"));
        QVERIFY(write("Baizhi.import.py", helperSource));
        session.command({{"op", "mode"}, {"team", team}, {"mode", "import"}});
        session.command({{"op", "mode"}, {"team", team}, {"mode", "builtin"}});
        const auto snapshot = readJson(folder+"/team.json");
        QFile active(folder+"/Baizhi.py");
        QVERIFY(active.open(QIODevice::ReadOnly));
        const auto before = active.readAll();
        active.close();
        QVERIFY(write("switch_helper.py", "value = 2\n"));
        QVERIFY_EXCEPTION_THROWN(session.command({{"op", "mode"}, {"team", team}, {"mode", "import"}}), std::runtime_error);
        QCOMPARE(readJson(folder+"/team.json"), snapshot);
        QVERIFY(active.open(QIODevice::ReadOnly));
        QCOMPARE(active.readAll(), before);
        std::sort(warm.begin(), warm.end());
        const QJsonObject metrics{{"cold_ms", double(cold)}, {"warm_median_ms", double(warm[warm.size()/2])},
                                  {"warm_max_ms", double(warm.last())}, {"same_process", true}, {"switches", 7}};
        qInfo().noquote() << QJsonDocument(metrics).toJson(QJsonDocument::Compact);
        const auto artifacts = qEnvironmentVariable("OKWW_UI_ARTIFACTS");
        if (!artifacts.isEmpty()) writeJson(artifacts+"/workshop-switch-metrics.json", metrics);
    }
    void newPythonTeamExecutesSwitchesEditsAndExportsWithoutNativeCompilation() {
        QTemporaryDir data, imported;
        seed(data.path());
        TeamStore store(root, data.path());
        QVERIFY(store.catalog()[0].toObject()["python"].toBool());
        store.validateEnabled();
        WorkshopHost host;
        auto macro = store.create(store.selection(host), host);
        QVERIFY(macro);
        macro->reset();
        QCOMPARE(*macro->priority(host.entry(), false), 333);
        QVERIFY(macro->perform(host.entry()));
        QVERIFY(!host.input.isEmpty());
        QVERIFY(host.keys.isEmpty());
        QCOMPARE(host.buttons, 0);
        macro.reset();
        store.setMode(team, "builtin");
        QVERIFY(store.selection(host).isEmpty());
        QVERIFY(store.editor(team, "Baizhi")["read_only"].toBool());
        const auto retained = store.editor(team, "Baizhi", {}, "import");
        QCOMPARE(retained["language"].toString(), QString("python"));
        QVERIFY(retained["read_only"].toBool());
        store.setMode(team, "import");
        const auto before = store.editor(team, "Baizhi")["code"].toString();
        QVERIFY_EXCEPTION_THROWN(store.saveDraft(team, "Baizhi.py", "class broken"), std::runtime_error);
        QCOMPARE(store.editor(team, "Baizhi")["code"].toString(), before);
        store.saveDraft(team, "Baizhi.py", before + "\n# saved in Python\n");
        const auto archive = data.path() + "/workshop.zip";
        store.exportTeam(team, archive);
        TeamStore destination(root, imported.path());
        destination.importTeam(archive);
        QCOMPARE(destination.editor(team, "Baizhi")["code"].toString().replace("\r\n", "\n"),
                 before + "\n# saved in Python\n");
    }
    void onlyCurrentWorkshopCharacterEndsCombatOnceAndTaskTypeIsPreserved() {
        QTemporaryDir data;
        seed(data.path());
        TeamStore store(root, data.path());
        for (const auto &value : team) {
            const auto name = value.toString();
            const auto code = "from src.char." + name + " import " + name + " as Native\n"
                "from src.task.AutoCombatTask import AutoCombatTask\n"
                "from src.task.FarmEchoTask import FarmEchoTask\n"
                "from src.task.FarmEchoTask2 import FarmEchoTask2\n"
                "class " + name + "(Native):\n"
                "    def get_switch_priority(self, *args, **kwargs):\n"
                "        if isinstance(self.task, AutoCombatTask): return 101\n"
                "        if isinstance(self.task, FarmEchoTask):\n"
                "            assert self.task.name == '🌀 Farm 4C Echo in Dungeon/World'\n"
                "            return 202\n"
                "        if isinstance(self.task, FarmEchoTask2): return 303\n"
                "        raise RuntimeError('Wrong host task')\n"
                "    def on_combat_end(self, chars):\n"
                "        self.task.send_key(str(self.index+1), down_time=.01)\n";
            store.saveDraft(team, name+".py", code);
        }
        WorkshopHost host;
        auto macro = store.create(store.selection(host), host);
        QVERIFY(store.selection(host).startsWith("python:"));
        macro->reset();
        QCOMPARE(*macro->priority(host.entry(), false), 101);
        host.contextTask = "FarmEchoTask";
        QCOMPARE(*macro->priority(host.entry(), false), 202);
        host.contextTask = "FarmEchoTask2";
        QCOMPARE(*macro->priority(host.entry(), false), 303);
        macro->combatEnd();
        macro->combatEnd();
        QJsonArray downs;
        for (const auto &v : host.input)
            if (v.toObject()["op"] == "key" && v.toObject()["down"].toBool()) downs.append(v);
        QCOMPARE(downs.size(), 1);
        QCOMPARE(downs[0].toObject()["code"].toInt(), 30);
        QVERIFY(host.keys.isEmpty());
        QVERIFY(host.telemetry["startup_ms"].toDouble() > 0);
        QVERIFY(host.telemetry["ping_ms"].toDouble() >= 0);
    }
    void validationBatchesTeamsCachesSuccessAndDetectsChangedSource() {
        QTemporaryDir data;
        seed(data.path());
        const auto marker = data.path() + "/checker-processes.txt";
        const QVector<QJsonArray> groups{team, {"Lucy", "Rebecca", "ShoreKeeper"}};
        for (const auto &group : groups) {
            QStringList names;
            for (const auto &name : group)
                names.append(name.toString());
            names.sort();
            const auto folder = data.path() + "/configs-dualpc/custom_teams/" + names.join("__");
            QDir().mkpath(folder);
            writeJson(folder + "/team.json", {{"team", QJsonArray::fromStringList(names)},
                                              {"code_mode", "import"}});
            for (const auto &name : names)
                for (const auto &suffix : {QString(".py"), QString(".import.py")}) {
                    QFile file(folder + '/' + name + suffix);
                    QVERIFY(file.open(QIODevice::WriteOnly));
                    file.write((source(name) + "\nimport os\nwith open(r'" + marker +
                                "', 'a') as marker:\n    marker.write(str(os.getpid()) + '\\n')\n").toUtf8());
                }
        }
        TeamStore store(root, data.path());
        store.validateEnabled();
        QFile log(marker);
        QVERIFY(log.open(QIODevice::ReadOnly));
        const auto checked = log.readAll();
        log.close();
        QSet<QByteArray> processes;
        for (const auto &line : checked.split('\n'))
            if (!line.isEmpty())
                processes.insert(line);
        QCOMPARE(processes.size(), 1);
        store.validateEnabled();
        QVERIFY(log.open(QIODevice::ReadOnly));
        QCOMPARE(log.readAll(), checked);
        log.close();
        // A changed active file must invalidate the cache, including a change
        // of the same byte length, and fail before hardware can be armed.
        QFile active(data.path() + "/configs-dualpc/custom_teams/Baizhi__Mortefi__Verina/Baizhi.py");
        QVERIFY(active.open(QIODevice::ReadWrite));
        const auto original = active.readAll();
        auto changed = original;
        changed.replace("333", "334");
        QCOMPARE(changed.size(), original.size());
        active.seek(0);
        active.write(changed);
        active.close();
        QVERIFY_EXCEPTION_THROWN(store.validateEnabled(), std::runtime_error);
    }
    void pauseKillsOnlyTheWorkshopChildAndReleasesHeldInput() {
        QTemporaryDir data;
        seed(data.path());
        TeamStore store(root, data.path());
        store.saveDraft(team, "Baizhi.py", source("Baizhi", true));
        WorkshopHost host;
        auto macro = store.create(store.selection(host), host);
        macro->reset();
        host.cancelAfter = host.clock.elapsed() + 300;
        QVERIFY_EXCEPTION_THROWN(macro->perform(host.entry()), Cancelled);
        QVERIFY(host.keys.isEmpty());
        QCOMPARE(host.buttons, 0);
    }
    void sameFrameIsCopiedOnceAndBridgeMeasurementsRemainSoftwareOnly() {
        QTemporaryDir data;
        seed(data.path());
        TeamStore store(root, data.path());
        const auto code = "from src.char.Baizhi import Baizhi as Native\nclass Baizhi(Native):\n"
            "    def do_perform(self):\n        for i in range(20):\n"
            "            self.task.next_frame()\n            self.task.send_key('e', down_time=0)\n";
        store.saveDraft(team, "Baizhi.py", code);
        WorkshopHost host;
        auto macro = store.create(store.selection(host), host);
        macro->reset();
        QVERIFY(macro->perform(host.entry()));
        QVERIFY(host.telemetry["frame_requests"].toDouble() >= 20);
        QCOMPARE(host.telemetry["frame_copies"].toDouble(), 1.);
        QCOMPARE(host.telemetry["ping_samples"].toInt(), 15);
        QVERIFY(host.telemetry["frame_input_samples"].toInt() >= 20);
        QVERIFY(host.keys.isEmpty());
        const auto artifacts = qEnvironmentVariable("OKWW_UI_ARTIFACTS");
        if (!artifacts.isEmpty()) writeJson(artifacts + "/workshop-metrics.json", host.telemetry);
        qInfo().noquote() << QJsonDocument(host.telemetry).toJson(QJsonDocument::Compact);
    }
};
QTEST_GUILESS_MAIN(WorkshopTests)
#include "test_workshop.moc"
