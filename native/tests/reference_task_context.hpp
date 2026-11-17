#pragma once
#include "runtime/task.hpp"
#include <opencv2/imgproc.hpp>
// Test-only frozen pre-migration task context. No production target includes it.
// Original UTF-8 LF SHA256: 596f87fbaf39c2085a75c2e7d07a086101c03d500972c86d1f34553cdcb7d641
namespace okww::runtime::reference_context {
class TaskContext {
    FrameSource source_;
    TextReader textReader_;
    bool handlingMonthlyWindow_=false;
    QJsonArray workflow_;QJsonObject timingHistory_;
    QString timingPath_,workflowTask_;double workflowBegan_=0;
    void publishWorkflow();
  public:
    Execution &execution;Input &input;Templates &templates;Profile &profile;
    std::function<void(QJsonObject)> progress;
    void beginWorkflow(QJsonArray,QString={});
    void workflowStep(const QString &,const QString &,const QString & = {});
    void reportProgress(QString,double=-1,double=-1,QString={},QString={});
    cv::Mat frame;uint64_t frameVersion=0;
    std::function<bool()> sourceRecovering;
    std::function<uint64_t()> sourceSequence;
    std::function<void(QString,QString)> info;
    std::shared_ptr<TaskState> state=std::make_shared<TaskState>();
    std::function<QDateTime()> wallTime=[] {return QDateTime::currentDateTime();};
    TaskContext(Execution &,Input &,Templates &,Profile &,FrameSource,TextReader);
    void refresh();void sleep(double);
    bool wait(const std::function<bool()> &,double,double=.05);
    std::optional<Match> find(const QString &,MatchOptions={});
    QVector<TextLine> text(QRect,double=.8);
    QVector<TextLine> text(QRect,double,Filter);
    QVector<TextLine> textScaled(QRect,double=.8,double=3);
    QVector<TextLine> text(double,double,double,double,double=.8);
    void clickRelative(double,double,double=0,bool=false);
    bool inTeam();PartyHud party();bool monthlyCard();void monthlyWindow();
    bool skipConfirm();bool travelButton();bool login();bool fastTravel();bool autoPick();bool autoDialog();
    QString task;QJsonObject taskOverrides;
    QJsonValue setting(const QString &key,QJsonValue fallback={}) const {
        if(taskOverrides.contains(key))return taskOverrides[key];return profile.value(task,key,fallback);
    }
    void key(const QString &,double=0,double=.02);
    std::optional<Match> findAny(const QStringList &,MatchOptions={});
    void click(QPointF,double=0);
    void click(const Match &match,double after=0){click(match.center(),after);}
    void click(const TextLine &line,double after=0){click(line.center(),after);}
};
inline QVector<TextLine> TaskContext::text(QRect area,double threshold,Filter filter){
    execution.checkpoint();auto result=textReader_(filtered(frame,filter),area,threshold);execution.checkpoint();return result;
}
inline QVector<TextLine> TaskContext::textScaled(QRect area,double threshold,double scale){
    execution.checkpoint();if(!std::isfinite(scale)||scale<=0||scale>8)fail("文字放大倍率无效");
    area=area.intersected({0,0,frame.cols,frame.rows});if(area.isEmpty())return {};
    cv::Mat enlarged;cv::resize(frame(cv::Rect(area.x(),area.y(),area.width(),area.height())),enlarged,{},scale,scale,cv::INTER_CUBIC);
    auto result=textReader_(enlarged,{0,0,enlarged.cols,enlarged.rows},threshold);
    for(auto &line:result)line.box={area.x()+pixelRound(line.box.x()/scale),area.y()+pixelRound(line.box.y()/scale),std::max(1,pixelRound(line.box.width()/scale)),std::max(1,pixelRound(line.box.height()/scale))};
    execution.checkpoint();return result;
}
inline TaskContext::TaskContext(Execution &e, Input &i, Templates &t, Profile &p, FrameSource source,
                         TextReader reader)
    : source_(std::move(source)), textReader_(std::move(reader)), execution(e), input(i), templates(t),
      profile(p) {}
inline void TaskContext::refresh() {
    execution.checkpoint();
    auto next = source_();
    bool recovering = false;
    if (next.empty() && sourceRecovering) {
        recovering = sourceRecovering();
        // Capture may recover between reading the frame and reading its status.
        // Recheck the frame before treating a cleared recovery flag as failure.
        if (!recovering) next = source_();
    }
    if (next.empty() && recovering) {
        input.release();
        const auto began = execution.now();
        double reported = -1;
        while (next.empty() && execution.now()-began < 120) {
            const auto elapsed=execution.now()-began;
            if (info && (reported < 0 || elapsed-reported >= 1)) {
                info("自动恢复",QString("等待采集恢复 · %1 秒").arg(int(elapsed)));
                reported = elapsed;
            }
            execution.wait(.1);
            next = source_();
            if (next.empty() && !sourceRecovering()) {
                next = source_();
                break;
            }
        }
        if (!next.empty() && info) info("自动恢复","采集画面已恢复，继续当前任务");
    }
    if (next.empty())
        fail("没有新鲜采集画面，自动任务已停止");
    frame = std::move(next);
    if (sourceSequence) frameVersion = sourceSequence();
    else ++frameVersion;
}
inline void TaskContext::sleep(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 86400)
        fail("等待时间无效");
    const auto began = execution.now();
    monthlyWindow();
    execution.wait(std::max(0., seconds - (execution.now() - began)));
    refresh();
}
inline bool TaskContext::wait(const std::function<bool()> &predicate, double seconds, double interval) {
    return execution.until(
        [&] {
            refresh();
            return predicate();
        },
        seconds, interval);
}
inline std::optional<Match> TaskContext::find(const QString &name, MatchOptions options) {
    execution.checkpoint();
    if (frame.empty())
        refresh();
    return templates.one(frame, name, options);
}
inline QVector<TextLine> TaskContext::text(QRect area, double threshold) {
    execution.checkpoint();
    auto result = textReader_(frame, area, threshold);
    execution.checkpoint();
    return result;
}
inline QVector<TextLine> TaskContext::text(double x, double y, double right, double bottom, double threshold) {
    return text(gameBox(frame, x, y, right, bottom), threshold);
}
inline void TaskContext::clickRelative(double x, double y, double after, bool hcenter) {
    // Navigation uses the configurable UI hold. Raw combat clicks retain their
    // explicit rotation timing and never inherit the UI hold setting.
    input.taskClick(gamePoint({frame.cols, frame.rows}, x, y, hcenter));
    if (after)
        sleep(after);
}
inline bool TaskContext::inTeam() { return party().visible; }
inline PartyHud TaskContext::party() {
    int found = 0, current = -1, index = 0;
    for (const auto &name : {"char_1_text", "char_2_text", "char_3_text"}) {
        // Small offsets from remote display scaling must not hide all three
        // slot numbers. Keep the search confined to each numbered HUD slot.
        if (find(name, {.horizontal = .006, .vertical = .012}) ||
            find(name, {.threshold = .84, .horizontal = .006, .vertical = .012,
                        .grayscale = true, .blurSigma = .9}))
            ++found;
        else if (current < 0)
            current = index;
        ++index;
    }
    const bool visible = found == 1 || found == 2;
    if (visible)
        state->loggedIn = true;
    return {visible, visible ? current : -1, found + 1};
}
inline bool TaskContext::monthlyCard() {
    if (!profile.value("Monthly Card Config", "Check Monthly Card", true).toBool())
        return false;
    bool clicked = false;
    for (int i = 0; i < 3; ++i) {
        const bool monthly =
            bool(find("monthly_card", {.threshold = .65, .horizontal = .05, .vertical = .05})) ||
            monthlyTextConfirmed(text(.28, .70, .72, .96, .7));
        const bool reward = !monthly && rewardTextConfirmed(text(.2, .12, .8, .96, .7));
        if (!monthly && !reward)
            return clicked;
        if (info)
            info("月卡", monthly ? "已确认领取界面" : "关闭领取结果");
        clickRelative(.5, .89, .8);
        clicked = true;
    }
    return clicked;
}
inline void TaskContext::monthlyWindow() {
    if (handlingMonthlyWindow_ || frame.empty() || state->inCombat || !input.idle() ||
        !profile.value("Monthly Card Config", "Check Monthly Card", true).toBool())
        return;
    const auto key =
        monthlyWindowKey(wallTime(), profile.value("Monthly Card Config", "Monthly Card Time", 4).toInt(4));
    if (key.isEmpty() || state->monthlyWindowHandled == key || !inTeam())
        return;
    state->monthlyWindowHandled = key;
    handlingMonthlyWindow_ = true;
    try {
        if (info)
            info("月卡", "到达设定的弹出时段，等待领取后继续当前步骤");
        const auto until = execution.now() + 120;
        do {
            refresh();
            if (monthlyCard())
                break;
            execution.wait(.25);
        } while (execution.now() < until);
    } catch (...) {
        handlingMonthlyWindow_ = false;
        throw;
    }
    handlingMonthlyWindow_ = false;
}
inline std::optional<Match> TaskContext::findAny(const QStringList &names, MatchOptions options) {
    for (const auto &name : names)
        if (auto found = find(name, options))
            return found;
    return {};
}
inline void TaskContext::click(QPointF point, double after) {
    input.taskClick(point);
    if (after)
        sleep(after);
}
inline bool TaskContext::skipConfirm() {
    auto confirm = findAny({"confirm_btn_hcenter_vcenter", "confirm_btn_highlight_hcenter_vcenter"},
                           {.horizontal = .1, .vertical = .1});
    if (!confirm)
        return false;
    auto check = find("skip_dialog_check", {.horizontal = .1, .vertical = .1});
    if (!check)
        check = find("skip_dialog_check",
                     {.search = relativeBox(frame, .35, .45, .55, .65), .templateScale = .75});
    if (!check)
        return false;
    sleep(.5);
    click(*check, .5);
    click(*confirm, .2);
    return true;
}
inline bool TaskContext::travelButton() {
    for (const auto &name : {"fast_travel_custom", "gray_teleport", "remove_custom"}) {
        if (!find(name, {.threshold = .7}))
            continue;
        sleep(.5);
        auto feature = find(name, {.threshold = .7});
        if (!feature)
            continue;
        click(*feature, 1);
        if (feature->name == "fast_travel_custom") {
            std::optional<Match> confirm;
            if (wait(
                    [&] {
                        confirm =
                            findAny({"confirm_btn_hcenter_vcenter", "confirm_btn_highlight_hcenter_vcenter"},
                                    {.threshold = .6});
                        return bool(confirm);
                    },
                    2)) {
                clickRelative(.49, .55, .5);
                click(*confirm, .5);
                if (wait(
                        [&] {
                            confirm = findAny(
                                {"confirm_btn_hcenter_vcenter", "confirm_btn_highlight_hcenter_vcenter"},
                                {.threshold = .6});
                            return bool(confirm);
                        },
                        1))
                    click(*confirm);
            }
        }
        if (feature->name != "remove_custom")
            wait([&] { return skipConfirm() || inTeam(); }, 3);
        return true;
    }
    return false;
}
inline bool TaskContext::login() {
    auto &c = *this;
    if (c.monthlyCard())
        return true;
    if (c.state->loggedIn || c.inTeam())
        return false;
    if (auto close = c.find("login_close", {.horizontal = .15, .vertical = .1})) {
        c.click(*close, 1);
        return true;
    }
    const QString loginPattern = "登录|登入|Log|点击连接|Click to Connect";
    const auto centerBox = gameBox(c.frame, .3, .3, .7, .7, true, true);
    auto center = c.text(centerBox);
    if (matchingText(center, loginPattern)) {
        if (matchingText(center, "\\+86"))
            return false;
        c.sleep(4);
        center = c.text(centerBox);
        auto login = matchingText(center, loginPattern);
        if (login && !matchingText(center, "\\+86")) {
            c.click(*login, 1);
            return true;
        }
        return false;
    }
    // Credential entry and agreement acceptance stay with the user.
    const auto full = c.text({0, 0, c.frame.cols, c.frame.rows});
    if (matchingText(full, "游戏即将重启|遊戲即將重啟|game will restart")) {
        if (auto confirm = matchingText(full, "^(?:确认|確認|Confirm)$")) {
            c.click(*confirm);
            c.sleep(90);
            return true;
        }
        return false;
    }
    const auto lower = c.text(.5, .5, 1, 1);
    if (auto start = matchingText(lower, "开始游戏|进入游戏|進入遊戲|Enter Game")) {
        if (!matchingText(lower, loginPattern)) {
            c.click(*start, 1);
            return true;
        }
    }
    if (c.find("switch_account", {.threshold = .7, .vertical = .1}) &&
        !c.text(gameBox(c.frame, .37, .63, .63, .99, true, true)).isEmpty()) {
        c.click(gamePoint({c.frame.cols, c.frame.rows}, .503, .926, true, true), 3);
        return true;
    }
    return false;
}
static bool fastTravel(TaskContext &c) {
    if (!c.find("gray_teleport"))
        return false;
    if (!matchingText(c.text(.7, .89, 1, 1), "Travel|快速旅行|前往|Proceed"))
        return false;
    return c.travelButton();
}
inline bool TaskContext::fastTravel(){return reference_context::fastTravel(*this);}
inline bool TaskContext::autoPick() {
    auto &c=*this;
    if (!c.inTeam())
        return false;
    const QRect original = c.templates.box("pick_up_f_hcenter_vcenter", {c.frame.cols, c.frame.rows});
    QRect search(pixelRound(original.x() - .3 * original.width()),
                 pixelRound(original.y() - 5 * original.height()), pixelRound(original.width() * 1.65),
                 pixelRound(original.height() * 7.5));
    const auto end = c.execution.now() + 1;
    while (c.execution.now() < end) {
        auto f = c.find("pick_up_f_hcenter_vcenter", {.search = search});
        if (!f)
            return false;
        const auto r = f->box;
        if (colorPercentage(c.frame, r, {235, 235, 235}, {255, 255, 255}) < .5) {
            c.sleep(.02);
            continue;
        }
        QRect dialog(r.x() + r.width() * 3, r.y() - r.height(), r.width() * 3, r.height() * 3);
        const bool speaking = bool(c.find("dialog_3_dots", {.threshold = .6, .search = dialog}));
        auto configured = c.setting(speaking ? "Pick Up White List" : "Pick Up Black List").toArray();
        bool matched = false;
        if (!configured.isEmpty()) {
            QRect textBox(pixelRound(r.x() + r.width() * 5.2), pixelRound(r.y() - .8 * r.height()),
                          r.width() * 7, pixelRound(r.height() * 5.5));
            const auto lines = c.text(textBox);
            for (const auto &line : lines) {
                for (auto entry : configured)
                    if (line.text.contains(entry.toString())) {
                        matched = true;
                        break;
                    }
                if (matched) {
                    if (line.box.y() > textBox.y() + r.height())
                        c.input.scroll(1, QPointF(c.frame.cols * .5, c.frame.rows * .5));
                    break;
                }
            }
        }
        if ((speaking && !matched) || (!speaking && matched))
            return false;
        for (int i = 0; i < 3; ++i)
            c.key("f", .2);
        return true;
    }
    return false;
}
inline bool TaskContext::autoDialog() {
    auto &c=*this;
    if (c.inTeam())
        return false;
    bool skipped = false;
    const auto deadline = c.execution.now() + 5;
    while (c.execution.now() < deadline) {
        auto skip = c.find("skip_dialog", {.threshold = .75, .horizontal = .02, .filter = Filter::Dialog});
        if (!skip)
            skip = c.find("skip_dialog_new", {.threshold = .75, .filter = Filter::Dialog});
        if (!skip)
            break;
        c.click(*skip, .2);
        skipped = true;
    }
    if (skipped) {
        c.wait(
            [&] {
                if (c.skipConfirm() || c.inTeam())
                    return true;
                auto button = c.findAny({"skip_quest_confirm", "skip_quest_confirm_new"});
                if (button) {
                    c.sleep(.2);
                    c.click(*button);
                    return true;
                }
                return false;
            },
            3);
        return true;
    }
    if (c.find("message", {.horizontal = .15})) {
        if (auto dialog = c.find("message_dialog", {.horizontal = .2, .vertical = .4})) {
            const auto r = dialog->box;
            c.click({r.x() + c.frame.cols * .315, r.y() + r.height() * 3.}, .2);
            return true;
        }
    }
    if (c.setting("继续普通NPC对白", false).toBool() && hasContinueMarker(c.frame)) {
        auto r = relativeBox(c.frame, .35, .73, .65, .79);
        cv::Mat hsv, mask;
        cv::cvtColor(c.frame(cv::Rect(r.x(), r.y(), r.width(), r.height())), hsv, cv::COLOR_BGR2HSV);
        cv::inRange(hsv, cv::Scalar(10, 90, 110), cv::Scalar(45, 255, 255), mask);
        if (cv::countNonZero(mask) < c.frame.cols * c.frame.rows * .00008)
            return false;
        if (!matchingText(c.text(r, .85), "^[\\x{4e00}-\\x{9fff}A-Za-z· :]{1,24}$"))
            return false;
        const auto lines = c.text(.12, .79, .88, .92, .85);
        if (std::any_of(lines.begin(), lines.end(),
                        [](const auto &line) { return line.text.trimmed().size() >= 2; })) {
            c.clickRelative(.5, .945, .3);
            return true;
        }
    }
    return false;
}
inline void TaskContext::key(const QString &key, double after, double hold) {
    input.key(key, hold);
    if (after)
        sleep(after);
}
inline void TaskContext::beginWorkflow(QJsonArray steps, QString timingPath) {
    workflow_ = {};
    workflowTask_ = task;
    workflowBegan_ = execution.now();
    timingPath_ = std::move(timingPath);
    timingHistory_ = {};
    try { if (QFile::exists(timingPath_)) timingHistory_ = readJson(timingPath_); } catch (...) {}
    for (auto value : steps) {
        auto step = value.toObject();
        step["state"] = "pending";
        step["elapsed_seconds"] = 0;
        const auto samples = timingHistory_.value(workflowTask_ + ':' + step["id"].toString()).toArray();
        double sum = 0;
        int valid = 0;
        for (auto sample : samples) if (sample.isDouble() && sample.toDouble() > 0 && sample.toDouble() < 86400) {
            sum += sample.toDouble(); ++valid;
        }
        if (valid) step["estimate_seconds"] = sum / valid;
        step["estimate_source"] = valid ? "history" : "reference";
        workflow_.append(step);
    }
    publishWorkflow();
}
inline void TaskContext::workflowStep(const QString &id, const QString &state, const QString &detail) {
    if (!QStringList{"running", "completed", "skipped", "failed"}.contains(state)) return;
    for (int i = 0; i < workflow_.size(); ++i) {
        auto step = workflow_[i].toObject();
        if (step["id"] != id) continue;
        const auto previous = step["state"].toString();
        if (previous == state) return;
        if (state == "running") step["started"] = execution.now();
        else if (previous == "running") step["elapsed_seconds"] = std::max(0., execution.now() - step["started"].toDouble());
        step["state"] = state;
        if (!detail.isEmpty()) step["detail"] = detail.left(500);
        workflow_[i] = step;
        if (state == "completed" && previous == "running" && step["elapsed_seconds"].toDouble() > 0 && !timingPath_.isEmpty()) {
            const auto key = workflowTask_ + ':' + id;
            auto samples = timingHistory_.value(key).toArray();
            samples.append(step["elapsed_seconds"]);
            while (samples.size() > 12) samples.removeFirst();
            timingHistory_[key] = samples;
            try { writeJson(timingPath_, timingHistory_); } catch (...) { /* Estimates must never interrupt a task. */ }
        }
        publishWorkflow();
        return;
    }
}
inline void TaskContext::publishWorkflow() {
    if (!progress) return;
    auto steps = workflow_;
    for (int i = 0; i < steps.size(); ++i) {
        auto step = steps[i].toObject();
        if (step["state"] == "running") step["elapsed_seconds"] = std::max(0., execution.now() - step["started"].toDouble());
        step.remove("started");
        steps[i] = step;
    }
    progress({{"flow", steps}, {"flow_task", workflowTask_},
              {"flow_elapsed_seconds", std::max(0., execution.now()-workflowBegan_)},
              {"flow_observed_at", double(QDateTime::currentMSecsSinceEpoch())}});
}
inline void TaskContext::reportProgress(QString stage, double current, double total, QString unit, QString detail) {
    if (!progress) return;
    if (!std::isfinite(current) || !std::isfinite(total) || current < 0 || total <= 0) current = total = -1;
    progress({{"stage", stage.left(160)}, {"current", current}, {"total", total},
              {"unit", unit.left(16)}, {"detail", detail.left(500)}, {"source_task", task}});
}
}
