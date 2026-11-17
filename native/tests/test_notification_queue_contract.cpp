#ifndef Q_MOC_RUN
#include "notification_queue_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww::runtime;
class NotificationQueueContracts final : public QObject {
    Q_OBJECT
  private slots:
    void sequencing_data() {
        QTest::addColumn<int>("scene");
        for (int i = 1; i <= 1024; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void sequencing() {
        QFETCH(int, scene);
        QCOMPARE(notice_queue_test::outcome(scene, true), notice_queue_test::outcome(scene, false));
    }
};
QTEST_GUILESS_MAIN(NotificationQueueContracts)
#include "test_notification_queue_contract.moc"
