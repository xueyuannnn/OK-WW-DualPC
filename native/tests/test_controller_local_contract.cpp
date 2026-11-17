#ifndef Q_MOC_RUN
#include "controller_local_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww;
class ControllerLocalContracts final : public QObject {
    Q_OBJECT
  private slots:
    void local_data() {
        QTest::addColumn<int>("scene");
        QTest::addColumn<bool>("releasing");
        for (int scene = 0; scene < 2048; ++scene)
            for (bool releasing : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(scene).arg(releasing))) << scene << releasing;
    }
    void local() {
        QFETCH(int, scene);
        QFETCH(bool, releasing);
        QCOMPARE(controller_local_test::outcome(scene, releasing, true),
                 controller_local_test::outcome(scene, releasing, false));
    }
};
QTEST_GUILESS_MAIN(ControllerLocalContracts)
#include "test_controller_local_contract.moc"
