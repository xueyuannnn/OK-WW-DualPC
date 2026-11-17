#include "team_macro_context_replay.hpp"
#include <QtTest>
using namespace okww::runtime;
class TeamMacroContracts final : public QObject {
    Q_OBJECT
  private slots:
    void sequences_data() {
        QTest::addColumn<int>("method");
        QTest::addColumn<int>("scene");
        for (int m = 0; m < 23; ++m)
            for (int s = 0; s < 128; ++s)
                QTest::newRow(qPrintable(QString("%1-%2").arg(m).arg(s))) << m << s;
    }
    void sequences() {
        QFETCH(int, method);
        QFETCH(int, scene);
        QCOMPARE(macro_test::replay<TeamMacro>(method, scene),
                 macro_test::replay<macro_reference::OriginalTeamMacro>(method, scene));
    }
};
QTEST_GUILESS_MAIN(TeamMacroContracts)
#include "test_team_macro_contract.moc"
