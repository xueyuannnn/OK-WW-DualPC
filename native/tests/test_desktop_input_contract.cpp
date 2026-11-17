#ifndef Q_MOC_RUN
#include "reference_desktop_input.hpp"
#endif
#include <QtTest>
using namespace okww;
class DesktopInputContracts final : public QObject {
    Q_OBJECT
  private slots:
    void hid_data() {
        QTest::addColumn<int>("usage");
        QTest::addColumn<bool>("down");
        for (int usage = -16; usage < 512; ++usage)
            for (bool down : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(usage).arg(down))) << usage << down;
    }
    void hid() {
        QFETCH(int, usage);
        QFETCH(bool, down);
        INPUT actual{}, expected{};
        QString actualError, expectedError;
        try {
            actual = hidInput(usage, down);
        } catch (const std::exception &error) {
            actualError = errorText(error);
        }
        try {
            expected = desktop_input_reference::hidInput(usage, down);
        } catch (const std::exception &error) {
            expectedError = errorText(error);
        }
        QCOMPARE(actualError, expectedError);
        QCOMPARE(actual.type, expected.type);
        QCOMPARE(actual.ki.wVk, expected.ki.wVk);
        QCOMPARE(actual.ki.wScan, expected.ki.wScan);
        QCOMPARE(actual.ki.dwFlags, expected.ki.dwFlags);
        QCOMPARE(actual.ki.time, expected.ki.time);
        QCOMPARE(actual.ki.dwExtraInfo, expected.ki.dwExtraInfo);
    }
    void absolute_data() {
        QTest::addColumn<int>("scene");
        for (int scene = 0; scene < 4096; ++scene)
            QTest::newRow(qPrintable(QString::number(scene))) << scene;
    }
    void absolute() {
        QFETCH(int, scene);
        const std::array<int, 10> widths{-1920, -1, 0, 1, 2, 3, 640, 1920, 3840, 7680};
        const int w = widths[size_t(scene) % widths.size()];
        const int h = widths[size_t(scene / 10) % widths.size()];
        const QRect desktop(scene % 3 ? -1920 : 33, scene % 5 ? -1080 : 17, w, h);
        const std::array<QPoint, 8> points{desktop.topLeft(),
                                           desktop.bottomRight(),
                                           desktop.center(),
                                           desktop.topLeft() + QPoint(1, 1),
                                           desktop.bottomRight() + QPoint(1, 1),
                                           desktop.topLeft() - QPoint(1, 1),
                                           desktop.topLeft() + QPoint(127, 251),
                                           QPoint(0, 0)};
        const QPoint point = points[size_t(scene / 100) % points.size()];
        QPoint actual, expected;
        QString actualError, expectedError;
        try {
            actual = desktopAbsolute(point, desktop);
        } catch (const std::exception &error) {
            actualError = errorText(error);
        }
        try {
            expected = desktop_input_reference::desktopAbsolute(point, desktop);
        } catch (const std::exception &error) {
            expectedError = errorText(error);
        }
        QCOMPARE(actualError, expectedError);
        QCOMPARE(actual, expected);
    }
};
QTEST_GUILESS_MAIN(DesktopInputContracts)
#include "test_desktop_input_contract.moc"
