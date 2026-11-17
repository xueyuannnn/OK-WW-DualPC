#include "navigation.hpp"
#include "combat_vision.hpp"
#include "workflow_abi.hpp"
namespace okww::runtime {
namespace {
[[noreturn]] void navigationError(int reason, const QString &name = {}) {
    switch (reason) {
    case 0:
        throw NavigationFailure("等待队伍界面超时");
    case 1:
        throw NavigationFailure("未识别到对话框按钮");
    case 2:
        throw NavigationFailure("退出副本后未确认回到大世界");
    case 3:
        throw NavigationFailure("请从游戏内队伍界面开始任务");
    case 4:
        throw NavigationFailure("未找到索拉指南分类，请核对指南快捷键");
    case 5:
        throw NavigationFailure("索拉指南未保持打开，未继续操作菜单");
    case 6:
        throw NavigationFailure("敌迹搜索参数无效");
    case 9:
        throw NavigationFailure("地图上未识别到回血传送点");
    case 10:
        throw NavigationFailure("未识别到回血传送按钮");
    case 11:
        throw NavigationFailure("探测目标后地图未打开");
    case 12:
        throw NavigationFailure("未识别到传送按钮");
    case 13:
        fail("当前挑战等级无法掉落声骸，请提高 Boss 等级");
    case 14:
        throw NavigationFailure("未识别到开始挑战按钮");
    case 15:
        throw NavigationFailure("图鉴序号或滚动范围无效");
    case 16:
        throw NavigationFailure("图鉴分组数量无效");
    case 17:
        throw NavigationFailure("图鉴分组高度超出范围");
    case 18:
        throw NavigationFailure("未找到图鉴前往按钮");
    case 19:
        throw NavigationFailure("滚动后未找到图鉴前往按钮");
    case 20:
        throw NavigationFailure("前往副本后未识别到目标界面");
    case 7:
        throw NavigationFailure("敌迹探寻列表中找不到 " + name + "，请确认已解锁及游戏语言");
    case 8:
        throw NavigationFailure("未知图鉴分类：" + name);
    default:
        fail("导航错误编号无效");
    }
}
struct NavigationBridge {
    TaskContext &c;
    QString name = "gray_book_all_monsters", step, expected, normalized, fingerprint, previous;
    QStringList names;
    MatchOptions options;
    QVector<int> structure;
    std::function<void()> operation;
    std::function<bool()> condition;
    QJsonArray catalog;
    std::optional<Match> found, tab, best, item, next;
    std::optional<TextLine> label;
    TextLine resultLine;
    QPointF point;
    QVector<TextLine> lines;
    QVector<Match> rows;
    QString key(int index) {
        const QStringList names{"esc", "alt", "ctrl", "a", "backspace"};
        if (index == 5)
            return c.hotkey("Guidebook Key", c.hotkey("索拉指南", "f2"));
        return names.at(index);
    }
    std::array<double, 3> run(rust::WorkflowMethod method, std::initializer_list<double> args = {}) {
        return rust::workflowRun(
            method, std::span<const double>(args.begin(), args.size()),
            [&](rust::WorkflowOp op, const double *a, double *out) {
                switch (op) {
                case rust::WorkflowOp::Now: {
                    out[0] = c.execution.now();
                    break;
                }
                case rust::WorkflowOp::Checkpoint: {
                    c.execution.checkpoint();
                    break;
                }
                case rust::WorkflowOp::Refresh: {
                    c.refresh();
                    break;
                }
                case rust::WorkflowOp::ExecutionWait: {
                    c.execution.wait(a[0]);
                    break;
                }
                case rust::WorkflowOp::Sleep: {
                    c.sleep(a[0]);
                    break;
                }
                case rust::WorkflowOp::InvalidCondition: {
                    fail("等待条件参数无效");
                    break;
                }
                case rust::WorkflowOp::InCombat: {
                    out[0] = c.state->inCombat;
                    break;
                }
                case rust::WorkflowOp::RetryingStep: {
                    out[0] = c.state->retryingStep;
                    break;
                }
                case rust::WorkflowOp::SetRetryingStep: {
                    c.state->retryingStep = a[0] != 0;
                    break;
                }
                case rust::WorkflowOp::RecoveryEnabled: {
                    out[0] = c.profile.value("Task Recovery", "Resume Interrupted Tasks", true).toBool();
                    break;
                }
                case rust::WorkflowOp::CatalogCount: {
                    catalog = c.profile.catalog()["tasks"].toArray();
                    out[0] = double(catalog.size());
                    break;
                }
                case rust::WorkflowOp::IsCurrentTrigger: {
                    const auto item = catalog.at(int(a[0])).toObject();
                    out[0] = item["id"].toString() == c.task && item["trigger"].toBool();
                    break;
                }
                case rust::WorkflowOp::RecoveryLimit: {
                    out[0] = c.profile.value("Task Recovery", "Recovery Retry Limit", 3).toInt();
                    break;
                }
                case rust::WorkflowOp::Operation: {
                    operation();
                    break;
                }
                case rust::WorkflowOp::Condition: {
                    out[0] = condition();
                    break;
                }
                case rust::WorkflowOp::HasInfo: {
                    out[0] = bool(c.info);
                    break;
                }
                case rust::WorkflowOp::RecoveryNotice: {
                    const QStringList labels{"返回队伍界面", "返回大世界", "打开索拉指南", "点击传送",
                                             "开始挑战"};
                    const QString label = a[0] < 0 ? step : labels.at(int(a[0]));
                    c.info("中断自动继续", QString("%1 · %2/%3").arg(label).arg(int(a[1])).arg(int(a[2])));
                    break;
                }
                case rust::WorkflowOp::RealmIcon: {
                    out[0] =
                        bool(c.find("illusive_realm_exit", {.threshold = a[0], .filter = Filter::White}));
                    break;
                }
                case rust::WorkflowOp::WorldIcon: {
                    out[0] = bool(c.find("world_earth_icon", {.threshold = a[0], .filter = Filter::White}));
                    break;
                }
                case rust::WorkflowOp::InTeam: {
                    out[0] = c.inTeam();
                    break;
                }
                case rust::WorkflowOp::Key: {
                    c.key(key(int(a[0])), a[1]);
                    break;
                }
                case rust::WorkflowOp::KeyDown: {
                    c.input.keyDown(key(int(a[0])));
                    break;
                }
                case rust::WorkflowOp::KeyUp: {
                    c.input.keyUp(key(int(a[0])));
                    break;
                }
                case rust::WorkflowOp::ReleaseInput: {
                    c.input.release();
                    break;
                }
                case rust::WorkflowOp::ClickRelative: {
                    c.clickRelative(a[0], a[1], a[2]);
                    break;
                }
                case rust::WorkflowOp::Scroll: {
                    c.input.scroll(int(a[0]), QPointF(c.frame.cols * a[1], c.frame.rows * a[2]));
                    break;
                }
                case rust::WorkflowOp::FindConfirm: {
                    const auto names = a[0] != 0 ? QStringList{"confirm_btn_hcenter_vcenter",
                                                               "confirm_btn_highlight_hcenter_vcenter"}
                                                 : QStringList{"cancel_button_hcenter_vcenter",
                                                               "cancel_button_highlight_hcenter_vcenter"};
                    found = c.findAny(names);
                    out[0] = found.has_value();
                    break;
                }
                case rust::WorkflowOp::ClickFound: {
                    c.click(*found, a[0]);
                    break;
                }
                case rust::WorkflowOp::FindNamed: {
                    found = c.findAny(names, options);
                    out[0] = found.has_value();
                    break;
                }
                case rust::WorkflowOp::FindExit: {
                    found = c.findAny({"gray_confirm_exit_button"}, {.threshold = a[0]});
                    out[0] = found.has_value();
                    break;
                }
                case rust::WorkflowOp::ClickExitLeft: {
                    c.click(QPointF(found->box.x() - found->box.width(), found->center().y()), a[0]);
                    break;
                }
                case rust::WorkflowOp::LoggedIn: {
                    out[0] = c.state->loggedIn;
                    break;
                }
                case rust::WorkflowOp::SetLoggedIn: {
                    c.state->loggedIn = a[0] != 0;
                    break;
                }
                case rust::WorkflowOp::Login: {
                    out[0] = c.login();
                    break;
                }
                case rust::WorkflowOp::MonthlyCard: {
                    out[0] = c.monthlyCard();
                    break;
                }
                case rust::WorkflowOp::ClearTab: {
                    tab.reset();
                    break;
                }
                case rust::WorkflowOp::FindBookTab: {
                    tab = c.find(name,
                                 {.threshold = a[0],
                                  .search = c.templates.box("box_gray_book", {c.frame.cols, c.frame.rows}),
                                  .grayscale = true});
                    out[0] = tab.has_value();
                    break;
                }
                case rust::WorkflowOp::ClickTab: {
                    c.click(*tab, a[0]);
                    break;
                }
                case rust::WorkflowOp::NormalizedNameEmpty: {
                    out[0] = normalizeNavigationText(name).isEmpty();
                    break;
                }
                case rust::WorkflowOp::BuildExpectedName: {
                    expected = normalizeNavigationText(name);
                    break;
                }
                case rust::WorkflowOp::ReadMonsterText: {
                    lines = c.text(.1, .18, .365, .85, a[0]);
                    out[0] = double(lines.size());
                    break;
                }
                case rust::WorkflowOp::ClearFingerprint: {
                    fingerprint.clear();
                    break;
                }
                case rust::WorkflowOp::NormalizeMonsterLine: {
                    normalized = normalizeNavigationText(lines.at(int(a[0])).text);
                    break;
                }
                case rust::WorkflowOp::MonsterNameMatches: {
                    out[0] = normalized == expected;
                    break;
                }
                case rust::WorkflowOp::ClickMonsterLine: {
                    resultLine = lines.at(int(a[0]));
                    c.click(resultLine, a[1]);
                    break;
                }
                case rust::WorkflowOp::MonsterLineY: {
                    out[0] = lines.at(int(a[0])).box.y();
                    break;
                }
                case rust::WorkflowOp::PixelRound: {
                    out[0] = pixelRound(a[0]);
                    break;
                }
                case rust::WorkflowOp::AppendFingerprint: {
                    fingerprint += normalized + ":" + QString::number(int(a[0])) + "|";
                    break;
                }
                case rust::WorkflowOp::FingerprintEmpty: {
                    out[0] = fingerprint.isEmpty();
                    break;
                }
                case rust::WorkflowOp::FingerprintMatches: {
                    out[0] = fingerprint == previous;
                    break;
                }
                case rust::WorkflowOp::StoreFingerprint: {
                    previous = fingerprint;
                    break;
                }
                case rust::WorkflowOp::CategoryKind: {
                    out[0] =
                        QStringList{"ningsu", "moni", "qiangdi", "wuyin", "zhange", "canxiang", "mengyan"}
                            .indexOf(name);
                    break;
                }
                case rust::WorkflowOp::ClearWaypoint: {
                    best.reset();
                    break;
                }
                case rust::WorkflowOp::FindWaypoint: {
                    const QStringList names{"map_way_point", "map_way_point_big"};
                    item = c.find(names.at(int(a[0])),
                                  {.threshold = a[1], .search = gameBox(c.frame, .1, .1, .9, .9)});
                    out[0] = item.has_value();
                    break;
                }
                case rust::WorkflowOp::WaypointExists: {
                    out[0] = best.has_value();
                    break;
                }
                case rust::WorkflowOp::ItemConfidence: {
                    out[0] = item->confidence;
                    break;
                }
                case rust::WorkflowOp::WaypointConfidence: {
                    out[0] = best->confidence;
                    break;
                }
                case rust::WorkflowOp::StoreWaypoint: {
                    best = item;
                    break;
                }
                case rust::WorkflowOp::ClickWaypoint: {
                    c.click(*best, a[0]);
                    break;
                }
                case rust::WorkflowOp::FindTravel: {
                    found = c.findAny({"gray_teleport"});
                    out[0] = found.has_value();
                    break;
                }
                case rust::WorkflowOp::FindWaypointPopup: {
                    found =
                        c.find("map_way_point", {.search = c.templates.box("map_way_point_pop_up_box",
                                                                           {c.frame.cols, c.frame.rows})});
                    out[0] = found.has_value();
                    break;
                }
                case rust::WorkflowOp::ClickTravelRight: {
                    c.click(QPointF(found->box.x() + found->box.width() * 1.5, found->center().y()), a[0]);
                    break;
                }
                case rust::WorkflowOp::DefaultBookName: {
                    name = "gray_book_all_monsters";
                    break;
                }
                case rust::WorkflowOp::LanguageKind: {
                    out[0] = QStringList{"en_US", "zh_TW"}.indexOf(c.gameLanguage);
                    break;
                }
                case rust::WorkflowOp::CrownlessName: {
                    const QStringList names{"Crownless", "無冠者", "无冠者"};
                    name = names.at(a[0] < 0 ? 2 : int(a[0]));
                    break;
                }
                case rust::WorkflowOp::TravelButton: {
                    out[0] = c.travelButton();
                    break;
                }
                case rust::WorkflowOp::FindStartChallenge: {
                    found = c.find("team_start_challenge");
                    out[0] = found.has_value();
                    break;
                }
                case rust::WorkflowOp::StoreMatchPoint: {
                    point = found->center();
                    break;
                }
                case rust::WorkflowOp::FindChallengeText: {
                    label = matchingText(c.text(.64, .82, .99, .98),
                                         "^(?:开启挑战|开始挑战|開啟挑戰|開始挑戰|Start\\s*Challenge)$");
                    out[0] = label.has_value();
                    break;
                }
                case rust::WorkflowOp::StoreTextPoint: {
                    point = label->center();
                    break;
                }
                case rust::WorkflowOp::NoEchoDrop: {
                    out[0] = bool(matchingText(
                        c.text(.22, .4, .78, .57),
                        "无法掉落任何声骸|無法掉落任何聲骸|(?:no|cannot\\s+drop)\\s+(?:any\\s+)?echo"));
                    break;
                }
                case rust::WorkflowOp::AdvancedSkillMode: {
                    out[0] = c.setting("Advanced Skill Material Mode", false).toBool();
                    break;
                }
                case rust::WorkflowOp::ClickPoint: {
                    c.click(point, a[0]);
                    break;
                }
                case rust::WorkflowOp::SkipConfirm: {
                    out[0] = c.skipConfirm();
                    break;
                }
                case rust::WorkflowOp::StructureCount: {
                    out[0] = double(structure.size());
                    break;
                }
                case rust::WorkflowOp::StructureValue: {
                    out[0] = structure.at(int(a[0]));
                    break;
                }
                case rust::WorkflowOp::BookScrollTop: {
                    const auto bars = colorRectangles(c.frame, {{190, 190, 190}, {255, 255, 255}},
                                                      c.frame.cols * 5. / 2560, c.frame.rows * 10. / 1440, -1,
                                                      gameBox(c.frame, .969, .191, .978, .271), .8);
                    out[0] = bars.empty() ? 424. / 2160 : double(bars.front().box.y()) / c.frame.rows;
                    break;
                }
                case rust::WorkflowOp::ReadProceedRows: {
                    rows = c.templates.find(
                        c.frame, "boss_proceed",
                        {.threshold = .8, .search = gameBox(c.frame, .9113, .229, .9613, .861), .limit = 0});
                    std::sort(rows.begin(), rows.end(),
                              [](const Match &a, const Match &b) { return a.box.y() < b.box.y(); });
                    out[0] = double(rows.size());
                    break;
                }
                case rust::WorkflowOp::LastRowAbove: {
                    out[0] = rows.back().box.y() < c.frame.rows * a[0];
                    break;
                }
                case rust::WorkflowOp::ClickProceedRow: {
                    c.click(rows.at(int(a[0])), a[1]);
                    break;
                }
                case rust::WorkflowOp::FindNextInterface: {
                    next = c.findAny({"fast_travel_custom", "gray_teleport", "remove_custom", "team_close"});
                    out[0] = next.has_value();
                    break;
                }
                case rust::WorkflowOp::WeeklyTeleport: {
                    out[0] = c.setting("Teleport to Boss").toString() == "Weekly Challenge";
                    break;
                }
                case rust::WorkflowOp::EarlyTeleportWarning: {
                    QString joined;
                    for (const auto &line : c.text(.23, .4, .78, .57))
                        joined += line.text;
                    joined = normalizeNavigationText(joined).replace("劇情", "剧情");
                    out[0] = QRegularExpression(
                                 "提前到[达達]目[标標]位置可能影[响響]剧情[体體][验驗].*是否[确確][认認]前往")
                                 .match(joined)
                                 .hasMatch();
                    break;
                }
                case rust::WorkflowOp::NextIsTeamClose: {
                    out[0] = next->name == "team_close";
                    break;
                }
                case rust::WorkflowOp::Throw: {
                    navigationError(int(a[0]), name);
                    break;
                }
                default:
                    fail("Rust 导航宿主操作无效");
                }
            });
    }
};
} // namespace
QString normalizeNavigationText(const QString &value) {
    auto out = value.toCaseFolded();
    out.remove(QRegularExpression("\\s+"));
    return out;
}
double bookScrollPosition(int serial, int total, const QVector<int> &structure, double top) {
    const std::array args{double(serial), double(total), top};
    return rust::workflowRun(rust::WorkflowMethod::BookScroll, args,
                             [&](rust::WorkflowOp op, const double *a, double *out) {
                                 switch (op) {
                                 case rust::WorkflowOp::StructureCount:
                                     out[0] = double(structure.size());
                                     break;
                                 case rust::WorkflowOp::StructureValue:
                                     out[0] = structure.at(int(a[0]));
                                     break;
                                 case rust::WorkflowOp::Throw:
                                     navigationError(int(a[0]));
                                     break;
                                 default:
                                     fail("Rust 图鉴滚动宿主操作无效");
                                 }
                             })[0];
}
int Navigation::retryLimit() const {
    NavigationBridge b{c_};
    return int(b.run(rust::WorkflowMethod::RetryLimit)[0]);
}
void Navigation::retry(const QString &step, const std::function<void()> &operation) {
    NavigationBridge b{c_};
    b.step = step;
    b.operation = operation;
    b.run(rust::WorkflowMethod::Retry);
}
bool Navigation::observe(const std::function<bool()> &condition, double timeout, double settle) {
    NavigationBridge b{c_};
    b.condition = condition;
    return b.run(rust::WorkflowMethod::Observe, {timeout, settle})[0] != 0;
}
std::optional<Match> Navigation::waitMatch(const QStringList &names, double timeout, MatchOptions options,
                                           double settle) {
    NavigationBridge b{c_};
    b.names = names;
    b.options = options;
    if (b.run(rust::WorkflowMethod::WaitMatch, {timeout, settle})[0] == 0)
        return {};
    return b.found;
}
bool Navigation::inRealm() {
    NavigationBridge b{c_};
    return b.run(rust::WorkflowMethod::InRealm)[0] != 0;
}
bool Navigation::inWorld() {
    NavigationBridge b{c_};
    return b.run(rust::WorkflowMethod::InWorld)[0] != 0;
}
bool Navigation::waitTeam(double timeout, bool required, bool back) {
    NavigationBridge b{c_};
    return b.run(rust::WorkflowMethod::WaitTeam, {timeout, double(required), double(back)})[0] != 0;
}
void Navigation::confirmDialog(bool right) {
    NavigationBridge b{c_};
    b.run(rust::WorkflowMethod::Confirm, {double(right)});
}
void Navigation::exitRealm(double timeout) {
    NavigationBridge b{c_};
    b.run(rust::WorkflowMethod::ExitRealm, {timeout});
}
void Navigation::ensureMain(double timeout, bool back) {
    NavigationBridge b{c_};
    b.run(rust::WorkflowMethod::EnsureMain, {timeout, double(back)});
}
Match Navigation::openBook(const QString &feature) {
    NavigationBridge b{c_};
    b.name = feature;
    b.run(rust::WorkflowMethod::OpenBook);
    return *b.tab;
}
TextLine Navigation::selectMonster(const QString &name, int maxPages) {
    NavigationBridge b{c_};
    b.name = name;
    b.run(rust::WorkflowMethod::SelectMonster, {double(maxPages)});
    return b.resultLine;
}
void Navigation::bookCategory(const QString &category, double after) {
    NavigationBridge b{c_};
    b.name = category;
    b.run(rust::WorkflowMethod::Category, {after});
}
std::optional<Match> Navigation::waypoint() {
    NavigationBridge b{c_};
    b.run(rust::WorkflowMethod::Waypoint);
    return b.best;
}
void Navigation::teleportNearest() {
    NavigationBridge b{c_};
    b.run(rust::WorkflowMethod::Teleport);
}
void Navigation::healAtBeacon() {
    NavigationBridge b{c_};
    b.run(rust::WorkflowMethod::Heal);
}
void Navigation::waitTravel() {
    NavigationBridge b{c_};
    b.run(rust::WorkflowMethod::Travel);
}
void Navigation::startChallenge() {
    NavigationBridge b{c_};
    b.run(rust::WorkflowMethod::StartChallenge);
}
bool Navigation::selectBookEntry(int serial, int total, const QVector<int> &structure) {
    NavigationBridge b{c_};
    b.structure = structure;
    return b.run(rust::WorkflowMethod::SelectEntry, {double(serial), double(total)})[0] != 0;
}
} // namespace okww::runtime
