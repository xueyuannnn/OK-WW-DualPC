#ifndef Q_MOC_RUN
#include "notification_windows_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww::runtime;
class NotificationWindowsContracts final : public QObject {
    Q_OBJECT
  private slots:
    void windows_data() {
        QTest::addColumn<int>("scene");
        QTest::addColumn<int>("method");
        for (int i = 1; i <= 512; ++i)
            for (int method = 0; method < 6; ++method)
                QTest::newRow(qPrintable(QString("%1-%2").arg(i).arg(method))) << i << method;
    }
    void windows() {
        QFETCH(int, scene);
        QFETCH(int, method);
        QCOMPARE(notice_windows_test::outcome(scene, method, true),
                 notice_windows_test::outcome(scene, method, false));
    }
};
QTEST_GUILESS_MAIN(NotificationWindowsContracts)
#include "test_notification_windows_contract.moc"
