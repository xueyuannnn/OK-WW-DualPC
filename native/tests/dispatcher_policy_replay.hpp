#pragma once
#include "runtime/task_registry.hpp"
#include "runtime/task_registry_abi.hpp"
#include "runtime/workflow_abi.hpp"
namespace okww::runtime::dispatcher_test {
struct Probe;
struct Context {
    Probe &p;
    struct Profile {
        Probe &p;
        QJsonValue value(QString, QString, QJsonValue);
    } profile{p};
    struct Execution {
        Probe &p;
        double now();
    } execution{p};
    std::function<void(QString, QString)> info;
};
struct Combat {
    Probe &p;
    bool runAutoCombat();
    void recoverStartingPopup();
};
struct Probe {
    int scene, ordinal = 0;
    double time = 0, recoveredAt_ = 0;
    int deathRecoveries_ = 0, detector_ = 0;
    QString root_ = "root", data_ = "data";
    QJsonArray events;
    Context c_{*this};
    Combat combat_{*this};
    explicit Probe(int scene)
        : scene(scene), time(scene % 2 ? 1000 : 50), recoveredAt_(scene % 3 ? time - 20 : -1e12),
          deathRecoveries_(scene % 13) {
        if (scene % 3)
            c_.info = [this](QString key, QString value) { note("info", {key, value}); };
    }
    void note(QString name, QJsonArray args = {}) {
        events.append(QJsonObject{{"op", name}, {"args", args}});
        if (++ordinal == scene % 17 + 1 && scene % 7 == 0)
            fail("scripted-dispatch-failure");
    }
    bool original(const QString &);
    bool migrated(const QString &);
};
inline QJsonValue Context::Profile::value(QString group, QString key, QJsonValue) {
    p.note("profile", {group, key});
    if (key == "Resume Interrupted Tasks")
        return p.scene % 5 != 0;
    return QJsonValue(p.scene % 13 - 1);
}
inline double Context::Execution::now() {
    p.note("now");
    const auto value = p.time;
    p.time += .01;
    return value;
}
inline bool Combat::runAutoCombat() {
    p.note("auto");
    switch (p.scene % 4) {
    case 0:
        return p.scene % 2;
    case 1:
        throw CharacterDead();
    case 2:
        fail("combat-error");
    default:
        throw CombatEnded("combat-ended");
    }
}
inline void Combat::recoverStartingPopup() { p.note("recover"); }
struct Daily {
    Context &c;
    Daily(Context &c, Combat &, int &, QString, QString) : c(c) { c.p.note("daily-create"); }
    void run(bool enhanced = false) { c.p.note("daily", {enhanced}); }
};
struct MultiAccountDaily {
    Context &c;
    explicit MultiAccountDaily(Context &c) : c(c) { c.p.note("accounts-create"); }
    void run(std::function<void()> action) {
        c.p.note("accounts");
        for (int i = 0; i < c.p.scene % 3; ++i)
            action();
    }
};
struct EchoFarm {
    Context &c;
    EchoFarm(Context &c, Combat &, int &) : c(c) { c.p.note("echo-create"); }
    void run(bool enhanced) { c.p.note("echo", {enhanced}); }
};
struct NightmareFarm {
    Context &c;
    NightmareFarm(Context &c, Combat &, int &) : c(c) { c.p.note("nightmare-create"); }
    void run() { c.p.note("nightmare"); }
};
struct DomainFarm {
    Context &c;
    DomainFarm(Context &c, Combat &) : c(c) { c.p.note("domain-create"); }
    void run(DomainKind kind, bool enhanced) { c.p.note("domain", {int(kind), enhanced}); }
};
struct Garden {
    Context &c;
    Garden(Context &c, QString) : c(c) { c.p.note("garden-create"); }
    void run() { c.p.note("garden"); }
};
struct EchoOperations {
    Context &c;
    EchoOperations(Context &c, QString directory) : c(c) { c.p.note("operation-create", {directory}); }
    void merge() { c.p.note("merge"); }
    void enhance() { c.p.note("enhance"); }
    void change() { c.p.note("change"); }
};
inline bool Probe::original(const QString &id) {
#include "reference_dispatcher_body.hpp"
}
inline bool Probe::migrated(const QString &id) {
    const std::array args{double(businessTaskKind(id))};
    const auto result = rust::workflowRun(
        rust::WorkflowMethod::DispatchBusinessTask, args,
        [&](rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &error) {
            switch (op) {
#include "dispatcher_host_cases.hpp"
            default:
                fail("Unexpected dispatcher test host");
            }
        });
    return result[0] != 0;
}
inline QJsonObject replay(const QString &id, int scene, bool original) {
    Probe p(scene);
    QJsonObject result;
    try {
        result["result"] = original ? p.original(id) : p.migrated(id);
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
        result["type"] = typeid(e).name();
    }
    result["events"] = p.events;
    result["time"] = p.time;
    result["recoveries"] = p.deathRecoveries_;
    result["recovered_at"] = p.recoveredAt_;
    return result;
}
} // namespace okww::runtime::dispatcher_test
