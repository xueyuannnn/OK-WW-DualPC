#include "reference_stamina.hpp"
#include "stamina_actions_replay.hpp"
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
namespace {
template <class Budget> QJsonObject journal(int scene) {
    QTemporaryDir dir;
    const auto path = dir.path() + "/cube.json";
    const QString day = "2026-11-16";
    QJsonObject data{{"day", scene & 1 ? "2026-11-15" : day},
                     {"reserved", scene % 5},
                     {"verified", (scene / 5) % 5},
                     {"pending", (scene & 2) != 0}};
    const QVector<QJsonValue> invalid{QJsonValue(), true, "2", 2.5, -1, 5, QJsonObject{}, QJsonArray{}};
    switch ((scene / 16) % 8) {
    case 0:
        data["reserved"] = invalid[scene % 8];
        break;
    case 1:
        data.remove("verified");
        break;
    case 2:
        data["pending"] = "true";
        break;
    case 3:
        data.remove("pending");
        break;
    case 4:
        data["verified"] = invalid[scene % 8];
        break;
    default:
        break;
    }
    if (scene & 4)
        writeJson(path, data);
    QJsonArray events;
    QJsonObject output;
    const auto error = [&](const std::exception &e) {
        return QString::fromUtf8(e.what()).replace(dir.path(), "DATA");
    };
    try {
        Budget budget(path, scene % 8 - 2, QDateTime::fromString("2026-11-16T05:00:00Z", Qt::ISODate));
        const auto snapshot = [&] {
            events.append(QJsonObject{{"used", budget.used()},
                                      {"remaining", budget.remaining()},
                                      {"pending", budget.pending()},
                                      {"limit", budget.limit()}});
        };
        snapshot();
        std::optional<QLockFile> lock;
        const auto op = [&](bool verify) {
            try {
                if (verify)
                    budget.verify();
                else
                    budget.reserve();
                events.append(verify ? "verify" : "reserve");
            } catch (const std::exception &e) {
                events.append(error(e));
            }
            snapshot();
        };
        switch ((scene / 8) % 8) {
        case 0:
            op(false);
            op(true);
            break;
        case 1:
            op(true);
            op(false);
            break;
        case 2:
            op(false);
            op(false);
            op(true);
            break;
        case 3:
            op(false);
            if (QFile::exists(path)) {
                auto changed = readJson(path);
                changed["reserved"] = 4;
                writeJson(path, changed);
            }
            op(true);
            break;
        case 4:
            op(false);
            try {
                Budget other(path, 4, QDateTime::fromString("2026-11-17T05:00:00Z", Qt::ISODate));
                events.append("other_loaded");
            } catch (const std::exception &e) {
                events.append(error(e));
            }
            op(true);
            break;
        case 5:
            for (int i = 0; i < 6; ++i) {
                op(false);
                op(true);
            }
            break;
        case 6:
            lock.emplace(path + ".lock");
            if (!lock->tryLock(0))
                throw std::runtime_error("fixture lock failed");
            op(false);
            break;
        case 7:
            op(false);
            op(true);
            op(true);
            break;
        }
        output["error"] = "";
    } catch (const std::exception &e) {
        output["error"] = error(e);
    }
    output["events"] = events;
    if (QFile::exists(path))
        output["disk"] = readJson(path);
    return output;
}
struct ClockProbe : Clock {
    double time = 0;
    std::function<void()> tick;
    double now() const override { return time; }
    void sleep(double seconds) override {
        time += seconds;
        if (tick)
            tick();
    }
};
QJsonObject readTrace(int scene, bool rust) {
    const auto root = QString(OKWW_SOURCE_ROOT);
    QTemporaryDir dir;
    Templates templates(root + "/assets/coco_annotations.json");
    Profile profile(dir.path(), root + "/native/resources/catalog.json");
    profile.set("Monthly Card Config", "Check Monthly Card", false);
    ClockProbe clock;
    ExecutionGate gate;
    Execution execution(gate, gate.start(), clock);
    clock.tick = [&] {
        if ((scene & 8) && clock.time >= .2)
            gate.stop();
    };
    QJsonArray events;
    Input input(Config::defaults(), {640, 360}, true, [](QJsonObject) { return QJsonObject{}; }, execution);
    cv::Mat frame(360, 640, CV_8UC3);
    int sources = 0, reads = 0;
    TaskContext context(
        execution, input, templates, profile,
        [&] {
            ++sources;
            return frame;
        },
        [&](const cv::Mat &, QRect area, double threshold) {
            ++reads;
            events.append(QJsonObject{{"read", reads},
                                      {"box", QJsonArray{area.x(), area.y(), area.width(), area.height()}},
                                      {"threshold", threshold},
                                      {"time", clock.time}});
            if (scene % 17 == 0)
                throw std::runtime_error("reader unavailable");
            const QStringList values{"none",  "12/240", "120／240", "2/19",
                                     "MAX60", "0/240",  "99/9999",  "000/0240"};
            QVector<TextLine> lines{{values[(scene + reads / 2) % 8], {450, 10, 60, 10}, .9}};
            if (scene & 2)
                lines.append({"MAX60", {400, 10, 40, 10}, .9});
            return lines;
        });
    context.frame = frame;
    context.task = "DailyTask2";
    if (scene & 4)
        context.info = [&](QString key, QString value) {
            events.append(QJsonObject{{"info", key}, {"value", value}, {"time", clock.time}});
        };
    QJsonObject output;
    try {
        QJsonArray values;
        if (rust) {
            const auto v = readStamina(context);
            values = {v.current, v.reserve, v.total};
        } else {
            const auto v = reference_stamina::reference_readStamina(context);
            values = {v.current, v.reserve, v.total};
        }
        output["values"] = values;
        output["error"] = "";
    } catch (const std::exception &e) {
        output["error"] = QString::fromUtf8(e.what());
    }
    output["events"] = events;
    output["sources"] = sources;
    output["time"] = clock.time;
    return output;
}
} // namespace
class StaminaContracts : public QObject {
    Q_OBJECT
  private slots:
    void cubeActions_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 128; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void cubeActions() {
        QFETCH(int, scene);
        const auto expected = stamina_actions_test::cube<false>(scene),
                   actual = stamina_actions_test::cube<true>(scene);
        if(scene%23==0) {
            // No complete counter: the new reader retries observation, while
            // the old reader could return on an unrelated inventory digit.
            for(const auto &key:QStringList{"confirmations","current","stock","error","used","pending","remaining","journal"})
                QCOMPARE(actual.value(key),expected.value(key));
            for(const auto &event:actual["events"].toArray())
                QVERIFY(!event.toObject().contains("op"));
            const auto time=actual["time"].toDouble();
            QVERIFY(time <= (scene&64 ? 2.25 : 10.25));
            if(!(scene&64))QVERIFY(time>=10.);
            return;
        }
        QVERIFY2(actual == expected,
                 qPrintable(
                     QString::fromUtf8(QJsonDocument(QJsonObject{{"expected", expected}, {"actual", actual}})
                                           .toJson(QJsonDocument::Compact))));
    }
    void rewardActions_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 64; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void rewardActions() {
        QFETCH(int, scene);
        const auto expected = stamina_actions_test::claim(scene, false),
                   actual = stamina_actions_test::claim(scene, true);
        QVERIFY2(actual == expected,
                 qPrintable(
                     QString::fromUtf8(QJsonDocument(QJsonObject{{"expected", expected}, {"actual", actual}})
                                           .toJson(QJsonDocument::Compact))));
    }
    void text_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 2048; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void text() {
        QFETCH(int, scene);
        const QStringList values{
            "12/240", "120／240",        "10 / 60",        "MAX10", "60MAX",     "1,000",   "99/9999",
            "2/19",   "10000000000/240", "2147483647/240", "0/20",  "MAX600MAX", "+12/240", "20/40"};
        cv::RNG random(4321 + scene);
        QVector<TextLine> lines;
        for (int i = 0; i < scene % 9; ++i)
            lines.append({values[random.uniform(0, int(values.size()))],
                          {random.uniform(-20, 1300), random.uniform(0, 40), random.uniform(1, 80),
                           random.uniform(0, 30)},
                          .9});
        const auto actual = staminaValues(lines);
        const auto expected = reference_stamina::reference_staminaValues(lines);
        QCOMPARE(actual.current, expected.current);
        QCOMPARE(actual.reserve, expected.reserve);
        QCOMPARE(actual.total, expected.total);
    }
    void journal_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 512; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void journal() {
        QFETCH(int, scene);
        const auto expected = ::journal<reference_stamina::CubeBudget>(scene),
                   actual = ::journal<CubeBudget>(scene);
        QVERIFY2(actual == expected,
                 qPrintable(
                     QString::fromUtf8(QJsonDocument(QJsonObject{{"expected", expected}, {"actual", actual}})
                                           .toJson(QJsonDocument::Compact))));
    }
    void reading_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 64; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void reading() {
        QFETCH(int, scene);
        const auto expected = readTrace(scene, false), actual = readTrace(scene, true);
        const int first=scene%8;
        if(scene%17!=0 && (first==0 || first==3 || first==4)) {
            if(first==3 && (scene&8)) {
                QCOMPARE(actual["error"].toString(),QString::fromUtf8(Cancelled().what()));
                QVERIFY(!actual.contains("values"));
                QVERIFY(qAbs(actual["time"].toDouble()-.2)<1e-8);
            } else {
                const int current=first==0?12:0,reserve=(scene&2)?60:0;
                QCOMPARE(actual["values"].toArray(),QJsonArray({current,reserve,current+reserve}));
                QCOMPARE(actual["error"].toString(),QString{});
                QVERIFY(qAbs(actual["time"].toDouble()-(first==3?.2:0.))<1e-8);
            }
            return;
        }
        QVERIFY2(actual == expected,
                 qPrintable(
                     QString::fromUtf8(QJsonDocument(QJsonObject{{"expected", expected}, {"actual", actual}})
                                           .toJson(QJsonDocument::Compact))));
    }
};
QTEST_GUILESS_MAIN(StaminaContracts)
#include "test_stamina_contract.moc"
