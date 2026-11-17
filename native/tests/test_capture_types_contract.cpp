#ifndef Q_MOC_RUN
#include "capture_types_policy_replay.hpp"
#endif
#include <QtTest>
#include <random>
using namespace okww;
class CaptureTypesContracts final : public QObject {
    Q_OBJECT
  private slots:
    void media_data() {
        QTest::addColumn<int>("scene");
        for (int scene = 0; scene < 1024; ++scene)
            QTest::newRow(qPrintable(QString::number(scene))) << scene;
    }
    void media() {
        QFETCH(int, scene);
        QCOMPARE(capture_types_test::mediaOutcome(scene, true),
                 capture_types_test::mediaOutcome(scene, false));
    }
    void math_data() {
        QTest::addColumn<int>("seed");
        for (int seed = 0; seed < 4096; ++seed)
            QTest::newRow(qPrintable(QString::number(seed))) << seed;
    }
    void math() {
        QFETCH(int, seed);
        std::mt19937_64 rng(seed);
        const DWORD value = seed % 3 == 0 ? DWORD(0x20202020 + (rng() & 0x5f5f5f5f)) : DWORD(rng());
        constexpr int bits[]{0, 8, 16, 24, 32, -1};
        auto current = capture_types_test::current::fourcc(value, bits[seed % 6]);
        auto original = capture_types_test::original::fourcc(value, bits[seed % 6]);
        QCOMPARE(current, original);
        QCOMPARE(current.isNull(), original.isNull());
        const i64 fixed[]{0,
                          1,
                          -1,
                          41667,
                          83333,
                          166667,
                          333333,
                          10000000,
                          std::numeric_limits<i64>::min(),
                          std::numeric_limits<i64>::max()};
        const i64 period = seed % 3 == 0 ? fixed[seed % 10] : std::bit_cast<i64>(rng());
        QCOMPARE(std::bit_cast<uint64_t>(capture_types_test::current::rate(period)),
                 std::bit_cast<uint64_t>(capture_types_test::original::rate(period)));
    }
    void formats() {
        const QStringList names{"NV12", "P010", "YUY2",  "UYVY",  "I420", "IYUV", "RGB4",   "RGB3",
                                "MJPG", "nv12", " NV12", "NV12 ", "中文", "",     QString()};
        for (const auto &name : names)
            QCOMPARE(capture_types_test::formatOutcome(name, true),
                     capture_types_test::formatOutcome(name, false));
        const QChar raw[]{QChar('N'), QChar('V'), QChar('1'), QChar('2'), QChar('X')};
        const auto borrowed = QString::fromRawData(raw, 4);
        const auto *before = borrowed.constData();
        QCOMPARE(capture_types_test::formatOutcome(borrowed, true),
                 capture_types_test::formatOutcome(borrowed, false));
        QCOMPARE(borrowed.constData(), before);
    }
};
QTEST_GUILESS_MAIN(CaptureTypesContracts)
#include "test_capture_types_contract.moc"
