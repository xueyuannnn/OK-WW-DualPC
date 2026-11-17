#include "echo_interactions_replay.hpp"
#include "echo_run_replay.hpp"
#include "echo_single_replay.hpp"
#include "reference_echo_farm.hpp"
#include "workflow_scene_replay.hpp"
#include <QtTest>
#include <type_traits>
using namespace okww;
using namespace okww::runtime;
namespace okww::runtime {
struct EchoFarmContractAccess {
    template <class Farm> static QJsonObject exercise(Farm &farm, int operation, int scene) {
        farm.config_ = {{"Boss", QStringList{"Other", "Sentry Construct", "Lioness of Glory",
                                             "Fallacy of No Return", "Hyvatia", "Lady of the Sea", "Fenrico",
                                             "Nameless Explorer", "Nightmare: Hecate"}
                                     .at(scene % 9)},
                        {"Combat Wait Time", scene % 3 == 0 ? 0. : 1.5},
                        {"Boss Level", scene % 5 == 0 ? "invalid"
                                       : scene & 1    ? "80"
                                                      : "50"},
                        {"Teleport to Boss", scene & 1 ? "No" : "Weekly Challenge"}};
        farm.realm_ = scene & 2;
        farm.treatAsWorld_ = scene & 4;
        farm.treasure_ = scene & 8;
        farm.started_ = scene % 3 * 30.;
        QJsonObject out;
        try {
            switch (operation) {
            case 0:
                farm.parameters();
                farm.checkRealm(60);
                out["teleport"] = farm.teleportEnabled();
                break;
            case 1:
                farm.bossLevel();
                break;
            case 2:
                farm.changeNight();
                break;
            case 3:
                out["result"] = farm.seekCombat(.08, scene & 1, scene & 2);
                break;
            case 4:
                farm.treasureHunt();
                break;
            case 5:
                farm.enterFromF();
                break;
            case 6:
                farm.scrollInteract();
                break;
            case 7:
                if constexpr (std::is_same_v<Farm, EchoFarm>)
                    awaitWeeklyMaterialReward(farm.c_);
                else
                    reference_echo_farm::reference_awaitWeeklyMaterialReward(farm.c_);
                break;
            case 8:
                farm.aimBoss_ = scene & 8 ? "sentinel" : "";
                farm.recognizeBoss();
                break;
            case 9:
                farm.realm_ = true;
                farm.config_["Teleport to Boss"] = "No";
                farm.heal();
                break;
            case 10:
                farm.config_["Teleport to Boss"] = "invalid";
                farm.walkResult_ = "combat";
                farm.teleport();
                break;
            }
            out["error"] = "";
        } catch (const Cancelled &) {
            out["error"] = "cancelled";
        } catch (const std::exception &e) {
            out["error"] = errorText(e);
        }
        out["config"] = farm.config_;
        out["realm"] = farm.realm_;
        out["world"] = farm.treatAsWorld_;
        out["treasure"] = farm.treasure_;
        out["wait"] = farm.wait_;
        out["bypass"] = farm.bypassWait_;
        out["timeout"] = farm.combat_.targetEnemyTimeout;
        out["night"] = farm.lastNight_;
        out["aim"] = farm.aimBoss_;
        out["walk"] = farm.walkResult_;
        return out;
    }
};
} // namespace okww::runtime
namespace {
EchoDetector &detector() {
    static EchoDetector d(QString(OKWW_SOURCE_ROOT) + "/assets/echo_model/echo.onnx");
    return d;
}
template <class Farm> QJsonObject trace(int operation, int scene) {
    Scenes s({640, 360});
    s.profile.set("Monthly Card Config", "Check Monthly Card", false);
    s.clock.minimumStep = .02;
    s.frame("interact", {"pick_up_f_hcenter_vcenter"});
    s.frame("level", {});
    s.show((operation == 5 || operation == 6) && (scene & 1) ? "interact" : "blank");
    s.c.task = "FarmEchoTask2";
    if (operation == 10)
        s.show("world");
    s.c.gameLanguage = operation == 8 && (scene & 1) ? "en_US" : "zh_CN";
    QJsonArray reads;
    s.textOverride = [&](QRect area, QVector<TextLine>) {
        reads.append(QJsonArray{area.x(), area.y(), area.width(), area.height()});
        if (operation == 7)
            return scene & 1 ? QVector<TextLine>{s.word("获得物品", .2, .2), s.word("离开", .5, .85)}
                             : QVector<TextLine>{};
        if (operation == 8)
            return QVector<TextLine>{
                s.word(QStringList{"伪作的神王", "加尔古耶", "荣耀狮像", "夜之女皇", "other"}.at(scene % 5),
                       .4, .03)};
        return QVector<TextLine>{s.word("80", .1, .3), s.word("50", .1, .4)};
    };
    s.action = [&](QJsonObject event) {
        if (s.keyUp(event, Input::keyCode("f")))
            s.scene = operation == 5 ? "level" : "blank";
        if (operation == 5 && s.leftUp(event) && s.pointer.x() > .9 * s.size.width() &&
            s.pointer.y() > .9 * s.size.height())
            s.scene = "world";
    };
    CombatSession combat(s.c, s.root, s.directory.path());
    Farm farm(s.c, combat, detector());
    auto out = EchoFarmContractAccess::exercise(farm, operation, scene);
    QJsonArray events;
    for (auto event : s.events)
        events.append(event);
    out["events"] = events;
    out["reads"] = reads;
    out["time"] = s.clock.time;
    out["idle"] = s.input.idle();
    return out;
}
} // namespace
class EchoContracts : public QObject {
    Q_OBJECT
  private slots:
    void run_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 2048; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void run() {
        QFETCH(int, scene);
        const auto expected = echo_run_test::trace(scene, false), actual = echo_run_test::trace(scene, true);
        QVERIFY2(actual == expected,
                 qPrintable(
                     QString::fromUtf8(QJsonDocument(QJsonObject{{"actual", actual}, {"expected", expected}})
                                           .toJson(QJsonDocument::Compact))));
    }
    void single_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 2048; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void single() {
        QFETCH(int, scene);
        const auto expected = echo_single_test::trace(scene, false),
                   actual = echo_single_test::trace(scene, true);
        QVERIFY2(actual == expected,
                 qPrintable(
                     QString::fromUtf8(QJsonDocument(QJsonObject{{"actual", actual}, {"expected", expected}})
                                           .toJson(QJsonDocument::Compact))));
    }
    void interactions_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 2048; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void interactions() {
        QFETCH(int, scene);
        const auto expected = echo_interactions_test::trace(scene, false),
                   actual = echo_interactions_test::trace(scene, true);
        QVERIFY2(actual == expected,
                 qPrintable(
                     QString::fromUtf8(QJsonDocument(QJsonObject{{"actual", actual}, {"expected", expected}})
                                           .toJson(QJsonDocument::Compact))));
    }
    void rotation_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 4096; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void rotation() {
        QFETCH(int, scene);
        cv::RNG random(37926 + scene);
        QJsonObject config{
            {"Cycle Scope",
             QStringList{"All Challenges", "Boss Challenge", "Weekly Challenge", "invalid"}.at(scene % 4)},
            {"Cycle Start Category",
             QStringList{"Boss Challenge", "Weekly Challenge", "invalid"}.at((scene / 4) % 3)}};
        if (scene & 16)
            config["Cycle Start Index"] = scene & 32   ? QJsonValue(random.uniform(-2, 30))
                                          : scene & 64 ? QJsonValue(2.5)
                                                       : QJsonValue(QString("1"));
        const int bosses = random.uniform(-2, 30), weekly = random.uniform(-2, 14);
        const auto run = [&](bool rust) {
            QJsonObject out;
            QJsonArray targets;
            try {
                if (rust) {
                    for (const auto &t : echoRotationTargets(config, bosses, weekly))
                        targets.append(QJsonArray{t.category, t.index});
                } else {
                    for (const auto &t :
                         reference_echo_farm::reference_echoRotationTargets(config, bosses, weekly))
                        targets.append(QJsonArray{t.category, t.index});
                }
                out["targets"] = targets;
            } catch (const std::exception &e) {
                out["error"] = errorText(e);
            }
            return out;
        };
        QCOMPARE(run(true), run(false));
    }
    void reward_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 2048; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void reward() {
        QFETCH(int, scene);
        cv::RNG random(3675 + scene);
        const QStringList values{"获得奖励", "获得物品", "Rewards Obtained:", "Items Obtained", "退出",
                                 "再次挑战", "Leave",    "Challenge Again",   "使用结晶波片",   "波片不足",
                                 "60/240",   "确认",     "领取奖励",          "Other"};
        QVector<TextLine> lines;
        for (int i = 0; i < scene % 6; ++i)
            lines.append({values[random.uniform(0, int(values.size()))], {100, 20 * i, 100, 10}, .9});
        QCOMPARE(weeklyMaterialRewardConfirmed(lines),
                 reference_echo_farm::reference_weeklyMaterialRewardConfirmed(lines));
    }
    void actions_data() {
        QTest::addColumn<int>("operation");
        QTest::addColumn<int>("scene");
        for (int operation = 0; operation < 11; ++operation)
            for (int scene = 0; scene < (operation == 0 ? 128 : 16); ++scene)
                QTest::newRow(qPrintable(QString("%1-%2").arg(operation).arg(scene))) << operation << scene;
    }
    void actions() {
        QFETCH(int, operation);
        QFETCH(int, scene);
        try {
            const auto expected = trace<reference_echo_farm::EchoFarm>(operation, scene),
                       actual = trace<EchoFarm>(operation, scene);
            QVERIFY2(actual == expected,
                     qPrintable(QString::fromUtf8(
                         QJsonDocument(QJsonObject{{"actual", actual}, {"expected", expected}})
                             .toJson(QJsonDocument::Compact))));
        } catch (const std::exception &e) {
            QFAIL(qPrintable(errorText(e)));
        }
    }
};
QTEST_GUILESS_MAIN(EchoContracts)
#include "test_echo_contract.moc"
