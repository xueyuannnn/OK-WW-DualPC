#include "daily_actions_replay.hpp"
#include "daily_stage_replay.hpp"
#include "reference_daily.hpp"
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
namespace {
QString firstDifference(const QJsonValue &a, const QJsonValue &b, QString path = {}) {
    if (a == b)
        return {};
    if (a.isObject() && b.isObject()) {
        const auto x = a.toObject(), y = b.toObject();
        auto keys = x.keys();
        for (const auto &key : y.keys())
            if (!keys.contains(key))
                keys.append(key);
        for (const auto &key : keys) {
            auto diff = firstDifference(x.value(key), y.value(key), path + "/" + key);
            if (!diff.isEmpty())
                return diff;
        }
    }
    if (a.isArray() && b.isArray()) {
        const auto x = a.toArray(), y = b.toArray();
        if (x.size() == y.size())
            for (int i = 0; i < x.size(); ++i) {
                auto diff = firstDifference(x[i], y[i], path + "/" + QString::number(i));
                if (!diff.isEmpty())
                    return diff;
            }
    }
    return path + ": " + QString::fromUtf8(QJsonDocument(QJsonArray{a, b}).toJson(QJsonDocument::Compact));
}
template <class Progress> QJsonObject progress(const Progress &p) {
    return {{"stamina", p.stamina ? QJsonValue(*p.stamina) : QJsonValue()},
            {"points", p.points},
            {"echo", p.echoNeeded ? QJsonValue(*p.echoNeeded) : QJsonValue()},
            {"ready", p.ready()}};
}
struct ClockProbe : Clock {
    double now() const override { return 0; }
    void sleep(double) override {}
};
QJsonObject observation(int scene, bool rust) {
    const auto root = QString(OKWW_SOURCE_ROOT);
    QTemporaryDir dir;
    Templates templates(root + "/assets/coco_annotations.json");
    Profile profile(dir.path(), root + "/native/resources/catalog.json");
    ClockProbe clock;
    ExecutionGate gate;
    Execution execution(gate, gate.start(), clock);
    Input input(Config::defaults(), {640, 360}, true, [](QJsonObject) { return QJsonObject{}; }, execution);
    cv::Mat image(360, 640, CV_8UC3);
    QJsonArray reads;
    const auto word = [&](QString value, double x, double y, double w = .05) {
        return TextLine{value, gameBox(image, x, y, x + w, y + .03), .9};
    };
    TaskContext context(
        execution, input, templates, profile, [&] { return image; },
        [&](const cv::Mat &, QRect area, double threshold) {
            reads.append(QJsonObject{{"area", QJsonArray{area.x(), area.y(), area.width(), area.height()}},
                                     {"threshold", threshold}});
            if (scene % 31 == 0)
                throw std::runtime_error("reader unavailable");
            if (area.y() < 40)
                return QVector<TextLine>{word(scene & 1 ? "活跃度" : "游历值", .15, .12),
                                         word("周度游历", .4, .12)};
            if (area.y() > 270) {
                QVector<TextLine> lines{word(scene & 2 ? "活跃度" : "游历值", .195, .91, .12),
                                        word(scene & 4 ? "100" : "0", .2, .85), word("100", .8, .85)};
                if (scene & 8)
                    lines.append(word("80", .24, .85));
                return lines;
            }
            return QVector<TextLine>{word("获得1个声骸0/1", .2, .4, .2),
                                     word(scene & 16 ? "180/180" : "0/180", .2, .5, .2)};
        });
    context.frame = image;
    QJsonObject detail{{"old", true}}, out;
    try {
        if (rust)
            out["progress"] = progress(observeDailyProgress(context, scene & 32 ? nullptr : &detail));
        else
            out["progress"] = progress(
                reference_daily::reference_observeDailyProgress(context, scene & 32 ? nullptr : &detail));
        out["error"] = "";
    } catch (const std::exception &error) {
        out["error"] = errorText(error);
    }
    out["detail"] = detail;
    out["reads"] = reads;
    return out;
}
} // namespace
class DailyContracts : public QObject {
    Q_OBJECT
  private slots:
    void progress_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 4096; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void progress() {
        QFETCH(int, scene);
        const QStringList words{"获得1个声骸", "Obtain Echo 0/1", "1/1",      "0/1",
                                "已完成",      "Claimed",         "0/180",    "180/180",
                                "181/180",     "99999999999/180", "120／180", "not an objective"};
        const QStringList values{"0", "100", "80", "080", "100/100", "999", "1000", "x", ""};
        cv::RNG random(4567 + scene);
        QVector<TextLine> objectives, points;
        for (int i = 0; i < scene % 7; ++i)
            objectives.append({words[random.uniform(0, int(words.size()))],
                               {100, random.uniform(0, 5) * 20, 100, random.uniform(5, 25)},
                               .9});
        for (int i = 0; i < (scene / 7) % 5; ++i)
            points.append({values[random.uniform(0, int(values.size()))], {100, 300, 30, 12}, .9});
        QCOMPARE(::progress(dailyProgress(objectives, points)),
                 ::progress(reference_daily::reference_dailyProgress(objectives, points)));
        QJsonObject config{
            {"Run Tacet or Forgery First", scene & 1 ? "Forgery First" : "Tacet First"},
            {"Which to Farm",
             QStringList{"Tacet Suppression", "Forgery Challenge", "Simulation Challenge", "other"}.at(
                 (scene / 2) % 4)}};
        QCOMPARE(int(dailyDomain(config)), int(reference_daily::reference_dailyDomain(config)));
    }
    void observe_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 128; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void observe() {
        QFETCH(int, scene);
        QCOMPARE(observation(scene, true), observation(scene, false));
    }
    void stage_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 2048; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void stage() {
        QFETCH(int, scene);
        const auto expected = daily_stage_test::trace(scene, false),
                   actual = daily_stage_test::trace(scene, true);
        QVERIFY2(actual == expected,
                 qPrintable(
                     QString::fromUtf8(QJsonDocument(QJsonObject{{"expected", expected}, {"actual", actual}})
                                           .toJson(QJsonDocument::Compact))));
    }
    void actions_data() {
        QTest::addColumn<int>("operation");
        QTest::addColumn<int>("scene");
        for (int operation = 0; operation < 8; ++operation)
            for (int scene = 0; scene < (operation == 0 ? 128 : operation == 4 ? 64 : 16); ++scene)
                QTest::newRow(qPrintable(QString("%1-%2").arg(operation).arg(scene))) << operation << scene;
    }
    void actions() {
        QFETCH(int, operation);
        QFETCH(int, scene);
        try {
            const auto expected = daily_actions_test::trace<reference_daily::Daily>(operation, scene);
            const auto actual = daily_actions_test::trace<Daily>(operation, scene);
            QVERIFY2(actual == expected, qPrintable(firstDifference(actual, expected)));
        } catch (const std::exception &e) {
            QFAIL(qPrintable(errorText(e)));
        }
    }
    void staminaBatch_data() {
        QTest::addColumn<int>("scene");
        for (int scene = 0; scene < 2048; ++scene)
            QTest::newRow(qPrintable(QString::number(scene))) << scene;
    }
    void staminaBatch() {
        QFETCH(int, scene);
        cv::RNG random(12879 + scene);
        const int progress = random.uniform(-20, 210), cost = random.uniform(-5, 85),
                  available = random.uniform(-20, 700);
        const bool prefer = scene & 1;
        const auto run = [&](bool rust) {
            QJsonObject result;
            try {
                result["value"] =
                    rust ? dailyStaminaBatch(progress, cost, available, prefer)
                         : reference_daily::reference_dailyStaminaBatch(progress, cost, available, prefer);
            } catch (const std::exception &e) {
                result["error"] = errorText(e);
            }
            return result;
        };
        QCOMPARE(run(true), run(false));
    }
};
QTEST_GUILESS_MAIN(DailyContracts)
#include "test_daily_contract.moc"
