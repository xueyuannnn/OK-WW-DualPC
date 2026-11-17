#include "scheduler_context_replay.hpp"
#include "scheduler_sdk_policy_replay.hpp"
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
class SchedulerContracts final : public QObject {
    Q_OBJECT
  private slots:
    void sdk_data() {
        QTest::addColumn<int>("kind");
        QTest::addColumn<int>("scene");
        for (int k = 0; k < 3; ++k)
            for (int s = 0; s < 256; ++s)
                QTest::newRow(qPrintable(QString("%1-%2").arg(k).arg(s))) << k << s;
    }
    void sdk() {
        QFETCH(int, kind);
        QFETCH(int, scene);
        QCOMPARE(scheduler_test::sdkPolicy(kind, scene, false), scheduler_test::sdkPolicy(kind, scene, true));
    }
    void quoting_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 2048; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void quoting() {
        QFETCH(int, scene);
        const QStringList units{"a",
                                " ",
                                "\\",
                                "\"",
                                QStringLiteral("测试"),
                                "\n",
                                QString(QChar(char16_t(0xd800))),
                                QString(QChar(char16_t(0))),
                                ""};
        QString text;
        for (int i = 0; i < scene % 31; ++i)
            text += units[(scene + i * 7) % units.size()];
        QCOMPARE(Scheduler::quoteArgument(text), scheduler_reference::OriginalScheduler::quoteArgument(text));
    }
    void definitions_data() { quoting_data(); }
    void definitions() {
        QFETCH(int, scene);
        scheduler_test::Fixture fixture(scene);
        QCOMPARE(scheduler_test::xml<Scheduler>(fixture, scene, "new"),
                 scheduler_test::xml<scheduler_reference::OriginalScheduler>(fixture, scene, "old"));
    }
    void lifecycle_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 256; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void lifecycle() {
        QFETCH(int, scene);
        scheduler_test::Fixture fixture(scene);
        QCOMPARE(scheduler_test::operations<Scheduler>(fixture, scene, "new"),
                 scheduler_test::operations<scheduler_reference::OriginalScheduler>(fixture, scene, "old"));
    }
    void currentUser() {
        QCOMPARE(Scheduler::currentUser(), scheduler_reference::OriginalScheduler::currentUser());
    }
};
QTEST_GUILESS_MAIN(SchedulerContracts)
#include "test_scheduler_contract.moc"
