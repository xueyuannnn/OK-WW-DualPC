#include "reference_instance.hpp"
#include <QtTest>
#include <opencv2/imgproc.hpp>
using namespace okww;
using namespace okww::runtime;
namespace {
QJsonArray rectangle(QRect b) { return {b.x(), b.y(), b.width(), b.height()}; }
QJsonValue selected(const std::optional<TextLine> &line) {
    if (!line)
        return {};
    return QJsonObject{{"text", line->text}, {"box", rectangle(line->box)}, {"score", line->confidence}};
}
struct ClockProbe final : Clock {
    double time = 0;
    std::function<void()> tick;
    double now() const override { return time; }
    void sleep(double value) override {
        time += value;
        if (tick)
            tick();
    }
};
QJsonObject replay(int scene, bool rust) {
    const auto root = QString(OKWW_SOURCE_ROOT);
    QTemporaryDir directory;
    Templates templates(root + "/assets/coco_annotations.json");
    Profile profile(directory.path(), root + "/native/resources/catalog.json");
    profile.set("Monthly Card Config", "Check Monthly Card", false);
    ClockProbe clock;
    ExecutionGate gate;
    Execution execution(gate, gate.start(), clock);
    clock.tick = [&] {
        if (clock.time > 4)
            gate.stop();
    };
    int stage = 0, frames = 0, reads = 0;
    QJsonArray events;
    Input input(
        Config::defaults(), {640, 360}, true,
        [&](QJsonObject value) {
            value["time"] = clock.time;
            events.append(value);
            if ((value["op"] == "key" || value["op"] == "button") && !value["down"].toBool())
                ++stage;
            return QJsonObject{};
        },
        execution);
    cv::Mat world(360, 640, CV_8UC3, cv::Scalar(17, 23, 31)), realm = world.clone(), blank = world.clone();
    const auto paint = [&](cv::Mat &frame, const QString &name) {
        auto image = templates.pattern(name, {640, 360});
        if (image.channels() == 1)
            cv::cvtColor(image, image, cv::COLOR_GRAY2BGR);
        auto box = templates.box(name, {640, 360});
        image.copyTo(frame(cv::Rect(box.x(), box.y(), box.width(), box.height())));
    };
    paint(world, "char_2_text");
    paint(world, "char_3_text");
    paint(world, "world_earth_icon");
    paint(realm, "char_2_text");
    paint(realm, "char_3_text");
    paint(realm, "illusive_realm_exit");
    if (scene & 8)
        paint(realm, "pick_up_f_hcenter_vcenter");
    if (scene & 16)
        paint(realm, "confirm_btn_hcenter_vcenter");
    TaskContext context(
        execution, input, templates, profile,
        [&] {
            ++frames;
            if (frames > 24)
                gate.stop();
            if (scene & 1)
                return world;
            if (stage > 0 && !(scene & 2))
                return world;
            if ((scene & 4) && frames % 4 < 2)
                return blank;
            return realm;
        },
        [&](const cv::Mat &, QRect area, double threshold) {
            ++reads;
            events.append(QJsonObject{
                {"read", reads}, {"area", rectangle(area)}, {"threshold", threshold}, {"time", clock.time}});
            if (scene % 31 == 0)
                throw std::runtime_error("OCR unavailable");
            QVector<TextLine> lines;
            const QStringList texts{"确认离开副本",     "确认",     "重新挑战",  "取消", "是否重新挑战",
                                    "领取奖励是否确认", "离开副本", "梦魇已净化"};
            const int kind = (scene / 2) % 8;
            lines.append({texts[kind], {400, 240, 80, 20}, .9});
            if (scene & 8)
                lines.append({"确认", {480, 280, 40, 20}, .9});
            if (scene & 16)
                lines.append({"重新挑战", {100, 280, 80, 20}, .9});
            return lines;
        });
    context.frame = scene & 1 ? world : realm;
    context.frameVersion = 1;
    context.task = "NightmareNestTask";
    context.info = [&](QString key, QString value) {
        events.append(QJsonObject{{"info", key}, {"value", value}, {"time", clock.time}});
    };
    QJsonObject result;
    try {
        switch (scene / 32) {
        case 0:
            result["completed"] =
                rust ? completedNest(context) : reference_instance::frozen_completedNest(context);
            break;
        case 1:
            if (rust)
                leaveNest(context, (scene & 1) != 0);
            else
                reference_instance::frozen_leaveNest(context, (scene & 1) != 0);
            break;
        case 2:
            if (rust)
                restartBossDomain(context);
            else
                reference_instance::frozen_restartBossDomain(context);
            break;
        }
        result["error"] = "";
    } catch (const std::exception &e) {
        result["error"] = QString::fromUtf8(e.what());
    }
    result["events"] = events;
    result["frames"] = frames;
    result["time"] = clock.time;
    result["stage"] = stage;
    return result;
}
} // namespace
class InstanceContracts : public QObject {
    Q_OBJECT
  private slots:
    void text_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 2048; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void text() {
        QFETCH(int, scene);
        const QStringList values{"离开副本是否确认",
                                 "确认",
                                 "取消",
                                 "重新挑战",
                                 "是否重新挑战",
                                 "waveplate claim reward confirm",
                                 "F： 离开副本",
                                 "梦魇已净化",
                                 "Confirm",
                                 "Leave Instance",
                                 "挑战已经完成",
                                 "结晶波片",
                                 "购买",
                                 "Challenge Again",
                                 "Retry Challenge",
                                 "确定",
                                 "Nightmare purified",
                                 "revive",
                                 "Return",
                                 "F:exit domain",
                                 "Leave Domain Progress"};
        cv::RNG random(999 + scene);
        QVector<TextLine> lines;
        for (int i = 0; i < scene % 7; ++i) {
            auto text = values[random.uniform(0, int(values.size()))];
            if (scene & 32)
                text = " " + text + " ";
            lines.append({text,
                          {random.uniform(-10, 1500), random.uniform(0, 800), random.uniform(0, 100),
                           random.uniform(1, 30)},
                          .9});
            QCOMPARE(isNestExitText(text), reference_instance::frozen_isNestExitText(text));
            QCOMPARE(isNestCompleteText(text), reference_instance::frozen_isNestCompleteText(text));
        }
        QCOMPARE(nestExitContext(lines), reference_instance::frozen_nestExitContext(lines));
        QCOMPARE(selected(nestExitConfirmation(lines, scene % 2 ? 1920 : 1280)),
                 selected(reference_instance::frozen_nestExitConfirmation(lines, scene % 2 ? 1920 : 1280)));
        const auto actual = restartDialog(lines);
        const auto expected = reference_instance::frozen_restartDialog(lines);
        QCOMPARE(int(actual.kind), int(expected.kind));
        QCOMPARE(selected(actual.button), selected(expected.button));
    }
    void flow_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 96; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void flow() {
        QFETCH(int, scene);
        const auto expected = replay(scene, false), actual = replay(scene, true);
        QVERIFY2(actual == expected,
                 qPrintable(
                     QString::fromUtf8(QJsonDocument(QJsonObject{{"expected", expected}, {"actual", actual}})
                                           .toJson(QJsonDocument::Compact))));
    }
};
QTEST_GUILESS_MAIN(InstanceContracts)
#include "test_instance_contract.moc"
