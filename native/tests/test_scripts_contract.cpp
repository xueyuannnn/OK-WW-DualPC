#include "scripts_receive_policy_replay.hpp"
#include <QCryptographicHash>
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
namespace okww::runtime {
struct ScriptContractAccess {
    template <class T> static QJsonObject rpc(T &runtime, QJsonObject request) {
        return runtime.rpc(request);
    }
    template <class T> static void mapping(T &runtime) {
        runtime.mapping_ =
            CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 64 * 1024 * 1024, nullptr);
        if (!runtime.mapping_)
            fail("Fixture mapping failed");
        runtime.mapped_ = MapViewOfFile(runtime.mapping_, FILE_MAP_ALL_ACCESS, 0, 0, 64 * 1024 * 1024);
        if (!runtime.mapped_)
            fail("Fixture map failed");
    }
    template <class T> static QByteArray pixels(T &runtime, int size) {
        return {static_cast<const char *>(runtime.mapped_), size};
    }
};
} // namespace okww::runtime
namespace {
struct ScriptClock final : Clock {
    double time = 0;
    double now() const override { return time; }
    void sleep(double seconds) override { time += seconds; }
};
template <class T> QJsonObject rpcReplay(const QString &directory, int scene) {
    auto config = Config::defaults();
    config.set("capture.output_width", 1280);
    config.set("capture.output_height", 720);
    config.set("kmbox.position_settle", .12);
    ScriptClock clock;
    ExecutionGate gate;
    QJsonArray trace;
    int checkpoints = 0;
    Execution execution(gate, gate.start(), clock, [&] {
        if (scene % 47 == 0 && ++checkpoints == 3)
            throw Cancelled();
    });
    Input input(
        config, {1280, 720}, true,
        [&](QJsonObject event) {
            event["at"] = clock.time;
            trace.append(event);
            QJsonObject state{{"keys", QJsonArray{1, 2}}, {"buttons", scene % 3}};
            return scene % 2 ? QJsonObject{{"box", state}} : state;
        },
        execution);
    Templates templates(directory + "/coco.json");
    Profile profile(directory + "/configs", directory + "/catalog.json");
    cv::Mat image(74, 130, scene % 37 == 0 ? CV_8UC4 : CV_8UC3, cv::Scalar(13, 27, 41, 55));
    TaskContext context(
        execution, input, templates, profile,
        [&] {
            if (scene % 31 == 0)
                return cv::Mat{};
            return scene % 2 ? image(cv::Rect(1, 1, 128, 72)) : image;
        },
        [](const cv::Mat &, QRect, double) { return QVector<TextLine>{}; });
    context.task = "outer-task";
    context.taskOverrides = {{"original", 7}};
    context.frameVersion = UINT64_C(9007199254740993);
    if (scene % 3)
        context.info = [&](QString key, QString value) {
            trace.append(QJsonObject{{"info", key}, {"value", value}});
        };
    if (scene % 5)
        context.progress = [&](QJsonObject event) { trace.append(QJsonObject{{"progress", event}}); };
    QJsonObject result;
    {
        T runtime(context, directory, directory);
        ScriptContractAccess::mapping(runtime);
        if (scene % 7)
            runtime.builtin = [&](QString task) {
                trace.append(QJsonObject{
                    {"builtin", task}, {"task", context.task}, {"overrides", context.taskOverrides}});
                if (scene % 11 == 0)
                    fail("builtin-failed");
                return scene % 2 != 0;
            };
        const QStringList actions{"move", "click",  "mouse_down", "mouse_up", "key_down", "key_up",
                                  "key",  "scroll", "swipe",      "text",     "unknown"};
        for (int op = 0; op < 17; ++op) {
            QJsonObject request;
            if (op == 0)
                request = {{"op", "frame"}};
            else if (op == 1)
                request = {{"op", "script_progress"},
                           {"current", scene % 5 - 1},
                           {"total", 9},
                           {"unit", "步"},
                           {"detail", "详情"}};
            else if (op == 2)
                request = {{"op", "script_info"}, {"key", "键"}, {"value", "值"}};
            else if (op <= 13)
                request = {{"op", "script_input"},
                           {"operation", actions[op - 3]},
                           {"key", op == 12 ? "text" : "left"},
                           {"text", "Hello"},
                           {"to_x", .7},
                           {"to_y", .8},
                           {"steps", scene % 7 - 3},
                           {"settle", .002}};
            else if (op == 14)
                request = {{"op", "script_builtin"}, {"task", "DailyTask2"}};
            else if (op == 15)
                request = {{"op", "state"}};
            else
                request = {{"op", "unknown-command"}};
            if (op >= 3 && op <= 13) {
                request["key"] = op >= 7 && op <= 9 ? (scene % 9 ? "w" : "invalid") : "left";
                if (scene % 3) {
                    request["x"] = scene % 7 ? .23 : -1;
                    request["y"] = .45;
                }
                if (scene % 4)
                    request["seconds"] = .003;
            }
            QJsonObject item{{"op", op}};
            try {
                const auto reply = ScriptContractAccess::rpc(runtime, request);
                item["reply"] = reply;
                if (op == 0 && reply["available"].toBool())
                    item["pixels"] = QString::fromLatin1(
                        QCryptographicHash::hash(ScriptContractAccess::pixels(runtime, reply["size"].toInt()),
                                                 QCryptographicHash::Sha256)
                            .toHex());
            } catch (const Cancelled &e) {
                item["cancelled"] = true;
                item["error"] = errorText(e);
            } catch (const std::exception &e) {
                item["error"] = errorText(e);
            }
            trace.append(item);
            trace.append(QJsonObject{{"task", context.task}, {"overrides", context.taskOverrides}});
        }
    }
    result["trace"] = trace;
    result["seconds"] = clock.time;
    result["idle"] = input.idle();
    return result;
}
QString recordingOutcome(const QJsonArray &events, const QString &name, bool original) {
    try {
        return original ? script_reference::recordedScript(events, name) : recordedScript(events, name);
    } catch (const std::exception &e) {
        return "ERROR:" + errorText(e);
    }
}
} // namespace
class ScriptContracts final : public QObject {
    Q_OBJECT
  private slots:
    void recording_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 2048; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void recording() {
        QFETCH(int, scene);
        const QStringList names{"RecordedTask", "_X1", "bad-name", "中文",
                                "A\n",          "",    "2Invalid", QString(QChar(char16_t(0xd800)))};
        const QStringList ops{"key_down", "key_up", "mouse_down", "mouse_up", "move", "wheel", "invalid"};
        const QStringList keys{"left",
                               "a",
                               "ctrl",
                               "\"\\\n\t",
                               QString(QChar(char16_t(0xd800))),
                               "中文",
                               QString(QChar(char16_t(0)))};
        QJsonArray events;
        double time = 0;
        for (int i = 0; i < scene % 19; ++i) {
            time += QJsonArray{0, .001, .001001, .123456, -.1}[((scene / 8) + i) % 5].toDouble();
            events.append(QJsonObject{{"time", time},
                                      {"op", ops[((scene / 8) + i) % ops.size()]},
                                      {"key", keys[(scene + i) % keys.size()]},
                                      {"x", scene * .000017 - 1},
                                      {"y", i * .14159},
                                      {"steps", scene % 17 - 8}});
        }
        const auto name = names[scene % names.size()];
        QCOMPARE(recordingOutcome(events, name, false), recordingOutcome(events, name, true));
    }
    void rpc_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 256; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void rpc() {
        QFETCH(int, scene);
        QTemporaryDir directory;
        writeJson(directory.filePath("coco.json"),
                  {{"images", QJsonArray{}}, {"annotations", QJsonArray{}}, {"categories", QJsonArray{}}});
        writeJson(directory.filePath("catalog.json"), {{"tasks", QJsonArray{}}, {"globals", QJsonArray{}}});
        QCOMPARE(rpcReplay<ScriptRuntime>(directory.path(), scene),
                 rpcReplay<script_reference::OriginalScriptRuntime>(directory.path(), scene));
    }
    void receive_data() { rpc_data(); }
    void receive() {
        QFETCH(int, scene);
        script_test::ReceiveReplay current(scene), original(scene);
        QCOMPARE(current.run(true), original.run(false));
    }
};
QTEST_GUILESS_MAIN(ScriptContracts)
#include "test_scripts_contract.moc"
