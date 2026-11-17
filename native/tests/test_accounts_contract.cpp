#include "reference_accounts.hpp"
#include "workflow_scene_replay.hpp"
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
namespace {
template <class AccountTask> QJsonObject trace(int scene) {
    Scenes s({640, 360});
    s.profile.set("Monthly Card Config", "Check Monthly Card", false);
    s.profile.set("Task Recovery", "Recovery Retry Limit", 1);
    s.profile.set("MultiAccountDailyTask", "Maximum Accounts", scene & 4 ? 1 : 20);
    s.profile.set("MultiAccountDailyTask", "Account List Pages", 1 + scene % 3);
    s.frame("menu", {"esc_setting"});
    s.frame("confirm-logout", {"confirm_btn_hcenter_vcenter"});
    s.frame("account-login", {});
    s.frame("account-list", {});
    const int count = 1 + scene % 4;
    QStringList names;
    for (int i = 0; i < count; ++i)
        names.append(QString(" User%1****@M0IL.Con ").arg(i));
    int current = 0, completed = 0, scroll = 0;
    const auto loginWords = [&] {
        s.words["account-login"] = {s.word(names[current], .4, .42, .2),
                                    s.word(scene & 8 ? "Log In" : "登录", .45, .68)};
    };
    loginWords();
    for (int i = count - 1; i >= 0; --i)
        s.words["account-list"].append(s.word(names[i], .4, .3 + i * .08, .2));
    if (scene & 16)
        s.words["account-list"].append(s.word("not an account", .2, .2));
    QJsonArray reads, notes;
    s.c.info = [&](QString k, QString v) { notes.append(QJsonArray{k, v}); };
    s.textOverride = [&](QRect area, QVector<TextLine> values) {
        reads.append(QJsonArray{s.scene, area.x(), area.y(), area.width(), area.height()});
        return values;
    };
    s.action = [&](QJsonObject event) {
        if (s.keyUp(event, Input::keyCode("esc"))) {
            if (s.scene == "world")
                s.scene = "menu";
            else if (s.scene == "account-list")
                s.scene = "account-login";
        }
        if (event["op"] == "scroll")
            ++scroll;
        if (!s.leftUp(event))
            return;
        if (s.scene == "menu")
            s.scene = "confirm-logout";
        else if (s.scene == "confirm-logout") {
            loginWords();
            s.scene = "account-login";
        } else if (s.scene == "account-login")
            s.scene = s.pointer.y() < s.size.height() * .6 ? "account-list" : "world";
        else if (s.scene == "account-list") {
            current = std::clamp(int(std::round((double(s.pointer.y()) / s.size.height() - .315) / .08)), 0,
                                 count - 1);
            loginWords();
            s.scene = "account-login";
        }
    };
    s.show("world");
    s.c.task = "MultiAccountDailyTask";
    QJsonObject result;
    try {
        AccountTask(s.c).run([&] {
            ++completed;
            if (scene & 32 && completed == 2)
                throw Cancelled{};
        });
        result["error"] = "";
    } catch (const Cancelled &) {
        result["error"] = "cancelled";
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    }
    QJsonArray events;
    for (auto e : s.events)
        events.append(e);
    result["events"] = events;
    result["notes"] = notes;
    result["reads"] = reads;
    result["completed"] = completed;
    result["current"] = current;
    result["time"] = s.clock.time;
    result["scroll"] = scroll;
    result["scene"] = s.scene;
    result["idle"] = s.input.idle();
    return result;
}
} // namespace
class AccountContracts : public QObject {
    Q_OBJECT
  private slots:
    void cycle_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 2048; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void cycle() {
        QFETCH(int, scene);
        const QStringList names{"User0@mail.CON",    "userO@MAIL.com", "   ",       " A\tB\nC@mail.com ",
                                "甲 乙@example.con", "Masked***",      "***0 .con", "ÜSER@DOMAIN.COM"};
        cv::RNG random(9875 + scene);
        AccountCycle actual;
        reference_accounts::AccountCycle expected;
        for (int i = 0; i < scene % 7; ++i) {
            const auto name = names[random.uniform(0, int(names.size()))];
            QString a, b;
            try {
                actual.complete(name);
            } catch (const std::exception &e) {
                a = errorText(e);
            }
            try {
                expected.complete(name);
            } catch (const std::exception &e) {
                b = errorText(e);
            }
            QCOMPARE(a, b);
            QCOMPARE(actual.count(), expected.count());
        }
        QVector<TextLine> accounts;
        for (int i = 0; i < (scene / 7) % 6; ++i) {
            const auto name = names[random.uniform(0, int(names.size()))];
            accounts.append({name, {10, 20 * i, 100, 10}, .9});
            QCOMPARE(normalizeAccount(name), reference_accounts::normalizeAccount(name));
            QCOMPARE(actual.completed(name), expected.completed(name));
        }
        const auto a = actual.next(accounts), b = expected.next(accounts);
        QCOMPARE(a ? QJsonValue(a->json()) : QJsonValue(), b ? QJsonValue(b->json()) : QJsonValue());
    }
    void lifecycle_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 64; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void lifecycle() {
        QFETCH(int, scene);
        try {
            const auto expected = trace<reference_accounts::MultiAccountDaily>(scene),
                       actual = trace<MultiAccountDaily>(scene);
            QVERIFY2(actual == expected,
                     qPrintable(QString::fromUtf8(
                         QJsonDocument(QJsonObject{{"actual", actual}, {"expected", expected}})
                             .toJson(QJsonDocument::Compact))));
        } catch (const std::exception &e) {
            QFAIL(qPrintable(errorText(e)));
        }
    }
};
QTEST_GUILESS_MAIN(AccountContracts)
#include "test_accounts_contract.moc"
