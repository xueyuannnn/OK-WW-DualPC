#ifndef Q_MOC_RUN
#include "backend_lifecycle_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww;
class BackendLifecycleContracts final : public QObject {
    Q_OBJECT
  private slots:
    void lifecycle_data() {
        QTest::addColumn<int>("scene");
        QTest::addColumn<int>("method");
        for (int scene = 0; scene < 512; ++scene)
            for (int method = 0; method < 5; ++method)
                QTest::newRow(qPrintable(QString("%1-%2").arg(scene).arg(method))) << scene << method;
    }
    void lifecycle() {
        QFETCH(int, scene);
        QFETCH(int, method);
        QCOMPARE(backend_lifecycle_test::outcome(scene, method, true),
                 backend_lifecycle_test::outcome(scene, method, false));
    }
    void pixels() {
        for (int width : {1, 2, 3, 7, 31, 127, 640})
            for (int height : {1, 2, 5, 19, 72})
                for (int padding : {0, 1, 7}) {
                    const int stride = width * 3 + padding;
                    QByteArray expected(stride * height, char(0x65)), actual = expected;
                    for (int y = 0; y < height; ++y)
                        for (int x = 0; x < width; ++x) {
                            auto *p = reinterpret_cast<uint8_t *>(expected.data()) + y * stride + x * 3;
                            p[0] = uint8_t(x * 255 / width);
                            p[1] = uint8_t(y * 255 / height);
                            p[2] = 80;
                        }
                    QCOMPARE(rust::okww_rs_backend_simulation(width, height, stride,
                                                              reinterpret_cast<uint8_t *>(actual.data()),
                                                              size_t(actual.size())),
                             1);
                    QCOMPARE(actual, expected);
                }
    }
};
QTEST_GUILESS_MAIN(BackendLifecycleContracts)
#include "test_backend_lifecycle_contract.moc"
