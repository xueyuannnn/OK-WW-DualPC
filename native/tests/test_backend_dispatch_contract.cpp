#ifndef Q_MOC_RUN
#include "backend_dispatch_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww;
class BackendDispatchContracts final : public QObject {
    Q_OBJECT
  private slots:
    void dispatch_data() {
        QTest::addColumn<int>("scene");
        QTest::addColumn<int>("command");
        for (int scene = 0; scene < 512; ++scene)
            for (int command = 0; command < 12; ++command)
                QTest::newRow(qPrintable(QString("%1-%2").arg(scene).arg(command))) << scene << command;
    }
    void dispatch() {
        QFETCH(int, scene);
        QFETCH(int, command);
        QCOMPARE(backend_dispatch_test::outcome(scene, command, true),
                 backend_dispatch_test::outcome(scene, command, false));
    }
};
QTEST_GUILESS_MAIN(BackendDispatchContracts)
#include "test_backend_dispatch_contract.moc"
