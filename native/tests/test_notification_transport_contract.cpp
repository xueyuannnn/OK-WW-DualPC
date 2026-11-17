#ifndef Q_MOC_RUN
#include "notification_transport_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww::runtime;
class NotificationTransportContracts final : public QObject {
    Q_OBJECT
  private slots:
    void smtp_data() {
        QTest::addColumn<int>("scene");
        for (int i = 1; i <= 512; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void smtp() {
        QFETCH(int, scene);
        notice_transport_test::Scenario current(scene), original(scene);
        QCOMPARE(current.outcome(true), original.outcome(false));
    }
};
QTEST_GUILESS_MAIN(NotificationTransportContracts)
#include "test_notification_transport_contract.moc"
