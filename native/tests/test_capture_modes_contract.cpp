#ifndef Q_MOC_RUN
#include "capture_modes_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww;
class CaptureModesContracts final : public QObject {
    Q_OBJECT
  private slots:
    void modes_data() {
        QTest::addColumn<int>("scene");
        QTest::addColumn<bool>("ds");
        for (int scene = 0; scene < 768; ++scene)
            for (bool ds : {true, false})
                QTest::newRow(qPrintable(QString("%1-%2").arg(scene).arg(ds))) << scene << ds;
    }
    void modes() {
        QFETCH(int, scene);
        QFETCH(bool, ds);
        QCOMPARE(capture_modes_test::outcome(scene, ds, true), capture_modes_test::outcome(scene, ds, false));
    }
    void duplicateBoundary() {
        const std::array<double, 10> rates{0.,
                                           -0.,
                                           120.,
                                           120.0009,
                                           120.001,
                                           240.,
                                           -1.,
                                           std::numeric_limits<double>::infinity(),
                                           -std::numeric_limits<double>::infinity(),
                                           std::numeric_limits<double>::quiet_NaN()};
        for (double a : rates)
            for (double b : rates)
                for (int mask = 0; mask < 8; ++mask) {
                    Mode first{1920, 1080, a, "NV12"};
                    Mode second{mask & 1 ? 1280 : 1920, mask & 2 ? 720 : 1080, b, mask & 4 ? "YUY2" : "NV12"};
                    capture_modes_test::current::CaptureModesSameSdk sdk{first, second};
                    const bool actual =
                        rust::platformWorkflowRun(rust::WorkflowMethod::CaptureModesSame, {}, sdk)[0] != 0;
                    QCOMPARE(actual, first.width == second.width && first.height == second.height &&
                                         first.fourcc == second.fourcc && std::abs(a - b) < .001);
                }
    }
};
QTEST_GUILESS_MAIN(CaptureModesContracts)
#include "test_capture_modes_contract.moc"
