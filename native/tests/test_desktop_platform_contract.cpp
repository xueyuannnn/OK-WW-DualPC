#ifndef Q_MOC_RUN
#include "desktop_platform_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww;
class DesktopPlatformContracts final : public QObject {
    Q_OBJECT
  private slots:
    void platform_data() {
        QTest::addColumn<int>("scene");
        QTest::addColumn<int>("method");
        for (int scene = 0; scene < 512; ++scene)
            for (int method = 0; method < 7; ++method)
                QTest::newRow(qPrintable(QString("%1-%2").arg(scene).arg(method))) << scene << method;
    }
    void platform() {
        QFETCH(int, scene);
        QFETCH(int, method);
        QCOMPARE(desktop_platform_test::outcome(scene, method, true),
                 desktop_platform_test::outcome(scene, method, false));
    }
};
QTEST_GUILESS_MAIN(DesktopPlatformContracts)
#include "test_desktop_platform_contract.moc"
