#include "reference_navigation.hpp"
#include <QtTest>
#include <opencv2/imgproc.hpp>
using namespace okww;
using namespace okww::runtime;
namespace {
struct NavigationClock final : Clock {
    double time = 0;
    std::function<void()> tick;
    double now() const override { return time; }
    void sleep(double seconds) override {
        time += seconds;
        if (tick)
            tick();
    }
};
QJsonArray rectangle(QRect box) { return {box.x(), box.y(), box.width(), box.height()}; }
template <class Nav> QJsonObject replayNavigation(int scenario) {
    const int mode = scenario / 16, variant = scenario % 16;
    const QSize size(640, 360);
    const auto root = QString(OKWW_SOURCE_ROOT);
    QTemporaryDir directory;
    Templates templates(root + "/assets/coco_annotations.json");
    Profile profile(directory.path(), root + "/native/resources/catalog.json");
    profile.set("Monthly Card Config", "Check Monthly Card", false);
    profile.set("Task Recovery", "Recovery Retry Limit", 1);
    NavigationClock clock;
    ExecutionGate gate;
    Execution execution(gate, gate.start(), clock);
    QJsonArray events;
    int stage = 0, reads = 0, checks = 0;
    clock.tick = [&] {
        if (clock.time >= 20)
            gate.stop();
    };
    cv::Mat main(size.height(), size.width(), CV_8UC3, cv::Scalar(17, 23, 31)), before = main.clone(),
                                                                                after = main.clone();
    const auto paint = [&](cv::Mat &image, const QString &name, QPoint offset = {}) {
        auto pattern = templates.pattern(name, size);
        if (pattern.channels() == 1)
            cv::cvtColor(pattern, pattern, cv::COLOR_GRAY2BGR);
        const auto box = templates.box(name, size);
        const auto pos = box.topLeft() + offset;
        if (pos.x() >= 0 && pos.y() >= 0 && pos.x() + pattern.cols <= image.cols &&
            pos.y() + pattern.rows <= image.rows)
            pattern.copyTo(image(cv::Rect(pos.x(), pos.y(), pattern.cols, pattern.rows)));
    };
    paint(main, "char_2_text");
    paint(main, "char_3_text");
    paint(main, "world_earth_icon");
    if (variant & 1)
        before = main.clone();
    if (variant & 2) {
        paint(before, "char_2_text");
        paint(before, "char_3_text");
        paint(before, "illusive_realm_exit");
    }
    if (mode == 3 || mode == 4) {
        paint(before, variant & 4 ? "cancel_button_hcenter_vcenter" : "confirm_btn_hcenter_vcenter");
        if (variant & 8)
            paint(before, "gray_confirm_exit_button");
        after = main;
    }
    if (mode == 5) {
        if (variant & 4)
            before = main;
        paint(after, "gray_book_all_monsters");
    }
    if (mode == 8) {
        paint(before, "map_way_point");
        paint(after, "gray_teleport");
    }
    if (mode == 9) {
        before = main;
        paint(after, "gray_book_all_monsters");
    }
    if (mode == 10)
        paint(before, variant & 4 ? "fast_travel_custom" : "gray_teleport");
    if (mode == 11) {
        paint(before, "team_start_challenge");
        after = main;
    }
    if (mode == 12) {
        paint(before, "boss_proceed");
        paint(after, variant & 4 ? "team_close" : "gray_teleport");
    }
    Input input(
        Config::defaults(), size, true,
        [&](QJsonObject command) {
            command["time"] = clock.time;
            events.append(command);
            if ((command["op"] == "key" || command["op"] == "button") && !command["down"].toBool())
                ++stage;
            return QJsonObject{};
        },
        execution);
    TaskContext context(
        execution, input, templates, profile,
        [&] {
            ++checks;
            return stage > 0 && !(variant & 8) ? after : before;
        },
        [&](const cv::Mat &, QRect area, double threshold) {
            ++reads;
            events.append(QJsonObject{
                {"read", reads}, {"area", rectangle(area)}, {"threshold", threshold}, {"time", clock.time}});
            if (variant == 15)
                throw std::runtime_error("OCR unavailable");
            QVector<TextLine> lines;
            if (mode == 6 || mode == 9) {
                lines.append({variant & 1 ? "无冠者" : "Other", {90, 100, 40, 12}, .9});
                if ((variant & 4) && reads > 1)
                    lines.append({"无冠者", {90, 132, 40, 12}, .9});
            }
            if (mode == 11 && (variant & 1))
                lines.append({"开启挑战", {500, 320, 50, 15}, .9});
            if (mode == 12 && (variant & 2))
                lines.append({"提前到达目标位置可能影响剧情体验是否确认前往", {200, 160, 200, 15}, .9});
            return lines;
        });
    context.info = [&](QString key, QString value) {
        events.append(QJsonObject{{"info", key}, {"value", value}, {"time", clock.time}});
    };
    context.frame = before;
    context.frameVersion = 1;
    context.state->loggedIn = true;
    context.state->retryingStep = (variant & 4) != 0;
    context.task = variant & 8 ? "AutoCombatTask" : "TacetTask";
    QJsonObject result;
    try {
        Nav nav(context);
        switch (mode) {
        case 0:
            result["realm"] = nav.inRealm();
            result["world"] = nav.inWorld();
            break;
        case 1:
            result["team"] = nav.waitTeam(.15, (variant & 4) != 0, (variant & 8) != 0);
            break;
        case 2:
            nav.ensureMain(.15, (variant & 8) != 0);
            break;
        case 3:
            nav.confirmDialog((variant & 4) == 0);
            break;
        case 4:
            nav.exitRealm(.15);
            break;
        case 5: {
            const auto m = nav.openBook();
            result["match"] =
                QJsonObject{{"name", m.name}, {"box", rectangle(m.box)}, {"confidence", m.confidence}};
            break;
        }
        case 6: {
            const int pages[]{-1, 1, 3, 101};
            const auto line = nav.selectMonster(variant & 2 ? " " : "无冠者", pages[variant % 4]);
            result["line"] = QJsonObject{{"text", line.text}, {"box", rectangle(line.box)}};
            break;
        }
        case 7: {
            const QStringList categories{"ningsu", "moni",     "qiangdi", "wuyin",
                                         "zhange", "canxiang", "mengyan", "unknown"};
            nav.bookCategory(categories[variant % 8], .1);
            break;
        }
        case 8:
            nav.teleportNearest();
            break;
        case 9:
            nav.healAtBeacon();
            break;
        case 10:
            nav.waitTravel();
            break;
        case 11:
            nav.startChallenge();
            break;
        case 12:
            result["entry"] = nav.selectBookEntry(variant % 5 + 1, 21,
                                                  variant & 1 ? QVector<int>{4, 5, 5, 7} : QVector<int>{});
            break;
        }
        result["error"] = "";
    } catch (const std::exception &e) {
        result["error"] = QString::fromUtf8(e.what());
    }
    result["events"] = events;
    result["stage"] = stage;
    result["reads"] = reads;
    result["sources"] = checks;
    result["time"] = clock.time;
    result["logged_in"] = context.state->loggedIn;
    result["retrying"] = context.state->retryingStep;
    return result;
}
} // namespace
class NavigationContracts : public QObject {
    Q_OBJECT
  private slots:
    void workflows_data() {
        QTest::addColumn<int>("scenario");
        for (int i = 0; i < 13 * 16; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void workflows() {
        QFETCH(int, scenario);
        const auto expected = replayNavigation<reference_navigation::Navigation>(scenario),
                   actual = replayNavigation<Navigation>(scenario);
        if (scenario >= 136 && scenario <= 143) {
            // Upstream now dismisses an overlapping waypoint selection and tries
            // another beacon instead of retrying the same hidden travel button.
            QCOMPARE(actual["error"], expected["error"]);
            QCOMPARE(actual["reads"].toInt(), 0);
            QCOMPARE(actual["stage"].toInt(), 2);
            QCOMPARE(actual["sources"].toInt(), 65);
            QVERIFY(qAbs(actual["time"].toDouble() - 7.48) < 1e-8);
            const auto events = actual["events"].toArray();
            QCOMPARE(events.size(), 6);
            QCOMPARE(events[0], expected["events"].toArray()[0]);
            QCOMPARE(events[1], expected["events"].toArray()[1]);
            QCOMPARE(events[2], expected["events"].toArray()[2]);
            QCOMPARE(events[3].toObject()["x"].toInt(), 38);
            QCOMPARE(events[3].toObject()["y"].toInt(), 180);
            QCOMPARE(events[4].toObject()["down"].toBool(), true);
            QCOMPARE(events[5].toObject()["down"].toBool(), false);
            QCOMPARE(actual["logged_in"], expected["logged_in"]);
            QCOMPARE(actual["retrying"], expected["retrying"]);
            return;
        }
        QVERIFY2(actual == expected,
                 qPrintable(
                     QString::fromUtf8(QJsonDocument(QJsonObject{{"expected", expected}, {"actual", actual}})
                                           .toJson(QJsonDocument::Compact))));
    }
    void bookScroll_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 1024; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void bookScroll() {
        QFETCH(int, scene);
        const int serial = scene % 28 - 2, total = (scene / 4) % 25 - 1;
        const QVector<QVector<int>> groups{{}, {4, 5, 5, 7}, {0}, {-1, 5}, {1, 1, 1, 1}, {30, 0}};
        const double tops[]{.1, 424. / 2160, .85, .8806, -1, std::numeric_limits<double>::quiet_NaN()};
        const auto group = groups[(scene / 7) % groups.size()];
        const auto top = tops[(scene / 13) % 6];
        QString actualError, expectedError;
        double actual = 0, expected = 0;
        try {
            expected = reference_navigation::frozen_bookScrollPosition(serial, total, group, top);
        } catch (const std::exception &e) {
            expectedError = QString::fromUtf8(e.what());
        }
        try {
            actual = bookScrollPosition(serial, total, group, top);
        } catch (const std::exception &e) {
            actualError = QString::fromUtf8(e.what());
        }
        QCOMPARE(actualError, expectedError);
        QCOMPARE(actual, expected);
    }
};
QTEST_GUILESS_MAIN(NavigationContracts)
#include "test_navigation_contract.moc"
