#ifndef Q_MOC_RUN
#include "notification_clipboard_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww::runtime;
class NotificationClipboardContracts final : public QObject {
    Q_OBJECT
  private slots:
    void ownership_data() {
        QTest::addColumn<int>("scene");
        for (int i = 1; i <= 1024; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void ownership() {
        QFETCH(int, scene);
        QCOMPARE((notice_clipboard_test::outcome<notice_clipboard_test::Current>(scene)),
                 (notice_clipboard_test::outcome<notice_clipboard_test::Original>(scene)));
    }
};
QTEST_GUILESS_MAIN(NotificationClipboardContracts)
#include "test_notification_clipboard_contract.moc"
