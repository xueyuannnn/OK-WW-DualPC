#ifndef Q_MOC_RUN
#include "desktop_targets_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww;
class DesktopTargetsContracts final : public QObject {
    Q_OBJECT
  private slots:
    void targets_data() {
        QTest::addColumn<int>("scene");
        QTest::addColumn<bool>("windowsOnly");
        for (int scene = 0; scene < 512; ++scene)
            for (bool windowsOnly : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(scene).arg(windowsOnly)))
                    << scene << windowsOnly;
    }
    void targets() {
        QFETCH(int, scene);
        QFETCH(bool, windowsOnly);
        QCOMPARE(desktop_targets_test::outcome(scene, windowsOnly, true),
                 desktop_targets_test::outcome(scene, windowsOnly, false));
    }
};
QTEST_GUILESS_MAIN(DesktopTargetsContracts)
#include "test_desktop_targets_contract.moc"
