#ifndef Q_MOC_RUN
#include "capture_duplication_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww;
class CaptureDuplicationContracts final : public QObject {
    Q_OBJECT
  private slots:
    void duplication_data() {
        QTest::addColumn<int>("scene");
        for (int scene = 0; scene < 1024; ++scene)
            QTest::newRow(qPrintable(QString::number(scene))) << scene;
    }
    void duplication() {
        QFETCH(int, scene);
        QCOMPARE(capture_duplication_test::outcome(scene, true),
                 capture_duplication_test::outcome(scene, false));
    }
};
QTEST_GUILESS_MAIN(CaptureDuplicationContracts)
#include "test_capture_duplication_contract.moc"
