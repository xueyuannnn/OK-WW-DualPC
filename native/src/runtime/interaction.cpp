#include "interaction.hpp"
#include <numbers>
namespace okww::runtime {
namespace {
// A single owner for movement keys and run-button; cancellation uses release()
// because ordinary keyUp is correctly rejected by a stopped execution epoch.
class Movement {
    TaskContext &c_;
    QString key_;
    bool running_ = false;

  public:
    explicit Movement(TaskContext &context) : c_(context) {}
    ~Movement() {
        if (!key_.isEmpty() || running_)
            c_.input.release();
    }
    void direction(const QString &key) {
        if (key == key_)
            return;
        if (!key_.isEmpty()) {
            c_.input.keyUp(key_);
            key_.clear();
            c_.sleep(.01);
        }
        if (!key.isEmpty()) {
            c_.input.keyDown(key);
            key_ = key;
        }
    }
    void run() {
        c_.sleep(.1);
        c_.input.buttonDown("right");
        running_ = true;
    }
    void stopRunning() {
        if (running_) {
            c_.input.buttonUp("right");
            running_ = false;
        }
    }
    void stop() {
        direction({});
        if (running_) {
            c_.sleep(.1);
            c_.input.buttonUp("right");
            running_ = false;
        }
    }
};
} // namespace
QRect fSearchBox(TaskContext &c) {
    const auto r = c.templates.box("pick_up_f_hcenter_vcenter", {c.frame.cols, c.frame.rows});
    return {pixelRound(r.x() - .3 * r.width()), r.y() - 5 * r.height(), pixelRound(r.width() * 1.65),
            pixelRound(r.height() * 7.5)};
}
std::optional<Match> findInteract(TaskContext &c, const QString &pattern) {
    const auto f = c.find("pick_up_f_hcenter_vcenter", {.search = fSearchBox(c)});
    if (!f || pattern.isEmpty())
        return f;
    double white = 0;
    c.wait(
        [&] {
            white = colorPercentage(c.frame, f->box, {235, 235, 235}, {255, 255, 255});
            return white > .5;
        },
        1, .01);
    if (white < .5)
        return {};
    const auto r = f->box;
    const QRect text(pixelRound(r.x() + r.width() * 5.2), pixelRound(r.y() - .8 * r.height()), r.width() * 7,
                     pixelRound(r.height() * 5.5));
    if (auto line = matchingText(c.text(text), pattern)) {
        if (line->box.y() > text.y() + r.height())
            c.input.scroll(1, QPointF(c.frame.cols * .5, c.frame.rows * .5));
        return f;
    }
    return {};
}
bool isDialogInteract(TaskContext &c) {
    const auto f = findInteract(c);
    if (!f)
        return false;
    const auto r = f->box;
    const QRect search(r.x() + r.width() * 3, r.y() - r.height(), pixelRound(r.width() * 2.8),
                       r.height() * 3);
    return bool(c.find("dialog_3_dots", {.threshold = .6, .search = search}));
}
bool WorldInteraction::claimDialog() {
    return !c_.inTeam() &&
           bool(c_.find("claim_cancel_button_hcenter_vcenter", {.horizontal = .05, .vertical = .1}));
}
bool WorldInteraction::dismissClaim() {
    if (!c_.wait([&] { return claimDialog(); }, 1.5))
        return false;
    c_.sleep(.5);
    c_.key("esc", .5);
    return true;
}
bool WorldInteraction::pick(bool handleClaim, bool echoOnly) {
    if (!findInteract(c_, echoOnly ? "吸收|Absorb" : ""))
        return false;
    c_.key("f", echoOnly ? 0 : 1);
    return !handleClaim || !dismissClaim();
}
bool WorldInteraction::walkUntil(const std::function<bool()> &condition, const QString &direction,
                                 double timeout, bool running, bool required, double postWalk) {
    if (!std::isfinite(timeout) || timeout < 0 || postWalk < 0)
        fail("移动等待参数无效");
    if (timeout == 0)
        return false;
    Movement movement(c_);
    movement.direction(direction);
    if (running)
        movement.run();
    const bool found = c_.wait(condition, timeout, .02);
    if (found && postWalk > 0)
        c_.sleep(postWalk);
    movement.stop();
    if (!found && required)
        throw NavigationFailure("移动后未找到目标交互提示");
    return found;
}
bool WorldInteraction::walkToInteract(const QString &direction, double timeout, bool required,
                                      double backward, const QString &pattern, bool checkCombat,
                                      bool running) {
    const auto condition = [&] {
        return bool(findInteract(c_, pattern)) || (checkCombat && inCombat_ && inCombat_());
    };
    if (findInteract(c_, pattern))
        return true;
    c_.input.click({}, .01, "middle");
    c_.sleep(.2);
    if (backward > 0 && walkUntil(condition, "s", backward, running, required))
        return true;
    return walkUntil(condition, direction, timeout, running, required);
}
QVector<Match> WorldInteraction::echoes(double threshold) {
    if (!detect_)
        fail("声骸识别模型尚未初始化");
    c_.execution.checkpoint();
    auto result = detect_(c_.frame, threshold);
    c_.execution.checkpoint();
    for (auto &box : result) {
        box.box.setY(box.box.y() + box.box.height() / 3);
        box.box.setHeight(1);
    }
    return result;
}
bool WorldInteraction::walkToEcho(double timeout, double threshold, const std::function<void()> &update) {
    Movement movement(c_);
    const auto end = c_.execution.now() + timeout;
    double noEcho = -1;
    bool moving = false;
    while (c_.execution.now() < end) {
        c_.sleep(.001);
        if (pick()) {
            movement.stop();
            return true;
        }
        if (inCombat_ && inCombat_()) {
            movement.stop();
            return false;
        }
        const auto found = echoes(threshold);
        if (found.empty()) {
            if (noEcho < 0)
                noEcho = c_.execution.now();
            else if (c_.execution.now() - noEcho > 3)
                break;
        } else
            noEcho = -1;
        movement.direction(echoWalkDirection(found, {c_.frame.cols, c_.frame.rows}, moving));
        moving = true;
        if (update)
            update();
    }
    movement.stop();
    return false;
}
void WorldInteraction::centerCamera() {
    c_.input.click(QPointF(c_.frame.cols * .5, c_.frame.rows * .5), .2, "middle");
    c_.sleep(1);
}
std::pair<bool, bool> WorldInteraction::findEcho(bool useColor, bool turn, double timeout, double threshold,
                                                 const std::function<void()> &update) {
    if (pick(true, true)) {
        c_.sleep(.5);
        return {true, true};
    }
    int maxCount = 0;
    for (int i = 0; i < 4; ++i) {
        if (turn)
            centerCamera();
        const auto found = echoes(threshold);
        maxCount = std::max(maxCount, int(found.size()));
        if (!found.empty())
            return {walkToEcho(timeout, .5, update), maxCount > 1};
        const auto front =
            Templates::anchored({672, 378, 576, 194}, {1920, 1080}, {c_.frame.cols, c_.frame.rows}, true);
        if (useColor && colorPercentage(c_.frame, front, {130, 150, 200}, {170, 220, 255}) > .02)
            return {walkToEcho(8, .5, update), maxCount > 1};
        if (!turn && i == 0)
            return {false, maxCount > 1};
        c_.key("a", .5, .05);
    }
    centerCamera();
    return {false, maxCount > 1};
}
bool WorldInteraction::circleEcho(int circles) {
    if (circles < 0 || circles > 20)
        fail("声骸搜索圈数无效");
    int index = 0;
    double duration = .8;
    for (int count = 0; count < circles; ++count)
        for (const auto &key : {"w", "a", "s", "d"}) {
            if (index > 2 && (index + 1) % 2 == 0 && !(count == circles - 1 && QString(key) == "d"))
                duration += .8;
            if (walkUntil([&] { return bool(findInteract(c_, "吸收|Absorb")); }, key, duration, true) &&
                pick())
                return true;
            ++index;
        }
    return false;
}
std::optional<Match> WorldInteraction::treasure() {
    const auto region =
        Templates::anchored({58, 108, 1804, 767}, {1920, 1080}, {c_.frame.cols, c_.frame.rows}, true, true);
    return c_.find("treasure_icon", {.search = region, .targetHeight = 720});
}
bool WorldInteraction::walkToBox(const std::function<std::optional<Match>()> &find,
                                 const std::function<bool()> &end, double timeout, double yOffset,
                                 double xThreshold, bool hook) {
    if (!find || !std::isfinite(timeout) || timeout <= 0 || timeout > 180)
        fail("目标移动参数无效");
    const double deadline = c_.execution.now() + timeout;
    if (end && !c_.wait([&] { return end() || bool(find()); }, timeout))
        return false;
    Movement movement(c_);
    QString last;
    std::optional<Match> target;
    bool centered = false, ended = false, running = false;
    while (c_.execution.now() < deadline) {
        c_.sleep(.001);
        if (end && end()) {
            ended = true;
            break;
        }
        if (auto found = find())
            target = found;
        QString next;
        if (!target)
            next = last == "w" ? "s" : last == "s" ? "w" : last == "a" ? "d" : last == "d" ? "a" : "w";
        else {
            const auto point = target->center();
            const double y = std::max(0., point.y() - c_.frame.rows * yOffset);
            const double dx = point.x() - c_.frame.cols * .5;
            centered = centered || std::abs(dx) <= c_.frame.cols * (last.isEmpty() ? .04 : xThreshold);
            if (!centered)
                next = dx > 0 ? "d" : "a";
            else {
                const double center = last == "s" ? .45 : last == "w" ? .6 : .5;
                next = y > c_.frame.rows * center ? "s" : "w";
            }
        }
        movement.direction(next);
        last = next;
        if (running) {
            if (!c_.find("on_the_wall", {.threshold = .7})) {
                movement.stopRunning();
                running = false;
            }
        } else if (next == "w" && c_.find("on_the_wall", {.threshold = .7})) {
            movement.run();
            running = true;
        }
        if (hook && next == "w" && c_.find("tool_teleport", {.threshold = .75}))
            c_.key(c_.hotkey("Tool Key", "t"), 3);
    }
    movement.stop();
    return end ? ended : !last.isEmpty();
}
void WorldInteraction::walkToTreasure(bool requireF) {
    const auto find = [&] { return treasure(); };
    if (!walkToBox(find, [&] { return bool(findInteract(c_, "领取奖励|領取獎勵|Claim Rewards")); }) &&
        !walkToBox(find, [&] { return bool(findInteract(c_)); }))
        throw NavigationFailure("未能接近奖励目标");
    if (requireF)
        walkToInteract("w", 2, true);
    c_.sleep(1);
}
double WorldInteraction::arrowAngle() {
    const auto box = c_.templates.box("arrow", {c_.frame.cols, c_.frame.rows});
    double best = 0, angle = 0;
    for (int i = 0; i < 360; ++i) {
        c_.execution.checkpoint();
        if (auto target = c_.find("arrow", {.threshold = .01, .search = box, .rotation = double(i)}))
            if (target->confidence > best) {
                best = target->confidence;
                angle = i;
            }
    }
    return angle;
}
std::optional<double> WorldInteraction::miniMapTurn(const QString &feature, double threshold, double xOffset,
                                                    double yOffset) {
    const auto box = c_.templates.box("box_minimap", {c_.frame.cols, c_.frame.rows});
    auto target = c_.find(feature, {.threshold = threshold, .search = box});
    if (!target)
        return {};
    const auto point =
        target->center() + QPointF(target->box.width() * xOffset, target->box.height() * yOffset);
    const QPointF center(box.x() + box.width() * .5, box.y() + box.height() * .5);
    double angle = std::atan2(point.y() - center.y(), point.x() - center.x()) * 180 / std::numbers::pi;
    if (angle < 0)
        angle += 360;
    double turn = angle - arrowAngle();
    if (turn > 180)
        turn -= 360;
    else if (turn < -180)
        turn += 360;
    return turn;
}
bool WorldInteraction::followBossMinimap(double timeout, double threshold) {
    if (!inCombat_)
        fail("小地图寻路缺少战斗状态");
    centerCamera();
    Movement movement(c_);
    bool forward = false;
    const double end = c_.execution.now() + timeout;
    while (c_.execution.now() < end) {
        c_.sleep(.01);
        if (inCombat_()) {
            movement.stop();
            return true;
        }
        const double angle = miniMapTurn("boss_check_mark_minimap", threshold, -1, 1).value_or(0);
        if (forward && ((angle >= 10 && angle <= 80) || (angle >= -80 && angle <= -10))) {
            const QString key = angle > 0 ? "d" : "a";
            c_.input.keyDown(key);
            try {
                c_.sleep(.1);
                c_.input.click({}, .1, "middle");
                c_.input.keyUp(key);
                c_.sleep(.01);
            } catch (...) {
                c_.input.release();
                throw;
            }
            continue;
        }
        const QString direction = angle >= -45 && angle <= 45    ? "w"
                                  : angle > 45 && angle <= 135   ? "d"
                                  : angle > -135 && angle <= -45 ? "a"
                                                                 : "s";
        if (!forward || direction != "w") {
            if (forward) {
                movement.stop();
                c_.wait(inCombat_, .2);
            }
            if (direction != "w")
                c_.key(direction, .5, .05);
            centerCamera();
            movement.direction("w");
            c_.wait(inCombat_, .2);
            movement.run();
            forward = true;
            c_.wait(inCombat_, 1);
        }
    }
    movement.stop();
    return inCombat_();
}
} // namespace okww::runtime
