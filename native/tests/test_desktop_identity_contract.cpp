#ifndef Q_MOC_RUN
#include "desktop_identity_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww;
class DesktopIdentityContracts final : public QObject {
    Q_OBJECT
  private slots:
    void identity_data() {
        QTest::addColumn<int>("scene");
        for (int scene = 0; scene < 1536; ++scene)
            QTest::newRow(qPrintable(QString::number(scene))) << scene;
    }
    void identity() {
        QFETCH(int, scene);
        QCOMPARE(desktop_identity_test::outcome(scene, true), desktop_identity_test::outcome(scene, false));
    }
};
QTEST_GUILESS_MAIN(DesktopIdentityContracts)
#include "test_desktop_identity_contract.moc"
