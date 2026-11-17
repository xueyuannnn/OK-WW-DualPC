#ifndef Q_MOC_RUN
#include "capture_dshow_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww;
class CaptureDshowContracts final : public QObject {
    Q_OBJECT
  private slots:
    void graph_data() {
        QTest::addColumn<int>("scene");
        for (int scene = 0; scene < 1536; ++scene)
            QTest::newRow(qPrintable(QString::number(scene))) << scene;
    }
    void graph() {
        QFETCH(int, scene);
        QCOMPARE(capture_dshow_test::outcome(scene, true), capture_dshow_test::outcome(scene, false));
    }
    void layout() {
        for (int width : {1, 2, 3, 7, 640, 1919, 1920, 2560, 3840, 7680})
            for (int format = 0; format < 8; ++format) {
                auto fmt = PixelFormat(format);
                const int stride =
                    fmt == PixelFormat::BGR24    ? ((width * 3 + 3) & ~3)
                    : fmt == PixelFormat::BGRA32 ? width * 4
                    : fmt == PixelFormat::YUY2 || fmt == PixelFormat::UYVY || fmt == PixelFormat::P010
                        ? width * 2
                        : width;
                QCOMPARE(rust::okww_rs_capture_stride(width, format), stride);
                for (int height : {-1080, 0, 1080})
                    QCOMPARE(bool(rust::okww_rs_capture_bottom(height, format)),
                             height > 0 && (fmt == PixelFormat::BGR24 || fmt == PixelFormat::BGRA32));
            }
    }
};
QTEST_GUILESS_MAIN(CaptureDshowContracts)
#include "test_capture_dshow_contract.moc"
