#include "dispatcher_policy_replay.hpp"
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
namespace {
#include "reference_business_tasks.hpp"
}
class DispatcherContracts final : public QObject {
    Q_OBJECT
  private slots:
    void sequences_data() {
        QTest::addColumn<int>("scene");
        QTest::addColumn<QString>("id");
        const QStringList ids{"RemoteMouseResetTask",
                              "AutoCombatTask",
                              "MultiAccountDailyTask",
                              "DailyTask",
                              "DailyTask2",
                              "FarmEchoTask",
                              "FarmEchoTask2",
                              "NightmareNestTask",
                              "TacetTask",
                              "TacetTask2",
                              "TacetTaskExtra2",
                              "ForgeryTask",
                              "ForgeryTask2",
                              "SimulationTask",
                              "SimulationTask2",
                              "GardenTask",
                              "MergeEchoTask",
                              "EnhanceEchoTask",
                              "ChangeEchoTask",
                              "Unknown",
                              "",
                              "autocombattask",
                              QStringLiteral("TacetTask新副本2")};
        for (int i = 0; i < 4096; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i << ids[i % ids.size()];
        for (int i = 0; i < 512; ++i)
            QTest::newRow(qPrintable(QString("death-%1").arg(i))) << i << QString("AutoCombatTask");
    }
    void sequences() {
        QFETCH(int, scene);
        QFETCH(QString, id);
        QCOMPARE(dispatcher_test::replay(id, scene, false), dispatcher_test::replay(id, scene, true));
    }
    void catalogue() {
        const auto callback = [](const QString &) { return true; };
        const auto actual = businessTasks(callback), expected = reference_businessTasks(callback);
        QCOMPARE(actual.size(), expected.size());
        for (int i = 0; i < actual.size(); ++i) {
            QCOMPARE(actual[i].id, expected[i].id);
            QCOMPARE(actual[i].trigger, expected[i].trigger);
            QCOMPARE(actual[i].interval, expected[i].interval);
            QVERIFY(bool(actual[i].step));
        }
    }
};
QTEST_GUILESS_MAIN(DispatcherContracts)
#include "test_dispatcher_contract.moc"
