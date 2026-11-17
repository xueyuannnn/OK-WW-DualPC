#ifndef Q_MOC_RUN
#include "controller_lifecycle_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww;
class ControllerLifecycleContracts final : public QObject {
    Q_OBJECT
  private slots:
    void lifecycle_data() {
        QTest::addColumn<int>("scene");
        QTest::addColumn<int>("method");
        for (int scene = 0; scene < 1024; ++scene)
            for (int method = 0; method < 4; ++method)
                QTest::newRow(qPrintable(QString("%1-%2").arg(scene).arg(method))) << scene << method;
    }
    void lifecycle() {
        QFETCH(int, scene);
        QFETCH(int, method);
        QCOMPARE(controller_lifecycle_test::outcome(scene, method, true),
                 controller_lifecycle_test::outcome(scene, method, false));
    }
};
QTEST_GUILESS_MAIN(ControllerLifecycleContracts)
#include "test_controller_lifecycle_contract.moc"
