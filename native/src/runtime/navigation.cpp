#include "navigation.hpp"
#include "combat_vision.hpp"
namespace okww::runtime {
QString normalizeNavigationText(const QString &value) {
    auto out = value.toCaseFolded();
    out.remove(QRegularExpression("\\s+"));
    return out;
}
double bookScrollPosition(int serial, int total, const QVector<int> &structure, double top) {
    if (serial < 1 || total < 1 || serial > total || top < 0 || top >= .8806)
        throw NavigationFailure("图鉴序号或滚动范围无效");
    int sum = 0, cross = 0;
    for (int count : structure) {
        if (count <= 0)
            throw NavigationFailure("图鉴分组数量无效");
        sum += count;
        if (serial > sum)
            ++cross;
        else
            break;
    }
    const double height = .8806 - top - (structure.empty() ? 0 : (structure.size() - 1) * .028);
    if (height <= 0)
        throw NavigationFailure("图鉴分组高度超出范围");
    return std::min(.8806, top + height / total * serial + cross * .028);
}
int Navigation::retryLimit() const {
    if (c_.state->inCombat || c_.state->retryingStep ||
        !c_.profile.value("Task Recovery", "Resume Interrupted Tasks", true).toBool())
        return 0;
    for (auto entry : c_.profile.catalog()["tasks"].toArray()) {
        const auto item = entry.toObject();
        if (item["id"].toString() == c_.task && item["trigger"].toBool())
            return 0;
    }
    return std::clamp(c_.profile.value("Task Recovery", "Recovery Retry Limit", 3).toInt(), 1, 10);
}
void Navigation::retry(const QString &step, const std::function<void()> &operation) {
    const int limit = retryLimit();
    const bool previous = c_.state->retryingStep;
    c_.state->retryingStep = true;
    try {
        for (int attempt = 0;; ++attempt) {
            try {
                operation();
                break;
            } catch (const NavigationFailure &) {
                if (attempt >= limit)
                    throw;
                if (c_.info)
                    c_.info("中断自动继续", QString("%1 · %2/%3").arg(step).arg(attempt + 1).arg(limit));
                c_.sleep(.5);
                c_.refresh();
            }
        }
    } catch (...) {
        c_.state->retryingStep = previous;
        throw;
    }
    c_.state->retryingStep = previous;
}
bool Navigation::observe(const std::function<bool()> &condition, double timeout, double settle) {
    double stable = -1;
    return c_.wait(
        [&] {
            if (!condition()) {
                stable = -1;
                return false;
            }
            if (settle <= 0)
                return true;
            if (stable < 0)
                stable = c_.execution.now();
            return c_.execution.now() - stable >= settle;
        },
        timeout);
}
std::optional<Match> Navigation::waitMatch(const QStringList &names, double timeout, MatchOptions options,
                                           double settle) {
    std::optional<Match> found;
    if (!observe(
            [&] {
                found = c_.findAny(names, options);
                return bool(found);
            },
            timeout, settle))
        return {};
    return found;
}
bool Navigation::inRealm() {
    return c_.find("illusive_realm_exit", {.threshold = .7, .filter = Filter::White}) && c_.inTeam() &&
           !c_.find("world_earth_icon", {.threshold = .55, .filter = Filter::White});
}
bool Navigation::inWorld() {
    return c_.find("world_earth_icon", {.threshold = .55, .filter = Filter::White}) && c_.inTeam() &&
           !c_.find("illusive_realm_exit", {.threshold = .7, .filter = Filter::White});
}
bool Navigation::waitTeam(double timeout, bool required, bool back) {
    bool success = false;
    auto operation = [&] {
        success = observe(
            [&] {
                if (c_.inTeam())
                    return true;
                if (back)
                    c_.key("esc", 2);
                return false;
            },
            timeout);
        if (!success && required)
            throw NavigationFailure("等待队伍界面超时");
        if (success)
            c_.sleep(.5);
    };
    if (required)
        retry("返回队伍界面", operation);
    else
        operation();
    return success;
}
void Navigation::confirmDialog(bool right) {
    const auto names =
        right ? QStringList{"confirm_btn_hcenter_vcenter", "confirm_btn_highlight_hcenter_vcenter"}
              : QStringList{"cancel_button_hcenter_vcenter", "cancel_button_highlight_hcenter_vcenter"};
    const auto button = c_.findAny(names);
    if (!button)
        throw NavigationFailure("未识别到对话框按钮");
    c_.click(*button, 2);
}
void Navigation::exitRealm(double timeout) {
    c_.key("esc", 1);
    if (auto button = waitMatch({"gray_confirm_exit_button"}, 3, {.threshold = .7})) {
        c_.sleep(.5);
        c_.click(QPointF(button->box.x() - button->box.width(), button->center().y()), 1);
    } else if (c_.findAny({"confirm_btn_hcenter_vcenter", "confirm_btn_highlight_hcenter_vcenter"}))
        confirmDialog();
    if (!observe([&] { return c_.inTeam() && !inRealm(); }, timeout))
        throw NavigationFailure("退出副本后未确认回到大世界");
    c_.sleep(.5);
}
void Navigation::ensureMain(double timeout, bool back) {
    retry("返回大世界", [&] {
        const double duration = c_.state->loggedIn ? timeout : 600;
        if (!observe(
                [&] {
                    if (c_.inTeam()) {
                        c_.state->loggedIn = true;
                        if (inRealm())
                            exitRealm(120);
                        return true;
                    }
                    if (c_.login() || c_.monthlyCard())
                        return false;
                    if (back)
                        c_.key("esc", 2);
                    return false;
                },
                duration))
            throw NavigationFailure("请从游戏内队伍界面开始任务");
        c_.sleep(.5);
    });
}
Match Navigation::openBook(const QString &feature) {
    std::optional<Match> tab;
    retry("打开索拉指南", [&] {
        ensureMain();
        if (c_.inTeam())
            c_.key(c_.hotkey("Guidebook Key", c_.hotkey("索拉指南", "f2")), 4);
        if (c_.inTeam()) {
            c_.input.keyDown("alt");
            try {
                c_.sleep(.05);
                c_.clickRelative(.77, .05);
                c_.sleep(.02);
                c_.input.keyUp("alt");
            } catch (...) {
                c_.input.release();
                throw;
            }
            c_.sleep(4);
        }
        tab = waitMatch(
            {feature}, 3,
            {.threshold = .3, .search = c_.templates.box("box_gray_book", {c_.frame.cols, c_.frame.rows})},
            1);
        if (!tab)
            throw NavigationFailure("未找到索拉指南分类，请核对指南快捷键");
        c_.sleep(2);
        c_.click(*tab, 1.5);
    });
    return *tab;
}
TextLine Navigation::selectMonster(const QString &name, int maxPages) {
    if (normalizeNavigationText(name).isEmpty() || maxPages < 1 || maxPages > 100)
        throw NavigationFailure("敌迹搜索参数无效");
    c_.clickRelative(.13, .14, .2);
    c_.input.keyDown("ctrl");
    try {
        c_.key("a");
        c_.input.keyUp("ctrl");
    } catch (...) {
        c_.input.release();
        throw;
    }
    c_.key("backspace");
    c_.clickRelative(.354, .137, .5);
    c_.input.scroll(80, QPointF(c_.frame.cols * .25, c_.frame.rows * .5));
    c_.sleep(.6);
    const auto expected = normalizeNavigationText(name);
    QString previous;
    int repeated = 0;
    for (int page = 0; page < maxPages; ++page) {
        c_.refresh();
        const auto boxes = c_.text(.1, .18, .365, .85, .7);
        QString fingerprint;
        for (const auto &box : boxes) {
            const auto normalized = normalizeNavigationText(box.text);
            if (normalized == expected) {
                c_.click(box, .5);
                return box;
            }
            fingerprint += normalized + ":" + QString::number(pixelRound(box.box.y() / 8.)) + "|";
        }
        repeated = !fingerprint.isEmpty() && fingerprint == previous ? repeated + 1 : 0;
        if (repeated >= 2)
            break;
        previous = fingerprint;
        c_.input.scroll(-4, QPointF(c_.frame.cols * .25, c_.frame.rows * .65));
        c_.sleep(.45);
    }
    throw NavigationFailure("敌迹探寻列表中找不到 " + name + "，请确认已解锁及游戏语言");
}
void Navigation::bookCategory(const QString &category, double after) {
    const QMap<QString, double> rows{{"ningsu", .4},  {"moni", .3},      {"qiangdi", .49}, {"wuyin", .73},
                                     {"zhange", .61}, {"canxiang", .83}, {"mengyan", .86}};
    if (!rows.contains(category))
        throw NavigationFailure("未知图鉴分类：" + category);
    c_.sleep(.4);
    if (category == "mengyan")
        c_.clickRelative(.356, .882, after);
    c_.clickRelative(.24, rows[category], after);
}
std::optional<Match> Navigation::waypoint() {
    std::optional<Match> best;
    for (const auto &name : {"map_way_point", "map_way_point_big"})
        if (auto item = c_.find(name, {.threshold = .6, .search = gameBox(c_.frame, .1, .1, .9, .9)}))
            if (!best || item->confidence > best->confidence)
                best = item;
    return best;
}
void Navigation::teleportNearest() {
    c_.sleep(2);
    const auto beacon = waypoint();
    if (!beacon)
        throw NavigationFailure("地图上未识别到回血传送点");
    c_.click(*beacon, 1);
    auto travel = waitMatch({"gray_teleport"}, 3);
    if (!travel) {
        if (auto popup = c_.find(
                "map_way_point",
                {.search = c_.templates.box("map_way_point_pop_up_box", {c_.frame.cols, c_.frame.rows})}))
            c_.click(*popup, 1);
        travel = waitMatch({"gray_teleport"}, 3);
    }
    if (!travel)
        throw NavigationFailure("未识别到回血传送按钮");
    c_.click(QPointF(travel->box.x() + travel->box.width() * 1.5, travel->center().y()), 1);
    waitTeam(20);
    c_.sleep(2);
}
void Navigation::healAtBeacon() {
    ensureMain(120);
    openBook();
    // Exact OCR list selection avoids clipboard/IME dependence on the game PC.
    QString name = "无冠者";
    const auto language = c_.gameLanguage;
    if (language == "en_US")
        name = "Crownless";
    else if (language == "zh_TW")
        name = "無冠者";
    selectMonster(name);
    c_.clickRelative(.89, .92, 1);
    if (!observe([&] { return bool(waypoint()); }, 6))
        throw NavigationFailure("探测目标后地图未打开");
    teleportNearest();
    c_.sleep(5);
}
void Navigation::waitTravel() {
    retry("点击传送", [&] {
        if (!observe([&] { return c_.travelButton(); }, 10))
            throw NavigationFailure("未识别到传送按钮");
    });
}
void Navigation::startChallenge() {
    retry("开始挑战", [&] {
        std::optional<QPointF> point;
        const bool found = observe(
            [&] {
                if (auto button = c_.find("team_start_challenge")) {
                    point = button->center();
                    return true;
                }
                if (auto label =
                        matchingText(c_.text(.64, .82, .99, .98),
                                     "^(?:开启挑战|开始挑战|開啟挑戰|開始挑戰|Start\\s*Challenge)$")) {
                    point = label->center();
                    return true;
                }
                if (matchingText(
                        c_.text(.22, .4, .78, .57),
                        "无法掉落任何声骸|無法掉落任何聲骸|(?:no|cannot\\s+drop)\\s+(?:any\\s+)?echo")) {
                    if (!c_.setting("Advanced Skill Material Mode", false).toBool())
                        fail("当前挑战等级无法掉落声骸，请提高 Boss 等级");
                    confirmDialog();
                }
                return false;
            },
            10, .2);
        if (!found)
            throw NavigationFailure("未识别到开始挑战按钮");
        c_.sleep(.5);
        c_.click(*point, 1);
        observe([&] { return c_.skipConfirm(); }, 3);
    });
}
bool Navigation::selectBookEntry(int serial, int total, const QVector<int> &structure) {
    c_.sleep(.5);
    const auto bars =
        colorRectangles(c_.frame, {{190, 190, 190}, {255, 255, 255}}, c_.frame.cols * 5. / 2560,
                        c_.frame.rows * 10. / 1440, -1, gameBox(c_.frame, .969, .191, .978, .271), .8);
    const double top = bars.empty() ? 424. / 2160 : double(bars.front().box.y()) / c_.frame.rows;
    const double scroll = bookScrollPosition(serial, total, structure, top);
    int target = serial <= 4 ? serial - 1 : -1;
    if (target < 0)
        c_.clickRelative(.973, scroll, 1);
    auto buttons = [&] {
        auto items = c_.templates.find(
            c_.frame, "boss_proceed",
            {.threshold = .8, .search = gameBox(c_.frame, .9113, .229, .9613, .861), .limit = 0});
        std::sort(items.begin(), items.end(),
                  [](const Match &a, const Match &b) { return a.box.y() < b.box.y(); });
        return items;
    };
    auto rows = buttons();
    if (target < 0 && !rows.empty() && (rows.size() == 3 || rows.size() == 4) && !structure.empty() &&
        rows.back().box.y() < c_.frame.rows * .73) {
        const double itemHeight = (.8806 - top - (structure.size() - 1) * .028) / total;
        c_.clickRelative(.973, std::min(.8806, scroll + itemHeight * .55), 1);
        rows = buttons();
    }
    if (rows.empty())
        throw NavigationFailure("未找到图鉴前往按钮");
    if (target >= rows.size()) {
        c_.clickRelative(.973, scroll, 1);
        rows = buttons();
        target = -1;
    }
    if (rows.empty())
        throw NavigationFailure("滚动后未找到图鉴前往按钮");
    c_.click(target < 0 ? rows.back() : rows[target], 1);
    bool confirmed = false;
    std::optional<Match> next;
    if (!observe(
            [&] {
                next = c_.findAny({"fast_travel_custom", "gray_teleport", "remove_custom", "team_close"});
                if (next)
                    return true;
                if (!confirmed && c_.setting("Teleport to Boss").toString() == "Weekly Challenge") {
                    QString joined;
                    for (const auto &line : c_.text(.23, .4, .78, .57))
                        joined += line.text;
                    joined = normalizeNavigationText(joined).replace("劇情", "剧情");
                    if (QRegularExpression(
                            "提前到[达達]目[标標]位置可能影[响響]剧情[体體][验驗].*是否[确確][认認]前往")
                            .match(joined)
                            .hasMatch()) {
                        confirmDialog();
                        confirmed = true;
                    }
                }
                return false;
            },
            10, .5))
        throw NavigationFailure("前往副本后未识别到目标界面");
    return next->name == "team_close";
}
} // namespace okww::runtime
