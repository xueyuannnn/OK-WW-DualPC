#include "domain_replay.hpp"
#include <QtTest>
using namespace okww::runtime;
class DomainContracts : public QObject {
    Q_OBJECT
  private slots:
    void flow_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 1024; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void flow() {
        QFETCH(int, scene);
        const auto expected = domain_test::trace(scene, false), actual = domain_test::trace(scene, true);
        QVERIFY2(actual == expected,
                 qPrintable(
                     QString::fromUtf8(QJsonDocument(QJsonObject{{"expected", expected}, {"actual", actual}})
                                           .toJson(QJsonDocument::Compact))));
    }
    void counts_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 512; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void counts() {
        QFETCH(int, scene);
        const int counts[]{-1, 0, 1, 5, 100000, 100001};
        const int total = (scene / 3) % 300 - 1, cost = (scene / 7) % 80 - 1, requested = counts[scene % 6];
        QString a, b;
        int expected = 0, actual = 0;
        try {
            expected = domain_test::domainCountLimit(requested, total, cost);
        } catch (const std::exception &e) {
            a = QString::fromUtf8(e.what());
        }
        try {
            actual = domainCountLimit(requested, total, cost);
        } catch (const std::exception &e) {
            b = QString::fromUtf8(e.what());
        }
        QCOMPARE(a, b);
        QCOMPARE(actual, expected);
        for (int kind = -1; kind < 4; ++kind)
            QCOMPARE(domainCost(DomainKind(kind)), domain_test::domainCost(domain_test::DomainKind(kind)));
    }
};
QTEST_GUILESS_MAIN(DomainContracts)
#include "test_domain_contract.moc"
