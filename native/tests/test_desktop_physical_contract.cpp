#ifndef Q_MOC_RUN
#include "desktop_physical_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww;
class DesktopPhysicalContracts final : public QObject {
    Q_OBJECT
  private slots:
    void physical_data() {
        QTest::addColumn<int>("scene");
        QTest::addColumn<bool>("life");
        for (int scene = 0; scene < 1536; ++scene) {
            QTest::newRow(qPrintable(QString("hook-%1").arg(scene))) << scene << false;
            if (scene < 512)
                QTest::newRow(qPrintable(QString("life-%1").arg(scene))) << scene << true;
        }
    }
    void physical() {
        QFETCH(int, scene);
        QFETCH(bool, life);
        QCOMPARE(desktop_physical_test::outcome(scene, life, true),
                 desktop_physical_test::outcome(scene, life, false));
    }
};
QTEST_GUILESS_MAIN(DesktopPhysicalContracts)
#include "test_desktop_physical_contract.moc"
