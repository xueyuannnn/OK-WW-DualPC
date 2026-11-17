#include "reference_workshop_state.hpp"
#include "team_replay.hpp"
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
namespace {
struct StateHost final : test::TeamReplay {
    mutable QJsonArray calls;
    explicit StateHost(int scene) : TeamReplay({{"names", QJsonArray{"Cartethyia", "Ciaccona", "Chisa"}}}) {
        auto party = TeamReplay::characters();
        for (int i = 0; i < party.size(); ++i) {
            auto &s = party[i]->state;
            s.role = Role((scene + i) % 4);
            s.currentCon = ((scene + i) % 129) / 128.;
            s.buffTime = (scene + i) * .13;
            s.hasIntro = (scene + i) % 2;
            s.hasSubDpsIntro = (scene + i) % 3;
            party[i]->checkFOnSwitch = (scene + i) % 5;
            for (int j = 0; j < 9; ++j)
                s.*workshop_state_reference::commonTimes[j].second =
                    (scene + i + j) % 5 ? 50.13 + j * 1.72 : -2.9;
        }
        for (int i = 0; i < scene % 7; ++i)
            freezes.add(100, 90 + i, .1 * i, .02 * (scene % 9));
        combatStart = scene % 13 ? -12.75 : 12.25;
        inLiberation = scene % 2;
    }
    double now() const override {
        calls.append("now");
        return TeamReplay::now();
    }
    QVector<Character *> characters() override {
        calls.append("party");
        return TeamReplay::characters();
    }
    QJsonObject raw() {
        QJsonArray chars;
        for (auto *c : TeamReplay::characters()) {
            auto &s = c->state;
            QJsonObject v{{"index", s.index},           {"current", s.current}, {"intro", s.hasIntro},
                          {"sub", s.hasSubDpsIntro},    {"con", s.currentCon},  {"buff", s.buffTime},
                          {"check", c->checkFOnSwitch}, {"role", int(s.role)}};
            for (const auto &[key, member] : workshop_state_reference::commonTimes)
                v[key] = s.*member;
            chars.append(v);
        }
        QJsonArray timeline;
        for (const auto &f : freezes.entries())
            timeline.append(QJsonArray{f.start, f.duration, f.threshold});
        return {
            {"chars", chars}, {"freezes", timeline}, {"start", combatStart}, {"liberation", inLiberation}};
    }
};
QJsonObject stateValue(StateHost &host, int scene) {
    auto out = workshop_state_reference::workshopCombatState(host);
    auto chars = out["chars"].toArray();
    for (int i = 0; i < chars.size(); ++i) {
        auto c = chars[i].toObject();
        c["current"] = i == scene % 3;
        c["current_con"] = (scene % 101) * .01;
        c["buff_time"] = scene * .75;
        c["role"] = QStringList{"Healer", "MainDps", "SubDps"}[(i + scene) % 3];
        chars[i] = c;
    }
    if (scene % 2) {
        const QJsonValue first = chars[0];
        chars[0] = chars[2];
        chars[2] = first;
    }
    auto c = chars[scene % 3].toObject();
    switch (scene % 32) {
    case 0:
        break;
    case 1:
        chars.removeLast();
        break;
    case 2:
        c["index"] = chars[(scene + 1) % 3].toObject()["index"];
        break;
    case 3:
        c["index"] = 1.5;
        break;
    case 4:
        c["index"] = true;
        break;
    case 5:
        c["role"] = "Tank";
        break;
    case 6:
        c["current"] = 1;
        break;
    case 7:
        c["intro"] = 1;
        break;
    case 8:
        c["sub_intro"] = 1;
        break;
    case 9:
        c["current_con"] = -.01;
        break;
    case 10:
        c["current_con"] = 1.01;
        break;
    case 11:
        c["buff_time"] = -1;
        break;
    case 12:
        c["buff_time"] = 86400.01;
        break;
    case 13:
        c["buff_time"] = "1";
        break;
    case 14:
        c.remove("last_res");
        break;
    case 15:
        c["check_f"] = "false";
        break;
    case 16:
        c["last_buff"] = true;
        break;
    case 17:
        out["combat_age"] = "0";
        break;
    case 18:
        out["in_liberation"] = 0;
        break;
    case 19:
        out["freezes"] = QJsonArray{QJsonObject{{"age", 1}, {"duration", -.01}, {"threshold", 0}}};
        break;
    case 20:
        out["freezes"] = QJsonArray{QJsonObject{{"age", 1}, {"duration", 600.01}, {"threshold", 0}}};
        break;
    case 21:
        out["freezes"] = QJsonArray{QJsonObject{{"age", 1}, {"duration", 600}, {"threshold", true}}};
        break;
    case 22:
        out["freezes"] = QJsonArray{QJsonObject{{"age", -.1}, {"duration", 600}, {"threshold", -1}}};
        break;
    case 23: {
        QJsonArray a;
        for (int i = 0; i < 2049; ++i)
            a.append(QJsonObject{{"age", 1}, {"duration", 0}, {"threshold", 0}});
        out["freezes"] = a;
        break;
    }
    case 24:
        c["index"] = 999;
        break;
    case 25:
        c["buff_time"] = 86400;
        c["current_con"] = 1;
        break;
    case 26:
        c["buff_time"] = 0;
        c["current_con"] = 0;
        break;
    case 27:
        c["last_perform"] = 1e300;
        break;
    case 28:
        c["last_switch"] = -1e300;
        break;
    case 29:
        out["combat_age"] = -2.75;
        break;
    case 30:
        c.remove("role");
        break;
    case 31:
        out["freezes"] = QJsonArray{};
        break;
    }
    if (scene % 32 != 1)
        chars[scene % 3] = c;
    out["chars"] = chars;
    return out;
}
QJsonObject applyOutcome(int scene, bool rust) {
    StateHost host(scene);
    auto value = stateValue(host, scene);
    host.calls = {};
    QJsonObject result;
    try {
        if (rust)
            applyWorkshopCombatState(host, value);
        else
            workshop_state_reference::applyWorkshopCombatState(host, value);
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    }
    result["raw"] = host.raw();
    result["calls"] = host.calls;
    return result;
}
QString handshake(QJsonObject value, bool rust) {
    try {
        if (rust)
            validateWorkshopHandshake(value);
        else
            workshop_state_reference::validateWorkshopHandshake(value);
        return "ok";
    } catch (const std::exception &e) {
        return errorText(e);
    }
}
} // namespace
class WorkshopStateContracts final : public QObject {
    Q_OBJECT
  private slots:
    void states_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 2048; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void states() {
        QFETCH(int, scene);
        QCOMPARE(applyOutcome(scene, true), applyOutcome(scene, false));
    }
    void snapshot_data() { states_data(); }
    void snapshot() {
        QFETCH(int, scene);
        StateHost current(scene), original(scene);
        QCOMPARE(workshopCombatState(current), workshop_state_reference::workshopCombatState(original));
        QCOMPARE(current.calls, original.calls);
    }
    void handshake_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 256; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void handshake() {
        QFETCH(int, scene);
        const QStringList names{"request-identity", "combat-state-ages", "immutable-frame-cache",
                                "bridge-metrics",   "character-policy",  "task-config-context"};
        auto caps = QJsonArray::fromStringList(names);
        for (int i = 0; i < 6; ++i)
            if (scene & (1 << i))
                caps[i] = scene % 2 ? QJsonValue{} : QJsonValue(names[0]);
        QJsonObject hello{{"event", scene & 64 ? "other" : "ready"},
                          {"protocol", scene & 128 ? 1 : 2},
                          {"api", 1},
                          {"capabilities", caps}};
        QCOMPARE(::handshake(hello, true), ::handshake(hello, false));
    }
};
QTEST_GUILESS_MAIN(WorkshopStateContracts)
#include "test_workshop_state_contract.moc"
