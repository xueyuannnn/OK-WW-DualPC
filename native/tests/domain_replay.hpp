#pragma once
#include "runtime/domain.hpp"
#include "runtime/workflow_abi.hpp"
namespace okww::runtime::domain_test {
struct Probe;
struct Execution {
    Probe &p;
    double now() const;
    void checkpoint();
    void wait(double seconds);
};
struct Profile {
    Probe &p;
    QJsonObject group(const QString &task);
};
struct TaskContext {
    Probe &p;
    Execution execution;
    Profile profile;
    cv::Mat frame = cv::Mat(1080, 1920, CV_8UC3);
    QString task = "outer";
    QJsonObject overrides;
    std::function<void(QString, QString)> info;
    explicit TaskContext(Probe &probe) : p(probe), execution{p}, profile{p} {}
    void refresh();
    void sleep(double seconds);
    void click(QPointF point, double after);
    void click(const Match &item, double after) { click(item.center(), after); }
    void clickRelative(double x, double y, double after);
    void key(QString name, double after);
    bool wait(const std::function<bool()> &condition, double timeout);
    bool skipConfirm();
    std::optional<Match> find(const QString &name);
    std::optional<Match> findAny(QStringList names, MatchOptions options);
    void reportProgress(QString label, int current, int total, QString unit, QString detail = {});
};
struct TaskScope {
    TaskContext &c;
    QString previous;
    QJsonObject overrides;
    TaskScope(TaskContext &context, QString task, QJsonObject config = {})
        : c(context), previous(c.task), overrides(c.overrides) {
        c.task = std::move(task);
        c.overrides = std::move(config);
        record();
    }
    ~TaskScope() {
        finish();
        c.task = previous;
        c.overrides = overrides;
    }
    void record();
    void finish();
};
struct CombatSession {
    Probe &p;
    bool active() { return false; }
    void recoverStartingPopup();
    void combatOnce(const CombatOptions &options);
};
struct Navigation {
    TaskContext &c;
    explicit Navigation(TaskContext &context) : c(context) {}
    void bookCategory(QString name);
    void selectBookEntry(int serial, int total, QVector<int> structure);
    void startChallenge();
    void waitTeam(double timeout);
    void ensureMain(double timeout = 30);
    void confirmDialog(bool right);
    void openBook(QString name);
};
struct WorldInteraction {
    TaskContext &c;
    WorldInteraction(TaskContext &context, std::function<bool()>) : c(context) {}
    void walkToInteract(QString key, double timeout, bool required);
    void pick(bool claim = true);
    void walkToTreasure();
};
using StaminaValues = runtime::StaminaValues;
using StaminaSpend = runtime::StaminaSpend;
StaminaValues readStamina(TaskContext &c);
StaminaSpend claimStamina(TaskContext &c, int once, int remaining, bool exact);
using Templates = runtime::Templates;
#include "reference_domain_body.hpp"
struct Probe {
    int scene;
    double time = 0;
    int reads = 0, rounds = 0, claims = 0, finds = 0;
    QJsonArray events;
    QJsonObject config;
    TaskContext c;
    CombatSession combat;
    Navigation nav;
    WorldInteraction world;
    std::function<void(int)> staminaSpent;
    explicit Probe(int s) : scene(s), c(*this), combat{*this}, nav(c), world(c, {}) {
        config = {{"Which Tacet Suppression to Farm", s % 5 + 1},
                  {"Which Forgery Challenge to Farm", s % 6 + 1},
                  {"Material Selection",
                   QStringList{"Shell Credit", "Resonator EXP", "Weapon EXP", "other"}.at(s % 4)},
                  {"Tacet Suppression Count", s % 7 - 1},
                  {"Forgery Challenge Count", s % 7 - 1},
                  {"Simulation Challenge Count", s % 7 - 1}};
        if (s & 8)
            c.info = [&](QString key, QString value) { event("info", QJsonArray{key, value}); };
        if (s & 16)
            staminaSpent = [&](int used) {
                event("spent", QJsonArray{used});
                if (scene % 29 == 0)
                    throw std::runtime_error("spent callback failed");
            };
    }
    void event(QString op, QJsonValue value = {}) {
        events.append(QJsonObject{{"op", op}, {"value", value}, {"time", time}, {"task", c.task}});
    }
    FarmResult rustRun(rust::WorkflowMethod method, std::initializer_list<double> args,
                       QJsonObject data = {}) {
        FarmResult result;
        std::optional<TaskScope> scope;
        std::optional<Match> confirm;
        rust::workflowRun(
            method, std::span<const double>(args.begin(), args.size()),
            [&](rust::WorkflowOp op, const double *a, double *out) {
                using O = rust::WorkflowOp;
                switch (op) {
                case O::Now:
                    out[0] = c.execution.now();
                    break;
                case O::Checkpoint:
                    c.execution.checkpoint();
                    break;
                case O::ExecutionWait:
                    c.execution.wait(a[0]);
                    break;
                case O::Refresh:
                    c.refresh();
                    break;
                case O::Sleep:
                    c.sleep(a[0]);
                    break;
                case O::InvalidCondition:
                    fail("等待条件参数无效");
                    break;
                case O::ClickRelative:
                    c.clickRelative(a[0], a[1], a[2]);
                    break;
                case O::Key:
                    c.key("esc", a[1]);
                    break;
                case O::SkipConfirm:
                    out[0] = c.skipConfirm();
                    break;
                case O::NavCategory:
                    nav.bookCategory(QStringList{"wuyin", "ningsu", "moni"}.at(int(a[0])));
                    break;
                case O::FarmEntrySerial:
                    out[0] = std::as_const(data)[a[0] == 0 ? "Which Tacet Suppression to Farm"
                                                           : "Which Forgery Challenge to Farm"]
                                 .toInt(1);
                    break;
                case O::NavFarmEntry:
                    nav.selectBookEntry(int(a[1]), a[0] == 0 ? 21 : 20,
                                        a[0] == 0 ? QVector<int>{4, 5, 5, 7} : QVector<int>{5, 5, 5, 5});
                    break;
                case O::MaterialKind: {
                    const auto value = std::as_const(data)["Material Selection"].toString("Shell Credit");
                    out[0] = value == "Resonator EXP" ? 0 : value == "Weapon EXP" ? 1 : 2;
                    break;
                }
                case O::NavStartChallenge:
                    nav.startChallenge();
                    break;
                case O::NavWaitTeam:
                    nav.waitTeam(a[0]);
                    break;
                case O::NavEnsureMain:
                    nav.ensureMain(a[0]);
                    break;
                case O::NavConfirm:
                    nav.confirmDialog(a[0] != 0);
                    break;
                case O::ClickDomainAnchor: {
                    const QPoint positions[]{{1229, 919}, {701, 921}};
                    c.click(positions[int(a[0])], a[1]);
                    break;
                }
                case O::FindFarmConfirm:
                    confirm =
                        c.findAny({"confirm_btn_hcenter_vcenter", "confirm_btn_highlight_hcenter_vcenter"},
                                  {.threshold = a[0]});
                    out[0] = confirm.has_value();
                    break;
                case O::ClickFarmConfirm:
                    if (a[0] == 0)
                        c.click(*confirm, a[1]);
                    else
                        c.click(QPointF(confirm->box.x() - confirm->box.width(), confirm->center().y()),
                                a[1]);
                    break;
                case O::DomainTaskScope: {
                    const QString id =
                        QStringList{"TacetTask", "ForgeryTask", "SimulationTask"}.at(int(a[0])) +
                        (a[1] != 0 ? "2" : "");
                    if (a[2] != 0)
                        scope.emplace(c, id, data);
                    else
                        scope.emplace(c, id);
                    break;
                }
                case O::RecoverStartingPopup:
                    combat.recoverStartingPopup();
                    break;
                case O::ReadFarmConfig:
                    data = c.profile.group(c.task);
                    break;
                case O::FarmRequested: {
                    const QStringList keys{"Tacet Suppression Count", "Forgery Challenge Count",
                                           "Simulation Challenge Count"};
                    out[0] = std::as_const(data)[keys.at(int(a[0]))].toInt();
                    break;
                }
                case O::OpenBossBook:
                    nav.openBook("gray_book_boss");
                    break;
                case O::ReadStamina: {
                    const auto value = readStamina(c);
                    out[0] = value.current;
                    out[1] = value.reserve;
                    out[2] = value.total;
                    break;
                }
                case O::StoreFarmResult:
                    result = {int(a[0]), int(a[1]), int(a[2]), a[3] != 0};
                    break;
                case O::CallFarm:
                    result = rustRun(rust::WorkflowMethod::DomainFarm, {a[0], a[1], a[2]}, data);
                    break;
                case O::FarmProgress: {
                    const auto label =
                        QStringList{"刷取无音区", "刷取凝素领域", "刷取模拟领域"}.at(int(a[0]));
                    c.reportProgress(label, int(a[1]), int(a[2]), "体力",
                                     a[3] < 0 ? QString() : QString("已完成 %1 轮").arg(int(a[3])));
                    break;
                }
                case O::WalkToFarmInteract:
                    world.walkToInteract("w", 4, true);
                    break;
                case O::FarmPick:
                    world.pick(a[0] != 0);
                    break;
                case O::CombatFarmRound: {
                    CombatOptions options;
                    options.target = a[0] != 0;
                    options.realmEntryAtHealPoint = a[1] != 0;
                    combat.combatOnce(options);
                    break;
                }
                case O::WalkToTreasure:
                    world.walkToTreasure();
                    break;
                case O::ClaimSign:
                    out[0] = c.find("claim_stamina_sign").has_value();
                    break;
                case O::ClaimFarmStamina: {
                    const auto spend = claimStamina(c, int(a[0]), int(a[1]), a[2] != 0);
                    out[0] = spend.used;
                    out[1] = spend.repeat;
                    break;
                }
                case O::HasStaminaCallback:
                    out[0] = bool(staminaSpent);
                    break;
                case O::StaminaCallback:
                    staminaSpent(int(a[0]));
                    break;
                case O::HasInfo:
                    out[0] = bool(c.info);
                    break;
                case O::FarmSpentInfo:
                    c.info("已消耗体力", QString::number(int(a[0])));
                    break;
                case O::FarmRecoveryInfo:
                    c.info("副本恢复", QString("第 %1 次；重新进入后继续剩余额度").arg(int(a[0])));
                    break;
                case O::Throw:
                    switch (int(a[0])) {
                    case 100:
                        fail("副本次数或体力消耗无效");
                    case 101:
                        fail("未读到体力数值");
                    case 102:
                        fail("刷取体力额度无效");
                    case 103:
                        fail("进入副本前未读到体力数值");
                    case 104:
                        throw runtime::NavigationFailure("连续未识别到无音区体力领奖界面");
                    case 105:
                        throw runtime::NavigationFailure("副本已连续恢复 3 次，未完成剩余额度");
                    }
                    break;
                default:
                    fail("Unexpected domain test SDK operation");
                }
            });
        return result;
    }
};
inline double Execution::now() const { return p.time; }
inline void Execution::checkpoint() {
    if (p.time > 300)
        throw Cancelled();
}
inline void Execution::wait(double seconds) {
    p.time += seconds;
    checkpoint();
}
inline QJsonObject Profile::group(const QString &task) {
    p.event("config", task);
    return p.config;
}
inline void TaskContext::refresh() {
    p.event("refresh");
    execution.checkpoint();
}
inline void TaskContext::sleep(double seconds) {
    p.event("sleep", seconds);
    p.time += seconds;
    execution.checkpoint();
}
inline void TaskContext::click(QPointF point, double after) {
    p.event("click", QJsonArray{point.x(), point.y(), after});
    if (after)
        sleep(after);
}
inline void TaskContext::clickRelative(double x, double y, double after) {
    p.event("relative", QJsonArray{x, y, after});
    if (after)
        sleep(after);
}
inline void TaskContext::key(QString name, double after) {
    p.event("key", QJsonArray{name, after});
    if (after)
        sleep(after);
}
inline bool TaskContext::wait(const std::function<bool()> &condition, double timeout) {
    const double end = execution.now() + timeout;
    while (true) {
        execution.checkpoint();
        refresh();
        if (condition())
            return true;
        if (execution.now() >= end)
            return false;
        execution.wait(std::min(.05, end - execution.now()));
    }
}
inline bool TaskContext::skipConfirm() {
    p.event("skip");
    return p.scene % 3 == 0;
}
inline std::optional<Match> TaskContext::find(const QString &name) {
    p.event("find", name);
    if (p.scene % 7 == 0)
        return {};
    return Match{name, {100, 100, 40, 20}, .9};
}
inline std::optional<Match> TaskContext::findAny(QStringList names, MatchOptions options) {
    p.event("find_any",
            QJsonObject{{"names", QJsonArray::fromStringList(names)}, {"threshold", options.threshold}});
    if (p.scene % 5 == 0)
        return {};
    return Match{names.first(), {100, 100, 40, 20}, .9};
}
inline void TaskContext::reportProgress(QString label, int current, int total, QString unit, QString detail) {
    p.event("progress", QJsonArray{label, current, total, unit, detail});
}
inline void TaskScope::record() { c.p.event("scope_enter", c.overrides); }
inline void TaskScope::finish() { c.p.event("scope_exit"); }
inline void CombatSession::recoverStartingPopup() { p.event("recover_start"); }
inline void CombatSession::combatOnce(const CombatOptions &options) {
    p.event("combat",
            QJsonArray{options.target, options.realmEntryAtHealPoint, options.waitSeconds, options.required});
    ++p.rounds;
    if (p.scene % 11 == 0 && p.rounds <= 3)
        throw CharacterRevived();
    if (p.scene % 13 == 0)
        throw Cancelled();
}
inline void Navigation::bookCategory(QString name) { c.p.event("category", name); }
inline void Navigation::selectBookEntry(int serial, int total, QVector<int> structure) {
    QJsonArray list;
    for (int i : structure)
        list.append(i);
    c.p.event("entry", QJsonArray{serial, total, list});
}
inline void Navigation::startChallenge() { c.p.event("start_challenge"); }
inline void Navigation::waitTeam(double timeout) { c.p.event("wait_team", timeout); }
inline void Navigation::ensureMain(double timeout) {
    c.p.event("main", timeout);
    if (c.p.scene % 41 == 0)
        throw runtime::NavigationFailure("navigation unavailable");
}
inline void Navigation::confirmDialog(bool right) { c.p.event("confirm", right); }
inline void Navigation::openBook(QString name) { c.p.event("book", name); }
inline void WorldInteraction::walkToInteract(QString key, double timeout, bool required) {
    c.p.event("interact", QJsonArray{key, timeout, required});
}
inline void WorldInteraction::pick(bool claim) { c.p.event("pick", claim); }
inline void WorldInteraction::walkToTreasure() { c.p.event("treasure"); }
inline StaminaValues readStamina(TaskContext &c) {
    c.p.event("stamina");
    ++c.p.reads;
    if (c.p.scene % 19 == 0 && c.p.reads == 1)
        return {};
    const int values[]{0, 40, 60, 120, 240};
    const int current = values[c.p.scene % 5];
    return {current, c.p.scene & 32 ? 60 : 0, current + (c.p.scene & 32 ? 60 : 0)};
}
inline StaminaSpend claimStamina(TaskContext &c, int once, int remaining, bool exact) {
    auto &p = c.p;
    p.event("claim", QJsonArray{once, remaining, exact});
    ++p.claims;
    if (p.scene % 17 == 0)
        throw std::runtime_error("claim unknown");
    const int used = p.scene % 23 == 0 ? 0 : once;
    return {used, p.claims < (p.scene % 3 + 1)};
}
inline QJsonObject trace(int scene, bool rust) {
    Probe p(scene);
    QJsonObject result;
    const auto kind = DomainKind(scene % 3);
    const bool exact = (scene & 4) != 0;
    const int budgets[]{0, 40, 60, 80, 120, -1, 24000001};
    const int must = budgets[(scene / 8) % 7];
    try {
        FarmResult outcome;
        if (rust)
            outcome = p.rustRun(
                scene & 128 ? rust::WorkflowMethod::DomainRun : rust::WorkflowMethod::DomainFarm,
                scene & 128 ? std::initializer_list<double>{double(int(kind)), double(exact)}
                            : std::initializer_list<double>{double(int(kind)), double(must), double(exact)},
                scene & 128 ? QJsonObject{} : p.config);
        else {
            DomainFarm farm(p.c, p.combat);
            farm.onStaminaSpent = p.staminaSpent;
            outcome = scene & 128 ? farm.run(kind, exact) : farm.farm(kind, must, exact, p.config);
        }
        result["result"] =
            QJsonArray{outcome.staminaUsed, outcome.completed, outcome.recoveries, outcome.noStamina};
        result["error"] = "";
    } catch (const std::exception &e) {
        result["error"] = QString::fromUtf8(e.what());
    }
    result["events"] = p.events;
    result["time"] = p.time;
    result["task"] = p.c.task;
    result["overrides"] = p.c.overrides;
    return result;
}
} // namespace okww::runtime::domain_test
