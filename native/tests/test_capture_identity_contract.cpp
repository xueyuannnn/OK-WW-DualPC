#ifndef Q_MOC_RUN
#include "capture_identity_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww;
class CaptureIdentityContracts final : public QObject {
    Q_OBJECT
  private slots:
    void identity_data() {
        QTest::addColumn<int>("scene");
        QTest::addColumn<int>("method");
        for (int scene = 0; scene < 512; ++scene)
            for (int method = 0; method < 6; ++method)
                QTest::newRow(qPrintable(QString("%1-%2").arg(scene).arg(method))) << scene << method;
    }
    void identity() {
        QFETCH(int, scene);
        QFETCH(int, method);
        QCOMPARE(capture_identity_test::outcome(scene, method, true),
                 capture_identity_test::outcome(scene, method, false));
    }
};
QTEST_GUILESS_MAIN(CaptureIdentityContracts)
#include "test_capture_identity_contract.moc"
