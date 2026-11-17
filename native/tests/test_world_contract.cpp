#include "reference_world.hpp"
#include "workflow_scene_replay.hpp"
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
namespace {
template <class World> QJsonObject trace(int operation, int scene) {
    Scenes s({640, 360});
    s.profile.set("Monthly Card Config", "Check Monthly Card", false);
    s.frame("interact", {"pick_up_f_hcenter_vcenter"});
    s.frame("claim", {"claim_cancel_button_hcenter_vcenter"});
    s.frame("map", {"arrow", "boss_check_mark_minimap"});
    s.show(operation == 2 ? "interact" : operation == 3 ? "claim" : operation == 7 ? "map" : "blank");
    s.words["interact"] = {s.word("吸收", .75, .52, .1)};
    if (operation >= 8)
        s.clock.minimumStep = operation == 12 ? 10. : .02;
    QJsonArray calls;
    int checks = 0;
    World world(
        s.c,
        [&] {
            calls.append("combat");
            return scene & 4;
        },
        [&](const cv::Mat &image, double threshold) {
            calls.append(QJsonArray{image.cols, image.rows, threshold});
            if (scene % 17 == 0)
                throw std::runtime_error("inference failed");
            QVector<Match> values;
            for (int i = 0; i < scene % 5; ++i)
                values.append({"echo", {10 * i, 20 * i + 3, 30 + i, 10 + i}, .8});
            return values;
        });
    QJsonObject out;
    const auto condition = [&] {
        calls.append(QJsonArray{"condition", ++checks});
        if (scene % 19 == 0)
            throw Cancelled{};
        if (scene % 23 == 0)
            throw std::runtime_error("callback failed");
        return checks >= 1 + scene % 7;
    };
    try {
        switch (operation) {
        case 0:
            out["result"] = world.walkUntil(condition, scene & 1 ? "w" : "a",
                                            scene % 13 == 0   ? 0.
                                            : scene % 11 == 0 ? -.1
                                                              : .08,
                                            bool(scene & 2), bool(scene & 4), scene % 3 * .02);
            break;
        case 1: {
            QJsonArray values;
            for (const auto &m : world.echoes(.3 + scene % 3 * .1))
                values.append(QJsonObject{{"name", m.name},
                                          {"x", m.box.x()},
                                          {"y", m.box.y()},
                                          {"w", m.box.width()},
                                          {"h", m.box.height()},
                                          {"confidence", m.confidence}});
            out["result"] = values;
            break;
        }
        case 2:
            out["result"] = world.pick(scene & 1, scene & 2);
            break;
        case 3:
            out["result"] = world.dismissClaim();
            break;
        case 4:
            out["result"] = world.circleEcho(scene % 3 - 1);
            break;
        case 5:
            world.centerCamera();
            break;
        case 6:
            out["result"] = bool(world.treasure());
            break;
        case 7: {
            const auto turn = world.miniMapTurn("boss_check_mark_minimap", .5, scene % 3 - 1, scene % 4 - 2);
            out["result"] = turn ? QJsonValue(*turn) : QJsonValue();
            break;
        }
        case 8:
            out["result"] =
                world.walkToInteract(scene & 1 ? "w" : "d", .08, bool(scene & 2), scene & 4 ? .04 : 0.,
                                     scene & 8 ? "吸收" : "", bool(scene & 16), bool(scene & 32));
            break;
        case 9:
            out["result"] = world.walkToEcho(scene % 3 == 0 ? 3.5 : .15, .4, [&] {
                calls.append("update");
                if (scene % 13 == 0)
                    throw Cancelled{};
            });
            break;
        case 10: {
            const auto value = world.findEcho(scene & 1, scene & 2, .05, .4, [&] { calls.append("update"); });
            out["result"] = QJsonArray{value.first, value.second};
            break;
        }
        case 11: {
            std::function<std::optional<Match>()> find = [&]() -> std::optional<Match> {
                calls.append(QJsonArray{"find", ++checks});
                if (scene % 13 == 0)
                    throw Cancelled{};
                if (scene & 2 && checks % 3 == 0)
                    return {};
                return Match{"box", {scene % 2 ? 410 : 160, scene % 3 ? 200 : 100, 30, 30}, .9};
            };
            std::function<bool()> end;
            if (scene & 4)
                end = [&] {
                    calls.append("end");
                    return checks > scene % 5;
                };
            if (scene == 0)
                find = {};
            out["result"] = world.walkToBox(find, end, scene % 7 == 0 ? 0. : .12, .05, .07, scene & 8);
            break;
        }
        case 12:
            world.walkToTreasure(scene & 1);
            break;
        case 13:
            out["result"] = world.followBossMinimap(.6, .7);
            break;
        }
        out["error"] = "";
    } catch (const Cancelled &) {
        out["error"] = "cancelled";
    } catch (const std::exception &e) {
        out["error"] = errorText(e);
    }
    QJsonArray events;
    for (auto event : s.events)
        events.append(event);
    out["events"] = events;
    out["calls"] = calls;
    out["time"] = s.clock.time;
    out["idle"] = s.input.idle();
    return out;
}
} // namespace
class WorldContracts : public QObject {
    Q_OBJECT
  private slots:
    void searchBox_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 64; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void searchBox() {
        QFETCH(int, scene);
        Scenes s({640 + scene * 8, 360 + scene * 4});
        s.show("blank");
        QCOMPARE(fSearchBox(s.c), reference_world::reference_fSearchBox(s.c));
    }
    void actions_data() {
        QTest::addColumn<int>("operation");
        QTest::addColumn<int>("scene");
        for (int op = 0; op < 14; ++op)
            for (int scene = 0; scene < (op == 0 ? 128 : op == 1 ? 64 : op == 7 ? 4 : 16); ++scene)
                QTest::newRow(qPrintable(QString("%1-%2").arg(op).arg(scene))) << op << scene;
    }
    void actions() {
        QFETCH(int, operation);
        QFETCH(int, scene);
        try {
            const auto expected = trace<reference_world::WorldInteraction>(operation, scene),
                       actual = trace<WorldInteraction>(operation, scene);
            QVERIFY2(actual == expected,
                     qPrintable(QString::fromUtf8(
                         QJsonDocument(QJsonObject{{"actual", actual}, {"expected", expected}})
                             .toJson(QJsonDocument::Compact))));
        } catch (const std::exception &e) {
            QFAIL(qPrintable(errorText(e)));
        }
    }
    void echoDirection_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 2048; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void echoDirection() {
        QFETCH(int, scene);
        cv::RNG random(8736 + scene);
        QVector<Match> echoes;
        for (int i = 0; i < scene % 4; ++i)
            echoes.append({"echo",
                           {random.uniform(0, 1920), random.uniform(0, 1080), random.uniform(1, 200),
                            random.uniform(1, 200)},
                           .9});
        const QSize size{640 + scene % 11 * 128, 360 + scene % 7 * 72};
        QCOMPARE(echoWalkDirection(echoes, size, scene & 1),
                 reference_world::reference_echoWalkDirection(echoes, size, scene & 1));
    }
};
QTEST_GUILESS_MAIN(WorldContracts)
#include "test_world_contract.moc"
