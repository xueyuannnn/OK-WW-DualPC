#include "nightmare_replay.hpp"
#include <QtTest>
using namespace okww::runtime;
class NightmareContracts : public QObject {
    Q_OBJECT
  private slots:
    void flow_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 512; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void flow() {
        QFETCH(int, scene);
        const auto expected = nightmare_test::trace(scene, false),
                   actual = nightmare_test::trace(scene, true);
        QVERIFY2(actual == expected,
                 qPrintable(
                     QString::fromUtf8(QJsonDocument(QJsonObject{{"expected", expected}, {"actual", actual}})
                                           .toJson(QJsonDocument::Compact))));
    }
    void target_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 1024; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void target() {
        QFETCH(int, scene);
        const QStringList values{"12/24", "24/24", "1 / 36 and 12／48", "13/41", "1/25", "112/24",
                                 "0/00",  "3/48"};
        QVector<TextLine> lines;
        for (int i = 0; i < scene % 5; ++i)
            lines.append({values[(scene + i) % 8], {700, i * 40 + scene % 300, 60, scene % 21}, .9});
        const QSize size(1920, scene % 7 ? 1080 : 0);
        const int tab = scene % 6 - 1;
        QSet<QString> unreachable;
        if (scene & 16)
            unreachable.insert(
                QString("go_nest:24:%1")
                    .arg(okww::runtime::pixelRound((scene % 300 + scene % 21 / 2.) / 1080 / .02)));
        QString a, b;
        QJsonValue expected, actual;
        try {
            const auto value = nightmare_test::referenceNextNestTarget(lines, size, tab, unreachable);
            if (value)
                expected = QJsonArray{value->point.x(), value->point.y(), value->key, value->tab};
        } catch (const std::exception &e) {
            a = QString::fromUtf8(e.what());
        }
        try {
            const auto value = nextNestTarget(lines, size, tab, unreachable);
            if (value)
                actual = QJsonArray{value->point.x(), value->point.y(), value->key, value->tab};
        } catch (const std::exception &e) {
            b = QString::fromUtf8(e.what());
        }
        QCOMPARE(a, b);
        QCOMPARE(actual, expected);
    }
};
QTEST_GUILESS_MAIN(NightmareContracts)
#include "test_nightmare_contract.moc"
