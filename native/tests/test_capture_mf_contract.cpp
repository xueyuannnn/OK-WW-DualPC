#ifndef Q_MOC_RUN
#include "capture_mf_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww;
class CaptureMfContracts final : public QObject {
    Q_OBJECT
  private slots:
    void reader_data() {
        QTest::addColumn<int>("scene");
        for (int scene = 0; scene < 2048; ++scene)
            QTest::newRow(qPrintable(QString::number(scene))) << scene;
    }
    void reader() {
        QFETCH(int, scene);
        QCOMPARE(capture_mf_test::outcome(scene, true), capture_mf_test::outcome(scene, false));
    }
    void publishCleanupExceptions() {
        for (int fault : {1, 2, 3}) {
            const auto actual = capture_mf_test::outcome(2, true, fault);
            const auto expected = capture_mf_test::outcome(2, false, fault);
            QCOMPARE(actual, expected);
            QCOMPARE(actual["error"].toString(), fault == 1   ? "publish fixture"
                                                 : fault == 2 ? "unknown"
                                                              : "unlock fixture");
        }
    }
};
QTEST_GUILESS_MAIN(CaptureMfContracts)
#include "test_capture_mf_contract.moc"
