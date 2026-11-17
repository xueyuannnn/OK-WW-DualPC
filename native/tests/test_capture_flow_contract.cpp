#ifndef Q_MOC_RUN
#include "capture_flow_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww;
class CaptureFlowContracts final : public QObject {
    Q_OBJECT
  private slots:
    void lifecycle_data() {
        QTest::addColumn<int>("scene");
        QTest::addColumn<int>("method");
        for (int scene = 0; scene < 512; ++scene)
            for (int method = 0; method < 12; ++method)
                QTest::newRow(qPrintable(QString("%1-%2").arg(scene).arg(method))) << scene << method;
    }
    void lifecycle() {
        QFETCH(int, scene);
        QFETCH(int, method);
        QCOMPARE((capture_flow_test::outcome<capture_flow_test::Current>(scene, method)),
                 (capture_flow_test::outcome<capture_flow_test::Original>(scene, method)));
    }
};
QTEST_GUILESS_MAIN(CaptureFlowContracts)
#include "test_capture_flow_contract.moc"
