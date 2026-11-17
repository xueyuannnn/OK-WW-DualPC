#ifndef Q_MOC_RUN
#include "reference_update_version.hpp"
#endif
#include <QtTest>
#include <array>
#include <random>
using namespace okww;
template <class T> QJsonObject parsed(QString text) {
    const auto value = T::parse(text);
    if (!value)
        return {};
    return {{"major", QString::number(value->major)},
            {"minor", QString::number(value->minor)},
            {"patch", QString::number(value->patch)},
            {"pre", QJsonArray::fromStringList(value->pre)}};
}
class UpdateVersionContracts final : public QObject {
    Q_OBJECT
  private slots:
    void parse_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 8192; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void parse() {
        QFETCH(int, scene);
        const QStringList cores{"0.0.0",
                                "1.2.3",
                                "0.2.0",
                                "18446744073709551615.0.0",
                                "18446744073709551616.0.0",
                                "9007199254740993.0.0",
                                "1.18446744073709551615.0",
                                "1.2.18446744073709551615",
                                "01.2.3",
                                "1.02.3",
                                "1.2.03",
                                "1.2",
                                "-1.2.3",
                                "1.2.3.4",
                                "1.2.3",
                                ""};
        const QStringList pre{"",         "-beta.8",   "-01",
                              "-0000",    "--",        "---",
                              "-0",       "-9",        "-9999999999999999999999999999999999",
                              "-alpha.0", "-alpha..0", "-a-b",
                              "-α",       "-a_1",      "-0abc",
                              "-a.00"};
        const QStringList build{"", "+build.001", "+001", "+", "+a..b", "+-", "++x", "+metadata-1"};
        const QStringList prefixes{"", "v", "vv", "V", " "}, tails{"", "\n", "\r\n", "\n\n", " ", "\t"};
        QString text = prefixes[(scene / 13) % prefixes.size()] + cores[scene % cores.size()] +
                       pre[(scene / 16) % pre.size()] + build[(scene / 256) % build.size()] +
                       tails[(scene / 11) % tails.size()];
        if (scene % 47 == 0)
            text += QChar(char16_t(0xd800));
        if (scene % 53 == 0)
            text += QChar(char16_t(0));
        if (scene == 0)
            text = QString{};
        if (scene == 1) text="1.2.3\n";
        if (scene == 2) text="1.2.3\r\n";
        if (scene == 3) text="v18446744073709551615.0.0-999999999999999999999999+001";
        QCOMPARE(parsed<update::Version>(text), parsed<update::version_reference::OriginalVersion>(text));
    }
    void compare_data() { parse_data(); }
    void compare() {
        QFETCH(int, scene);
        std::mt19937 generator{uint32_t(scene)};
        const std::array<quint64, 8> numbers{0,
                                             1,
                                             2,
                                             10,
                                             UINT64_C(9007199254740991),
                                             UINT64_C(9007199254740992),
                                             UINT64_C(9007199254740993),
                                             UINT64_MAX};
        update::Version a, b;
        update::version_reference::OriginalVersion oldA, oldB;
        a.major = oldA.major = numbers[generator() % numbers.size()];
        a.minor = oldA.minor = numbers[generator() % numbers.size()];
        a.patch = oldA.patch = numbers[generator() % numbers.size()];
        b.major = oldB.major = scene % 7 ? a.major : numbers[generator() % numbers.size()];
        b.minor = oldB.minor = scene % 5 ? a.minor : numbers[generator() % numbers.size()];
        b.patch = oldB.patch = scene % 3 ? a.patch : numbers[generator() % numbers.size()];
        const QStringList parts{"",
                                "0",
                                "00",
                                "01",
                                "1",
                                "10",
                                "999999999999999999999999999999",
                                "alpha",
                                "ALPHA",
                                "beta",
                                "-",
                                "α",
                                QString::fromUtf8("😀"),
                                QString(QChar(char16_t(0xd800))),
                                "１２",
                                QString(QChar(char16_t(0)))};
        for (int i = 0; i < scene % 9; ++i)
            a.pre << parts[generator() % parts.size()];
        b.pre = a.pre;
        if (scene % 2 && !b.pre.empty())
            b.pre[scene % b.pre.size()] = parts[generator() % parts.size()];
        if (scene % 11 == 0)
            b.pre.append(parts[generator() % parts.size()]);
        if (scene % 13 == 0)
            b.pre.clear();
        oldA.pre = a.pre;
        oldB.pre = b.pre;
        QCOMPARE(a.compare(b), oldA.compare(oldB));
        QCOMPARE(b.compare(a), oldB.compare(oldA));
    }
};
QTEST_GUILESS_MAIN(UpdateVersionContracts)
#include "test_update_version_contract.moc"
