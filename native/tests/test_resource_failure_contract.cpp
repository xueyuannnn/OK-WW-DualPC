#ifndef Q_MOC_RUN
#include "resource_failure_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww::runtime;
class ResourceFailureContracts final : public QObject {
    Q_OBJECT
  private slots:
    void retry_data() {
        QTest::addColumn<int>("scene");
        QTest::addColumn<int>("type");
        for (int scene = 1; scene <= 256; ++scene)
            for (int type = 0; type < 8; ++type)
                QTest::newRow(qPrintable(QString("%1-%2").arg(scene).arg(type))) << scene << type;
    }
    void retry() {
        QFETCH(int, scene);
        QFETCH(int, type);
        QCOMPARE(resource_test::outcome<resource_test::current::EmergencyMemory>(scene, type),
                 resource_test::outcome<resource_test::original::EmergencyMemory>(scene, type));
    }
    void classification() {
        for (int mode = 0; mode < 6; ++mode)
            QCOMPARE(resource_test::classify(mode, true), resource_test::classify(mode, false));
    }
};
QTEST_GUILESS_MAIN(ResourceFailureContracts)
#include "test_resource_failure_contract.moc"
