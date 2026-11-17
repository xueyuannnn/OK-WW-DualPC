#ifndef Q_MOC_RUN
#include "backend_frames_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww;
class BackendFramesContracts final : public QObject {
    Q_OBJECT
  private slots:
    void frames_data() {
        QTest::addColumn<int>("scene");
        QTest::addColumn<int>("method");
        for (int scene = 0; scene < 1024; ++scene)
            for (int method = 0; method < 6; ++method)
                QTest::newRow(qPrintable(QString("%1-%2").arg(scene).arg(method))) << scene << method;
    }
    void frames() {
        QFETCH(int, scene);
        QFETCH(int, method);
        QCOMPARE(backend_frames_test::outcome(scene, method, true),
                 backend_frames_test::outcome(scene, method, false));
    }
    void integerWidth() {
        for (i64 stamp : {INT64_MIN, INT64_C(-1), INT64_C(0), INT64_C(16666666), INT64_C(16666667),
                          INT64_C(0x123456789abcde00), INT64_MAX})
            QCOMPARE(rust::okww_rs_backend_simulation_sequence(stamp), uint64_t(stamp / 16666667));
        for (i64 base : {INT64_C(0), INT64_C(0x123456789abcde00)})
            for (i64 elapsed : {INT64_C(119999999999), INT64_C(120000000000), INT64_C(120000000001)})
                QCOMPARE(bool(rust::okww_rs_backend_recovery_timed_out(base + elapsed, base)),
                         elapsed >= INT64_C(120000000000));
    }
};
QTEST_GUILESS_MAIN(BackendFramesContracts)
#include "test_backend_frames_contract.moc"
