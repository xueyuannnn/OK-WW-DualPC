#pragma once
#include "runtime/echo_operations.hpp"
#include "runtime/workflow_abi.hpp"
namespace okww::runtime::echo_operation_policy_test {
struct Probe;
struct Execution {
    Probe &p;
    double now() const;
    void checkpoint();
    void wait(double);
};
struct Profile {
    Probe &p;
    QJsonObject group(QString);
};
struct Context {
    Probe &p;
    Execution execution;
    Profile profile;
    QString task = "outer", gameLanguage;
    QJsonObject taskOverrides{{"old", true}};
    cv::Mat frame = cv::Mat(1080, 1920, CV_8UC3);
    std::function<void(QString, QString)> info;
    std::function<void(QString, QString, bool)> notify;
    void refresh();
    void sleep(double);
    void key(QString, double after = 0, double hold = .02);
    void clickRelative(double, double, double after = 0, bool center = false);
    void click(const TextLine &, double after = 0);
    void click(const Match &, double after = 0);
    bool inTeam();
    QString hotkey(QString, QString);
    QJsonValue setting(QString, QJsonValue);
    QVector<TextLine> text(double, double, double, double, double threshold = .8);
    std::optional<Match> find(QString, MatchOptions = {});
    std::optional<Match> findAny(QStringList);
    bool wait(const std::function<bool()> &, double, double interval = .05);
};
struct TaskScope {
    Context &c;
    QString previous;
    QJsonObject overrides;
    TaskScope(Context &, QString);
    ~TaskScope();
};
struct Navigation {
    Probe &p;
    void ensureMain();
    void confirmDialog(bool);
};
double number(QString);
QString fixedMain(QString);
void awaitEchoMergeResult(Context &);
struct Probe {
    int scene, operation, upgrades = 0, finished = 0, waitCalls = 0, completed = 0, commits = 0,
                          checkpoints = 0;
    double time = 0;
    bool team = true;
    QString target;
    QJsonObject saved;
    QJsonArray events;
    Context c_;
    Navigation navigation_;
    Probe(int value, int kind)
        : scene(value), operation(kind),
          c_{*this, {*this}, {*this}, "outer", QStringList{"zh_CN", "zh_TW", "en_US"}.at(value % 3)},
          navigation_{*this} {
        saved = {{"有效词条", QJsonArray{"攻击", "生命", "暴击", "暴击伤害", "共鸣效率"}},
                 {"必须有双爆", bool(value & 16)},
                 {"双爆出现之前必须全有效词条", bool(value & 32)},
                 {"首条双爆>=", value % 5},
                 {"双爆总计>=", value % 7},
                 {"第一条必须为有效词条", bool(value & 64)},
                 {"有效词条>=", value % 6},
                 {"Pause after Success", bool(value & 8)}};
        target = value & 128 ? "unknown" : value & 256 ? "衍射伤害加成" : "攻击";
        if (value & 2)
            c_.info = [&](QString k, QString v) { events.append(QJsonArray{"info", k, v}); };
        if (value & 4)
            c_.notify = [&](QString k, QString v, bool error) {
                events.append(QJsonArray{"notify", k, v, error});
            };
    }
    std::optional<TextLine> cultivate() {
        events.append("cultivate");
        if (scene % 29 == 0)
            return {};
        return TextLine{"培养", {100, 100, 40, 20}, .9};
    }
    bool zeroLevel(bool change = false) {
        events.append(QJsonArray{"zero", change});
        return finished < 1 && scene % 31 != 0;
    }
    void enterCultivate() {
        events.append("enter");
        upgrades = 0;
    }
    void upgrade() {
        events.append("upgrade");
        ++upgrades;
        if (scene % 7 == 0)
            throw std::runtime_error("upgrade result uncertain");
    }
    void mark(bool lock) { events.append(QJsonArray{"mark", lock}); }
    void screenshot(QString category, int count, QString reason = {}) {
        events.append(QJsonArray{"screenshot", category, count, reason});
    }
    void backToBag() {
        events.append("back");
        ++finished;
    }
    void centered(double x, double y, double after) { c_.clickRelative(x, y, after, true); }
    std::optional<TextLine> waitText(QRect area, const QString &pattern, double timeout = 5) {
        events.append(
            QJsonArray{"wait-text", area.x(), area.y(), area.width(), area.height(), pattern, timeout});
        if (++waitCalls == 1 + scene % 9)
            return {};
        return TextLine{pattern, {500, 300, 100, 20}, .9};
    }
    int referenceEnhance()
#include "reference_echo_enhance_body.hpp"
        int referenceChange()
#include "reference_echo_change_body.hpp"
            int referenceMerge()
#include "reference_echo_merge_body.hpp"
                int candidate() {
        std::optional<TaskScope> scope;
        QVector<TextLine> lines;
        QVector<EchoStat> stats;
        EchoDecision decision;
        QJsonObject config;
        QString target;
        QRect screen;
        struct Value {
            std::optional<TextLine> text;
        } value;
        std::optional<Match> selectAll;
        return int(rust::workflowRun(operation == 0   ? rust::WorkflowMethod::EchoEnhance
                                     : operation == 1 ? rust::WorkflowMethod::EchoChange
                                                      : rust::WorkflowMethod::EchoMerge,
                                     {}, [&](rust::WorkflowOp op, const double *a, double *out) {
                                         switch (op) {
#include "echo_operation_policy_host_cases.hpp"
                                         default:
                                             fail("Unknown echo item policy SDK action");
                                         }
                                     })[0]);
    }
};
inline double Execution::now() const { return p.time; }
inline void Execution::checkpoint() {
    if (++p.checkpoints > 1000)
        throw std::runtime_error("replay deadline");
    if (p.scene % 101 == 0)
        throw Cancelled{};
}
inline void Execution::wait(double value) {
    p.time += value;
    checkpoint();
}
inline QJsonObject Profile::group(QString name) {
    p.events.append(QJsonArray{"config", name});
    return p.saved;
}
inline void Context::refresh() {
    p.events.append("refresh");
    execution.checkpoint();
}
inline void Context::sleep(double value) {
    p.events.append(QJsonArray{"sleep", value});
    p.time += value;
    execution.checkpoint();
}
inline void Context::key(QString key, double after, double hold) {
    p.events.append(QJsonArray{"key", key, after, hold});
    p.time += after + hold;
    if (p.scene % 37 != 0)
        p.team = false;
}
inline void Context::clickRelative(double x, double y, double after, bool center) {
    p.events.append(QJsonArray{"click", x, y, after, center});
    p.time += after;
    if (x == .782 && y == .910)
        ++p.commits;
}
inline void Context::click(const TextLine &line, double after) {
    p.events.append(QJsonArray{"text-click", line.text, line.box.x(), line.box.y(), after});
    p.time += after;
    if (line.text == "^数据重构$")
        ++p.commits;
}
inline void Context::click(const Match &m, double after) {
    p.events.append(QJsonArray{"match-click", m.name, after});
    p.time += after;
}
inline bool Context::inTeam() {
    p.events.append("team");
    return p.team;
}
inline QString Context::hotkey(QString name, QString fallback) {
    p.events.append(QJsonArray{"hotkey", name, fallback});
    return fallback;
}
inline QJsonValue Context::setting(QString name, QJsonValue fallback) {
    p.events.append(QJsonArray{"setting", name, fallback});
    return p.target;
}
inline QVector<TextLine> Context::text(double x, double y, double right, double bottom, double threshold) {
    p.events.append(QJsonArray{"text", x, y, right, bottom, threshold});
    if (p.operation == 2)
        return {{p.completed < p.scene % 3 ? "100/100" : "99/100", {100, 100, 80, 20}, .9}};
    if (p.operation == 1)
        return {{p.scene & 512 ? p.target : QString("生命"), {100, 100, 80, 20}, .9}};
    QVector<TextLine> lines;
    const int count = p.scene & 1024 ? std::max(0, p.upgrades - 1) : p.upgrades;
    for (int i = 0; i < count; ++i) {
        lines.append(
            {QStringList{"攻击", "暴击", "暴击伤害", "生命", "共鸣效率"}.at(i), {100, 30 * i, 80, 20}, .9});
        lines.append({p.scene & 2048 ? "0" : "8.1%", {250, 30 * i, 80, 20}, .9});
    }
    return lines;
}
inline std::optional<Match> Context::find(QString name, MatchOptions options) {
    p.events.append(QJsonArray{"find", name, options.horizontal});
    return p.scene % 7 == 0 ? std::optional<Match>{}
                            : std::optional<Match>{Match{name, {100, 100, 80, 20}, .9}};
}
inline std::optional<Match> Context::findAny(QStringList names) {
    p.events.append(QJsonArray{"find-any", names.join('|')});
    return p.scene & 64 ? std::optional<Match>{Match{names.first(), {100, 100, 80, 20}, .9}}
                        : std::optional<Match>{};
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
inline TaskScope::TaskScope(Context &context, QString name)
    : c(context), previous(c.task), overrides(c.taskOverrides) {
    c.p.events.append(QJsonArray{"scope", name});
    c.task = name;
}
inline TaskScope::~TaskScope() {
    c.p.events.append("scope-end");
    c.task = previous;
    c.taskOverrides = overrides;
}
inline void Navigation::ensureMain() {
    p.events.append("main");
    p.team = true;
}
inline void Navigation::confirmDialog(bool yes) { p.events.append(QJsonArray{"confirm", yes}); }
inline double number(QString value) {
    value.replace(QChar(0xff05), '%');
    bool ok = false;
    const auto result = value.section('%', 0, 0).trimmed().toDouble(&ok);
    return ok && std::isfinite(result) ? result : 0;
}
// This SDK fixture supplies already-normalized current main attributes.
inline QString fixedMain(QString value) { return value; }
inline void awaitEchoMergeResult(Context &c) {
    c.p.events.append("merge-result");
    if (c.p.scene % 11 == 0)
        throw std::runtime_error("merge result uncertain");
    ++c.p.completed;
}
inline QJsonObject trace(int operation, int scene, bool rust) {
    Probe p(scene * 37 % 4096, operation);
    QJsonObject out;
    try {
        out["result"] = rust             ? p.candidate()
                        : operation == 0 ? p.referenceEnhance()
                        : operation == 1 ? p.referenceChange()
                                         : p.referenceMerge();
        out["error"] = "";
    } catch (const Cancelled &) {
        out["error"] = "cancelled";
    } catch (const std::exception &e) {
        out["error"] = errorText(e);
    }
    out["events"] = p.events;
    out["task"] = p.c_.task;
    out["overrides"] = p.c_.taskOverrides;
    out["upgrades"] = p.upgrades;
    out["finished"] = p.finished;
    out["commits"] = p.commits;
    out["completed"] = p.completed;
    out["time"] = p.time;
    return out;
}
} // namespace okww::runtime::echo_operation_policy_test
