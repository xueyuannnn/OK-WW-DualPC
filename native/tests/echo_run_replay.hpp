#pragma once
#include "runtime/echo_farm.hpp"
#include "runtime/workflow_abi.hpp"
namespace okww::runtime::echo_run_test {
struct Probe;
struct Profile {
    Probe &p;
    QJsonObject group(QString);
};
struct Context {
    Probe &p;
    Profile profile;
    QString task = "outer";
    QJsonObject taskOverrides{{"old", true}};
    std::function<void(QString, QString)> info;
    void sleep(double);
};
struct TaskScope {
    Context &c;
    QString previous;
    QJsonObject overrides;
    TaskScope(Context &, QString, QJsonObject);
    ~TaskScope();
};
struct Combat {
    Probe &p;
    std::function<bool()> onCombatCheck, combatEndCondition;
    double targetEnemyTimeout = 2.7;
    void recoverStartingPopup();
    bool hasHealthBar();
};
struct World {
    Probe &p;
    bool pick();
    QVector<Match> echoes();
};
struct Probe {
    int scene, drops_ = 0, totalCompleted_ = 0, calls = 0;
    bool enhanced_ = false, realm_ = false, treatAsWorld_ = true, justEntered_ = true;
    QString aimBoss_ = "old-boss";
    QJsonObject config_, saved;
    QJsonArray events;
    Context c_;
    Combat combat_;
    World world_;
    Probe(int value) : scene(value), realm_(value & 2), c_{*this, {*this}}, combat_{*this}, world_{*this} {
        const QJsonValue counts[]{0, 3, -1, 1.5, QString("2"), 100001, 1};
        saved = {{"Repeat Farm Count", counts[value % 7]},
                 {"Farm Mode", value & 2 ? "Cycle All Dungeons" : "Single Boss"},
                 {"Advanced Skill Material Mode", bool(value & 4)},
                 {"Rounds per Dungeon", value % 5 == 0 ? QJsonValue(1.5) : QJsonValue(1 + value % 3)},
                 {"Cycle Start Category", value & 512 ? "Weekly Challenge" : "Boss Challenge"}};
        if (value & 8)
            c_.info = [&](QString k, QString v) { events.append(QJsonArray{"info", k, v}); };
        combat_.onCombatCheck = [&] {
            events.append("old-check");
            return true;
        };
        combat_.combatEndCondition = [&] {
            events.append("old-end");
            return false;
        };
    }
    void checkRealm(double value) {
        events.append(QJsonArray{"realm-check", value});
        if (scene % 41 == 0)
            throw NavigationFailure("callback failed");
    }
    int single() {
        events.append(QJsonArray{"single", config_});
        ++calls;
        realm_ = scene & 32;
        combat_.targetEnemyTimeout = 7.;
        if (scene % 17 == 0)
            throw NavigationFailure("single failed");
        if (combat_.onCombatCheck)
            events.append(QJsonArray{"check-result", combat_.onCombatCheck()});
        if (combat_.combatEndCondition)
            events.append(QJsonArray{"end-result", combat_.combatEndCondition()});
        const int count = config_["Repeat Farm Count"].toInt();
        totalCompleted_ += count;
        return scene & 16 ? std::max(0, count - 1) : count;
    }
    int reference(bool enhanced, const QJsonObject &supplied)
#include "reference_echo_run_body.hpp"
        int candidate(bool enhanced, const QJsonObject &supplied) {
        std::optional<TaskScope> scope;
        QVector<EchoTarget> targets;
        EchoTarget target;
        std::function<bool()> oldCheck, oldEnd;
        double oldTimeout = 0;
        std::function<void(rust::WorkflowOp, const double *, double *)> action;
        action = [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
#include "echo_run_host_cases.hpp"
            default:
                fail("Unknown echo run host action");
            }
        };
        const std::array args{double(enhanced)};
        return int(rust::workflowRun(rust::WorkflowMethod::EchoRun, args, action)[0]);
    }
};
inline QJsonObject Profile::group(QString name) {
    p.events.append(QJsonArray{"config", name});
    return p.saved;
}
inline void Context::sleep(double seconds) {
    p.events.append(QJsonArray{"sleep", seconds});
    if (p.scene % 43 == 0)
        throw Cancelled{};
}
inline TaskScope::TaskScope(Context &context, QString name, QJsonObject config)
    : c(context), previous(c.task), overrides(c.taskOverrides) {
    c.p.events.append(QJsonArray{"scope", name, config});
    c.task = name;
    c.taskOverrides = config;
}
inline TaskScope::~TaskScope() {
    c.p.events.append("scope-end");
    c.task = previous;
    c.taskOverrides = overrides;
}
inline void Combat::recoverStartingPopup() {
    p.events.append("recover");
    if (p.scene % 37 == 0)
        throw std::runtime_error("popup recovery failed");
}
inline bool Combat::hasHealthBar() {
    p.events.append("health");
    return p.scene & 128;
}
inline bool World::pick() {
    p.events.append("pick");
    return p.scene & 256;
}
inline QVector<Match> World::echoes() {
    p.events.append("echoes");
    return p.scene & 1024 ? QVector<Match>{Match{"echo", {0, 0, 1, 1}, 1}} : QVector<Match>{};
}
inline QJsonObject trace(int scene, bool rust) {
    Probe p(scene);
    const auto supplied = scene & 64 ? p.saved : QJsonObject{};
    QJsonObject out;
    try {
        out["result"] = rust ? p.candidate(scene & 1, supplied) : p.reference(scene & 1, supplied);
        out["error"] = "";
    } catch (const Cancelled &) {
        out["error"] = "cancelled";
    } catch (const std::exception &e) {
        out["error"] = errorText(e);
    }
    out["old-check"] = p.combat_.onCombatCheck();
    out["old-end"] = p.combat_.combatEndCondition();
    out["events"] = p.events;
    out["timeout"] = p.combat_.targetEnemyTimeout;
    out["calls"] = p.calls;
    out["total"] = p.totalCompleted_;
    out["drops"] = p.drops_;
    out["config"] = p.config_;
    out["task"] = p.c_.task;
    out["overrides"] = p.c_.taskOverrides;
    out["aim"] = p.aimBoss_;
    out["world"] = p.treatAsWorld_;
    out["entered"] = p.justEntered_;
    return out;
}
} // namespace okww::runtime::echo_run_test
