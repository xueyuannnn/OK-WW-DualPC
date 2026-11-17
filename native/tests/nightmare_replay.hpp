#pragma once
#include "runtime/nightmare.hpp"
#include "runtime/workflow_abi.hpp"
namespace okww::runtime::nightmare_test {
struct Probe;
struct Execution {
    Probe &p;
    double now() const;
    void checkpoint();
    void wait(double seconds);
};
struct Input {
    Probe &p;
    void click(std::optional<QPointF>, double hold, QString button);
};
struct Profile {
    Probe &p;
    QJsonObject group(QString task);
};
struct TaskContext {
    Probe &p;
    Execution execution;
    Input input;
    Profile profile;
    cv::Mat frame = cv::Mat(1080, 1920, CV_8UC3);
    QString task = "outer";
    QJsonObject overrides;
    std::function<void(QString, QString)> info;
    explicit TaskContext(Probe &probe) : p(probe), execution{p}, input{p}, profile{p} {}
    void refresh();
    void sleep(double seconds);
    void click(QPointF point, double after);
    void click(const Match &m, double after);
    void clickRelative(double x, double y, double after);
    void key(QString key, double after);
    bool wait(const std::function<bool()> &condition, double timeout, double interval = .05);
    std::optional<Match> find(QString name, MatchOptions options = {});
    std::optional<Match> findAny(QStringList names, MatchOptions options = {});
    QVector<TextLine> text(double x, double y, double right, double bottom);
    void reportProgress(QString label, int current, int total, QString unit, QString detail);
};
struct TaskScope {
    TaskContext &c;
    QString previous;
    QJsonObject overrides;
    TaskScope(TaskContext &context, QString task, QJsonObject config);
    ~TaskScope();
};
struct CombatSession {
    Probe &p;
    std::function<bool()> onCombatCheck;
    bool active();
    bool combatOnce(CombatOptions options);
    void recoverStartingPopup();
};
struct Navigation {
    TaskContext &c;
    explicit Navigation(TaskContext &context) : c(context) {};
    void bookCategory(QString name);
    bool waitTeam(double timeout, bool required = true);
    void startChallenge();
    void ensureMain();
    void openBook(QString name);
    bool inWorld();
};
struct EchoDetector {
    QVector<Match> detect(const cv::Mat &, double, int, const std::function<void()> &) { return {}; }
};
struct WorldInteraction {
    TaskContext &c;
    WorldInteraction(TaskContext &context, std::function<bool()>,
                     std::function<QVector<Match>(const cv::Mat &, double)>)
        : c(context) {}
    bool pick(bool claim = true);
    bool walkToInteract(QString key, double timeout, bool required, double back, QString pattern, bool check);
    bool walkUntil(const std::function<bool()> &condition, QString key, double timeout, bool running = false);
    std::pair<bool, bool> findEcho(bool color, bool turn, double timeout);
};
std::optional<Match> findInteract(TaskContext &c);
bool completedNest(TaskContext &c);
void leaveNest(TaskContext &c);
#include "reference_nightmare_body.hpp"
struct Probe {
    int scene, rounds = 0, prompts = 0, activeCalls = 0;
    double time = 0;
    bool travelClicked = false, world = false, capture = false, captured = false;
    QSet<QString> unreachable;
    QMap<QString, int> deaths;
    QJsonObject config;
    QJsonArray events;
    TaskContext c;
    CombatSession combat;
    Navigation nav;
    WorldInteraction interaction;
    EchoDetector detector;
    explicit Probe(int input) : scene(input), c(*this), combat{*this}, nav(c), interaction(c, {}, {}) {
        const QVector<QJsonArray> choices{{},
                                          {"Tacet Discord Nest"},
                                          {"Nightmare Purification"},
                                          {"Tacet Discord Nest", "Nightmare Purification"},
                                          {"other"}};
        config = {{"Which to Farm", choices[input % choices.size()]}};
        if (input & 8)
            c.info = [&](QString key, QString value) { event("info", QJsonArray{key, value}); };
        combat.onCombatCheck = [&] {
            event("old_check");
            return false;
        };
    }
    void event(QString op, QJsonValue value = {}) {
        events.append(QJsonObject{{"op", op}, {"value", value}, {"time", time}, {"task", c.task}});
    }
    std::array<double, 3> rustRun(rust::WorkflowMethod method, std::initializer_list<double> args = {},
                                  QJsonObject supplied = {}, std::optional<NestTarget> target = {}) {
        std::optional<Match> button, confirm, feature;
        QRect area;
        std::optional<TaskScope> scope;
        std::function<bool()> oldCheck;
        QJsonObject data;
        QJsonArray choices;
        QMap<QString, int> attempts;
        return rust::workflowRun(
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
                    c.key(a[0] == 6 ? "f" : "esc", a[1]);
                    break;
                case O::HasInfo:
                    out[0] = bool(c.info);
                    break;
                case O::NestCategory:
                    nav.bookCategory(a[0] == 0 ? "canxiang" : "mengyan");
                    break;
                case O::FindNestTravel:
                    button = c.findAny({"fast_travel_custom", "gray_teleport", "remove_custom"},
                                       {.threshold = a[0]});
                    out[0] = button.has_value();
                    break;
                case O::NestTravelExists:
                    out[0] = button.has_value();
                    break;
                case O::ClickNestTravel:
                    c.click(*button, a[0]);
                    break;
                case O::FindNestConfirm:
                    confirm =
                        c.findAny({"confirm_btn_hcenter_vcenter", "confirm_btn_highlight_hcenter_vcenter"},
                                  {.threshold = a[0]});
                    out[0] = confirm.has_value();
                    break;
                case O::ClickNestConfirm:
                    c.click(*confirm, a[0]);
                    break;
                case O::NestTravelStillVisible:
                    out[0] = c.find(button->name, {.threshold = a[0]}).has_value();
                    break;
                case O::NestWaitTeam:
                    out[0] = nav.waitTeam(a[0], a[1] != 0);
                    break;
                case O::MarkNestUnreachable:
                    unreachable.insert(target->key);
                    break;
                case O::NestSkippedInfo:
                    c.info("巢穴跳过", "本次无法到达：" + target->key);
                    break;
                case O::CapturedNoticeArea:
                    area = gameBox(c.frame, .078, .488, .094, .514);
                    break;
                case O::CapturedNoticeFeature:
                    out[0] = c.find(QStringList{"char_1_text", "char_3_text"}.at(int(a[0])),
                                    {.threshold = a[1], .search = area, .filter = Filter::Negative80})
                                 .has_value();
                    break;
                case O::SetCaptured:
                    captured = a[0] != 0;
                    break;
                case O::Captured:
                    out[0] = captured;
                    break;
                case O::SetCaptureOnly:
                    capture = a[0] != 0;
                    break;
                case O::CaptureOnly:
                    out[0] = capture;
                    break;
                case O::CombatActive:
                    out[0] = combat.active();
                    break;
                case O::MiddleClick:
                    c.input.click({}, a[0], "middle");
                    break;
                case O::NestWalkInteract:
                    out[0] = interaction.walkToInteract("w", 5, false, 2.5, "吸收|Absorb", true);
                    break;
                case O::NestPick:
                    out[0] = interaction.pick(a[0] != 0);
                    break;
                case O::ClickNestTarget:
                    c.click(target->point, a[0]);
                    break;
                case O::FindNestInterface:
                    feature =
                        c.findAny({"fast_travel_custom", "gray_teleport", "remove_custom", "team_close"});
                    out[0] = feature.has_value();
                    break;
                case O::NestInterfaceTeam:
                    out[0] = feature->name == "team_close";
                    break;
                case O::NavStartChallenge:
                    nav.startChallenge();
                    break;
                case O::NestInteractVisible:
                    out[0] = findInteract(c).has_value();
                    break;
                case O::NestWalkApproach: {
                    double last = -1;
                    interaction.walkUntil(
                        [&] {
                            const auto tick = rustRun(rust::WorkflowMethod::NestSeekTick, {last});
                            last = tick[1];
                            return tick[0] != 0;
                        },
                        "w", 10);
                    break;
                }
                case O::NestCombatRound:
                    out[0] = combat.combatOnce({.waitSeconds = a[0], .required = false, .target = true});
                    break;
                case O::NestDeaths:
                    out[0] = deaths[target->key];
                    break;
                case O::SetNestDeaths:
                    deaths[target->key] = int(a[0]);
                    break;
                case O::NestRecoveryInfo:
                    c.info("巢穴恢复", QString("%1；本次死亡 %2 次").arg(target->key).arg(int(a[0])));
                    break;
                case O::NestFindEcho:
                    out[0] = interaction.findEcho(false, true, a[0]).first;
                    break;
                case O::NavEnsureMain:
                    nav.ensureMain();
                    break;
                case O::OpenBossBook:
                    nav.openBook("gray_book_boss");
                    break;
                case O::NestTargetTab:
                    out[0] = target->tab;
                    break;
                case O::FindNestTravelProbe:
                    out[0] = c.findAny({"fast_travel_custom", "gray_teleport", "remove_custom"}).has_value();
                    break;
                case O::NestRunForward:
                    interaction.walkUntil([] { return false; }, "w", a[0], true);
                    break;
                case O::LeaveNest:
                    leaveNest(c);
                    break;
                case O::NestTaskScope:
                    scope.emplace(c, "NightmareNestTask", supplied);
                    break;
                case O::ClearUnreachableNests:
                    unreachable.clear();
                    break;
                case O::ClearNestDeaths:
                    deaths.clear();
                    break;
                case O::InstallNestCombatCheck:
                    oldCheck = combat.onCombatCheck;
                    combat.onCombatCheck = [&] {
                        return rustRun(rust::WorkflowMethod::NestCombatCheck)[0] != 0;
                    };
                    break;
                case O::RestoreNestCombatCheck:
                    combat.onCombatCheck = oldCheck;
                    break;
                case O::NavInWorld:
                    out[0] = nav.inWorld();
                    break;
                case O::CompletedNest:
                    out[0] = completedNest(c);
                    break;
                case O::RecoverStartingPopup:
                    combat.recoverStartingPopup();
                    break;
                case O::NestSuppliedEmpty:
                    out[0] = supplied.isEmpty();
                    break;
                case O::ReadNestConfig:
                    data = a[0] != 0 ? c.profile.group("NightmareNestTask") : supplied;
                    choices = data["Which to Farm"].toArray();
                    break;
                case O::NestChoiceCount:
                    out[0] = double(choices.size());
                    break;
                case O::DefaultNestChoices:
                    choices = {"Nightmare Purification", "Tacet Discord Nest"};
                    break;
                case O::NestChoice:
                    out[0] = choices.contains(
                        QStringList{"Tacet Discord Nest", "Nightmare Purification"}.at(int(a[0])));
                    break;
                case O::NestProgress:
                    c.reportProgress(
                        a[0] != 0 ? "获取每日声骸" : "清理聚落", -1, -1, {},
                        QString("%1 · 已处理 %2 处").arg(a[1] < 2 ? "残像聚落" : "梦魇巢穴").arg(int(a[2])));
                    break;
                case O::ReadNextNestTarget: {
                    const auto value = runtime::nextNestTarget(
                        c.text(.35, .25, 1, .96), {c.frame.cols, c.frame.rows}, int(a[0]), unreachable);
                    target = value ? std::optional<NestTarget>({value->point, value->key, value->tab})
                                   : std::nullopt;
                    out[0] = target.has_value();
                    break;
                }
                case O::NestTargetAttempts:
                    out[0] = attempts[target->key];
                    break;
                case O::SetNestTargetAttempts:
                    attempts[target->key] = int(a[0]);
                    break;
                case O::Throw:
                    switch (int(a[0])) {
                    case 301:
                        throw runtime::NavigationFailure("巢穴前往后未识别到传送或配队界面");
                    case 302:
                        throw runtime::NavigationFailure("巢穴入口交互未结束");
                    case 303:
                        throw runtime::NavigationFailure("巢穴连续战斗超过上限，未记录为完成");
                    default:
                        fail("Unexpected nest test error");
                    }
                    break;
                default:
                    fail("Unexpected nest test SDK operation");
                }
            });
    }
};
inline double Execution::now() const { return p.time; }
inline void Execution::checkpoint() {
    if (p.time > 1000)
        throw Cancelled();
}
inline void Execution::wait(double seconds) {
    p.time += seconds;
    checkpoint();
}
inline void Input::click(std::optional<QPointF>, double hold, QString button) {
    p.event("mouse", QJsonArray{hold, button});
    p.time += hold;
}
inline QJsonObject Profile::group(QString task) {
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
    p.event("point", QJsonArray{point.x(), point.y(), after});
    if (after)
        sleep(after);
}
inline void TaskContext::click(const Match &m, double after) {
    p.event("match", m.name);
    if (m.name == "gray_teleport")
        p.travelClicked = true;
    click(m.center(), after);
}
inline void TaskContext::clickRelative(double x, double y, double after) {
    p.event("relative", QJsonArray{x, y, after});
    if (after)
        sleep(after);
}
inline void TaskContext::key(QString key, double after) {
    p.event("key", QJsonArray{key, after});
    if (after)
        sleep(after);
}
inline bool TaskContext::wait(const std::function<bool()> &condition, double timeout, double interval) {
    const double end = execution.now() + timeout;
    while (true) {
        execution.checkpoint();
        refresh();
        if (condition())
            return true;
        if (execution.now() >= end)
            return false;
        execution.wait(std::min(interval, end - execution.now()));
    }
}
inline std::optional<Match> TaskContext::find(QString name, MatchOptions options) {
    p.event("find", QJsonArray{name, options.threshold, int(options.filter)});
    if (name == "char_1_text" || name == "char_3_text") {
        if ((p.scene & 4) && p.rounds > 0)
            return Match{name, {100, 100, 20, 20}, .9};
        return {};
    }
    if (p.travelClicked)
        return {};
    return Match{name, {100, 100, 20, 20}, .9};
}
inline std::optional<Match> TaskContext::findAny(QStringList names, MatchOptions options) {
    p.event("find_any",
            QJsonObject{{"names", QJsonArray::fromStringList(names)}, {"threshold", options.threshold}});
    if (p.scene % 5 == 0)
        return {};
    return Match{names.contains("team_close") && (p.scene & 1)    ? QString("team_close")
                 : names.first() == "confirm_btn_hcenter_vcenter" ? names.first()
                                                                  : QString("gray_teleport"),
                 {100, 100, 20, 20},
                 .9};
}
inline QVector<TextLine> TaskContext::text(double x, double y, double right, double bottom) {
    p.event("text", QJsonArray{x, y, right, bottom});
    if (p.scene % 7 == 0 || p.rounds >= p.scene % 3 + 1)
        return {{"24/24", {700, 500, 30, 20}, .9}};
    return {{"12/24", {700, 500, 30, 20}, .9}};
}
inline void TaskContext::reportProgress(QString label, int current, int total, QString unit, QString detail) {
    p.event("progress", QJsonArray{label, current, total, unit, detail});
}
inline TaskScope::TaskScope(TaskContext &context, QString task, QJsonObject data)
    : c(context), previous(c.task), overrides(c.overrides) {
    c.task = std::move(task);
    c.overrides = std::move(data);
    c.p.event("scope_enter", c.overrides);
}
inline TaskScope::~TaskScope() {
    c.p.event("scope_exit");
    c.task = previous;
    c.overrides = overrides;
}
inline bool CombatSession::active() {
    p.event("active");
    ++p.activeCalls;
    return p.scene % 3 == 0 || p.activeCalls % 5 == 0;
}
inline bool CombatSession::combatOnce(CombatOptions options) {
    p.event("combat", QJsonArray{options.waitSeconds, options.required, options.target});
    ++p.rounds;
    if (onCombatCheck) {
        const bool keep = onCombatCheck();
        p.event("combat_check", keep);
    }
    if (p.scene % 11 == 0 && p.rounds <= 3)
        throw CharacterRevived();
    if (p.scene % 13 == 0)
        throw std::runtime_error("combat unavailable");
    if (p.scene % 17 == 0)
        throw Cancelled();
    return (p.scene & 32) == 0;
}
inline void CombatSession::recoverStartingPopup() { p.event("recover_start"); }
inline void Navigation::bookCategory(QString name) { c.p.event("category", name); }
inline bool Navigation::waitTeam(double timeout, bool required) {
    c.p.event("wait_team", QJsonArray{timeout, required});
    return c.p.scene % 9 != 0;
}
inline void Navigation::startChallenge() { c.p.event("start"); }
inline void Navigation::ensureMain() {
    c.p.event("main");
    if (c.p.scene % 41 == 0)
        throw runtime::NavigationFailure("navigation unavailable");
}
inline void Navigation::openBook(QString name) { c.p.event("book", name); }
inline bool Navigation::inWorld() {
    c.p.event("in_world");
    return c.p.world;
}
inline bool WorldInteraction::pick(bool claim) {
    c.p.event("pick", claim);
    return c.p.scene % 4 != 0;
}
inline bool WorldInteraction::walkToInteract(QString key, double timeout, bool required, double back,
                                             QString pattern, bool check) {
    c.p.event("walk_interact", QJsonArray{key, timeout, required, back, pattern, check});
    return c.p.scene % 6 != 0;
}
inline bool WorldInteraction::walkUntil(const std::function<bool()> &condition, QString key, double timeout,
                                        bool running) {
    c.p.event("walk", QJsonArray{key, timeout, running});
    if (condition())
        return true;
    c.p.time += .6;
    return condition();
}
inline std::pair<bool, bool> WorldInteraction::findEcho(bool color, bool turn, double timeout) {
    c.p.event("find_echo", QJsonArray{color, turn, timeout});
    return {c.p.scene % 8 != 0, false};
}
inline std::optional<Match> findInteract(TaskContext &c) {
    c.p.event("interact");
    if (c.p.scene % 37 == 0 || ++c.p.prompts <= c.p.scene % 3)
        return Match{"f", {100, 100, 20, 20}, .9};
    return {};
}
inline bool completedNest(TaskContext &c) {
    c.p.event("completed");
    return c.p.scene % 29 == 0;
}
inline void leaveNest(TaskContext &c) {
    c.p.event("leave");
    c.p.world = true;
}
inline QJsonObject trace(int scene, bool rust) {
    Probe p(scene);
    QJsonObject result;
    const auto config = scene & 64 ? p.config : QJsonObject{};
    try {
        bool captured;
        if (rust)
            captured = p.rustRun(rust::WorkflowMethod::NestRun, {double((scene & 2) != 0)}, config)[0] != 0;
        else {
            NightmareFarm farm(p.c, p.combat, p.detector);
            captured = farm.run((scene & 2) != 0, config);
        }
        result["captured"] = captured;
        result["error"] = "";
    } catch (const std::exception &e) {
        result["error"] = QString::fromUtf8(e.what());
    }
    p.combat.onCombatCheck();
    result["events"] = p.events;
    result["time"] = p.time;
    result["task"] = p.c.task;
    result["overrides"] = p.c.overrides;
    return result;
}
} // namespace okww::runtime::nightmare_test
