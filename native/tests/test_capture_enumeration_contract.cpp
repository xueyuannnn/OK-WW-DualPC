#ifndef Q_MOC_RUN
#include "capture_enumeration_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww;
class CaptureEnumerationContracts final : public QObject {
    Q_OBJECT
  private slots:
    void enumeration_data() {
        QTest::addColumn<int>("scene");
        QTest::addColumn<int>("method");
        for (int scene = 0; scene < 512; ++scene)
            for (int method = 0; method < 4; ++method)
                QTest::newRow(qPrintable(QString("%1-%2").arg(scene).arg(method))) << scene << method;
    }
    void enumeration() {
        QFETCH(int, scene);
        QFETCH(int, method);
        QCOMPARE(capture_enumeration_test::outcome(scene, method, true),
                 capture_enumeration_test::outcome(scene, method, false));
    }
};
QTEST_GUILESS_MAIN(CaptureEnumerationContracts)
#include "test_capture_enumeration_contract.moc"
