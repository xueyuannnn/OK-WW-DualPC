#include "nightmare.hpp"
#include "workflow_abi.hpp"
namespace okww::runtime {
std::optional<NestTarget> nextNestTarget(const QVector<TextLine> &lines, QSize screen, int tab,
                                         const QSet<QString> &unreachable) {
    const QRegularExpression count("(?<!\\d)(\\d{1,2})\\s*[/／]\\s*(\\d{1,2})(?!\\d)");
    const QStringList names{"go_nest", "go_nest_scroll", "go_nightmare", "go_nightmare_scroll"};
    QRegularExpressionMatchIterator iterator;
    QRegularExpressionMatch match;
    QString key;
    std::optional<NestTarget> result;
    const std::array args{double(tab)};
    rust::workflowRun(
        rust::WorkflowMethod::NextNestTarget, args, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::NestScreen: {
                out[0] = screen.width();
                out[1] = screen.height();
                break;
            }
            case rust::WorkflowOp::NestLineCount: {
                out[0] = double(lines.size());
                break;
            }
            case rust::WorkflowOp::NestLineMatches: {
                iterator = count.globalMatch(lines.at(int(a[0])).text);
                break;
            }
            case rust::WorkflowOp::NestMatchHasNext: {
                out[0] = iterator.hasNext();
                break;
            }
            case rust::WorkflowOp::NextNestMatch: {
                match = iterator.next();
                out[0] = match.captured(1).toInt();
                out[1] = match.captured(2).toInt();
                break;
            }
            case rust::WorkflowOp::NestLineGeometry: {
                const auto &line = lines.at(int(a[0]));
                out[0] = line.center().y();
                out[1] = line.box.y();
                out[2] = line.box.height();
                break;
            }
            case rust::WorkflowOp::PixelRound: {
                out[0] = pixelRound(a[0]);
                break;
            }
            case rust::WorkflowOp::BuildNestKey: {
                key = QString("%1:%2:%3").arg(names.at(int(a[0])), match.captured(2)).arg(int(a[1]));
                break;
            }
            case rust::WorkflowOp::NestUnreachable: {
                out[0] = unreachable.contains(key);
                break;
            }
            case rust::WorkflowOp::StoreNestTarget: {
                result = NestTarget{{a[0], a[1]}, key, int(a[2])};
                break;
            }
            case rust::WorkflowOp::Throw: {
                fail("巢穴列表参数无效");
                break;
            }
            default:
                fail("Rust NextNestTarget 巢穴宿主操作无效");
            }
        });
    return result;
}
std::array<double, 3> NightmareFarm::rustRun(int method, std::initializer_list<double> args,
                                             const QJsonObject &supplied, const NestTarget *requested) {
    std::optional<NestTarget> target;
    if (requested)
        target = *requested;
    std::optional<Match> button, confirm, feature;
    QRect search;
    std::optional<TaskScope> scope;
    std::function<bool()> oldCheck;
    QJsonObject config;
    QJsonArray choices;
    QMap<QString, int> attempts;
    return rust::workflowRun(
        rust::WorkflowMethod(method), std::span<const double>(args.begin(), args.size()),
        [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::Now: {
                out[0] = c_.execution.now();
                break;
            }
            case rust::WorkflowOp::Checkpoint: {
                c_.execution.checkpoint();
                break;
            }
            case rust::WorkflowOp::ExecutionWait: {
                c_.execution.wait(a[0]);
                break;
            }
            case rust::WorkflowOp::Refresh: {
                c_.refresh();
                break;
            }
            case rust::WorkflowOp::Sleep: {
                c_.sleep(a[0]);
                break;
            }
            case rust::WorkflowOp::InvalidCondition: {
                fail("等待条件参数无效");
                break;
            }
            case rust::WorkflowOp::ClickRelative: {
                c_.clickRelative(a[0], a[1], a[2]);
                break;
            }
            case rust::WorkflowOp::Key: {
                c_.key(a[0] == 6 ? "f" : "esc", a[1]);
                break;
            }
            case rust::WorkflowOp::HasInfo: {
                out[0] = bool(c_.info);
                break;
            }
            case rust::WorkflowOp::NestCategory: {
                navigation_.bookCategory(a[0] == 0 ? "canxiang" : "mengyan");
                break;
            }
            case rust::WorkflowOp::FindNestTravel: {
                button =
                    c_.findAny({"fast_travel_custom", "gray_teleport", "remove_custom"}, {.threshold = a[0]});
                out[0] = button.has_value();
                break;
            }
            case rust::WorkflowOp::NestTravelExists: {
                out[0] = button.has_value();
                break;
            }
            case rust::WorkflowOp::ClickNestTravel: {
                c_.click(*button, a[0]);
                break;
            }
            case rust::WorkflowOp::FindNestConfirm: {
                confirm = c_.findAny({"confirm_btn_hcenter_vcenter", "confirm_btn_highlight_hcenter_vcenter"},
                                     {.threshold = a[0]});
                out[0] = confirm.has_value();
                break;
            }
            case rust::WorkflowOp::ClickNestConfirm: {
                c_.click(*confirm, a[0]);
                break;
            }
            case rust::WorkflowOp::NestTravelStillVisible: {
                out[0] = bool(c_.find(button->name, {.threshold = a[0]}));
                break;
            }
            case rust::WorkflowOp::NestWaitTeam: {
                out[0] = navigation_.waitTeam(a[0], a[1] != 0);
                break;
            }
            case rust::WorkflowOp::MarkNestUnreachable: {
                unreachable_.insert(target->key);
                break;
            }
            case rust::WorkflowOp::NestSkippedInfo: {
                c_.info("巢穴跳过", "本次无法到达：" + target->key);
                break;
            }
            case rust::WorkflowOp::CapturedNoticeArea: {
                search = gameBox(c_.frame, .078, .488, .094, .514);
                break;
            }
            case rust::WorkflowOp::CapturedNoticeFeature: {
                const QStringList names{"char_1_text", "char_3_text"};
                out[0] = bool(c_.find(names.at(int(a[0])),
                                      {.threshold = a[1], .search = search, .filter = Filter::Negative80}));
                break;
            }
            case rust::WorkflowOp::SetCaptured: {
                captured_ = a[0] != 0;
                break;
            }
            case rust::WorkflowOp::Captured: {
                out[0] = captured_;
                break;
            }
            case rust::WorkflowOp::SetCaptureOnly: {
                capture_ = a[0] != 0;
                break;
            }
            case rust::WorkflowOp::CaptureOnly: {
                out[0] = capture_;
                break;
            }
            case rust::WorkflowOp::CombatActive: {
                out[0] = combat_.active();
                break;
            }
            case rust::WorkflowOp::MiddleClick: {
                c_.input.click({}, a[0], "middle");
                break;
            }
            case rust::WorkflowOp::NestWalkInteract: {
                out[0] = world_.walkToInteract("w", 5, false, 2.5, "吸收|Absorb", true);
                break;
            }
            case rust::WorkflowOp::NestPick: {
                out[0] = world_.pick(a[0] != 0);
                break;
            }
            case rust::WorkflowOp::ClickNestTarget: {
                c_.click(target->point, a[0]);
                break;
            }
            case rust::WorkflowOp::FindNestInterface: {
                feature = c_.findAny({"fast_travel_custom", "gray_teleport", "remove_custom", "team_close"});
                out[0] = feature.has_value();
                break;
            }
            case rust::WorkflowOp::NestInterfaceTeam: {
                out[0] = feature->name == "team_close";
                break;
            }
            case rust::WorkflowOp::NavStartChallenge: {
                navigation_.startChallenge();
                break;
            }
            case rust::WorkflowOp::NestInteractVisible: {
                out[0] = bool(findInteract(c_));
                break;
            }
            case rust::WorkflowOp::NestWalkApproach: {
                double last = -1;
                world_.walkUntil(
                    [&] {
                        const auto tick = rustRun(int(rust::WorkflowMethod::NestSeekTick), {last});
                        last = tick[1];
                        return tick[0] != 0;
                    },
                    "w", 10);
                break;
            }
            case rust::WorkflowOp::NestCombatRound: {
                out[0] = combat_.combatOnce({.waitSeconds = a[0], .required = false, .target = true});
                break;
            }
            case rust::WorkflowOp::NestDeaths: {
                out[0] = deaths_[target->key];
                break;
            }
            case rust::WorkflowOp::SetNestDeaths: {
                deaths_[target->key] = int(a[0]);
                break;
            }
            case rust::WorkflowOp::NestRecoveryInfo: {
                c_.info("巢穴恢复", QString("%1；本次死亡 %2 次").arg(target->key).arg(int(a[0])));
                break;
            }
            case rust::WorkflowOp::NestFindEcho: {
                out[0] = world_.findEcho(false, true, a[0]).first;
                break;
            }
            case rust::WorkflowOp::NavEnsureMain: {
                navigation_.ensureMain(a[0]);
                break;
            }
            case rust::WorkflowOp::OpenBossBook: {
                navigation_.openBook("gray_book_boss");
                break;
            }
            case rust::WorkflowOp::NestTargetTab: {
                out[0] = target->tab;
                break;
            }
            case rust::WorkflowOp::FindNestTravelProbe: {
                out[0] = bool(c_.findAny({"fast_travel_custom", "gray_teleport", "remove_custom"}));
                break;
            }
            case rust::WorkflowOp::NestRunForward: {
                world_.walkUntil([] { return false; }, "w", a[0], true);
                break;
            }
            case rust::WorkflowOp::LeaveNest: {
                leaveNest(c_);
                break;
            }
            case rust::WorkflowOp::NestTaskScope: {
                scope.emplace(c_, "NightmareNestTask", supplied);
                break;
            }
            case rust::WorkflowOp::ClearUnreachableNests: {
                unreachable_.clear();
                break;
            }
            case rust::WorkflowOp::ClearNestDeaths: {
                deaths_.clear();
                break;
            }
            case rust::WorkflowOp::InstallNestCombatCheck: {
                oldCheck = combat_.onCombatCheck;
                combat_.onCombatCheck = [this] {
                    return rustRun(int(rust::WorkflowMethod::NestCombatCheck), {})[0] != 0;
                };
                break;
            }
            case rust::WorkflowOp::RestoreNestCombatCheck: {
                combat_.onCombatCheck = oldCheck;
                break;
            }
            case rust::WorkflowOp::NavInWorld: {
                out[0] = navigation_.inWorld();
                break;
            }
            case rust::WorkflowOp::CompletedNest: {
                out[0] = completedNest(c_);
                break;
            }
            case rust::WorkflowOp::RecoverStartingPopup: {
                combat_.recoverStartingPopup();
                break;
            }
            case rust::WorkflowOp::NestSuppliedEmpty: {
                out[0] = supplied.isEmpty();
                break;
            }
            case rust::WorkflowOp::ReadNestConfig: {
                config = a[0] != 0 ? c_.profile.group("NightmareNestTask") : supplied;
                choices = config["Which to Farm"].toArray();
                break;
            }
            case rust::WorkflowOp::NestChoiceCount: {
                out[0] = double(choices.size());
                break;
            }
            case rust::WorkflowOp::DefaultNestChoices: {
                choices = {"Nightmare Purification", "Tacet Discord Nest"};
                break;
            }
            case rust::WorkflowOp::NestChoice: {
                const QStringList names{"Tacet Discord Nest", "Nightmare Purification"};
                out[0] = choices.contains(names.at(int(a[0])));
                break;
            }
            case rust::WorkflowOp::NestProgress: {
                c_.reportProgress(
                    a[0] != 0 ? "获取每日声骸" : "清理聚落", -1, -1, {},
                    QString("%1 · 已处理 %2 处").arg(a[1] < 2 ? "残像聚落" : "梦魇巢穴").arg(int(a[2])));
                break;
            }
            case rust::WorkflowOp::ReadNextNestTarget: {
                target = nextNestTarget(c_.text(.35, .25, 1, .96), {c_.frame.cols, c_.frame.rows}, int(a[0]),
                                        unreachable_);
                out[0] = target.has_value();
                break;
            }
            case rust::WorkflowOp::NestTargetAttempts: {
                out[0] = attempts[target->key];
                break;
            }
            case rust::WorkflowOp::SetNestTargetAttempts: {
                attempts[target->key] = int(a[0]);
                break;
            }
            case rust::WorkflowOp::Throw: {
                switch (int(a[0])) {
                case 301:
                    throw NavigationFailure("巢穴前往后未识别到传送或配队界面");
                case 302:
                    throw NavigationFailure("巢穴入口交互未结束");
                case 303:
                    throw NavigationFailure("巢穴连续战斗超过上限，未记录为完成");
                default:
                    fail("巢穴错误编号无效");
                }
                break;
            }
            default:
                fail("Rust NestRun 巢穴宿主操作无效");
            }
        });
}
void NightmareFarm::tab(int index) { rustRun(int(rust::WorkflowMethod::NestTab), {double(index)}); }
bool NightmareFarm::travel(const NestTarget &target) {
    return rustRun(int(rust::WorkflowMethod::NestTravel), {}, {}, &target)[0] != 0;
}
bool NightmareFarm::capturedNotice() {
    return rustRun(int(rust::WorkflowMethod::NestCapturedNotice), {})[0] != 0;
}
bool NightmareFarm::waitCombat(double timeout) {
    return rustRun(int(rust::WorkflowMethod::NestWaitCombat), {timeout})[0] != 0;
}
bool NightmareFarm::walkEcho() { return rustRun(int(rust::WorkflowMethod::NestWalkEcho), {})[0] != 0; }
void NightmareFarm::fight(const NestTarget &target) {
    rustRun(int(rust::WorkflowMethod::NestFight), {}, {}, &target);
}
bool NightmareFarm::run(bool captureOnly, const QJsonObject &supplied) {
    return rustRun(int(rust::WorkflowMethod::NestRun), {double(captureOnly)}, supplied)[0] != 0;
}
} // namespace okww::runtime
