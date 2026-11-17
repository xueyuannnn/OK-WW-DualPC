#ifndef Q_MOC_RUN
#include "backend_control_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww;
class BackendControlContracts final : public QObject {
    Q_OBJECT
  private slots:
    void control_data() {
        QTest::addColumn<int>("scene");
        QTest::addColumn<int>("method");
        for (int scene = 0; scene < 1024; ++scene)
            for (int method = 0; method < 6; ++method)
                QTest::newRow(qPrintable(QString("%1-%2").arg(scene).arg(method))) << scene << method;
    }
    void control() {
        QFETCH(int, scene);
        QFETCH(int, method);
        QCOMPARE(backend_control_test::outcome(scene, method, true),
                 backend_control_test::outcome(scene, method, false));
    }
};
QTEST_GUILESS_MAIN(BackendControlContracts)
#include "test_backend_control_contract.moc"
