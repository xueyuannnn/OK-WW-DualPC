#include "task.hpp"
#include <opencv2/imgproc.hpp>
namespace okww::runtime {
TaskContext::TaskContext(Execution &e, Input &i, Templates &t, Profile &p, FrameSource source,
                         TextReader reader)
    : source_(std::move(source)), textReader_(std::move(reader)), execution(e), input(i), templates(t),
      profile(p) {}
void TaskContext::refresh() {
    execution.checkpoint();
    auto next = source_();
    if (next.empty())
        fail("没有新鲜采集画面，自动任务已停止");
    frame = std::move(next);
    if (sourceSequence) frameVersion = sourceSequence();
    else ++frameVersion;
}
void TaskContext::sleep(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 86400)
        fail("等待时间无效");
    const auto began = execution.now();
    monthlyWindow();
    execution.wait(std::max(0., seconds - (execution.now() - began)));
    refresh();
}
bool TaskContext::wait(const std::function<bool()> &predicate, double seconds, double interval) {
    return execution.until(
        [&] {
            refresh();
            return predicate();
        },
        seconds, interval);
}
std::optional<Match> TaskContext::find(const QString &name, MatchOptions options) {
    execution.checkpoint();
    if (frame.empty())
        refresh();
    return templates.one(frame, name, options);
}
std::optional<Match> TaskContext::findAny(const QStringList &names, MatchOptions options) {
    for (const auto &name : names)
        if (auto found = find(name, options))
            return found;
    return {};
}
QVector<TextLine> TaskContext::text(QRect area, double threshold) {
    execution.checkpoint();
    auto result = textReader_(frame, area, threshold);
    execution.checkpoint();
    return result;
}
QVector<TextLine> TaskContext::text(QRect area, double threshold, Filter filter) {
    execution.checkpoint();
    auto result = textReader_(filtered(frame, filter), area, threshold);
    execution.checkpoint();
    return result;
}
QVector<TextLine> TaskContext::text(double x, double y, double right, double bottom, double threshold) {
    return text(gameBox(frame, x, y, right, bottom), threshold);
}
QVector<TextLine> TaskContext::textScaled(QRect area, double threshold, double scale) {
    execution.checkpoint();
    if (!std::isfinite(scale) || scale <= 0 || scale > 8)
        fail("文字放大倍率无效");
    area = area.intersected({0, 0, frame.cols, frame.rows});
    if (area.isEmpty())
        return {};
    cv::Mat enlarged;
    cv::resize(frame(cv::Rect(area.x(), area.y(), area.width(), area.height())), enlarged, {}, scale, scale,
               cv::INTER_CUBIC);
    auto result = textReader_(enlarged, {0, 0, enlarged.cols, enlarged.rows}, threshold);
    for (auto &line : result)
        line.box = {area.x() + pixelRound(line.box.x() / scale), area.y() + pixelRound(line.box.y() / scale),
                    std::max(1, pixelRound(line.box.width() / scale)),
                    std::max(1, pixelRound(line.box.height() / scale))};
    execution.checkpoint();
    return result;
}
void TaskContext::click(QPointF point, double after) {
    input.taskClick(point);
    if (after)
        sleep(after);
}
void TaskContext::clickRelative(double x, double y, double after, bool hcenter) {
    // Navigation uses the configurable UI hold. Raw combat clicks retain their
    // explicit rotation timing and never inherit the UI hold setting.
    input.taskClick(gamePoint({frame.cols, frame.rows}, x, y, hcenter));
    if (after)
        sleep(after);
}
void TaskContext::key(const QString &key, double after, double hold) {
    input.key(key, hold);
    if (after)
        sleep(after);
}
QString TaskContext::hotkey(const QString &setting, const QString &fallback) const {
    return profile.value("Game Hotkey", setting, fallback).toString();
}
bool TaskContext::inTeam() { return party().visible; }
PartyHud TaskContext::party() {
    int found = 0, current = -1, index = 0;
    for (const auto &name : {"char_1_text", "char_2_text", "char_3_text"}) {
        if (find(name))
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
std::optional<TextLine> matchingText(const QVector<TextLine> &lines, const QString &pattern) {
    QRegularExpression regex(pattern, QRegularExpression::CaseInsensitiveOption);
    if (!regex.isValid())
        fail("文本匹配规则无效");
    for (const auto &line : lines)
        if (regex.match(line.text).hasMatch())
            return line;
    return {};
}
bool TaskContext::monthlyCard() {
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
QString monthlyWindowKey(QDateTime now, int hour) {
    if (hour < 1 || hour > 24 || !now.isValid())
        fail("月卡时间需要为 1 到 24 点");
    auto popup = now;
    popup.setTime(QTime(hour % 24, 0));
    if (hour == 24 && now.time() >= QTime(23, 59, 30))
        popup = popup.addDays(1);
    const auto offset = popup.secsTo(now);
    if (offset < -30 || offset >= 90)
        return {};
    return popup.toString("yyyy-MM-dd/HH");
}
void TaskContext::monthlyWindow() {
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
bool TaskContext::skipConfirm() {
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
bool TaskContext::travelButton() {
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
static bool fastTravel(TaskContext &c) {
    if (!c.find("gray_teleport"))
        return false;
    if (!matchingText(c.text(.7, .89, 1, 1), "Travel|快速旅行|前往|Proceed"))
        return false;
    return c.travelButton();
}
bool TaskContext::login() {
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
static bool autoPick(TaskContext &c) {
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
static bool autoDialog(TaskContext &c) {
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
static QString normalized(QString s) { return s.remove(QRegularExpression("\\s+")).toLower(); }
bool monthlyTextConfirmed(const QVector<TextLine> &lines) {
    bool claim = false, days = false;
    const QRegularExpression remaining("(剩余|剩餘)\\d+天|remaining\\d+days?|\\d+days?remaining");
    for (const auto &line : lines) {
        const auto s = normalized(line.text);
        claim |= ((s.contains("点击领取") || s.contains("點擊領取")) &&
                  (s.contains("今日") || s.contains("本日")) && (s.contains("月相") || s.contains("月卡"))) ||
                 (s.contains("claim") && (s.contains("today") || s.contains("daily")) &&
                  (s.contains("lunite") || s.contains("subscription")));
        days |= remaining.match(s).hasMatch();
    }
    return claim && days;
}
bool rewardTextConfirmed(const QVector<TextLine> &lines) {
    bool heading = false;
    QString combined;
    const QRegularExpression title(
        "^(获得(?:物品|奖励)?|獲得(?:物品|獎勵)?|rewardsobtained|itemsobtained|obtained)[.:：·]*$");
    for (const auto &line : lines) {
        const auto s = normalized(line.text);
        heading |= title.match(s).hasMatch();
        combined += s;
    }
    if (!heading)
        return false;
    for (const auto &word : {"点击空白", "點擊空白", "clickanywhere", "clickthedark", "tapanywhere"})
        if (combined.contains(word))
            return true;
    return false;
}
bool hasContinueMarker(const cv::Mat &frame) {
    auto rect = relativeBox(frame, .475, .925, .525, .98);
    if (rect.isEmpty())
        return false;
    auto area = frame(cv::Rect(rect.x(), rect.y(), rect.width(), rect.height()));
    cv::Mat mask(area.rows, area.cols, CV_8U);
    for (int y = 0; y < area.rows; ++y)
        for (int x = 0; x < area.cols; ++x) {
            auto p = area.at<cv::Vec3b>(y, x);
            const int low = std::min({p[0], p[1], p[2]}), high = std::max({p[0], p[1], p[2]});
            mask.at<uchar>(y, x) = low >= 180 && high - low < 65 ? 255 : 0;
        }
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    for (const auto &contour : contours) {
        auto b = cv::boundingRect(contour);
        const double aspect = double(b.width) / b.height, fill = cv::contourArea(contour) / b.area();
        if (b.width >= frame.cols * .006 && b.width <= frame.cols * .023 && b.height >= frame.rows * .012 &&
            b.height <= frame.rows * .04 && aspect >= .5 && aspect <= 1.9 && fill >= .12 && fill <= .8)
            return true;
    }
    return false;
}
QVector<TaskDefinition> builtinTasks() {
    return {{"AutoLoginTask", true, 5, [](TaskContext &c) { return c.login(); }},
            {"FastTravelTask", true, .1, fastTravel},
            {"AutoPickTask", true, .1, autoPick},
            {"AutoDialogTask", true, .5, autoDialog}};
}
} // namespace okww::runtime
