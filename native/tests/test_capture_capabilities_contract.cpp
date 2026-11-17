#ifndef Q_MOC_RUN
#include "capture_capabilities_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww;
class CaptureCapabilitiesContracts final : public QObject {
    Q_OBJECT
  private slots:
    void enumeration_data() {
        QTest::addColumn<int>("scene");
        for (int scene = 0; scene < 1024; ++scene)
            QTest::newRow(qPrintable(QString::number(scene))) << scene;
    }
    void enumeration() {
        QFETCH(int, scene);
        QCOMPARE(capture_capabilities_test::outcome(scene, true),
                 capture_capabilities_test::outcome(scene, false));
    }
};
QTEST_GUILESS_MAIN(CaptureCapabilitiesContracts)
#include "test_capture_capabilities_contract.moc"
