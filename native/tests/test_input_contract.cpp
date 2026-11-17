#include "reference_input_control.hpp"
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
namespace {
struct TestClock final : Clock {
    double time = 0;
    std::function<void()> tick;
    double now() const override { return time; }
    void sleep(double seconds) override {
        time += seconds;
        if (tick)
            tick();
    }
};
template <class T> QJsonObject replay(int operation, int scene) {
    Config config = Config::defaults();
    config.set("kmbox.protocol", scene % 2 ? "sendinput" : "kmbox_net");
    config.set("kmbox.move_chunk", QJsonArray{1, 17, 127, 700, 0, -1, 100.5}[scene % 7]);
    config.set("kmbox.move_interval", QJsonArray{0, .002, .011}[scene % 3]);
    config.set("kmbox.home_counts", 2300.75);
    config.set("kmbox.home_wait", .007);
    config.set("kmbox.position_settle", .12);
    config.set("kmbox.counts_per_pixel_x", scene % 4 ? .9 : 1.25);
    config.set("kmbox.counts_per_pixel_y", 1.1);
    config.set("kmbox.desktop_width", 2560);
    config.set("kmbox.desktop_height", 1440);
    config.set("capture.output_width", 1920);
    config.set("capture.output_height", 1080);
    if (scene % 11 == 0)
        config.set("capture.crop", QJsonArray{20, 10, 1800, 1000});
    if (scene % 29 == 0)
        config.set("capture.crop", QJsonArray{20, 10, -1800, 1000});
    if (scene % 31 == 0)
        config.set("capture.crop", QJsonArray{20, 10, 1000});
    TestClock clock;
    ExecutionGate gate;
    Execution execution(gate, gate.start(), clock);
    QJsonArray events, notices, replies;
    int attempts = 0;
    bool injecting = false;
    InputTransport transport = [&](QJsonObject event) {
        event["at"] = clock.time;
        events.append(event);
        if (injecting && event["op"] != "release") {
            ++attempts;
            if (scene % 17 == 0 && attempts == 2)
                fail("transport-failed");
            if (scene % 13 == 0 && clock.time < .23)
                throw LocalInputBlocked("blocked", scene % 2 ? LocalInputBlocked::Recovery::DismissRunDialog
                                                             : LocalInputBlocked::Recovery::Wait);
        }
        return QJsonObject{{"reply", event["op"]}, {"attempt", attempts}};
    };
    QJsonObject outcome;
    {
        T input(config, {1920, 1080}, scene % 3 == 0, transport, execution);
        if (scene % 5 == 0)
            input.keyDown("w");
        if (scene % 7 == 0)
            input.workshopCommand({{"op", "button"}, {"name", "left"}, {"down", true}});
        if (scene % 19 == 0)
            input.workshopCommand({{"op", "move"}, {"x", 12}, {"y", -9}});
        input.recovery = [&](QString text) {
            notices.append(text);
            if (scene % 47 == 0)
                fail("recovery-notice-failed");
        };
        clock.tick = [&] {
            if (scene % 23 == 0 && clock.time > .13)
                gate.stop();
        };
        injecting = true;
        const QPointF point(scene % 37 == 0 ? -1 : 127.5 + (scene % 4) * .25,
                            scene % 41 == 0 ? 1080 : 238.25);
        try {
            switch (operation) {
            case 0:
                input.keyDown(scene % 9 ? "R" : "???");
                break;
            case 1:
                input.keyUp(scene % 9 ? "w" : "???");
                break;
            case 2:
                input.key(scene % 9 ? "w" : "???", scene % 4 ? .031 : -1);
                break;
            case 3:
                input.buttonDown(scene % 9 ? "left" : "unknown");
                break;
            case 4:
                input.buttonUp(scene % 9 ? "left" : "unknown");
                break;
            case 5:
                input.move(point);
                break;
            case 6:
                input.relative((scene % 6 - 3) * 127.5, 93.25);
                break;
            case 7:
                input.click(scene % 2 ? std::optional<QPointF>(point) : std::nullopt, scene % 3 ? .025 : -1,
                            scene % 9 ? "left" : "unknown");
                break;
            case 8:
                input.scroll(scene % 8 - 4, scene % 2 ? std::optional<QPointF>(point) : std::nullopt);
                break;
            case 9:
                input.swipe(point, {328.5, 441.25}, scene % 9 ? -1 : 30, .04);
                break;
            case 10:
                input.text(
                    QStringList{"aA! b\n\t", "??", QStringLiteral("中文"), "", "_+{}|:\"~<>?"}[scene % 5]);
                break;
            case 11: {
                const QStringList ops{"state", "connect",  "heartbeat", "probe",   "key",    "button",
                                      "move",  "absolute", "wheel",     "release", "invalid"};
                replies.append(input.workshopCommand({{"op", ops[scene % ops.size()]},
                                                      {"code", 4},
                                                      {"down", scene % 2 == 0},
                                                      {"name", "left"},
                                                      {"x", 21},
                                                      {"y", -5}}));
                break;
            }
            case 12: {
                const auto p = input.screenPoint(point);
                outcome["point"] = QJsonArray{p.x(), p.y()};
                break;
            }
            case 13:
                input.keyDown("shift");
                input.text("Aa!");
                input.keyUp("shift");
                break;
            case 14:
                input.move(point);
                input.buttonDown();
                input.move({350, 400});
                input.buttonUp();
                break;
            case 15:
                input.release();
                break;
            }
        } catch (const std::exception &error) {
            outcome["error"] = errorText(error);
            outcome["type"] = typeid(error).name();
            if (const auto failure = dynamic_cast<const ExecutionFailure *>(&error))
                outcome["details"] = failure->details;
        }
        outcome["idle"] = input.idle();
    }
    outcome["events"] = events;
    outcome["notices"] = notices;
    outcome["replies"] = replies;
    outcome["time"] = clock.time;
    return outcome;
}
} // namespace
class InputContracts final : public QObject {
    Q_OBJECT
  private slots:
    void sequences_data() {
        QTest::addColumn<int>("operation");
        QTest::addColumn<int>("scene");
        for (int operation = 0; operation < 16; ++operation)
            for (int scene = 0; scene < 256; ++scene)
                QTest::newRow(qPrintable(QString("%1-%2").arg(operation).arg(scene))) << operation << scene;
    }
    void sequences() {
        QFETCH(int, operation);
        QFETCH(int, scene);
        const auto expected = replay<input_reference::OriginalInput>(operation, scene);
        const auto actual = replay<Input>(operation, scene);
        QCOMPARE(actual, expected);
    }
};
QTEST_GUILESS_MAIN(InputContracts)
#include "test_input_contract.moc"
