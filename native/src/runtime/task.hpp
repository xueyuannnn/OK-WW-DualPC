#pragma once
#include "input.hpp"
#include "ocr.hpp"
#include "profile.hpp"

namespace okww::runtime {
using FrameSource = std::function<cv::Mat()>;
using TextReader = std::function<QVector<TextLine>(const cv::Mat &, QRect, double)>;
struct TaskState {
    bool loggedIn = false;
    bool inCombat = false, retryingStep = false;
    std::atomic<uint64_t> combatEnableRevision{0};
    QString monthlyWindowHandled;
};
struct PartyHud {
    bool visible = false;
    int current = -1, size = 0;
};
class TaskContext {
    FrameSource source_;
    TextReader textReader_;
    bool handlingMonthlyWindow_ = false;

  public:
    Execution &execution;
    Input &input;
    Templates &templates;
    Profile &profile;
    cv::Mat frame;
    uint64_t frameVersion = 0;
    QString task;
    QString gameLanguage = "zh_CN";
    QJsonObject taskOverrides;
    std::function<void(QString, QString)> info;
    std::function<uint64_t()> sourceSequence;
    std::function<void(QJsonObject)> combatInfo;
    std::function<void(QString, QString, bool)> notify;
    std::function<bool()> physicalFDown;
    std::function<QDateTime()> wallTime = [] { return QDateTime::currentDateTime(); };
    std::shared_ptr<TaskState> state = std::make_shared<TaskState>();
    TaskContext(Execution &execution, Input &input, Templates &templates, Profile &profile,
                FrameSource source, TextReader textReader);
    void refresh();
    void sleep(double seconds);
    bool wait(const std::function<bool()> &predicate, double seconds, double interval = .05);
    std::optional<Match> find(const QString &name, MatchOptions options = {});
    std::optional<Match> findAny(const QStringList &names, MatchOptions options = {});
    QVector<TextLine> text(QRect area, double threshold = .8);
    QVector<TextLine> text(QRect area, double threshold, Filter filter);
    QVector<TextLine> textScaled(QRect area, double threshold = .8, double scale = 3);
    QVector<TextLine> text(double x, double y, double right, double bottom, double threshold = .8);
    QJsonValue setting(const QString &key, QJsonValue fallback = {}) const {
        if (taskOverrides.contains(key))
            return taskOverrides[key];
        return profile.value(task, key, fallback);
    }
    void click(QPointF point, double after = 0);
    void click(const Match &match, double after = 0) { click(match.center(), after); }
    void click(const TextLine &text, double after = 0) { click(text.center(), after); }
    void clickRelative(double x, double y, double after = 0, bool hcenter = false);
    void key(const QString &key, double after = 0, double hold = .02);
    QString hotkey(const QString &setting, const QString &fallback) const;
    bool inTeam();
    PartyHud party();
    bool monthlyCard();
    void monthlyWindow();
    bool login();
    bool skipConfirm();
    bool travelButton();
};
class TaskScope {
    TaskContext &context_;
    QString previous_;
    QJsonObject overrides_;

  public:
    TaskScope(TaskContext &context, QString id, QJsonObject overrides = {})
        : context_(context), previous_(context.task), overrides_(context.taskOverrides) {
        context_.task = std::move(id);
        context_.taskOverrides = std::move(overrides);
    }
    ~TaskScope() {
        context_.task = std::move(previous_);
        context_.taskOverrides = std::move(overrides_);
    }
    TaskScope(const TaskScope &) = delete;
    TaskScope &operator=(const TaskScope &) = delete;
};
struct TaskDefinition {
    QString id;
    bool trigger = true;
    double interval = .1;
    std::function<bool(TaskContext &)> step;
};
QVector<TaskDefinition> builtinTasks();
std::optional<TextLine> matchingText(const QVector<TextLine> &lines, const QString &pattern);
bool monthlyTextConfirmed(const QVector<TextLine> &lines);
QString monthlyWindowKey(QDateTime now, int hour);
bool rewardTextConfirmed(const QVector<TextLine> &lines);
bool hasContinueMarker(const cv::Mat &frame);
} // namespace okww::runtime
