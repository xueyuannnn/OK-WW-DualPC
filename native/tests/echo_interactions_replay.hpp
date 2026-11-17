#pragma once
#include "runtime/workflow_abi.hpp"
namespace okww::runtime::echo_interactions_test {
struct Probe;
struct Clock {
    Probe &p;
    double now() const;
};
struct Context {
    Probe &p;
    Clock execution;
    void sleep(double);
    void key(QString, double after = 0, double hold = .02);
};
bool findInteract(Context &);
struct Combat {
    Probe &p;
    bool active();
};
struct Navigation {
    Probe &p;
    bool inWorld();
    void exitRealm(double);
    void ensureMain();
    void waitTeam(double);
};
struct World {
    Probe &p;
    bool treasure();
    bool pick(bool claim = true, bool echo = false);
    bool walkToInteract(QString, double, bool, double, QString, bool, bool);
    bool followBossMinimap(double);
};
struct Probe {
    int scene, prompts = 0, activeCalls = 0, drops_ = 0;
    double time = 0, lastNight_ = -1000;
    bool realm_ = false, treasure_ = false, world = false;
    QJsonObject config_;
    QJsonArray events;
    Context c_;
    Combat combat_;
    Navigation navigation_;
    World world_;
    explicit Probe(int value)
        : scene(value), realm_(value & 2), treasure_(value & 4), world(value & 8), c_{*this, {*this}},
          combat_{*this}, navigation_{*this}, world_{*this} {
        config_["Boss"] = QStringList{"Fallacy of No Return", "Fenrico", "Lady of the Sea",
                                      "Nameless Explorer",    "Lorelei", "Other"}
                              .at(value % 6);
    }
    void changeNight() {
        events.append("night");
        lastNight_ = time;
    }
    void nearestBoss() {
        events.append("nearest");
        if (scene % 7 == 0)
            throw NavigationFailure("nearest failed");
    }
    bool seekCombat(double duration, bool move = false, bool running = false) {
        events.append(QJsonArray{"seek", duration, move, running});
        return scene & 16;
    }
    void scrollInteract() { events.append("scroll"); }
    bool treasureRestart() {
        events.append("restart");
        return scene & 32;
    }
    void treasureHunt() { events.append("hunt"); }
    void reference()
#include "reference_echo_interactions_body.hpp"
        void candidate() {
        rust::workflowRun(rust::WorkflowMethod::EchoInteractions, {},
                          [&](rust::WorkflowOp op, const double *a, double *out) {
                              switch (op) {
                              case rust::WorkflowOp::EchoCombat:
                                  out[0] = combat_.active();
                                  break;
                              case rust::WorkflowOp::EchoInteractionBoss:
                                  out[0] = QStringList{"Fallacy of No Return", "Fenrico", "Lady of the Sea",
                                                       "Nameless Explorer", "Lorelei"}
                                               .indexOf(config_["Boss"].toString("Other"));
                                  break;
                              case rust::WorkflowOp::Now:
                                  out[0] = c_.execution.now();
                                  break;
                              case rust::WorkflowOp::EchoLastNight:
                                  out[0] = lastNight_;
                                  break;
                              case rust::WorkflowOp::ChangeEchoNight:
                                  changeNight();
                                  break;
                              case rust::WorkflowOp::EchoHasInteract:
                                  out[0] = findInteract(c_);
                                  break;
                              case rust::WorkflowOp::FindEchoNearestBoss:
                                  nearestBoss();
                                  break;
                              case rust::WorkflowOp::EchoTapKey:
                                  c_.key(QStringList{"w", "a", "s", "d"}.at(int(a[0])), a[1], a[2]);
                                  break;
                              case rust::WorkflowOp::WalkEchoFallacyInteract:
                                  out[0] = world_.walkToInteract("w", 20, true, 0, {}, true, true);
                                  break;
                              case rust::WorkflowOp::ScrollEchoInteractions:
                                  scrollInteract();
                                  break;
                              case rust::WorkflowOp::SeekEchoCombat:
                                  out[0] = seekCombat(a[0], a[1] != 0, a[2] != 0);
                                  break;
                              case rust::WorkflowOp::Sleep:
                                  c_.sleep(a[0]);
                                  break;
                              case rust::WorkflowOp::EchoPickEchoOnly:
                                  out[0] = world_.pick(true, true);
                                  break;
                              case rust::WorkflowOp::IncrementEchoDrops:
                                  ++drops_;
                                  break;
                              case rust::WorkflowOp::EnsureEchoMainDefault:
                                  navigation_.ensureMain();
                                  break;
                              case rust::WorkflowOp::HuntEchoTreasure:
                                  treasureHunt();
                                  break;
                              case rust::WorkflowOp::StoreEchoRealm:
                                  realm_ = a[0] != 0;
                                  break;
                              case rust::WorkflowOp::EchoInWorld:
                                  out[0] = navigation_.inWorld();
                                  break;
                              case rust::WorkflowOp::ExitEchoRealm:
                                  navigation_.exitRealm(a[0]);
                                  break;
                              case rust::WorkflowOp::EchoPickDefault:
                                  out[0] = world_.pick();
                                  break;
                              case rust::WorkflowOp::EchoWaitTeam:
                                  events.append(QJsonArray{"team", a[0]});
                                  break;
                              case rust::WorkflowOp::EchoTreasureFound:
                                  out[0] = world_.treasure();
                                  break;
                              case rust::WorkflowOp::EchoTreasureRestart:
                                  out[0] = treasureRestart();
                                  break;
                              case rust::WorkflowOp::StoreEchoTreasure:
                                  treasure_ = a[0] != 0;
                                  break;
                              case rust::WorkflowOp::EchoTreasure:
                                  out[0] = treasure_;
                                  break;
                              case rust::WorkflowOp::FollowEchoBossMinimap:
                                  out[0] = world_.followBossMinimap(a[0]);
                                  break;
                              default:
                                  fail("Unknown fake echo interaction action");
                              }
                          });
    }
};
inline double Clock::now() const { return p.time; }
inline void Context::sleep(double value) {
    p.events.append(QJsonArray{"sleep", value});
    if (p.scene % 53 == 0)
        throw Cancelled{};
    if (p.scene % 47 == 0)
        throw std::runtime_error("sleep failed");
    p.time += value;
}
inline void Context::key(QString key, double after, double hold) {
    p.events.append(QJsonArray{"key", key, after, hold});
    p.time += after + hold;
}
inline bool findInteract(Context &c) {
    const bool value = (c.p.scene & 512) ? false : (c.p.scene & 128) || ++c.p.prompts <= c.p.scene % 4;
    c.p.events.append(QJsonArray{"interact", value});
    return value;
}
inline bool Combat::active() {
    ++p.activeCalls;
    const bool value = (p.scene & 256) && p.activeCalls % 3 != 0;
    p.events.append(QJsonArray{"combat", value});
    return value;
}
inline bool Navigation::inWorld() {
    p.events.append(QJsonArray{"world", p.world});
    return p.world;
}
inline void Navigation::exitRealm(double value) {
    p.events.append(QJsonArray{"exit", value});
    p.world = true;
}
inline void Navigation::ensureMain() {
    p.events.append("main");
    if (p.scene % 11 == 0)
        throw std::runtime_error("recovery failed");
}
inline void Navigation::waitTeam(double value) { p.events.append(QJsonArray{"team", value}); }
inline bool World::treasure() {
    p.events.append("treasure");
    return p.scene & 64;
}
inline bool World::pick(bool claim, bool echo) {
    p.events.append(QJsonArray{"pick", claim, echo});
    return p.scene & 32;
}
inline bool World::walkToInteract(QString key, double timeout, bool required, double back, QString pattern,
                                  bool combat, bool running) {
    p.events.append(QJsonArray{"walk", key, timeout, required, back, pattern, combat, running});
    return p.scene & 8;
}
inline bool World::followBossMinimap(double value) {
    p.events.append(QJsonArray{"follow", value});
    return p.scene & 16;
}
inline QJsonObject trace(int scene, bool rust) {
    Probe p(scene);
    QString error;
    try {
        if (rust)
            p.candidate();
        else
            p.reference();
    } catch (const Cancelled &) {
        error = "cancelled";
    } catch (const std::exception &e) {
        error = errorText(e);
    }
    return {{"events", p.events}, {"time", p.time},        {"realm", p.realm_},    {"treasure", p.treasure_},
            {"drops", p.drops_},  {"night", p.lastNight_}, {"prompts", p.prompts}, {"active", p.activeCalls},
            {"world", p.world},   {"error", error}};
}
} // namespace okww::runtime::echo_interactions_test
