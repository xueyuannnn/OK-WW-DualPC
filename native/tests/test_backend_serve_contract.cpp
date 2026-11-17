#ifndef Q_MOC_RUN
#include "backend_serve_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww;
class BackendServeContracts final : public QObject {
    Q_OBJECT
  private slots:
    void serve_data() {
        QTest::addColumn<int>("scene");
        for (int scene = 0; scene < 1024; ++scene)
            QTest::newRow(qPrintable(QString::number(scene))) << scene;
    }
    void serve() {
        QFETCH(int, scene);
        QCOMPARE(backend_serve_test::outcome(scene, true), backend_serve_test::outcome(scene, false));
    }
};
QTEST_GUILESS_MAIN(BackendServeContracts)
#include "test_backend_serve_contract.moc"
