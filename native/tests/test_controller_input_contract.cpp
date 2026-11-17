#ifndef Q_MOC_RUN
#include "controller_input_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww;
class ControllerInputContracts final : public QObject {
    Q_OBJECT
  private slots:
    void input_data() {
        QTest::addColumn<int>("scene");
        QTest::addColumn<int>("method");
        for (int scene = 0; scene < 1024; ++scene)
            for (int method = 0; method < 5; ++method)
                QTest::newRow(qPrintable(QString("%1-%2").arg(scene).arg(method))) << scene << method;
    }
    void input() {
        QFETCH(int, scene);
        QFETCH(int, method);
        QCOMPARE(controller_input_test::outcome(scene, method, true),
                 controller_input_test::outcome(scene, method, false));
    }
    void mapPoint() {
        for (int scene = 0; scene < 4096; ++scene) {
            const int width = (scene * 17) % 7680 + 1, height = (scene * 13) % 4320 + 1;
            const int x = scene % width, y = (scene * 7) % height;
            const QRect rect(scene % 2 ? -1920 : 333, -27, 2560, 1440);
            const QPoint expected(rect.x() + int(int64_t(x) * rect.width() / width),
                                  rect.y() + int(int64_t(y) * rect.height() / height));
            std::array<int32_t, 2> actual{};
            QCOMPARE(rust::okww_rs_controller_map_point(x, y, width, height, rect.x(), rect.y(), rect.width(),
                                                        rect.height(), actual.data()),
                     1);
            QCOMPARE(QPoint(actual[0], actual[1]), expected);
        }
    }
};
QTEST_GUILESS_MAIN(ControllerInputContracts)
#include "test_controller_input_contract.moc"
