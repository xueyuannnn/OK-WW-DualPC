#include "runtime/input.hpp"
#include "runtime/vision.hpp"
#include "backend.hpp"
#include "runtime/profile.hpp"
#include "runtime/task.hpp"
#include "runtime/combat_vision.hpp"
#include "runtime/navigation.hpp"
#include "runtime/stamina.hpp"
#include "runtime/interaction.hpp"
#include "runtime/combat_session.hpp"
#include "runtime/domain.hpp"
#include "runtime/nightmare.hpp"
#include "runtime/garden.hpp"
#include "runtime/echo_farm.hpp"
#include "runtime/echo_operations.hpp"
#include "runtime/daily.hpp"
#include "runtime/team_macro.hpp"
#include "runtime/task_registry.hpp"
#include "runtime/scheduler.hpp"
#include <QXmlStreamReader>
#include <opencv2/imgproc.hpp>
#include "rotation_replay.hpp"
#include <QtTest>

using namespace okww;
using namespace okww::runtime;
struct FakeClock : Clock {
    double time = 0;
    std::function<void()> tick;
    double now() const override { return time; }
    void sleep(double seconds) override {
        time += seconds;
        if (tick)
            tick();
    }
};
struct Harness {
    Config config = Config::defaults();
    FakeClock clock;
    ExecutionGate gate;
    Execution execution{gate, gate.start(), clock};
    QVector<QJsonObject> events;
    InputTransport transport = [this](QJsonObject event) {
        event["time"] = clock.now();
        events.append(event);
        return QJsonObject{};
    };
};
class MacroProbe final : public TeamMacro {
  public:
    explicit MacroProbe(CombatHost &host) : TeamMacro(host) {}
    using TeamMacro::hold;
    using TeamMacro::quickSwitch;
    using TeamMacro::spam;
    using TeamMacro::spamKey;
    using TeamMacro::wait;
    bool perform(Character &) override { return false; }
};
class RuntimeTests : public QObject {
    Q_OBJECT
  private slots:
    void positionedActionsWaitAtLeast120msWithoutDelayingRawCombatOrDragging() {
        Harness h;
        Input input(h.config, {1920, 1080}, true, h.transport, h.execution);
        input.taskClick({400, 300});
        QCOMPARE(h.events[0]["op"].toString(), QString("absolute"));
        QVERIFY(std::abs(h.events[1]["time"].toDouble() - .12) < 1e-9);
        QVERIFY(std::abs(h.events[2]["time"].toDouble() - .24) < 1e-9);
        input.move({600, 300});
        h.execution.wait(.15);
        const auto alreadyWaited = h.clock.now();
        input.buttonDown();
        QCOMPARE(h.events.last()["time"].toDouble(), alreadyWaited);
        input.buttonUp();
        input.release();
        const auto raw = h.clock.now();
        input.click({}, .005);
        QVERIFY(std::abs(h.clock.now() - raw - .02) < 1e-9);
        const auto drag = h.clock.now();
        input.swipe({100, 100}, {500, 500}, 300);
        QVERIFY(std::abs(h.clock.now() - drag - .42) < 1e-9);
        input.move({700, 700});
        const auto wheel = h.clock.now();
        input.scroll(1);
        QVERIFY(std::abs(h.events.last()["time"].toDouble() - wheel - .12) < 1e-9);
        h.config.set("kmbox.position_settle", .1);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, h.config.validateBox());
    }
    void positionedActionCanBePausedBeforeAnyButtonIsPressed() {
        Harness h;
        Input input(h.config, {1920, 1080}, true, h.transport, h.execution);
        h.clock.tick = [&] { if (h.clock.now() >= .05) h.gate.stop(); };
        QVERIFY_THROWS_EXCEPTION(Cancelled, input.taskClick({400, 300}));
        for (const auto &event : h.events)
            QVERIFY(!(event["op"] == "button" && event["down"].toBool()));
        QCOMPARE(h.events.last()["op"].toString(), QString("release"));
    }
    void echoInferenceCancellationCanBeFollowedByAnotherDetection() {
        const QString root = OKWW_SOURCE_ROOT;
        EchoDetector detector(root + "/assets/echo_model/echo.onnx");
        const auto frame = readImage(root + "/assets/images/38.png");
        const auto expected = detector.detect(frame);
        int calls = 0;
        QVERIFY_THROWS_EXCEPTION(Cancelled, detector.detect(frame, .5, 0, [&] {
            if (++calls >= 3) throw Cancelled();
        }));
        const auto recovered = detector.detect(frame, .5, 0, [] {});
        QCOMPARE(recovered.size(), expected.size());
        for (int i = 0; i < recovered.size(); ++i) {
            QCOMPARE(recovered[i].name, expected[i].name);
            QCOMPARE(recovered[i].box, expected[i].box);
        }
    }
    void templateSearchCancellationAndHeartbeatWorkForDirectCallers() {
        const QString root = OKWW_SOURCE_ROOT;
        Templates templates(root + "/assets/coco_annotations.json");
        cv::Mat image(1080, 1920, CV_8UC3, cv::Scalar(17, 23, 31));
        const auto expected = templates.find(image, "garden_not_interested", {.vertical = .4, .limit = 0});
        int progress = 0;
        templates.setCheckpoint([&] { ++progress; });
        const auto actual = templates.find(image, "garden_not_interested", {.vertical = .4, .limit = 0});
        QCOMPARE(actual.size(), expected.size());
        QVERIFY(progress >= 3);
        templates.setCheckpoint([&] { throw Cancelled(); });
        QVERIFY_EXCEPTION_THROWN(templates.find(image, "garden_not_interested"), Cancelled);
    }
    void ocrProgressCancellationPreservesRequestForTheNextTask() {
        const QString root = OKWW_SOURCE_ROOT;
        auto models = root + "/assets/ocr/ppocrv5";
        if (!QFile::exists(models + "/det/det.onnx"))
            models = root + "/.venv/Lib/site-packages/onnxocr/models/ppocrv5";
        if (!QFile::exists(models + "/det/det.onnx")) QSKIP("OCR models absent");
        Ocr ocr(models);
        const auto image = readImage(root + "/assets/images/38.png");
        const auto expected = ocr.read(image);
        QVERIFY(!expected.empty());
        int progress = 0;
        const auto actual = ocr.read(image, {}, .5, true, false, [&] { ++progress; });
        QCOMPARE(actual.size(), expected.size());
        for (int i = 0; i < actual.size(); ++i) {
            QCOMPARE(actual[i].text, expected[i].text);
            QCOMPARE(actual[i].box, expected[i].box);
        }
        QVERIFY(progress > 4);
        int calls = 0;
        // Entry and pre-inference checkpoints pass; cancellation happens once
        // the async detector has started, not just before request submission.
        QVERIFY_EXCEPTION_THROWN(ocr.read(image, {}, .5, true, false, [&] {
            if (++calls >= 3) throw Cancelled();
        }), Cancelled);
        const auto recovered = ocr.read(image, {}, .5, true, false, [] {});
        QCOMPARE(recovered.size(), expected.size());
        for (int i = 0; i < recovered.size(); ++i)
            QCOMPARE(recovered[i].text, expected[i].text);
    }
    void movementWaitsOnlyBetweenReportsAndKeepsTheSameDisplacement() {
        Harness h;
        Input input(h.config, {1920, 1080}, false, h.transport, h.execution);
        input.move({400, 300});
        QCOMPARE(h.clock.time, h.events.last()["time"].toDouble());
        int x = 0, y = 0;
        for (int i = 1; i < h.events.size(); ++i) {
            x += h.events[i]["x"].toInt();
            y += h.events[i]["y"].toInt();
        }
        QCOMPARE(x, 400); QCOMPARE(y, 300);
        const auto began = h.clock.time;
        input.relative(20, 30);
        QCOMPARE(h.clock.time, began);
        QCOMPARE(h.events.last()["x"].toInt(), 20);
        QCOMPARE(h.events.last()["y"].toInt(), 30);
    }
    void monthlyCheckWindowFollowsLocalHourIncludingMidnight() {
        QVERIFY(monthlyWindowKey(QDateTime(QDate(2030,1,2),QTime(3,59,29)),4).isEmpty());
        QCOMPARE(monthlyWindowKey(QDateTime(QDate(2030,1,2),QTime(3,59,30)),4),QString("2030-01-02/04"));
        QCOMPARE(monthlyWindowKey(QDateTime(QDate(2030,1,2),QTime(4,1,29)),4),QString("2030-01-02/04"));
        QVERIFY(monthlyWindowKey(QDateTime(QDate(2030,1,2),QTime(4,1,30)),4).isEmpty());
        QCOMPARE(monthlyWindowKey(QDateTime(QDate(2030,1,2),QTime(0,0,10)),24),QString("2030-01-02/00"));
        QCOMPARE(monthlyWindowKey(QDateTime(QDate(2030,1,1),QTime(23,59,50)),24),QString("2030-01-02/00"));
        QVERIFY_EXCEPTION_THROWN(monthlyWindowKey(QDateTime::currentDateTime(),25),std::runtime_error);
    }
    void scheduleXmlAndScopedChangesUseNoSystemTasksInTests() {
        QTemporaryDir data;
        const QString root=OKWW_SOURCE_ROOT;
        QVector<QStringList> calls;
        QString registeredXml;
        bool reject=false;
        Scheduler schedule("C:/含空格 & 目录/OKWW.exe",root,data.path(),"S-1-5-21-123",[&](const QStringList &args){
            calls.append(args);
            if(reject) fail("fixture: Windows registration failed");
            if(args[0]=="/Create") {
                // schtasks opens the XML independently. A same-process QFile
                // read would miss QTemporaryFile's lingering Windows handle.
                const auto handle=CreateFileW(reinterpret_cast<LPCWSTR>(args[4].utf16()),GENERIC_READ,0,
                                              nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
                if(handle==INVALID_HANDLE_VALUE)fail("schedule XML is still locked");
                CloseHandle(handle);
                QFile file(args[4]);if(!file.open(QIODevice::ReadOnly))fail("missing fixture xml");registeredXml=QString::fromUtf8(file.readAll());
            }
            return QByteArray{};
        });
        QJsonObject config{{"task","DailyTask"},{"label","每日 <测试> & 领取"},{"start","2030-01-02T09:30:00"},{"exit_after",true},{"limit_hours",2}};
        for(const auto &kind:QStringList{"Once","Daily","Weekly","Monthly","Days","Hours"}) {
            config["kind"]=kind;
            const auto xml=schedule.xml(config);
            QXmlStreamReader reader(xml);
            QString command,arguments;
            while(!reader.atEnd()) {
                reader.readNext();
                if(reader.isStartElement()&&reader.name()=="Command")command=reader.readElementText();
                else if(reader.isStartElement()&&reader.name()=="Arguments")arguments=reader.readElementText();
            }
            QVERIFY2(!reader.hasError(),qPrintable(reader.errorString()));
            QCOMPARE(command,QString("C:/含空格 & 目录/OKWW.exe"));
            QVERIFY(arguments.contains("\"--run-task\" \"DailyTask\""));
            QVERIFY(arguments.contains("\"--limit-seconds\" \"7200\""));
            QVERIFY(!arguments.contains("python"));
            QVERIFY(xml.contains("InteractiveToken"));
            QVERIFY(xml.contains("AllowHardTerminate>false"));
        }
        const auto name=schedule.add(config);
        QCOMPARE(schedule.list().size(),1);QVERIFY(!registeredXml.isEmpty());
        schedule.setEnabled(name,false);QVERIFY(!schedule.list()[0].toObject()["enabled"].toBool());
        auto changed=config;changed["limit_hours"]=3;changed["kind"]="Weekly";
        schedule.update(name,changed);
        QCOMPARE(schedule.list().size(),1);
        QCOMPARE(schedule.list()[0].toObject()["name"].toString(),name);
        QCOMPARE(schedule.list()[0].toObject()["kind"].toString(),QString("Weekly"));
        QVERIFY(registeredXml.contains("<Enabled>false</Enabled>"));
        reject=true;QVERIFY_EXCEPTION_THROWN(schedule.update(name,config),std::runtime_error);
        QCOMPARE(schedule.list()[0].toObject()["kind"].toString(),QString("Weekly"));reject=false;
        const auto count=calls.size();
        QVERIFY_EXCEPTION_THROWN(schedule.remove("\\unrelated-task"),std::runtime_error);QCOMPARE(calls.size(),count);
        reject=true;QVERIFY_EXCEPTION_THROWN(schedule.add(config),std::runtime_error);QCOMPARE(schedule.list().size(),1);
        reject=false;schedule.remove(name);QVERIFY(schedule.list().isEmpty());
        config["task"]="AutoCombatTask";QVERIFY_EXCEPTION_THROWN(schedule.xml(config),std::runtime_error);
        config["task"]="DailyTask";config["interval"]=-1;QVERIFY_EXCEPTION_THROWN(schedule.xml(config),std::runtime_error);
        QCOMPARE(Scheduler::quoteArgument("C:\\path\\"),QString("\"C:\\path\\\\\""));
        QCOMPARE(Scheduler::quoteArgument("a\"b"),QString("\"a\\\"b\""));
    }
    void scheduleWindowsRegistrationIntegration() {
        if(qEnvironmentVariableIntValue("OKWW_TEST_WINDOWS_SCHEDULER")!=1)
            QSKIP("Opt-in Windows registration check; ordinary tests do not register tasks");
        QTemporaryDir data;
        // Future-only fixture with an inert system lookup utility; never points
        // at the game application and is never started by this check.
        Scheduler schedule(qEnvironmentVariable("SystemRoot")+"/System32/where.exe",
                           OKWW_SOURCE_ROOT,data.path());
        struct Cleanup {
            Scheduler &scheduler;QString name;
            ~Cleanup(){if(!name.isEmpty())try{scheduler.remove(name);}catch(...){}}
        } cleanup{schedule,{}};
        QJsonObject spec{{"task","DailyTask"},{"label","OKWW offline scheduler verification"},
                         {"start","2035-01-02T09:30:00"},{"kind","Once"},{"exit_after",false}};
        cleanup.name=schedule.add(spec);
        for(const auto &kind:QStringList{"Once","Daily","Weekly","Monthly","Days","Hours"}) {
            spec["kind"]=kind;
            schedule.update(cleanup.name,spec);
            const auto registered=Scheduler::runCommand({"/Query","/TN",cleanup.name,"/XML"});
            QVERIFY(!registered.isEmpty());
        }
        schedule.setEnabled(cleanup.name,false);
        QVERIFY(!schedule.list()[0].toObject()["enabled"].toBool());
        schedule.setEnabled(cleanup.name,true);
        schedule.remove(cleanup.name);
        cleanup.name.clear();
        QVERIFY(schedule.list().isEmpty());
    }
    void remoteControlEdgesAndDisableInterruptRespectMacroState() {
        Harness h;
        Input input(h.config, {1920, 1080}, false, h.transport, h.execution);
        QTemporaryDir temp;
        const QString root = OKWW_SOURCE_ROOT;
        Profile profile(temp.path(), root + "/native/resources/catalog.json");
        Templates templates(root + "/assets/coco_annotations.json");
        TaskContext context(
            h.execution, input, templates, profile, [] { return cv::Mat::zeros(1080, 1920, CV_8UC3); },
            [](auto &, auto, double) { return QVector<TextLine>{}; });
        context.task = "AutoCombatTask";
        bool pressed = false;
        context.physicalFDown = [&] { return pressed; };
        CombatSession session(context, root, temp.path());
        session.pollControls();
        QCOMPARE(session.macroRearmAt, -1.);
        h.clock.time = 1;
        pressed = true;
        session.pollControls();
        QCOMPARE(session.macroRearmAt, 1.);
        h.clock.time = 2;
        session.pollControls();
        QCOMPARE(session.macroRearmAt, 1.);
        pressed = false;
        session.pollControls();
        session.macroBusy = true;
        pressed = true;
        session.pollControls();
        QCOMPARE(session.macroRearmAt, 1.);
        session.macroBusy = false;
        session.pollControls();
        QCOMPARE(session.macroRearmAt, 1.);
        ++context.state->combatEnableRevision;
        session.pollControls();
        QCOMPARE(session.macroRearmAt, 2.);
        session.macroBusy = true;
        ++context.state->combatEnableRevision;
        QVERIFY_EXCEPTION_THROWN(session.pollControls(), CombatEnded);
        session.macroBusy = false;
        session.pollControls();
        profile.set("AutoCombatTask", "_enabled", false);
        QVERIFY_EXCEPTION_THROWN(session.pollControls(), CombatEnded);
        context.task = "DailyTask";
        session.pollControls();
        QVERIFY(h.events.isEmpty());
    }
    void mergeWithoutRepeatConfirmationRequiresPositiveResult() {
        Harness h;
        Input input(h.config, {1920, 1080}, false, h.transport, h.execution);
        QTemporaryDir temp;
        const QString root = OKWW_SOURCE_ROOT;
        Profile profile(temp.path(), root + "/native/resources/catalog.json");
        Templates templates(root + "/assets/coco_annotations.json");
        bool result = true;
        TaskContext context(
            h.execution, input, templates, profile, [] { return cv::Mat::zeros(1080, 1920, CV_8UC3); },
            [&](auto &, QRect area, double) {
                return result ? QVector<TextLine>{{"获得声骸", area, .99}} : QVector<TextLine>{};
            });
        awaitEchoMergeResult(context);
        QVERIFY(h.events.isEmpty());
        result = false;
        QVERIFY_EXCEPTION_THROWN(awaitEchoMergeResult(context), std::runtime_error);
        QVERIFY(h.events.isEmpty());
        QVERIFY(!echoMergeResultConfirmed({{"是否消耗 100 个声骸进行数据融合", {}, .99}}));
        QVERIFY(echoMergeResultConfirmed({{"Echoes Obtained", {}, .99}}));
    }
    void fullTaskRegistryHasNoMissingOrDuplicateEntries() {
        const auto catalog = readJson(QString(OKWW_SOURCE_ROOT) + "/native/resources/catalog.json");
        auto tasks = builtinTasks();
        tasks += businessTasks([](const QString &) { return true; });
        QSet<QString> ids;
        for (const auto &task : tasks) {
            QVERIFY(!ids.contains(task.id));
            ids.insert(task.id);
        }
        QCOMPARE(ids.size(), catalog["tasks"].toArray().size());
        for (auto entry : catalog["tasks"].toArray())
            QVERIFY(ids.contains(entry.toObject()["id"].toString()));
    }
    void nativeMacroUsesBoxInputWithoutMovingPointerAndReleasesOnCancel() {
        Harness h;
        Input input(h.config, {1920, 1080}, false, h.transport, h.execution);
        QTemporaryDir temp;
        const QString root = OKWW_SOURCE_ROOT;
        Profile profile(temp.path(), root + "/native/resources/catalog.json");
        Templates templates(root + "/assets/coco_annotations.json");
        TaskContext context(
            h.execution, input, templates, profile,
            [] { return cv::Mat(1080, 1920, CV_8UC3, cv::Scalar(0, 0, 0)); },
            [](auto &, auto, auto) { return QVector<TextLine>{}; });
        context.task = "AutoCombatTask";
        CombatSession host(context, root, temp.path());
        {
            MacroProbe macro(host);
            QCOMPARE(macro.spamKey([&] { host.rawTap("e", .01); }, 0, {.interval = 0, .presses = 2}), 2);
            QCOMPARE(h.events.size(), 4);
            macro.spam(.36, .12);
            for (const auto &event : h.events)
                QVERIFY(event["op"] != "absolute" && event["op"] != "relative");
            macro.hold();
            h.clock.tick = [&] { h.gate.stop(); };
            QVERIFY_THROWS_EXCEPTION(Cancelled, macro.wait(1));
        }
        QCOMPARE(h.events.back()["op"].toString(), QString("button"));
        QCOMPARE(h.events.back()["down"].toBool(), false);
    }
    void echoDecisionAndDailyProgressGuards() {
        QJsonObject config{{"必须有双爆", true},
                           {"双爆出现之前必须全有效词条", true},
                           {"首条双爆>=", 6.9},
                           {"双爆总计>=", 13.8},
                           {"有效词条>=", 3},
                           {"第一条必须为有效词条", true},
                           {"有效词条", QJsonArray{"暴击", "暴击伤害", "攻击百分比"}}};
        QVERIFY(evaluateEcho({{"暴击", "6.9%"}}, config).keep);
        QVERIFY(!evaluateEcho({{"暴击", "6.3%"}}, config).keep);
        QVERIFY(!evaluateEcho({{"生命", "450"}, {"暴击", "10.5%"}}, config).keep);
        QVERIFY(
            evaluateEcho(
                {{"暴击", "6.9%"}, {"暴击伤害", "13.8%"}, {"攻击", "8.6%"}, {"生命", "450"}, {"防御", "50"}},
                config)
                .keep);
        QCOMPARE(normalizeEchoStat("暴擊傷害", "13.8％"), QString("暴击伤害"));
        const auto stats = pairEchoStats({{"辅音", {10, 10, 80, 20}, 1},
                                          {"攻击", {10, 100, 60, 20}, 1},
                                          {"暴击", {10, 130, 60, 20}, 1},
                                          {"6.9％", {180, 131, 50, 20}, 1},
                                          {"8.6%", {180, 101, 50, 20}, 1}});
        QCOMPARE(stats.size(), 2);
        QCOMPARE(stats[0].value, QString("8.6%"));
        QCOMPARE(stats[1].value, QString("6.9％"));
        auto progress = dailyProgress({{"120 ／ 180", {}, 1}}, {{"100", {}, 1}});
        QCOMPARE(progress.stamina.value(), 120);
        QVERIFY(progress.ready());
        progress = dailyProgress({{"100/100", {}, 1}}, {{"100/100", {}, 1}});
        QVERIFY(!progress.stamina);
        QVERIFY(!progress.ready());
        QCOMPARE(dailyDomain({{"Run Tacet or Forgery First", "Forgery First"}}), DomainKind::Forgery);
    }
    void scopedTaskSettingsRestoreAfterExceptions() {
        Harness h;
        Input input(h.config, {1920, 1080}, false, h.transport, h.execution);
        QTemporaryDir temp;
        const QString root = OKWW_SOURCE_ROOT;
        Profile profile(temp.path(), root + "/native/resources/catalog.json");
        Templates templates(root + "/assets/coco_annotations.json");
        TaskContext context(
            h.execution, input, templates, profile,
            [] { return cv::Mat(1080, 1920, CV_8UC3, cv::Scalar(0, 0, 0)); },
            [](auto &, auto, auto) { return QVector<TextLine>{}; });
        context.task = "DailyTask";
        context.taskOverrides = {{"example", 2}};
        try {
            TaskScope nested(context, "FarmEchoTask", {{"example", 4}});
            QCOMPARE(context.setting("example").toInt(), 4);
            throw Cancelled();
        } catch (const Cancelled &) {
        }
        QCOMPARE(context.task, QString("DailyTask"));
        QCOMPARE(context.setting("example").toInt(), 2);
        QVERIFY(h.events.empty());
    }
    void hidAliasesAndRounding() {
        QCOMPARE(Input::keyCode("LSHIFT"), 0xe1);
        QCOMPARE(Input::keyCode("e"), 8);
        QCOMPARE(Input::keyCode("F12"), 0x45);
        QCOMPARE(pixelRound(.5), 0);
        QCOMPARE(pixelRound(1.5), 2);
        QCOMPARE(pixelRound(-.5), 0);
        QCOMPARE(pixelRound(-1.5), -2);
        QVERIFY_EXCEPTION_THROWN(Input::keyCode("鼠标"), std::runtime_error);
    }
    void holdsAndPulse() {
        Harness h;
        Input input(h.config, {1920, 1080}, false, h.transport, h.execution);
        input.keyDown("w");
        input.key("w", .12);
        QCOMPARE(h.events.size(), 2);
        input.keyUp("w");
        QCOMPARE(h.events.back()["down"].toBool(), false);
        QVERIFY(std::abs(h.clock.now() - .12) < 1e-9);
        input.click({}, .005);
        QCOMPARE(h.events[h.events.size() - 2]["op"].toString(), QString("button"));
        QVERIFY(std::abs(h.clock.now() - .14) < 1e-9);
    }
    void cancelledHeavyReleases() {
        Harness h;
        Input input(h.config, {1920, 1080}, false, h.transport, h.execution);
        h.clock.tick = [&] {
            if (h.clock.now() >= .03)
                h.gate.stop();
        };
        QVERIFY_EXCEPTION_THROWN(input.click({}, 4), Cancelled);
        QCOMPARE(h.events.front()["down"].toBool(), true);
        QCOMPARE(h.events.back()["op"].toString(), QString("release"));
        QVERIFY(h.clock.now() < .05);
    }
    void epochsCannotResumeOldStack() {
        Harness h;
        h.gate.stop();
        h.gate.start();
        QVERIFY_EXCEPTION_THROWN(h.execution.checkpoint(), Cancelled);
    }
    void croppedCoordinateMapping() {
        Harness h;
        h.config.set("capture.output_width", 1280);
        h.config.set("capture.output_height", 720);
        h.config.set("capture.crop", QJsonArray{320, 180, 1920, 1080});
        h.config.set("kmbox.desktop_width", 3840);
        h.config.set("kmbox.desktop_height", 2160);
        Input input(h.config, {2560, 1440}, true, h.transport, h.execution);
        QCOMPARE(input.screenPoint({640, 360}), QPoint(1920, 1080));
        QCOMPARE(input.screenPoint({0, 0}), QPoint(480, 270));
        input.move({640, 360});
        QCOMPARE(h.events.front()["op"].toString(), QString("absolute"));
        QVERIFY_EXCEPTION_THROWN(input.move({1280, 0}), std::runtime_error);
    }
    void relativeHomingAndDrag() {
        Harness h;
        h.config.set("capture.output_width", 1920);
        h.config.set("capture.output_height", 1080);
        h.config.set("kmbox.desktop_width", 1920);
        h.config.set("kmbox.desktop_height", 1080);
        h.config.set("kmbox.counts_per_pixel_x", 1);
        h.config.set("kmbox.counts_per_pixel_y", 1);
        Input input(h.config, {1920, 1080}, false, h.transport, h.execution);
        input.move({400, 300});
        QCOMPARE(h.events.front()["x"].toInt(), -h.config.n("kmbox.home_counts"));
        int x = 0, y = 0;
        for (int i = 1; i < h.events.size(); ++i) {
            x += h.events[i]["x"].toInt();
            y += h.events[i]["y"].toInt();
            QVERIFY(std::abs(h.events[i]["x"].toInt()) <= 127);
        }
        QCOMPARE(x, 400);
        QCOMPARE(y, 300);
        input.buttonDown();
        h.events.clear();
        input.move({440, 340});
        QCOMPARE(h.events.front()["x"].toInt(), 40);
        QCOMPARE(h.events.front()["y"].toInt(), 40);
    }
    void textPrevalidationSendsNothing() {
        Harness h;
        Input input(h.config, {1920, 1080}, false, h.transport, h.execution);
        QVERIFY_EXCEPTION_THROWN(input.text("abc中文"), std::runtime_error);
        QVERIFY(h.events.isEmpty());
        input.text("A!\n");
        QCOMPARE(h.events.front()["code"].toInt(), 0xe1);
    }
    void frameDecodingAndBounds() {
        Frame f;
        f.width = 4;
        f.height = 4;
        f.stride = 8;
        f.format = PixelFormat::NV12;
        f.bytes = QByteArray(48, char(128));
        for (int y = 0; y < 4; ++y)
            memset(f.bytes.data() + y * 8, 16, 4);
        auto out = decode(f);
        QCOMPARE(cv::countNonZero(out.reshape(1)), 0);
        f.bytes.resize(2);
        QVERIFY_EXCEPTION_THROWN(decode(f), std::runtime_error);
    }
    void anchorScalingAcrossResolutions() {
        QCOMPARE(Templates::anchored({1800, 900, 80, 40}, {1920, 1080}, {2560, 1440}),
                 QRect(2400, 1200, 107, 53));
        QCOMPARE(Templates::anchored({1800, 900, 80, 40}, {1920, 1080}, {2560, 1080}),
                 QRect(2440, 900, 80, 40));
        QCOMPARE(Templates::anchored({900, 500, 80, 40}, {1920, 1080}, {2560, 1080}, true, true),
                 QRect(1220, 500, 80, 40));
    }
    void realAnnotationMatching() {
        const QString root = QString::fromUtf8(OKWW_SOURCE_ROOT);
        Templates templates(root + "/assets/coco_annotations.json");
        QVERIFY(templates.names().size() > 100);
        auto metadata = readJson(root + "/assets/coco_annotations.json");
        int category = -1;
        for (auto c : metadata["categories"].toArray())
            if (c.toObject()["name"] == "char_1_text")
                category = c.toObject()["id"].toInt();
        QVERIFY(category >= 0);
        int imageId = -1;
        for (auto a : metadata["annotations"].toArray())
            if (a.toObject()["category_id"].toInt() == category) {
                imageId = a.toObject()["image_id"].toInt();
                break;
            }
        QString path;
        for (auto i : metadata["images"].toArray())
            if (i.toObject()["id"].toInt() == imageId)
                path = i.toObject()["file_name"].toString();
        QVERIFY(!path.isEmpty());
        auto image = readImage(root + "/assets/" + path);
        auto found = templates.one(image, "char_1_text");
        QVERIFY(found.has_value());
        QVERIFY(found->confidence > .99);
        const auto reduced = templates.one(image, "char_1_text", {.threshold = .8, .targetHeight = 360});
        QVERIFY(reduced.has_value());
        QVERIFY((reduced->center() - found->center()).manhattanLength() < 8);
        QCOMPARE(reduced->box.size(), found->box.size());
    }
    void backendWithoutPythonOrIpc() {
        Backend backend(false);
        QCOMPARE(backend.port(), 0);
        backend.start(Config::defaults(), true);
        backend.arm(true);
        QVERIFY(backend.latestFrame() != nullptr);
        backend.nativeCommand({{"op", "key"}, {"code", 8}, {"down", true}});
        QVERIFY(backend.snapshot()["box"].toObject()["keys"].toArray().contains(8));
        backend.arm(false);
        QVERIFY(backend.snapshot()["box"].toObject()["keys"].toArray().isEmpty());
    }
    void profilePersistenceAndValidation() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writeJson(directory.path() + "/AutoCombatTask.json", {{"_enabled", false}, {"legacy_extra", 17}});
        const auto catalog = QString::fromUtf8(OKWW_SOURCE_ROOT) + "/native/resources/catalog.json";
        Profile profile(directory.path(), catalog);
        QCOMPARE(profile.group("AutoCombatTask")["legacy_extra"].toInt(), 17);
        QVERIFY(!profile.value("AutoCombatTask", "_enabled").toBool());
        profile.set("AutoCombatTask", "Auto Target", false);
        Profile reloaded(directory.path(), catalog);
        QVERIFY(!reloaded.value("AutoCombatTask", "Auto Target").toBool());
        QCOMPARE(reloaded.group("AutoCombatTask")["legacy_extra"].toInt(), 17);
        QVERIFY_EXCEPTION_THROWN(profile.set("AutoCombatTask", "Auto Target", 3), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(profile.set("Task Recovery", "Recovery Retry Limit", 100),
                                 std::runtime_error);
    }
    void monthlyRequiresClaimAndRemainingDays() {
        QVector<TextLine> lines{{"今日月相 点击领取", {}, .99}, {"剩余28天", {}, .99}};
        QVERIFY(monthlyTextConfirmed(lines));
        lines.removeLast();
        QVERIFY(!monthlyTextConfirmed(lines));
        QVERIFY(!monthlyTextConfirmed({{"剩余28天", {}, .99}, {"领取每日任务奖励", {}, .99}}));
        QVERIFY(monthlyTextConfirmed({{"Claim today's Lunite", {}, .99}, {"28 days remaining", {}, .99}}));
        QVERIFY(rewardTextConfirmed({{"获得物品", {}, .99}, {"点击空白处关闭", {}, .99}}));
        QVERIFY(!rewardTextConfirmed({{"获得物品", {}, .99}}));
    }
    void nativeRotationInterruptReleasesHeldMouse() {
        test::RotationReplay host({{"ready", QJsonArray{true, true, true}}, {"stop_at", 100.1}});
        CharacterState state;
        state.index = 0;
        state.current = true;
        Character actor(host, state);
        host.character = &actor;
        QVERIFY_EXCEPTION_THROWN(actor.heavyAttack(1), CombatEnded);
        QVERIFY(!host.held);
        QCOMPARE(host.events.last().toObject()["kind"].toString(), QString("up"));
    }
    void skillRequiresObservedAnimationAndRestoresFlags() {
        test::RotationReplay host({{"ready", QJsonArray{false, false, true}}, {"animation", false}});
        CharacterState state;
        state.index = 0;
        state.current = true;
        Character actor(host, state);
        host.character = &actor;
        QVERIFY(!actor.liberation());
        QVERIFY(!host.inLiberation);
        test::RotationReplay interrupted({{"ready", QJsonArray{false, false, true}}, {"stop_at", 100.2}});
        Character actor2(interrupted, state);
        interrupted.character = &actor2;
        QVERIFY_EXCEPTION_THROWN(actor2.liberation(), CombatEnded);
        QVERIFY(!interrupted.inLiberation);
    }
    void unknownCharacterNeverSilentlyUsesGenericRotation() {
        test::RotationReplay host({{"ready", QJsonArray{false, false, false}}});
        CharacterState state;
        QVERIFY_EXCEPTION_THROWN(makeCharacter("UnknownImportedTeam", host, state), std::runtime_error);
    }
    void plungeInterruptReleasesForwardKey() {
        test::RotationReplay host(
            {{"ready", QJsonArray{false, false, false}}, {"forte", true}, {"stop_at", 100.2}});
        CharacterState state;
        state.index = 0;
        state.current = true;
        auto actor = makeCharacter("Roccia", host, state);
        host.character = actor.get();
        QVERIFY_EXCEPTION_THROWN(actor->perform(), CombatEnded);
        QVERIFY(host.heldKeys.isEmpty());
        QVERIFY(!host.held);
        QCOMPARE(host.events.last().toObject()["kind"].toString(), QString("key_up"));
        QCOMPARE(host.events.last().toObject()["value"].toString(), QString("w"));
    }
    void jiyanMissingGaugeCannotLoopForever() {
        test::RotationReplay host({{"ready", QJsonArray{false, false, false}}, {"forte", false}, {"con", 0}});
        CharacterState state;
        state.index = 0;
        state.current = true;
        auto actor = makeCharacter("Jiyan", host, state);
        host.character = actor.get();
        QVERIFY_EXCEPTION_THROWN(actor->perform(), CombatEnded);
        QVERIFY(host.now() >= 115 && host.now() < 116);
        QVERIFY(!host.held);
        QVERIFY(host.heldKeys.isEmpty());
        for (auto value : host.events)
            QVERIFY(value.toObject()["kind"].toString() != "switch");
    }
    void shoreKeeperIntroStillAllowsCancellation() {
        test::RotationReplay host({{"ready", QJsonArray{true, true, true}}, {"cancel_at", 100.05}});
        CharacterState state;
        state.index = 0;
        state.current = state.hasIntro = true;
        auto actor = makeCharacter("ShoreKeeper", host, state);
        host.character = actor.get();
        QVERIFY(actor->skipSwitchCombatCheck(true));
        QVERIFY(!actor->skipSwitchCombatCheck(false));
        QVERIFY_EXCEPTION_THROWN(actor->perform(), Cancelled);
        QVERIFY(!host.skipCombatCheck);
        QVERIFY(host.events.isEmpty());
    }
    void yangyangInterruptedHoldAlwaysReleases() {
        test::RotationReplay host({{"ready", QJsonArray{false, false, true}}, {"cancel_at", 100.2}});
        CharacterState state;
        state.index = 0;
        state.current = state.hasIntro = true;
        auto actor = makeCharacter("YangYangSp", host, state);
        host.character = actor.get();
        QVERIFY_EXCEPTION_THROWN(actor->perform(), Cancelled);
        QVERIFY(!host.held);
        QVERIFY(!host.inLiberation);
        QCOMPARE(host.events.last().toObject()["kind"].toString(), QString("up"));
        for (auto value : host.events)
            QVERIFY(value.toObject()["kind"].toString() != "switch");
    }
    void xiangliyaoSkillNeverBecomingReadyHasDeadline() {
        test::RotationReplay host({{"ready", QJsonArray{false, false, true}}});
        CharacterState state;
        state.index = 0;
        state.current = true;
        auto actor = makeCharacter("Xiangliyao", host, state);
        host.character = actor.get();
        QVERIFY_EXCEPTION_THROWN(actor->perform(), CombatEnded);
        QVERIFY(host.now() >= 130 && host.now() < 132);
        QVERIFY(!host.held);
        QVERIFY(!host.inLiberation);
    }
    void ultrawideCooldownCoordinates() {
        QCOMPARE(CombatVision::cdX(1920, 1080, .82), .82);
        QVERIFY(std::abs(CombatVision::cdX(2560, 1080, .82) - .865) < 1e-9);
        QCOMPARE(Concerto::region({3840, 2160}), QRect(1431, 1942, 126, 126));
        QCOMPARE(Concerto::region({2560, 1080}), QRect(1036, 971, 63, 63));
    }
    void allBuiltInCharacterDefinitionsHaveNativeFactories() {
        const auto catalog =
            readJson(QString::fromUtf8(OKWW_SOURCE_ROOT) + "/native/resources/characters.json");
        const auto registered = nativeCharacterClasses();
        for (auto entry : catalog["characters"].toArray()) {
            const auto name = entry.toObject()["class"].toString();
            QVERIFY2(registered.contains(name), qPrintable(name));
        }
        QCOMPARE(registered.size(), 58); // 57 roles plus BaseChar.
    }
    void addedCharacterRotationsFinishWithoutStuckInputs() {
        for (const auto &name : QStringList{"Aalto", "Lingyang", "Lumi", "Yangyang", "Suoming"}) {
            test::RotationReplay host({{"ready", QJsonArray{false, false, false}}, {"budget_until", 130}});
            CharacterState state;
            state.current = true;
            state.index = 0;
            auto character = makeCharacter(name, host, state);
            host.character = character.get();
            character->perform();
            QVERIFY2(host.now() < 130, qPrintable(name));
            QVERIFY(!host.held);
            QVERIFY(host.heldKeys.isEmpty());
            QCOMPARE(host.events.last().toObject()["kind"].toString(), QString("switch"));
        }
    }
    void suomingSupportPassesIntroWithoutConsumingResonance() {
        test::RotationReplay host({{"ready", QJsonArray{false, true, true}},
                                   {"teammate", "Other"}, {"confirm_switch", true}});
        CharacterState state;
        state.current = true;
        state.index = 0;
        auto character = makeCharacter("Suoming", host, state);
        host.character = character.get();
        character->perform();
        QVERIFY(character->state.lastBuff >= 100);
        for (const auto &event : host.events) {
            const auto object = event.toObject();
            QVERIFY(object["kind"] != "key_down");
            QVERIFY(object["kind"] != "key" || object["value"] != "e");
        }
        QVERIFY(!host.held && host.heldKeys.isEmpty());
    }
    void suomingDpsSettingUpdatesRoleAndBuffDuration() {
        test::RotationReplay host({{"ready", QJsonArray{false, false, false}},
                                   {"character_config", QJsonObject{{"Suoming DPS", true}}}});
        auto character = makeCharacter("Suoming", host, {});
        host.character = character.get();
        QCOMPARE(character->state.role, Role::MainDps);
        QCOMPARE(character->state.buffTime, 14.);
        host.observe({{"character_config", QJsonObject{{"Suoming DPS", false}}}});
        character->resetState();
        QCOMPARE(character->state.role, Role::SubDps);
        QCOMPARE(character->state.buffTime, 8.);
    }
    void suomingResonanceHoldReleasesOnPauseAndCombatEnd() {
        for (const auto &interruption : {QString("cancel_at"), QString("stop_at"), QString()}) {
            QJsonObject config{{"ready", QJsonArray{false, false, false}},
                               {"features", QJsonObject{{"suoming_forte_e", true}}},
                               {"budget_until", 130}};
            if (!interruption.isEmpty())
                config[interruption] = 104;
            test::RotationReplay host(config);
            CharacterState state;
            state.current = state.hasIntro = true;
            state.index = 0;
            auto character = makeCharacter("Suoming", host, state);
            host.character = character.get();
            if (interruption == "cancel_at")
                QVERIFY_EXCEPTION_THROWN(character->perform(), Cancelled);
            else if (interruption == "stop_at")
                QVERIFY_EXCEPTION_THROWN(character->perform(), CombatEnded);
            else
                character->perform();
            QVERIFY(host.heldKeys.isEmpty());
            QVERIFY(!host.held);
            int downs = 0, ups = 0;
            for (const auto &event : host.events) {
                const auto object = event.toObject();
                if (object["value"] == "e") {
                    downs += object["kind"] == "key_down";
                    ups += object["kind"] == "key_up";
                }
            }
            QVERIFY(downs > 0);
            QCOMPARE(ups, downs);
            QVERIFY(downs <= 3);
            QVERIFY(host.now() < 130);
        }
    }
    void upstreamPortraitAndForteTemplatesScaleAcrossCaptureResolutions() {
        const QString root = OKWW_SOURCE_ROOT;
        Templates templates(root + "/assets/coco_annotations.json");
        const auto manifest = readJson(root + "/assets/upstream-characters/manifest.json");
        for (const auto &entry : manifest["templates"].toArray()) {
            const auto object = entry.toObject();
            const auto bbox = object["bbox"].toArray(), reference = object["reference"].toArray();
            const QRect original(bbox[0].toInt(), bbox[1].toInt(), bbox[2].toInt(), bbox[3].toInt());
            const QSize source(reference[0].toInt(), reference[1].toInt());
            const auto crop = readImage(root + "/assets/" + object["original"].toString());
            for (const auto size : {QSize(1280, 720), QSize(1920, 1080), QSize(2560, 1440),
                                    QSize(3840, 2160), QSize(2560, 1080)}) {
                const auto area = Templates::anchored(original, source, size);
                cv::Mat frame = cv::Mat::zeros(size.height(), size.width(), CV_8UC3), resized;
                cv::resize(crop, resized, {area.width(), area.height()});
                resized.copyTo(frame(cv::Rect(area.x(), area.y(), area.width(), area.height())));
                auto found = templates.one(frame, object["label"].toString(), {.threshold = .99});
                QVERIFY2(found.has_value(), qPrintable(object["label"].toString()));
                QCOMPARE(found->box, area);
            }
        }
    }
    void monsterListUsesExactNamesAndStopsAtRepeatedPage() {
        const auto root = QString::fromUtf8(OKWW_SOURCE_ROOT);
        Templates templates(root + "/assets/coco_annotations.json");
        QTemporaryDir directory;
        Profile profile(directory.path(), root + "/native/resources/catalog.json");
        Harness h;
        Input input(h.config, {1920, 1080}, true, h.transport, h.execution);
        cv::Mat frame = cv::Mat::zeros(1080, 1920, CV_8UC3);
        int reads = 0;
        bool exact = true;
        TaskContext context(
            h.execution, input, templates, profile, [&] { return frame; },
            [&](const cv::Mat &, QRect, double) {
                ++reads;
                QVector<TextLine> lines{{"Crownless (Elite)", {200, 250, 120, 30}, .99}};
                if (exact)
                    lines.append({" CROWNLESS ", {200, 400, 120, 30}, .99});
                return lines;
            });
        context.task = "FarmEchoTask";
        context.refresh();
        Navigation nav(context);
        QCOMPARE(nav.selectMonster("Crownless").box.y(), 400);
        QCOMPARE(reads, 1);
        reads = 0;
        exact = false;
        QVERIFY_EXCEPTION_THROWN(nav.selectMonster("Crownless"), NavigationFailure);
        QCOMPARE(reads, 3);
    }
    void monsterSearchCancellationReleasesControlKey() {
        const auto root = QString::fromUtf8(OKWW_SOURCE_ROOT);
        Templates templates(root + "/assets/coco_annotations.json");
        QTemporaryDir directory;
        Profile profile(directory.path(), root + "/native/resources/catalog.json");
        Harness h;
        Input input(h.config, {1920, 1080}, true, h.transport, h.execution);
        cv::Mat frame = cv::Mat::zeros(1080, 1920, CV_8UC3);
        TaskContext context(
            h.execution, input, templates, profile, [&] { return frame; },
            [](const cv::Mat &, QRect, double) { return QVector<TextLine>{}; });
        context.refresh();
        bool control = false, released = false;
        h.clock.tick = [&] {
            for (const auto &event : h.events)
                if (event["op"] == "key" && event["code"].toInt() == 0xe0 && event["down"].toBool())
                    control = true;
            if (control)
                h.gate.stop();
        };
        Navigation nav(context);
        QVERIFY_EXCEPTION_THROWN(nav.selectMonster("Crownless"), Cancelled);
        for (const auto &event : h.events)
            if (event["op"] == "release" ||
                (event["op"] == "key" && event["code"].toInt() == 0xe0 && !event["down"].toBool()))
                released = true;
        QVERIFY(control);
        QVERIFY(released);
    }
    void bookScrollHonorsGroupHeadersAndBounds() {
        const double top = 424. / 2160;
        const QVector<int> groups{4, 6, 3};
        const double row = (.8806 - top - 2 * .028) / 13;
        QVERIFY(std::abs(bookScrollPosition(5, 13, groups, top) - (top + row * 5 + .028)) < 1e-12);
        QCOMPARE(bookScrollPosition(13, 13, groups, top), .8806);
        QVERIFY_EXCEPTION_THROWN(bookScrollPosition(0, 13, groups, top), NavigationFailure);
        QVERIFY_EXCEPTION_THROWN(bookScrollPosition(14, 13, groups, top), NavigationFailure);
    }
    void staminaCountersIgnorePerformanceOverlay() {
        const auto values = staminaValues({{"165", {1500, 20, 40, 20}, .99},
                                           {"15/20", {960, 100, 70, 20}, .99},
                                           {"125 / 240", {1300, 30, 100, 30}, .99},
                                           {"MAX1,234MAX", {1150, 30, 120, 30}, .99},
                                           {"999", {1200, 150, 40, 20}, .99}});
        QCOMPARE(values.current, 125);
        QCOMPARE(values.reserve, 1234);
        QCOMPARE(values.total, 1359);
        QCOMPARE(staminaValues({{"240 FPS", {}, 1}, {"4/5", {}, 1}}).current, -1);
    }
    void exactDomainCountsNeverSpendBeyondQuota() {
        QCOMPARE(domainCountLimit(100, 239, 60), 3);
        QCOMPARE(domainCountLimit(2, 999, 40), 2);
        QCOMPARE(domainCountLimit(0, 999, 40), 0);
        QVERIFY_EXCEPTION_THROWN(domainCountLimit(1, -1, 40), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(planStaminaSpend({-1, -1, -1}, 40, 80, true), std::runtime_error);
        const auto noReserve = planStaminaSpend({40, 240, 280}, 40, 0, false);
        QCOMPARE(noReserve.used, 40);
        QVERIFY(!noReserve.repeat);
        const auto useCurrent = planStaminaSpend({180, 0, 180}, 60, 0, false);
        QCOMPARE(useCurrent.used, 120);
        QVERIFY(useCurrent.repeat);
        const auto counted = planStaminaSpend({240, 0, 240}, 60, 60, true);
        QCOMPARE(counted.used, 60);
        QVERIFY(!counted.repeat);
        const auto reserve = planStaminaSpend({0, 120, 120}, 40, 80, true);
        QCOMPARE(reserve.used, 80);
        QVERIFY(!reserve.repeat);
        for (int once : {40, 60})
            for (int count = 1; count < 10; ++count) {
                StaminaValues stamina{240, 1200, 1440};
                int remaining = count * once, used = 0;
                while (remaining > 0) {
                    const auto spend = planStaminaSpend(stamina, once, remaining, true);
                    QVERIFY(spend.used > 0 && spend.used <= remaining);
                    const int reserveUsed = std::max(0, spend.used - stamina.current);
                    stamina.current = std::max(0, stamina.current - spend.used);
                    stamina.reserve -= reserveUsed;
                    stamina.total -= spend.used;
                    used += spend.used;
                    remaining -= spend.used;
                    QCOMPARE(spend.repeat, remaining > 0);
                }
                QCOMPARE(used, count * once);
            }
    }
    void instanceDialogsNeverTreatResourceConfirmationAsRetry() {
        const auto lines = [](QString title) {
            return QVector<TextLine>{{title, {400, 350, 700, 80}, .99},
                                     {"取消", {600, 750, 100, 40}, .99},
                                     {"确认", {1200, 750, 100, 40}, .99}};
        };
        for (const auto &title : {"确认消耗60结晶波片领取奖励？", "是否消耗结晶波片领取奖励后重新挑战？",
                                  "確認消耗體力領取獎勵？", "Confirm claim reward with Waveplates?"}) {
            const auto dialog = restartDialog(lines(title));
            QCOMPARE(dialog.kind, RestartKind::Resource);
            QVERIFY(dialog.button);
            QCOMPARE(dialog.button->text, QString("取消"));
            QVERIFY(!nestExitConfirmation(lines(title), 1920));
        }
        QCOMPARE(restartDialog(lines("是否重新挑战当前副本？")).kind, RestartKind::Confirm);
        QCOMPARE(restartDialog(lines("是否退出当前副本？")).kind, RestartKind::None);
        QVERIFY(nestExitConfirmation(lines("是否退出当前副本？"), 1920));
        QVERIFY(!nestExitConfirmation(lines("是否消耗星声退出副本？"), 1920));
        QVERIFY(!nestExitConfirmation(lines("是否购买商品？"), 1920));
        for (const auto &text : {"F：离开当前副本", "Leave Domain", "退出秘境"})
            QVERIFY(isNestExitText(text));
        for (const auto &text : {"吸收", "领取奖励", "是否退出副本"})
            QVERIFY(!isNestExitText(text));
    }
    void repeatedBossMustObserveLoadingAndNewRealmHud() {
        RestartTracker tracker;
        QCOMPARE(tracker.step(RestartKind::None, true, true), RestartAction::Menu);
        QCOMPARE(tracker.step(RestartKind::Restart, false, false), RestartAction::Click);
        for (int i = 0; i < 4; ++i)
            QCOMPARE(tracker.step(RestartKind::Restart, false, false), RestartAction::Wait);
        // A still-visible old HUD cannot be counted as the next round.
        QCOMPARE(tracker.step(RestartKind::None, true, true), RestartAction::Observe);
        QCOMPARE(tracker.step(RestartKind::Confirm, false, false), RestartAction::Click);
        QCOMPARE(tracker.step(RestartKind::None, false, false), RestartAction::Observe);
        QCOMPARE(tracker.step(RestartKind::None, true, false), RestartAction::Observe);
        QCOMPARE(tracker.step(RestartKind::None, true, true), RestartAction::Complete);
        RestartTracker resource;
        QCOMPARE(resource.step(RestartKind::Restart, false, false), RestartAction::Click);
        QCOMPARE(resource.step(RestartKind::Resource, false, false), RestartAction::Click);
        QCOMPARE(resource.step(RestartKind::None, true, true), RestartAction::Menu);
        RestartTracker missed;
        for (int attempt = 0; attempt < 3; ++attempt) {
            QCOMPARE(missed.step(RestartKind::Restart, false, false), RestartAction::Click);
            for (int i = 0; i < 4; ++i)
                QCOMPARE(missed.step(RestartKind::Restart, false, false), RestartAction::Wait);
        }
        QCOMPARE(missed.step(RestartKind::Restart, false, false), RestartAction::Fail);
    }
    void nestSelectionSkipsCompletedAndCachedRows() {
        const QVector<TextLine> lines{{"24/24", {1000, 200, 100, 30}, .99},
                                      {"0/8", {1000, 300, 100, 30}, .99},
                                      {"0/36", {1000, 450, 100, 30}, .99},
                                      {"0/48", {1000, 650, 100, 30}, .99}};
        const auto first = nextNestTarget(lines, {1920, 1080}, 0, {});
        QVERIFY(first);
        QCOMPARE(first->point, QPointF(1728, 423));
        const auto second = nextNestTarget(lines, {1920, 1080}, 0, {first->key});
        QVERIFY(second);
        QVERIFY(second->key != first->key);
        QVERIFY(!nextNestTarget(lines, {1920, 1080}, 0, {first->key, second->key}));
        const auto otherTab = nextNestTarget(lines, {1920, 1080}, 1, {first->key, second->key});
        QVERIFY(otherTab);
        QVERIFY(otherTab->key != first->key);
    }
    void gardenStyleRecommendationAndRarityOrder() {
        QVector<GardenCard> cards{{{"left", {100, 200, 80, 30}, 1}, 3, false, false},
                                  {{"middle", {500, 200, 80, 30}, 1}, 0, true, false},
                                  {{"right", {900, 200, 80, 30}, 1}, 2, false, false}};
        QCOMPARE(selectGardenCard(cards), 1); // Recommended gray beats unmarked gold.
        cards[2].recommended = true;
        QCOMPARE(selectGardenCard(cards), 2);
        cards[0].preferred = true;
        QCOMPARE(selectGardenCard(cards), 0);
        cards[1].preferred = true;
        QCOMPARE(selectGardenCard(cards), 0); // Style group: rarity first.
        cards[1].rarity = 3;
        QCOMPARE(selectGardenCard(cards), 1); // Same style and color: recommendation.
        QCOMPARE(preferredGardenStyle({{"Preferred Garden Style", "Exchange Dream"}}), QString("换取之梦"));
        QCOMPARE(normalizeGardenStyle(" 換取 之夢 "), QString("换取之梦"));
        for (const auto &text : {"6000/6000", "6,000 / 6,000", "本周游历值已满", "本週遊歷值 已達上限"})
            QVERIFY(gardenPointsComplete({{text, {}, 1}}));
        for (const auto &text : {"0/6000", "5900/6000", "6000", "最大日收益 已达到上限"})
            QVERIFY(!gardenPointsComplete({{text, {}, 1}}));
    }
    void gardenGrayRecommendationSurvivesMissingLabelAndResolution() {
        const auto root = QString::fromUtf8(OKWW_SOURCE_ROOT);
        Templates templates(root + "/assets/coco_annotations.json");
        QTemporaryDir directory;
        Profile profile(directory.path(), root + "/native/resources/catalog.json");
        auto badge = readImage(root + "/assets/dualpc/garden_recommended.png");
        cv::Mat gray;
        cv::cvtColor(badge(cv::Rect(6, 6, 35, 36)), gray, cv::COLOR_BGR2GRAY);
        cv::Mat three;
        cv::cvtColor(gray, three, cv::COLOR_GRAY2BGR);
        for (int width : {1280, 1920, 2560}) {
            Harness h;
            const double scale = width / 1920.;
            const int height = pixelRound(1080 * scale);
            Input input(h.config, {width, height}, false, h.transport, h.execution);
            cv::Mat source = cv::Mat::zeros(1080, 1920, CV_8UC3), frame;
            source(cv::Rect(510, 230, 110, 70)).setTo(cv::Scalar(10, 220, 245));
            three.copyTo(source(cv::Rect(1587, 163, three.cols, three.rows)));
            cv::resize(source, frame, {width, height});
            TaskContext context(
                h.execution, input, templates, profile, [&] { return frame; },
                [&](auto &, QRect area, double) -> QVector<TextLine> {
                    const auto line = [&](QString text, int x, int y, int w, int hh) {
                        return TextLine{text,
                                        {pixelRound(x * scale), pixelRound(y * scale), pixelRound(w * scale),
                                         pixelRound(hh * scale)},
                                        .99};
                    };
                    if (area.y() > height * .5)
                        return {line("跳过", 1080, 849, 70, 32), line("确认", 1440, 849, 75, 32)};
                    return {line("声骸", 620, 250, 60, 30), line("声骸", 1080, 250, 60, 30)};
                });
            context.refresh();
            Garden garden(context, root);
            const auto selected = garden.rewardChoice();
            QVERIFY(selected);
            QCOMPARE(selected->name, QString("garden_reward_choice_recommended_white"));
            QCOMPARE(selected->box.x(), pixelRound(width * .805));
            QVERIFY(h.events.empty());
        }
    }
    void gardenShopKeepsReserveAndDoesNotSkipUnknownRecommendedPrice() {
        const QVector<GardenItem> items{
            {0, 2, false, {}}, {1, std::nullopt, true, {}}, {2, 8, true, {}}, {3, 4, false, {}}};
        const auto ordered = affordableGardenItems(items, 15 - 10);
        QCOMPARE(ordered.size(), 3);
        QCOMPARE(ordered[0].index, 1);
        QCOMPARE(ordered[1].index, 0);
        QCOMPARE(ordered[2].index, 3);
        auto controls = gardenPurchaseControls({{"-4购买", {}, 1}});
        QVERIFY(controls.buy);
        QCOMPARE(*controls.price, 4);
        controls = gardenPurchaseControls({{"购买", {}, 1}, {"4", {}, 1}, {"8", {}, 1}});
        QVERIFY(!controls.price);
        controls = gardenPurchaseControls({{"已售罄", {}, 1}});
        QVERIFY(controls.sold);
    }
    void cubeBudgetPersistsAndBlocksUnverifiedAcrossReset() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/DailyStaminaUsage.json";
        const QDateTime before(QDate(2026, 11, 9), QTime(3, 59));
        CubeBudget budget(path, 99, before);
        QCOMPARE(budget.limit(), 4);
        QCOMPARE(budget.remaining(), 4);
        for (int i = 0; i < 4; ++i) {
            budget.reserve();
            QVERIFY(budget.pending());
            QVERIFY_EXCEPTION_THROWN(CubeBudget(path, 4, before), std::runtime_error);
            QVERIFY_EXCEPTION_THROWN(CubeBudget(path, 4, before.addSecs(120)), std::runtime_error);
            budget.verify();
            QCOMPARE(budget.used(), i + 1);
        }
        QVERIFY_EXCEPTION_THROWN(budget.reserve(), std::runtime_error);
        CubeBudget restarted(path, 4, before);
        QCOMPARE(restarted.remaining(), 0);
        CubeBudget nextDay(path, 4, before.addSecs(120));
        QCOMPARE(nextDay.remaining(), 4);
        CubeBudget another(path, 4, before.addSecs(120));
        nextDay.reserve();
        QVERIFY_EXCEPTION_THROWN(another.reserve(), std::runtime_error);
        writeJson(path, {{"day", "2025-01-01"}, {"reserved", 0}, {"verified", 0}, {"pending", "invalid"}});
        QVERIFY_EXCEPTION_THROWN(CubeBudget(path, 4, before), std::runtime_error);
    }
    void echoApproachUsesDeadbandAndDistance() {
        QCOMPARE(echoWalkDirection({}, {1920, 1080}, false), QString("w"));
        QCOMPARE(echoWalkDirection({{"echo", {900, 500, 120, 1}, .9}}, {1920, 1080}, false), QString("w"));
        QCOMPARE(echoWalkDirection({{"echo", {900, 800, 120, 1}, .9}}, {1920, 1080}, false), QString("s"));
        const QVector<Match> right{{"echo", {1020, 500, 80, 1}, .9}};
        QCOMPARE(echoWalkDirection(right, {1920, 1080}, false), QString("d"));
        QCOMPARE(echoWalkDirection(right, {1920, 1080}, true), QString("w"));
        QCOMPARE(echoWalkDirection({{"echo", {600, 500, 80, 1}, .9}}, {1920, 1080}, true), QString("a"));
    }
    void echoCycleStartWrapsWithoutOmittingEarlierDungeons() {
        const auto targets = echoRotationTargets({{"Cycle Scope", "All Challenges"},
                                                  {"Cycle Start Category", "Weekly Challenge"},
                                                  {"Cycle Start Index", 7}});
        QCOMPARE(targets.size(), 29);
        QCOMPARE(targets[0].category, QString("Weekly Challenge"));
        QCOMPARE(targets[0].index, 7);
        QCOMPARE(targets[3].category, QString("Boss Challenge"));
        QCOMPARE(targets[3].index, 1);
        QCOMPARE(targets.back().category, QString("Weekly Challenge"));
        QCOMPARE(targets.back().index, 6);
        QSet<QString> seen;
        for (const auto &item : targets)
            seen.insert(item.category + QString::number(item.index));
        QCOMPARE(seen.size(), 29);
        const auto single =
            echoRotationTargets({{"Cycle Scope", "Boss Challenge"}, {"Cycle Start Index", 20}});
        QCOMPARE(single.size(), 20);
        QCOMPARE(single.front().index, 20);
        QCOMPARE(single[1].index, 1);
        QVERIFY_EXCEPTION_THROWN(
            echoRotationTargets({{"Cycle Scope", "Weekly Challenge"}, {"Cycle Start Index", 10}}),
            std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(echoRotationTargets({{"Cycle Start Index", 1.5}}), std::runtime_error);
    }
    void combatMissingTargetNeverCountsAsCompleted() {
        const auto root = QString::fromUtf8(OKWW_SOURCE_ROOT);
        Templates templates(root + "/assets/coco_annotations.json");
        QTemporaryDir directory;
        Profile profile(directory.path(), root + "/native/resources/catalog.json");
        Harness h;
        Input input(h.config, {1920, 1080}, false, h.transport, h.execution);
        cv::Mat frame = cv::Mat::zeros(1080, 1920, CV_8UC3);
        TaskContext context(
            h.execution, input, templates, profile, [&] { return frame; },
            [](auto &, auto, double) { return QVector<TextLine>{}; });
        context.task = "FarmEchoTask";
        context.refresh();
        CombatSession session(context, root, directory.path());
        QVERIFY(!session.combatOnce({.waitSeconds = .1, .required = false}));
        QVERIFY_EXCEPTION_THROWN(session.combatOnce({.waitSeconds = .1}), NavigationFailure);
        QVERIFY(!context.state->inCombat);
        QVERIFY(!session.skipCombatCheck);
        for (const auto &event : h.events)
            QCOMPARE(event["op"].toString(), QString("release"));
    }
    void combatDeathRecoveryReportsRetryAndOnlyClosesPopup() {
        const auto root = QString::fromUtf8(OKWW_SOURCE_ROOT);
        Templates templates(root + "/assets/coco_annotations.json");
        QTemporaryDir directory;
        Profile profile(directory.path(), root + "/native/resources/catalog.json");
        Harness h;
        bool dialog = true;
        int recoveries = 0;
        Input input(
            h.config, {1920, 1080}, true,
            [&](QJsonObject event) {
                h.events.append(event);
                if (event["op"] == "key" && event["code"].toInt() == Input::keyCode("esc") &&
                    !event["down"].toBool())
                    dialog = false;
                return QJsonObject{};
            },
            h.execution);
        cv::Mat frame = cv::Mat::zeros(1080, 1920, CV_8UC3);
        TaskContext context(
            h.execution, input, templates, profile, [&] { return frame; },
            [&](auto &, QRect area, double) -> QVector<TextLine> {
                if (!dialog)
                    return {};
                return {{area.y() < 500 ? "选择复苏物品" : "复苏", area, .99}};
            });
        context.refresh();
        CombatSession session(context, root, directory.path());
        CombatOptions options;
        options.recover = [&] { ++recoveries; };
        QVERIFY_EXCEPTION_THROWN(session.combatOnce(options), CharacterRevived);
        QCOMPARE(recoveries, 1);
        QVERIFY(!dialog);
        QVERIFY(!context.state->inCombat);
        QVERIFY(!session.skipCombatCheck);
        for (const auto &event : h.events) {
            QVERIFY(event["op"] != "button");
            if (event["op"] == "key")
                QCOMPARE(event["code"].toInt(), Input::keyCode("esc"));
        }
        h.gate.stop();
        QVERIFY_EXCEPTION_THROWN(session.combatOnce(options), Cancelled);
        QCOMPARE(recoveries, 1);
    }
    void walkingCancellationAndErrorsReleaseRunAndDirection() {
        const auto root = QString::fromUtf8(OKWW_SOURCE_ROOT);
        Templates templates(root + "/assets/coco_annotations.json");
        QTemporaryDir directory;
        Profile profile(directory.path(), root + "/native/resources/catalog.json");
        for (int scenario = 0; scenario < 3; ++scenario) {
            Harness h;
            Input input(h.config, {1920, 1080}, false, h.transport, h.execution);
            cv::Mat frame = cv::Mat::zeros(1080, 1920, CV_8UC3);
            TaskContext context(
                h.execution, input, templates, profile, [&] { return frame; },
                [](auto &, auto, double) { return QVector<TextLine>{}; });
            WorldInteraction movement(context);
            const auto condition = [&] {
                if (scenario == 1)
                    throw NavigationFailure("fixture");
                if (scenario == 2 && h.clock.now() > .25)
                    h.gate.stop();
                return false;
            };
            if (scenario == 0)
                QVERIFY(!movement.walkUntil(condition, "w", .5, true));
            else
                QVERIFY_EXCEPTION_THROWN(movement.walkUntil(condition, "w", 1, true), std::runtime_error);
            QSet<QString> held;
            for (const auto &event : h.events) {
                if (event["op"] == "release")
                    held.clear();
                if (event["op"] == "key" || event["op"] == "button") {
                    const auto key = event["op"].toString() + (event["op"] == "key"
                                                                   ? QString::number(event["code"].toInt())
                                                                   : event["name"].toString());
                    if (event["down"].toBool())
                        held.insert(key);
                    else
                        held.remove(key);
                }
            }
            QVERIFY(held.isEmpty());
        }
    }
    void cubeUseVerifiesBothCountersAndNeverRetriesMaterialClick() {
        const auto root = QString::fromUtf8(OKWW_SOURCE_ROOT);
        Templates templates(root + "/assets/coco_annotations.json");
        for (int scenario = 0; scenario < 3; ++scenario) {
            QTemporaryDir directory;
            Profile profile(directory.path(), root + "/native/resources/catalog.json");
            Harness h;
            QPointF mouse;
            bool dialog = false;
            int current = 0, stock = 7, confirmations = 0;
            Input input(
                h.config, {1920, 1080}, true,
                [&](QJsonObject event) {
                    h.events.append(event);
                    if (event["op"] == "absolute")
                        mouse = {event["x"].toDouble(), event["y"].toDouble()};
                    if (event["op"] == "button" && !event["down"].toBool()) {
                        if (mouse.y() < 100)
                            dialog = true;
                        else if (mouse.x() > 1100 && mouse.y() > 700) {
                            ++confirmations;
                            dialog = false;
                            if (scenario != 2) {
                                current += 60;
                                --stock;
                            }
                        } else if (mouse.x() < 800 && mouse.y() > 700)
                            dialog = false;
                    }
                    return QJsonObject{};
                },
                h.execution);
            cv::Mat frame = cv::Mat::zeros(1080, 1920, CV_8UC3);
            TaskContext context(
                h.execution, input, templates, profile, [&] { return frame; },
                [&](const cv::Mat &image, QRect area, double) -> QVector<TextLine> {
                    if (image.rows < 500)
                        return {{QString::number(stock), {0, 0, 30, 30}, .99}};
                    if (area.y() == 0)
                        return {{QString("%1/240").arg(current), {1000, 20, 100, 30}, .99},
                                {"25", {900, 20, 50, 30}, .99}};
                    if (!dialog)
                        return {};
                    if (area.y() < 250)
                        return {{"补充结晶波片", area, .99}};
                    if (area.y() < 650)
                        return {{"消耗1结晶溶剂补充60结晶波片", area, .99}};
                    return {{area.x() > 900 ? "确认" : "取消", area, .99}};
                });
            context.refresh();
            CubeBudget budget(directory.path() + "/DailyStaminaUsage.json", 4);
            if (scenario == 2) {
                QVERIFY_EXCEPTION_THROWN(useOneDailyCube(context, budget), std::runtime_error);
                QVERIFY(budget.pending());
                QCOMPARE(confirmations, 1);
                QCOMPARE(budget.used(), 1);
            } else {
                QVERIFY(useOneDailyCube(context, budget, scenario == 1));
                QCOMPARE(confirmations, scenario);
                QCOMPARE(budget.used(), scenario);
                QVERIFY(!budget.pending());
                QCOMPARE(current, scenario * 60);
                QCOMPARE(stock, 7 - scenario);
            }
        }
    }
    void concertoObservationDoesNotCacheUntilExplicitRead() {
        test::RotationReplay host({{"ready", QJsonArray{false, false, false}}, {"con", 1}});
        CharacterState state;
        state.current = true;
        Character actor(host, state);
        host.character = &actor;
        QVERIFY(actor.isConFull());
        QCOMPARE(actor.state.currentCon, 0.);
        host.observe({{"con", .4}});
        QVERIFY(!actor.isConFull());
        QCOMPARE(actor.currentCon(), .4);
        host.observe({{"con", 1}});
        QCOMPARE(actor.currentCon(), 1.);
        host.observe({{"con", .4}});
        QVERIFY(actor.isConFull());
    }
    void allRotationsReleaseOnCancellation() {
        for (const auto &name : nativeCharacterClasses())
            for (double at : {100.05, 100.2, 100.6, 101.1, 102.7}) {
                test::RotationReplay host({{"ready", QJsonArray{true, true, true}},
                                           {"forte", true},
                                           {"con", .99},
                                           {"intro", true},
                                           {"cancel_at", at},
                                           {"con_at", 104.}});
                CharacterState state;
                state.current = state.hasIntro = true;
                state.index = 0;
                auto actor = makeCharacter(name, host, state);
                host.character = actor.get();
                try {
                    actor->perform();
                } catch (const Cancelled &) {
                } catch (const CombatEnded &) {
                }
                const auto context = name + " @ " + QString::number(at);
                QVERIFY2(!host.held, qPrintable(context));
                QVERIFY2(host.heldKeys.isEmpty(), qPrintable(context));
                QVERIFY2(!host.inLiberation, qPrintable(context));
            }
    }
};
QTEST_APPLESS_MAIN(RuntimeTests)
#include "test_runtime.moc"
