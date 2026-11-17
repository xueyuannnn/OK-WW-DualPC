#pragma once
#include "runtime/echo_farm.hpp"
#include "runtime/workflow_abi.hpp"
namespace okww::runtime::echo_single_test {
struct Probe;
struct Execution {
    Probe &p;
    double now() const;
    void checkpoint();
    void wait(double);
};
struct Context {
    Probe &p;
    Execution execution;
    std::function<void(QString, QString)> info;
    void refresh();
    void sleep(double);
    void click(QPointF, double after = 0);
    void clickRelative(double, double, double);
    bool wait(const std::function<bool()> &, double, double interval = .05);
    std::optional<Match> find(QString, MatchOptions = {});
};
struct Combat {
    Probe &p;
    bool active();
    void combatOnce(const CombatOptions &);
};
struct Navigation {
    Probe &p;
    bool inRealm();
    bool inWorld();
    void ensureMain(double);
    void waitTeam(double);
};
struct World {
    Probe &p;
    bool treasure();
    bool pick(bool claim = true, bool echo = false);
    bool followBossMinimap(double timeout = 15, double threshold = .5);
    std::pair<bool, bool> findEcho(bool, bool, double, double);
    bool circleEcho(int);
    bool walkToInteract(QString, double, bool, double, QString, bool);
    void walkToTreasure();
};
void restartBossDomain(Context &);
bool findInteract(Context &);
void awaitWeeklyMaterialReward(Context &);
struct Probe {
    int scene, attempts = 0, drops_ = 0, totalCompleted_ = 0, checkpoints = 0;
    double time = 0, started_ = 0, wait_ = 0;
    bool enhanced_, realm_ = false, treasure_ = false, entryAtHeal_ = false, justEntered_ = false,
                    bypassWait_ = false, failed = false;
    QString walkResult_ = "combat";
    QJsonObject config_;
    QJsonArray events;
    Context c_;
    Combat combat_;
    Navigation navigation_;
    World world_;
    Probe(int value)
        : scene(value), enhanced_(value & 1), c_{*this, {*this}, {}}, combat_{*this}, navigation_{*this},
          world_{*this} {
        config_ = {{"Advanced Skill Material Mode", bool(value & 2)},
                   {"Repeat Farm Count", value % 5},
                   {"Teleport to Boss", value & 4 ? "Weekly Challenge" : "Boss Challenge"},
                   {"Echo Pickup Method", QStringList{"Yolo", "Run in Circle", "Other"}.at(value % 3)}};
        if (value & 8)
            c_.info = [&](QString k, QString v) { events.append(QJsonArray{"info", k, v}); };
        if (value % 127 == 0)
            config_["Repeat Farm Count"] = 10000001;
        else if (value % 131 == 0)
            config_["Repeat Farm Count"] = -2;
    }
    bool teleportEnabled() {
        if (failed && scene % 97 == 0)
            throw std::runtime_error("recovery guard failed");
        return scene & 16;
    }
    int weeklyMaterialCount() {
        events.append("weekly-count");
        return 1 + scene % 3;
    }
    void parameters() {
        events.append("parameters");
        bypassWait_ = scene & 32;
        wait_ = .1;
    }
    void teleport() {
        events.append("teleport");
        if (failed && scene % 53 == 0)
            throw NavigationFailure("return teleport failed");
        walkResult_ = scene & 64 ? "realm" : "combat";
        justEntered_ = scene & 128;
    }
    void checkRealm(double v) { events.append(QJsonArray{"realm-check", v}); }
    void interactions() {
        events.append("interactions");
        if (scene % 43 == 0)
            throw NavigationFailure("interaction failed");
    }
    bool treasureRestart() {
        events.append("restart-treasure");
        return scene & 256;
    }
    void enterFromF() {
        events.append("enter-f");
        justEntered_ = true;
    }
    void scrollInteract() { events.append("scroll"); }
    void nearestBoss() { events.append("nearest"); }
    bool seekCombat(double d, bool m, bool r) {
        events.append(QJsonArray{"seek", d, m, r});
        return scene & 512;
    }
    void treasureHunt() { events.append("hunt"); }
    void recognizeBoss() { events.append("recognize"); }
    void heal() { events.append("heal"); }
    int reference()
#include "reference_echo_single_body.hpp"
        int candidate() {
        std::optional<CombatOptions> combatOptions;
        std::optional<Match> materialClaim;
        return int(rust::workflowRun(rust::WorkflowMethod::EchoSingle, {},
                                     [&](rust::WorkflowOp op, const double *a, double *out) {
                                         switch (op) {
#include "echo_single_host_cases.hpp"
                                         default:
                                             fail("Unknown echo single host action");
                                         }
                                     })[0]);
    }
};
inline double Execution::now() const { return p.time; }
inline void Execution::checkpoint() {
    if (++p.checkpoints > 2000)
        throw std::runtime_error("replay deadline");
    if (p.scene % 101 == 0)
        throw Cancelled{};
}
inline void Execution::wait(double v) {
    p.time += v;
    checkpoint();
}
inline void Context::refresh() {
    p.events.append("refresh");
    execution.checkpoint();
}
inline void Context::sleep(double v) {
    p.events.append(QJsonArray{"sleep", v});
    p.time += v;
    execution.checkpoint();
}
inline void Context::click(QPointF point, double after) {
    p.events.append(QJsonArray{"click", point.x(), point.y(), after});
    p.time += after;
}
inline void Context::clickRelative(double x, double y, double after) {
    p.events.append(QJsonArray{"relative", x, y, after});
    p.time += after;
}
inline bool Context::wait(const std::function<bool()> &condition, double timeout, double interval) {
    const auto end = execution.now() + timeout;
    for (;;) {
        execution.checkpoint();
        refresh();
        if (condition())
            return true;
        if (execution.now() >= end)
            return false;
        execution.wait(std::min(interval, end - execution.now()));
    }
}
inline std::optional<Match> Context::find(QString name, MatchOptions options) {
    p.events.append(QJsonArray{"find", name, options.threshold});
    return p.scene & 1024 ? std::optional<Match>{Match{name, {500, 100, 40, 30}, .9}}
                          : std::optional<Match>{};
}
inline bool Combat::active() {
    p.events.append("active");
    return p.scene & 2048;
}
inline void Combat::combatOnce(const CombatOptions &o) {
    p.events.append(
        QJsonArray{"combat", o.waitSeconds, o.required, o.useLiberation, o.target, o.maximumSeconds});
    ++p.attempts;
    if (p.scene % 13 == 0) {
        p.failed = true;
        throw CharacterRevived{};
    }
    if (p.scene % 17 == 0) {
        p.failed = true;
        throw NavigationFailure("round navigation failed");
    }
    if (p.scene % 19 == 0) {
        p.failed = true;
        throw Cancelled{};
    }
    if (p.attempts <= p.scene % 3) {
        p.failed = true;
        if (p.scene & 4096)
            throw NavigationFailure("temporary navigation");
        throw CharacterRevived{};
    }
}
inline bool Navigation::inRealm() {
    p.events.append("in-realm");
    return p.scene & 8192;
}
inline bool Navigation::inWorld() {
    p.events.append("in-world");
    return p.scene & 16384;
}
inline void Navigation::ensureMain(double timeout) { p.events.append(QJsonArray{"main", timeout}); }
inline void Navigation::waitTeam(double timeout) { p.events.append(QJsonArray{"team", timeout}); }
inline bool World::treasure() {
    p.events.append("treasure");
    return p.scene & 512;
}
inline bool World::pick(bool claim, bool echo) {
    p.events.append(QJsonArray{"pick", claim, echo});
    return p.scene & 64;
}
inline bool World::followBossMinimap(double timeout, double threshold) {
    p.events.append(QJsonArray{"follow", timeout, threshold});
    return p.scene & 128;
}
inline std::pair<bool, bool> World::findEcho(bool color, bool turn, double timeout, double threshold) {
    p.events.append(QJsonArray{"echo", color, turn, timeout, threshold});
    return {bool(p.scene & 256), false};
}
inline bool World::circleEcho(int circles) {
    p.events.append(QJsonArray{"circle", circles});
    return p.scene & 512;
}
inline bool World::walkToInteract(QString key, double timeout, bool required, double back, QString pattern,
                                  bool combat) {
    p.events.append(QJsonArray{"walk", key, timeout, required, back, pattern, combat});
    return p.scene & 1024;
}
inline void World::walkToTreasure() { p.events.append("walk-treasure"); }
inline void restartBossDomain(Context &c) {
    c.p.events.append("restart-domain");
    if (c.p.scene % 29 == 0)
        throw NavigationFailure("restart failed");
}
inline bool findInteract(Context &c) {
    c.p.events.append("interact");
    return c.p.scene & 1024;
}
inline void awaitWeeklyMaterialReward(Context &c) {
    c.p.events.append("reward-confirm");
    if (c.p.scene % 31 == 0)
        throw std::runtime_error("reward uncertain");
}
inline QJsonObject trace(int scene, bool rust) {
    Probe p(scene * 37 % 32768);
    QJsonObject out;
    try {
        out["result"] = rust ? p.candidate() : p.reference();
        out["error"] = "";
    } catch (const Cancelled &) {
        out["error"] = "cancelled";
    } catch (const std::exception &e) {
        out["error"] = errorText(e);
    }
    out["events"] = p.events;
    out["time"] = p.time;
    out["attempts"] = p.attempts;
    out["drops"] = p.drops_;
    out["total"] = p.totalCompleted_;
    out["realm"] = p.realm_;
    out["treasure"] = p.treasure_;
    out["entry"] = p.entryAtHeal_;
    out["entered"] = p.justEntered_;
    out["config"] = p.config_;
    return out;
}
} // namespace okww::runtime::echo_single_test
