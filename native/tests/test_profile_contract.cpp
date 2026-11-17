#include "profile_context_replay.hpp"
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
class ProfileContracts final : public QObject {
    Q_OBJECT
  private slots:
    void configuration_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 2048; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void configuration() {
        QFETCH(int, scene);
        profile_test::Fixture fixture(scene);
        const auto expected = profile_test::replay<profile_reference::OriginalProfile>(fixture, "old", scene);
        const auto actual = profile_test::replay<Profile>(fixture, "new", scene);
        QCOMPARE(actual, expected);
    }
};
QTEST_GUILESS_MAIN(ProfileContracts)
#include "test_profile_contract.moc"
