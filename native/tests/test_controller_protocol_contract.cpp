#ifndef Q_MOC_RUN
#include "controller_protocol_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww;
class ControllerProtocolContracts final : public QObject {
    Q_OBJECT
  private slots:
    void protocol_data() {
        QTest::addColumn<int>("scene");
        QTest::addColumn<int>("method");
        for (int scene = 0; scene < 512; ++scene)
            for (int method = 0; method < 6; ++method)
                QTest::newRow(qPrintable(QString("%1-%2").arg(scene).arg(method))) << scene << method;
    }
    void protocol() {
        QFETCH(int, scene);
        QFETCH(int, method);
        QCOMPARE(controller_protocol_test::outcome(scene, method, true),
                 controller_protocol_test::outcome(scene, method, false));
    }
    void elapsedWidth() {
        for (i64 base : {INT64_C(0), INT64_C(0x123456789abcde00)})
            for (i64 elapsed : {INT64_C(-1), INT64_C(0), INT64_C(1), INT64_C(999999), INT64_C(123456789)}) {
                const double expected = (base + elapsed - base) / 1e6;
                const double actual = rust::okww_rs_controller_elapsed_ms(base + elapsed, base);
                QCOMPARE(std::bit_cast<uint64_t>(actual), std::bit_cast<uint64_t>(expected));
            }
    }
};
QTEST_GUILESS_MAIN(ControllerProtocolContracts)
#include "test_controller_protocol_contract.moc"
