#ifndef Q_MOC_RUN
#include "notification_messenger_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww::runtime;
class NotificationMessengerContracts final : public QObject {
    Q_OBJECT
  private slots:
    void operations_data() {
        QTest::addColumn<int>("scene");
        QTest::addColumn<int>("method");
        for (int i = 1; i <= 128; ++i)
            for (int method = 0; method < 12; ++method)
                QTest::newRow(qPrintable(QString("%1-%2").arg(i).arg(method))) << i << method;
    }
    void operations() {
        QFETCH(int, scene);
        QFETCH(int, method);
        QCOMPARE((notice_messenger_test::outcome<notice_messenger_test::Current>(scene, method)),
                 (notice_messenger_test::outcome<notice_messenger_test::Original>(scene, method)));
    }
};
QTEST_GUILESS_MAIN(NotificationMessengerContracts)
#include "test_notification_messenger_contract.moc"
