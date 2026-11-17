#ifndef Q_MOC_RUN
#include "notification_delivery_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww::runtime;
class NotificationDeliveryContracts final : public QObject {
    Q_OBJECT
  private slots:
    void delivery_data() {
        QTest::addColumn<int>("scene");
        QTest::addColumn<int>("method");
        for (int i = 1; i <= 256; ++i)
            for (int method = 0; method < 3; ++method)
                QTest::newRow(qPrintable(QString("%1-%2").arg(i).arg(method))) << i << method;
    }
    void delivery() {
        QFETCH(int, scene);
        QFETCH(int, method);
        QCOMPARE((notice_delivery_test::outcome<notice_delivery_test::Current>(scene, method)),
                 (notice_delivery_test::outcome<notice_delivery_test::Original>(scene, method)));
    }
};
QTEST_GUILESS_MAIN(NotificationDeliveryContracts)
#include "test_notification_delivery_contract.moc"
