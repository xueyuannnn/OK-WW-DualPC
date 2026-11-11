#include "runtime/engine.hpp"
#include "runtime/scripts.hpp"
#include "runtime/workshop.hpp"
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
static Config simulationConfig() {
    auto config = Config::defaults();
    config.set("kmbox.dry_run", true);
    return config;
}
class EngineTests final : public QObject {
    Q_OBJECT
  private slots:
    void unexpectedDeviceDisarmReportsFailureButExplicitPauseDoesNot() {
        const QString root = OKWW_SOURCE_ROOT;
        QTemporaryDir data;
        writeJson(data.path()+"/dualpc.json", simulationConfig().json);
        Profile profile(data.path()+"/configs-dualpc", root+"/native/resources/catalog.json");
        for (auto value : profile.catalog()["tasks"].toArray()) {
            const auto task = value.toObject();
            if (task["trigger"].toBool()) profile.set(task["id"].toString(), "_enabled", false);
        }
        Backend backend(false);
        backend.start(Config::defaults(), true);
        Engine engine(backend, root, data.path());
        QJsonObject failure;
        int errors = 0;
        connect(&engine, &Engine::message, &engine, [&](QJsonObject event) {
            if (event["event"] == "error") { failure = event; ++errors; }
        });
        engine.load();
        engine.command({{"op", "start"}});
        QTRY_VERIFY_WITH_TIMEOUT(backend.snapshot()["armed"].toBool(), 30000);
        backend.arm(false);
        QTRY_VERIFY_WITH_TIMEOUT(engine.idle() && !failure.isEmpty(), 5000);
        QVERIFY(QFile::exists(failure["diagnostic_report"].toString()));
        QVERIFY(failure["error"].toString().contains("中断"));
        QVERIFY(backend.snapshot()["capture_connected"].toBool());
        engine.command({{"op", "start"}});
        QTRY_VERIFY_WITH_TIMEOUT(backend.snapshot()["armed"].toBool(), 10000);
        engine.command({{"op", "pause"}});
        QTRY_VERIFY_WITH_TIMEOUT(engine.idle(), 5000);
        QCOMPARE(errors, 1);
        QVERIFY(backend.snapshot()["capture_connected"].toBool());
        backend.stop();
    }
    void taskFailurePausesAndReleasesInputBeforePublishingLocalDiagnostics() {
        const QString root = OKWW_SOURCE_ROOT;
        if (workshopPython(root).isEmpty()) QSKIP("Python SDK absent");
        QTemporaryDir data;
        writeJson(data.path()+"/dualpc.json", simulationConfig().json);
        const auto id = scriptManage(root, data.path(), {{"op", "create"}, {"name", "Failure"}})["selected"].toString();
        scriptManage(root, data.path(), {{"op", "save"}, {"script", id}, {"code",
            "from ok import BaseTask\nclass Failure(BaseTask):\n    def run(self):\n"
            "        self.send_key_down('w')\n        self.mouse_down()\n"
            "        raise RuntimeError('engine diagnostic marker')\n"}});
        Backend backend(false);
        backend.start(Config::defaults(), true);
        Engine engine(backend, root, data.path());
        int catalogs = 0;
        QJsonObject failure;
        bool safeWhenPublished = false;
        connect(&engine, &Engine::message, &engine, [&](QJsonObject event) {
            if (event["event"] == "catalog_changed") ++catalogs;
            if (event["event"] == "error") {
                failure = event;
                const auto state = backend.snapshot();
                safeWhenPublished = !state["armed"].toBool() && state["capture_connected"].toBool() &&
                    state["box"].toObject()["keys"].toArray().isEmpty() &&
                    state["box"].toObject()["buttons"].toInt() == 0;
            }
        });
        engine.load();
        QTRY_VERIFY_WITH_TIMEOUT(catalogs > 0, 15000);
        engine.command({{"op", "start"}, {"task", id}});
        QTRY_VERIFY_WITH_TIMEOUT(!failure.isEmpty() && engine.idle(), 30000);
        QVERIFY(safeWhenPublished);
        const auto report = readJson(failure["diagnostic_report"].toString());
        QVERIFY(report["error"].toString().contains("engine diagnostic marker"));
        QVERIFY(report["traceback"].toString().contains("Traceback"));
        QCOMPARE(report["task"].toString(), id);
        QVERIFY(report.contains("memory"));
        QCOMPARE(readJson(data.path()+"/native-last-run.json")["diagnostic_report"], failure["diagnostic_report"]);
        backend.stop();
    }
    void workshopModeSwitchKeepsTheEventLoopResponsiveAndSerializesTaskStart() {
        const QString root = OKWW_SOURCE_ROOT;
        QTemporaryDir data;
        writeJson(data.path()+"/dualpc.json", simulationConfig().json);
        const QJsonArray team{"Baizhi", "Mortefi", "Verina"};
        const auto folder = data.path()+"/configs-dualpc/custom_teams/Baizhi__Mortefi__Verina";
        QDir().mkpath(folder);
        writeJson(folder+"/team.json", {{"team", team}, {"code_mode", "builtin"}});
        for (auto name : team) {
            const auto member = name.toString();
            QFile file(folder+'/'+member+".import.py");
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(("import time\ntime.sleep(.25)\nfrom src.char."+member+" import "+member+
                        " as Native\nclass "+member+"(Native):\n    pass\n").toUtf8());
        }
        Backend backend(false);
        Engine engine(backend, root, data.path());
        QMap<int, QJsonObject> replies;
        int partyInvalidations = 0;
        connect(&engine, &Engine::message, &engine, [&](QJsonObject event) {
            if (event["id"].isDouble()) replies[event["id"].toInt()] = event;
            if (event["event"] == "party_invalidated") ++partyInvalidations;
        });
        engine.load();
        int heartbeats = 0;
        QTimer timer;
        timer.setInterval(10);
        connect(&timer, &QTimer::timeout, &engine, [&] { ++heartbeats; });
        timer.start();
        QElapsedTimer elapsed;
        elapsed.start();
        engine.command({{"op", "team_mode"}, {"id", 101}, {"team", team}, {"mode", "import"}});
        QVERIFY2(elapsed.elapsed() < 250, "Mode switching blocked the event loop");
        QVERIFY(!engine.idle());
        elapsed.restart();
        engine.command({{"op", "catalog"}, {"id", 102}});
        QVERIFY2(elapsed.elapsed() < 250, "Catalog waited on the file-operation mutex");
        QVERIFY(replies[102]["ok"].toBool());
        engine.command({{"op", "start"}, {"id", 103}});
        QVERIFY(!replies[103]["ok"].toBool());
        QVERIFY(replies[103]["error"].toString().contains("正在切换"));
        engine.command({{"op", "team_mode"}, {"id", 104}, {"team", team}, {"mode", "builtin"}});
        QVERIFY(!replies[104]["ok"].toBool());
        QTRY_VERIFY_WITH_TIMEOUT(replies.contains(101), 15000);
        QVERIFY2(replies[101]["ok"].toBool(), qPrintable(replies[101]["error"].toString()));
        QVERIFY(heartbeats > 5);
        QVERIFY(engine.idle());
        QCOMPARE(readJson(folder+"/team.json")["code_mode"].toString(), QString("import"));
        QVERIFY(!backend.snapshot()["armed"].toBool());
        QCOMPARE(partyInvalidations, 1);
    }
    void liveTriggerSettingsSaveWithoutInterruptingTheCurrentIteration() {
        const QString root = OKWW_SOURCE_ROOT;
        QTemporaryDir data;
        writeJson(data.path()+"/dualpc.json", simulationConfig().json);
        const auto created = scriptManage(root, data.path(), {{"op", "create"}, {"name", "LiveSettings"}});
        const auto id = created["selected"].toString();
        const auto marker = data.path()+"/values.txt";
        const auto code = "from ok import TriggerTask\nfrom pathlib import Path\nclass LiveSettings(TriggerTask):\n"
            "    def __init__(self, *args, **kwargs):\n        super().__init__(*args, **kwargs)\n        self.default_config = {'Value': 1}\n"
            "    def should_trigger(self):\n"
            "        assert self.executor.current_task is self\n"
            "        assert self.executor.frame is not None\n"
            "        return self.config['Value'] > 1 or not Path(r'" + marker + "').exists()\n"
            "    def run(self):\n        path = Path(r'" + marker + "')\n"
            "        with path.open('a') as file: file.write('start:'+str(self.config['Value'])+'\\n')\n"
            "        self.sleep(.5)\n"
            "        with path.open('a') as file: file.write('end:'+str(self.config['Value'])+'\\n')\n";
        scriptManage(root, data.path(), {{"op", "save"}, {"script", id}, {"code", code}});
        Backend backend(false);
        backend.start(Config::defaults(), true);
        Engine engine(backend, root, data.path());
        int catalogs = 0;
        bool saved = false;
        connect(&engine, &Engine::message, &engine, [&](QJsonObject event) {
            if (event["event"] == "catalog_changed") ++catalogs;
            if (event["id"].toInt() == 123) saved = event["ok"].toBool();
            if (event["event"] == "error" || event["ok"] == false) qInfo() << event;
        });
        engine.load();
        QTRY_VERIFY_WITH_TIMEOUT(catalogs > 0, 15000);
        for (auto value : readJson(root+"/native/resources/catalog.json")["tasks"].toArray()) {
            auto task = value.toObject();
            if (task["trigger"].toBool()) engine.command({{"op", "trigger"}, {"task", task["id"]}, {"enabled", false}});
        }
        engine.command({{"op", "trigger"}, {"task", id}, {"enabled", true}});
        engine.command({{"op", "start"}});
        const auto contents = [&] {
            QFile file(marker);
            return file.open(QIODevice::ReadOnly) ? file.readAll().replace("\r\n", "\n") : QByteArray{};
        };
        QTRY_VERIFY_WITH_TIMEOUT(contents().contains("start:1\n"), 15000);
        engine.command({{"op", "task_config"}, {"id", 123}, {"task", id}, {"key", "Value"}, {"value", 7}});
        QTRY_VERIFY(saved);
        QTRY_VERIFY_WITH_TIMEOUT(contents().contains("start:7\n"), 10000);
        QVERIFY(contents().startsWith("start:1\nend:1\nstart:7\n"));
        QCOMPARE(readJson(data.path()+"/native-scripts.json")[id].toObject()["values"].toObject()["Value"].toInt(), 7);
        QVERIFY(!engine.idle());
        engine.command({{"op", "pause"}});
        QTRY_VERIFY_WITH_TIMEOUT(engine.idle(), 5000);
        QVERIFY(backend.snapshot()["box"].toObject()["keys"].toArray().isEmpty());
        backend.stop();
    }
    void generalScriptsRepeatAndLiveDisableReleasesHeldKeysWithoutStoppingTheExecutor() {
        const QString root = OKWW_SOURCE_ROOT;
        if (workshopPython(root).isEmpty())
            QSKIP("Python SDK absent");
        QTemporaryDir data;
        writeJson(data.path()+"/dualpc.json", simulationConfig().json);
        const auto created = scriptManage(root, data.path(), {{"op", "create"}, {"name", "Counter"}});
        const auto id = created["selected"].toString();
        const auto marker = data.path() + "/counter.txt";
        const auto counter = "from ok import BaseTask\nfrom pathlib import Path\nclass Counter(BaseTask):\n"
                             "    def run(self):\n        with Path(r'" +
                             marker + "').open('a') as file:\n            file.write('x')\n";
        scriptManage(root, data.path(), {{"op", "save"}, {"script", id}, {"code", counter}});
        scriptManage(root, data.path(),
                     {{"op", "configure"}, {"script", id}, {"key", "_repeat"}, {"value", 3}});
        Backend backend(false);
        backend.start(Config::defaults(), true);
        Engine engine(backend, root, data.path());
        int catalogs = 0;
        connect(&engine, &Engine::message, &engine, [&](QJsonObject event) {
            if (event["event"] == "catalog_changed")
                ++catalogs;
            if (event["event"] == "error" || event["event"] == "log" || event["ok"] == false)
                qInfo().noquote() << QJsonDocument(event).toJson(QJsonDocument::Compact);
        });
        engine.load();
        QTRY_VERIFY_WITH_TIMEOUT(catalogs > 0, 15000);
        engine.command({{"op", "start"}, {"task", id}});
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(data.path() + "/native-last-run.json") && engine.idle(),
                                 30000);
        QVERIFY(readJson(data.path() + "/native-last-run.json")["success"].toBool());
        QFile result(marker);
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), QByteArray("xxx"));
        result.close();
        const auto trigger = "from ok import TriggerTask\nimport time\nclass Counter(TriggerTask):\n"
                             "    def run(self):\n        self.send_key_down('w')\n        time.sleep(100)\n";
        catalogs = 0;
        engine.command({{"op", "script_save"}, {"script", id}, {"code", trigger}});
        QTRY_VERIFY_WITH_TIMEOUT(catalogs > 0, 15000);
        for (auto value : readJson(root + "/native/resources/catalog.json")["tasks"].toArray()) {
            const auto task = value.toObject();
            if (task["trigger"].toBool())
                engine.command({{"op", "trigger"}, {"task", task["id"]}, {"enabled", false}});
        }
        engine.command({{"op", "trigger"}, {"task", id}, {"enabled", true}});
        const auto stored = readJson(data.path() + "/native-script-catalog.json")["scripts"].toArray()[0].toObject();
        QVERIFY2(stored["trigger"].toBool(), QJsonDocument(stored).toJson(QJsonDocument::Compact));
        QVERIFY(stored["enabled"].toBool());
        engine.command({{"op", "start"}});
        QTRY_VERIFY_WITH_TIMEOUT(backend.snapshot()["box"].toObject()["keys"].toArray().contains(26), 15000);
        engine.command({{"op", "trigger"}, {"task", id}, {"enabled", false}});
        QTRY_VERIFY_WITH_TIMEOUT(backend.snapshot()["box"].toObject()["keys"].toArray().isEmpty(), 5000);
        QVERIFY(!engine.idle());
        engine.command({{"op", "pause"}});
        QTRY_VERIFY_WITH_TIMEOUT(engine.idle(), 5000);
        QVERIFY(!backend.snapshot()["armed"].toBool());
        backend.stop();
    }
    void workshopValidationNeverBlocksStartOrPauseOnTheGuiThread() {
        const QString root = OKWW_SOURCE_ROOT;
        if (workshopPython(root).isEmpty())
            QSKIP("Workshop Python SDK absent");
        QTemporaryDir data;
        writeJson(data.path()+"/dualpc.json", simulationConfig().json);
        const QStringList names{"Baizhi", "Mortefi", "Verina"};
        const auto folder = data.path() + "/configs-dualpc/custom_teams/" + names.join("__");
        const auto marker = data.path() + "/validation-started.txt";
        QDir().mkpath(folder);
        writeJson(folder + "/team.json",
                  {{"team", QJsonArray::fromStringList(names)}, {"code_mode", "import"}});
        for (const auto &name : names)
            for (const auto &suffix : {QString(".py"), QString(".import.py")}) {
                QFile file(folder + '/' + name + suffix);
                QVERIFY(file.open(QIODevice::WriteOnly));
                file.write(("from src.char." + name + " import " + name +
                            " as Native\n"
                            "import time\nfrom pathlib import Path\nPath(r'" +
                            marker + "').touch()\ntime.sleep(.3)\nclass " + name + "(Native):\n    pass\n")
                               .toUtf8());
            }
        Backend backend(false);
        backend.start(Config::defaults(), true);
        Engine engine(backend, root, data.path());
        engine.load();
        QElapsedTimer elapsed;
        elapsed.start();
        engine.command({{"op", "start"}, {"id", 1}, {"task", "DailyTask2"}});
        QVERIFY2(elapsed.elapsed() < 500, "Python imports blocked the UI start handler");
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(marker), 10000);
        elapsed.restart();
        engine.command({{"op", "pause"}, {"id", 2}});
        QVERIFY2(elapsed.elapsed() < 500, "Pause waited for workshop validation");
        QTRY_VERIFY_WITH_TIMEOUT(engine.idle(), 10000);
        QVERIFY(!backend.snapshot()["armed"].toBool());
        QVERIFY(readJson(data.path() + "/native-last-run.json")["error"].toString().contains("暂停"));
        backend.stop();
    }
    void modelPreparationIsAsynchronousAndPausePreventsRearming() {
        QTemporaryDir data;
        const QString root = OKWW_SOURCE_ROOT;
        writeJson(data.path()+"/dualpc.json", simulationConfig().json);
        Profile profile(data.path() + "/configs-dualpc", root + "/native/resources/catalog.json");
        for (auto value : profile.catalog()["tasks"].toArray()) {
            const auto task = value.toObject();
            if (task["trigger"].toBool())
                profile.set(task["id"].toString(), "_enabled", false);
        }
        Backend backend(false);
        backend.start(Config::defaults(), true);
        Engine engine(backend, root, data.path());
        QSignalSpy spy(&engine, &Engine::message);
        engine.load();
        QElapsedTimer elapsed;
        elapsed.start();
        engine.command({{"op", "start"}, {"id", 1}});
        QVERIFY2(elapsed.elapsed() < 500, "start blocked the Qt event loop during model compilation");
        engine.command({{"op", "pause"}, {"id", 2}});
        QVERIFY(!backend.snapshot()["armed"].toBool());
        bool stopped = false;
        connect(&engine, &Engine::message, &engine, [&](QJsonObject message) {
            if (message["id"] == 99)
                stopped = message["ok"].toBool();
        });
        const auto workerEnded = [&] {
            engine.command(
                {{"op", "team_create"}, {"id", 99}, {"team", QJsonArray{"Lucy", "Rebecca", "ShoreKeeper"}}});
            return stopped;
        };
        QTRY_VERIFY_WITH_TIMEOUT(workerEnded(), 60000);
        QVERIFY(engine.idle());
        QVERIFY(!backend.snapshot()["armed"].toBool());
        const auto result = readJson(data.path() + "/native-last-run.json");
        QVERIFY(!result["success"].toBool());
        QVERIFY(result["error"].toString().contains("暂停"));
        // Starting once more after cancellation must use a new execution epoch.
        stopped = false;
        engine.command({{"op", "start"}, {"id", 3}});
        QVERIFY(!engine.idle());
        QTest::qWait(20);
        engine.command({{"op", "pause"}, {"id", 4}});
        QTRY_VERIFY_WITH_TIMEOUT(workerEnded(), 60000);
        QVERIFY(!backend.snapshot()["armed"].toBool());
        backend.stop();
    }
};
QTEST_GUILESS_MAIN(EngineTests)
#include "test_engine.moc"
