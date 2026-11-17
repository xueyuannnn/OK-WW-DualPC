#ifndef Q_MOC_RUN
#include "desktop_helpers_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww;
class DesktopHelpersContracts final : public QObject {
    Q_OBJECT
  private slots:
    void helpers_data() {
        QTest::addColumn<int>("scene");
        QTest::addColumn<bool>("pacer");
        for (int scene = 0; scene < 1024; ++scene)
            for (bool pacer : {true, false})
                QTest::newRow(qPrintable(QString("%1-%2").arg(scene).arg(pacer))) << scene << pacer;
    }
    void helpers() {
        QFETCH(int, scene);
        QFETCH(bool, pacer);
        QCOMPARE(desktop_helpers_test::outcome(scene, pacer, true),
                 desktop_helpers_test::outcome(scene, pacer, false));
    }
    void timerWidth() {
        for (i64 remaining : {INT64_MIN, INT64_C(-1), INT64_C(0), INT64_C(1), INT64_C(99), INT64_C(100),
                              INT64_C(199), INT64_C(1000000000), INT64_C(0x123456789abcdef), INT64_MAX})
            QCOMPARE(rust::okww_rs_capture_timer_due(remaining), -std::max<i64>(1, remaining / 100));
    }
};
QTEST_GUILESS_MAIN(DesktopHelpersContracts)
#include "test_desktop_helpers_contract.moc"
