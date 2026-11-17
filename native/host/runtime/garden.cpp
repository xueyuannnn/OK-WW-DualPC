#include "garden.hpp"
#include "workflow_abi.hpp"
#include <tuple>
namespace okww::runtime {
std::optional<QPair<int, int>> Garden::turn() {
    QVector<TextLine> lines;
    QString text;
    QRegularExpressionMatch stage, day;
    const auto result = rust::workflowRun(
        rust::WorkflowMethod::GardenTurn, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::ReadGardenTurnText: {
                lines = c_.text(.025, .085, .23, .185, .75);
                out[0] = double(lines.size());
                break;
            }
            case rust::WorkflowOp::AppendGardenTurnText: {
                text += lines.at(int(a[0])).text + ' ';
                break;
            }
            case rust::WorkflowOp::ParseGardenStage: {
                stage = QRegularExpression("(?:[阶階]段|stage)\\s*[:：]?\\s*(\\d)\\s*[/／]\\s*7",
                                           QRegularExpression::CaseInsensitiveOption)
                            .match(text);
                break;
            }
            case rust::WorkflowOp::ParseGardenDay: {
                day = QRegularExpression("第\\s*(\\d+)\\s*天|day\\s*(\\d+)",
                                         QRegularExpression::CaseInsensitiveOption)
                          .match(text);
                break;
            }
            case rust::WorkflowOp::GardenStageExists: {
                out[0] = stage.hasMatch();
                break;
            }
            case rust::WorkflowOp::GardenDayExists: {
                out[0] = day.hasMatch();
                break;
            }
            case rust::WorkflowOp::GardenTurnValues: {
                out[0] = stage.captured(1).toInt();
                out[1] = (day.captured(1).isEmpty() ? day.captured(2) : day.captured(1)).toInt();
                break;
            }
            default:
                fail("Rust GardenTurn 周常流程宿主无效");
            }
        });
    if (result[0] == 0)
        return {};
    return QPair(int(result[1]), int(result[2]));
}
std::pair<std::optional<TextLine>, std::optional<int>> Garden::blessingControls() {
    QVector<TextLine> boxes;
    std::optional<TextLine> button;
    QString text;
    QRegularExpressionMatch uses;
    const auto result = rust::workflowRun(
        rust::WorkflowMethod::GardenBlessingControls, {},
        [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::ReadBlessingText: {
                boxes = c_.text(.035, .70, .23, .815, .75);
                out[0] = double(boxes.size());
                break;
            }
            case rust::WorkflowOp::FindBlessingButton: {
                button = matchingText(boxes, "^(?:使用祝福|use blessing)$");
                break;
            }
            case rust::WorkflowOp::AppendBlessingText: {
                text += boxes.at(int(a[0])).text + ' ';
                break;
            }
            case rust::WorkflowOp::ParseBlessingUses: {
                uses = QRegularExpression("(?:剩[余餘]次[数數]|remaining\\s+uses?)\\s*[:：]?\\s*(\\d+)",
                                          QRegularExpression::CaseInsensitiveOption)
                           .match(text);
                out[0] = uses.hasMatch();
                break;
            }
            case rust::WorkflowOp::BlessingUsesValue: {
                out[0] = uses.captured(1).toInt();
                break;
            }
            default:
                fail("Rust GardenBlessingControls 周常流程宿主无效");
            }
        });
    return {button, result[0] != 0 ? std::optional<int>(int(result[1])) : std::nullopt};
}
bool Garden::useBlessing(bool open) {
    std::optional<QPair<int, int>> current;
    std::optional<TextLine> button, after;
    std::optional<int> uses, remaining;
    const std::array args{double(open)};
    return rust::workflowRun(rust::WorkflowMethod::GardenUseBlessing, args,
                             [&](rust::WorkflowOp op, const double *a, double *out) {
                                 switch (op) {
                                 case rust::WorkflowOp::Now: {
                                     out[0] = c_.execution.now();
                                     break;
                                 }
                                 case rust::WorkflowOp::Sleep: {
                                     c_.sleep(a[0]);
                                     break;
                                 }
                                 case rust::WorkflowOp::Refresh: {
                                     c_.refresh();
                                     break;
                                 }
                                 case rust::WorkflowOp::HasInfo: {
                                     out[0] = bool(c_.info);
                                     break;
                                 }
                                 case rust::WorkflowOp::ClickRelative: {
                                     c_.clickRelative(a[0], a[1], a[2]);
                                     break;
                                 }
                                 case rust::WorkflowOp::ReadGardenTurn: {
                                     current = turn();
                                     break;
                                 }
                                 case rust::WorkflowOp::GardenCurrentTurnExists: {
                                     out[0] = current.has_value();
                                     break;
                                 }
                                 case rust::WorkflowOp::GardenCheckedTurnMatches: {
                                     out[0] = checkedTurn_ == current;
                                     break;
                                 }
                                 case rust::WorkflowOp::GardenTurnAttempts: {
                                     out[0] = attempts_[*current];
                                     break;
                                 }
                                 case rust::WorkflowOp::SetGardenTurnAttempts: {
                                     attempts_[*current] = int(a[0]);
                                     break;
                                 }
                                 case rust::WorkflowOp::ReadBlessingControls: {
                                     std::tie(button, uses) = blessingControls();
                                     break;
                                 }
                                 case rust::WorkflowOp::BlessingButtonExists: {
                                     out[0] = button.has_value();
                                     break;
                                 }
                                 case rust::WorkflowOp::BlessingUsesExists: {
                                     out[0] = uses.has_value();
                                     break;
                                 }
                                 case rust::WorkflowOp::CurrentBlessingUses: {
                                     out[0] = *uses;
                                     break;
                                 }
                                 case rust::WorkflowOp::ClickBlessingButton: {
                                     c_.click(*button, a[0]);
                                     break;
                                 }
                                 case rust::WorkflowOp::ReadAfterBlessingControls: {
                                     std::tie(after, remaining) = blessingControls();
                                     break;
                                 }
                                 case rust::WorkflowOp::AfterBlessingButtonExists: {
                                     out[0] = after.has_value();
                                     break;
                                 }
                                 case rust::WorkflowOp::AfterBlessingUsesExists: {
                                     out[0] = remaining.has_value();
                                     break;
                                 }
                                 case rust::WorkflowOp::AfterBlessingUses: {
                                     out[0] = *remaining;
                                     break;
                                 }
                                 case rust::WorkflowOp::StoreGardenCheckedTurn: {
                                     checkedTurn_ = current;
                                     break;
                                 }
                                 case rust::WorkflowOp::BlessingUsedInfo: {
                                     c_.info("周常技能", current ? QString("第 %1 阶段，第 %2 天已使用")
                                                                       .arg(current->first)
                                                                       .arg(current->second)
                                                                 : "已使用");
                                     break;
                                 }
                                 case rust::WorkflowOp::BlessingEmptyInfo: {
                                     c_.info("周常技能", "本回合暂无次数，下一回合重新检查");
                                     break;
                                 }
                                 case rust::WorkflowOp::SetBlessingOpen: {
                                     blessingOpen_ = a[0] != 0;
                                     break;
                                 }
                                 default:
                                     fail("Rust GardenUseBlessing 周常流程宿主无效");
                                 }
                             })[0] != 0;
}
bool Garden::maximizeSpeed() {
    QVector<TextLine> lines;
    QString text;
    QRegularExpression speed;
    return rust::workflowRun(
               rust::WorkflowMethod::GardenMaxSpeed, {},
               [&](rust::WorkflowOp op, const double *a, double *out) {
                   switch (op) {
                   case rust::WorkflowOp::Now: {
                       out[0] = c_.execution.now();
                       break;
                   }
                   case rust::WorkflowOp::Sleep: {
                       c_.sleep(a[0]);
                       break;
                   }
                   case rust::WorkflowOp::Refresh: {
                       c_.refresh();
                       break;
                   }
                   case rust::WorkflowOp::HasInfo: {
                       out[0] = bool(c_.info);
                       break;
                   }
                   case rust::WorkflowOp::ClickRelative: {
                       c_.clickRelative(a[0], a[1], a[2]);
                       break;
                   }
                   case rust::WorkflowOp::SpeedChecked: {
                       out[0] = speedChecked_;
                       break;
                   }
                   case rust::WorkflowOp::SetSpeedChecked: {
                       speedChecked_ = a[0];
                       break;
                   }
                   case rust::WorkflowOp::SpeedAttempts: {
                       out[0] = speedAttempts_;
                       break;
                   }
                   case rust::WorkflowOp::SetSpeedAttempts: {
                       speedAttempts_ = int(a[0]);
                       break;
                   }
                   case rust::WorkflowOp::ReadGardenSpeedText: {
                       speed =
                           QRegularExpression("^(?:[x×*]\\s*\\d{1,2}(?:\\.\\d{1,2})?|\\d{1,2}\\.\\d{1,2})$",
                                              QRegularExpression::CaseInsensitiveOption);
                       lines = c_.text(.625, .025, .715, .09, .75);
                       out[0] = double(lines.size());
                       break;
                   }
                   case rust::WorkflowOp::SelectGardenSpeedLine: {
                       text = QString(lines.at(int(a[0])).text).trimmed().remove(' ');
                       break;
                   }
                   case rust::WorkflowOp::GardenSpeedMax: {
                       out[0] = text.contains("MAX", Qt::CaseInsensitive) || text == "最大" || text == "最快";
                       break;
                   }
                   case rust::WorkflowOp::GardenMaxSpeedInfo: {
                       c_.info("周常倍速", "MAX");
                       break;
                   }
                   case rust::WorkflowOp::GardenSpeedMatches: {
                       out[0] = speed.match(text).hasMatch();
                       break;
                   }
                   case rust::WorkflowOp::ClickGardenSpeedLine: {
                       c_.click(lines.at(int(a[0])), a[1]);
                       break;
                   }
                   default:
                       fail("Rust GardenMaxSpeed 周常流程宿主无效");
                   }
               })[0] != 0;
}
std::optional<Match> Garden::bestFeature() {
    std::optional<Match> modern, target, result;
    QStringList names;
    QVector<Match> matches;
    rust::workflowRun(
        rust::WorkflowMethod::GardenBestFeature, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::Now: {
                out[0] = c_.execution.now();
                break;
            }
            case rust::WorkflowOp::Sleep: {
                c_.sleep(a[0]);
                break;
            }
            case rust::WorkflowOp::Refresh: {
                c_.refresh();
                break;
            }
            case rust::WorkflowOp::HasInfo: {
                out[0] = bool(c_.info);
                break;
            }
            case rust::WorkflowOp::ClickRelative: {
                c_.clickRelative(a[0], a[1], a[2]);
                break;
            }
            case rust::WorkflowOp::InitialBlessingChecked: {
                out[0] = initialBlessingChecked_;
                break;
            }
            case rust::WorkflowOp::SetInitialBlessingChecked: {
                initialBlessingChecked_ = a[0] != 0;
                break;
            }
            case rust::WorkflowOp::ReadInitialBlessingButton: {
                out[0] = blessingControls().first.has_value();
                break;
            }
            case rust::WorkflowOp::SetBlessingOpen: {
                blessingOpen_ = a[0] != 0;
                break;
            }
            case rust::WorkflowOp::BlessingOpen: {
                out[0] = blessingOpen_;
                break;
            }
            case rust::WorkflowOp::UseGardenBlessing: {
                out[0] = useBlessing(a[0] != 0);
                break;
            }
            case rust::WorkflowOp::ReadModernGardenReward: {
                modern = rewardChoice();
                out[0] = modern.has_value();
                break;
            }
            case rust::WorkflowOp::ModernGardenChoice: {
                out[0] = modern->name.startsWith("garden_reward_choice_");
                break;
            }
            case rust::WorkflowOp::ReloadGardenRewardResult: {
                result = rewardChoice();
                break;
            }
            case rust::WorkflowOp::StoreModernGardenReward: {
                result = modern;
                break;
            }
            case rust::WorkflowOp::ReadGardenTemplateNames: {
                names = c_.templates.names();
                out[0] = double(names.size());
                break;
            }
            case rust::WorkflowOp::GardenTemplateName: {
                out[0] = names.at(int(a[0])).startsWith("garden_");
                break;
            }
            case rust::WorkflowOp::GardenTemplateKind: {
                out[0] = QStringList{"garden_get_confirm_gray", "garden_not_interested_confirm",
                                     "garden_not_interested"}
                             .indexOf(names.at(int(a[0])));
                break;
            }
            case rust::WorkflowOp::AppendGardenTemplateMatches: {
                MatchOptions options;
                options.limit = 0;
                options.vertical = a[1];
                matches += c_.templates.find(c_.frame, names.at(int(a[0])), options);
                break;
            }
            case rust::WorkflowOp::GardenFeatureMatchCount: {
                out[0] = double(matches.size());
                break;
            }
            case rust::WorkflowOp::GardenFeaturePriority: {
                out[0] = matches.at(int(a[0])).name ==
                         QStringList{"garden_get_skip", "garden_not_interested_confirm"}.at(int(a[1]));
                break;
            }
            case rust::WorkflowOp::GardenFeatureTargetExists: {
                out[0] = target.has_value();
                break;
            }
            case rust::WorkflowOp::GardenFeatureConfidence: {
                out[0] = matches.at(int(a[0])).confidence;
                break;
            }
            case rust::WorkflowOp::GardenTargetConfidence: {
                out[0] = target->confidence;
                break;
            }
            case rust::WorkflowOp::SelectGardenFeatureTarget: {
                target = matches.at(int(a[0]));
                break;
            }
            case rust::WorkflowOp::GardenFeatureTargetKind: {
                out[0] = QStringList{"garden_shop_close", "garden_shop_buy", "garden_next_day"}.indexOf(
                    target->name);
                break;
            }
            case rust::WorkflowOp::OpenGardenShop: {
                shop();
                break;
            }
            case rust::WorkflowOp::MaximizeGardenSpeed: {
                out[0] = maximizeSpeed();
                break;
            }
            case rust::WorkflowOp::StoreGardenFeatureTarget: {
                result = target;
                break;
            }
            default:
                fail("Rust GardenBestFeature 周常流程宿主无效");
            }
        });
    return result;
}
bool Garden::active() {
    QVector<TextLine> lines;
    QString text;
    return rust::workflowRun(
               rust::WorkflowMethod::GardenActive, {},
               [&](rust::WorkflowOp op, const double *a, double *out) {
                   switch (op) {
                   case rust::WorkflowOp::Now: {
                       out[0] = c_.execution.now();
                       break;
                   }
                   case rust::WorkflowOp::Sleep: {
                       c_.sleep(a[0]);
                       break;
                   }
                   case rust::WorkflowOp::Refresh: {
                       c_.refresh();
                       break;
                   }
                   case rust::WorkflowOp::HasInfo: {
                       out[0] = bool(c_.info);
                       break;
                   }
                   case rust::WorkflowOp::ClickRelative: {
                       c_.clickRelative(a[0], a[1], a[2]);
                       break;
                   }
                   case rust::WorkflowOp::GardenActiveFeatures: {
                       out[0] = bool(c_.findAny(
                           {"garden_next_day", "garden_new_stage", "garden_get_skip", "garden_shop_close"},
                           {.threshold = a[0]}));
                       break;
                   }
                   case rust::WorkflowOp::GardenActiveReward: {
                       out[0] = rewardChoice().has_value();
                       break;
                   }
                   case rust::WorkflowOp::GardenActiveBlessing: {
                       out[0] = blessingControls().first.has_value();
                       break;
                   }
                   case rust::WorkflowOp::SetBlessingOpen: {
                       blessingOpen_ = a[0] != 0;
                       break;
                   }
                   case rust::WorkflowOp::ReadGardenActiveText: {
                       lines = c_.text(0, 0, .3, .2);
                       out[0] = double(lines.size());
                       break;
                   }
                   case rust::WorkflowOp::AppendGardenActiveText: {
                       text += lines.at(int(a[0])).text + ' ';
                       break;
                   }
                   case rust::WorkflowOp::GardenActiveHeading: {
                       out[0] = QRegularExpression("本[周週][游遊][园園]").match(text).hasMatch();
                       break;
                   }
                   case rust::WorkflowOp::GardenActiveStage: {
                       out[0] =
                           QRegularExpression("[阶階]段\\s*[:：]?\\s*\\d\\s*/\\s*7").match(text).hasMatch();
                       break;
                   }
                   default:
                       fail("Rust GardenActive 周常流程宿主无效");
                   }
               })[0] != 0;
}
void Garden::loop() {
    std::optional<Match> target, tier, restart, back;
    QVector<Match> items;
    rust::workflowRun(
        rust::WorkflowMethod::GardenLoop, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::Now: {
                out[0] = c_.execution.now();
                break;
            }
            case rust::WorkflowOp::Sleep: {
                c_.sleep(a[0]);
                break;
            }
            case rust::WorkflowOp::Refresh: {
                c_.refresh();
                break;
            }
            case rust::WorkflowOp::HasInfo: {
                out[0] = bool(c_.info);
                break;
            }
            case rust::WorkflowOp::ClickRelative: {
                c_.clickRelative(a[0], a[1], a[2]);
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
            case rust::WorkflowOp::InvalidCondition: {
                fail("等待条件参数无效");
                break;
            }
            case rust::WorkflowOp::ResetGardenCheckedTurn: {
                checkedTurn_.reset();
                break;
            }
            case rust::WorkflowOp::ClearGardenTurnAttempts: {
                attempts_.clear();
                break;
            }
            case rust::WorkflowOp::SetInitialBlessingChecked: {
                initialBlessingChecked_ = a[0] != 0;
                break;
            }
            case rust::WorkflowOp::SetSpeedChecked: {
                speedChecked_ = a[0];
                break;
            }
            case rust::WorkflowOp::ReadBestGardenFeature: {
                target = bestFeature();
                break;
            }
            case rust::WorkflowOp::BestGardenFeatureExists: {
                out[0] = target.has_value();
                break;
            }
            case rust::WorkflowOp::BestGardenFeatureInfo: {
                c_.info("周常步骤", target->name);
                break;
            }
            case rust::WorkflowOp::BestGardenFeatureKind: {
                out[0] = QStringList{"garden_get_skip", "garden_not_interested", "garden_start_game"}.indexOf(
                    target->name);
                break;
            }
            case rust::WorkflowOp::FindGardenGold: {
                tier = c_.find("garden_get_gold", {.horizontal = a[0]});
                out[0] = tier.has_value();
                break;
            }
            case rust::WorkflowOp::FindGardenPurple: {
                tier = c_.find("garden_get_purple", {.horizontal = a[0]});
                out[0] = tier.has_value();
                break;
            }
            case rust::WorkflowOp::ClickGardenRewardTier: {
                c_.click(*tier, a[0]);
                break;
            }
            case rust::WorkflowOp::ClickGardenFixedFeature: {
                const QStringList names{"garden_get_confirm_gray", "garden_not_interested_confirm"};
                const auto name = names.at(int(a[0]));
                c_.click(Match{name, c_.templates.box(name, {c_.frame.cols, c_.frame.rows}), 1}, a[1]);
                break;
            }
            case rust::WorkflowOp::ReadGardenNotInterested: {
                items = c_.templates.find(c_.frame, "garden_not_interested", {.vertical = a[0], .limit = 0});
                out[0] = double(items.size());
                break;
            }
            case rust::WorkflowOp::ClickLastGardenNotInterested: {
                c_.click(items.back(), a[0]);
                break;
            }
            case rust::WorkflowOp::ClickBestGardenFeature: {
                c_.click(*target, a[0]);
                break;
            }
            case rust::WorkflowOp::ReadGardenFinishButtons: {
                restart = c_.find("a_garden_restart");
                back = c_.find("a_garden_back");
                out[0] = restart.has_value();
                out[1] = back.has_value();
                break;
            }
            case rust::WorkflowOp::ClickGardenBack: {
                c_.click(*back, a[0]);
                break;
            }
            case rust::WorkflowOp::FindGardenStart: {
                out[0] = bool(c_.find("garden_start_game"));
                break;
            }
            case rust::WorkflowOp::Key: {
                c_.key("esc", a[1]);
                break;
            }
            case rust::WorkflowOp::Throw: {
                throw NavigationFailure("周常当前步骤长时间未推进，保留现场等待恢复");
                break;
            }
            default:
                fail("Rust GardenLoop 周常流程宿主无效");
            }
        });
}
void Garden::openWeekly() {
    rust::workflowRun(
        rust::WorkflowMethod::GardenOpenWeekly, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::Now: {
                out[0] = c_.execution.now();
                break;
            }
            case rust::WorkflowOp::Sleep: {
                c_.sleep(a[0]);
                break;
            }
            case rust::WorkflowOp::Refresh: {
                c_.refresh();
                break;
            }
            case rust::WorkflowOp::HasInfo: {
                out[0] = bool(c_.info);
                break;
            }
            case rust::WorkflowOp::ClickRelative: {
                c_.clickRelative(a[0], a[1], a[2]);
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
            case rust::WorkflowOp::InvalidCondition: {
                fail("等待条件参数无效");
                break;
            }
            case rust::WorkflowOp::OpenGardenWeeklyBook: {
                navigation_.openBook("gray_book_quest");
                break;
            }
            case rust::WorkflowOp::ReadGardenWeeklyProgress: {
                out[0] = weeklyProgress().has_value();
                break;
            }
            case rust::WorkflowOp::GardenWeeklyRetryInfo: {
                c_.info("周常进度", QString("页面或计数尚未确认，重新打开 %1/2").arg(int(a[0])));
                break;
            }
            case rust::WorkflowOp::InvokeGardenWeeklyFailure: {
                const QStringList reasons{"无法确认周常页面或游历值，已保存识别现场，不自动开始乐园",
                                          "领奖后无法确认周任务进度，已保存识别现场，不自动重开乐园"};
                weeklyFailure(reasons.at(int(a[0])));
                break;
            }
            default:
                fail("Rust GardenOpenWeekly 周常流程宿主无效");
            }
        });
}
void Garden::weeklyFailure(const QString &reason) {
    rust::workflowRun(rust::WorkflowMethod::GardenWeeklyFailure, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::HasGardenDiagnostic: {
                              out[0] = bool(c_.diagnostic);
                              break;
                          }
                          case rust::WorkflowOp::EmitGardenWeeklyDiagnostic: {
                              c_.diagnostic(reason, {{"weekly_observation", weeklyObservation_}});
                              break;
                          }
                          case rust::WorkflowOp::Throw: {
                              throw NavigationFailure(reason);
                              break;
                          }
                          default:
                              fail("Rust GardenWeeklyFailure 周常流程宿主无效");
                          }
                      });
    throw NavigationFailure(reason);
}
void Garden::runLoop() {
    rust::workflowRun(
        rust::WorkflowMethod::GardenRunLoop, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::Now: {
                out[0] = c_.execution.now();
                break;
            }
            case rust::WorkflowOp::Sleep: {
                c_.sleep(a[0]);
                break;
            }
            case rust::WorkflowOp::Refresh: {
                c_.refresh();
                break;
            }
            case rust::WorkflowOp::HasInfo: {
                out[0] = bool(c_.info);
                break;
            }
            case rust::WorkflowOp::ClickRelative: {
                c_.clickRelative(a[0], a[1], a[2]);
                break;
            }
            case rust::WorkflowOp::RecoveryEnabled: {
                out[0] = c_.profile.value("Task Recovery", "Resume Interrupted Tasks", true).toBool();
                break;
            }
            case rust::WorkflowOp::RecoveryLimit: {
                out[0] = c_.profile.value("Task Recovery", "Recovery Retry Limit", 3).toInt();
                break;
            }
            case rust::WorkflowOp::RunGardenLoop: {
                loop();
                break;
            }
            case rust::WorkflowOp::GardenLoopRetryInfo: {
                c_.info("周常恢复", QString("保留当前阶段重试 %1/%2").arg(int(a[0])).arg(int(a[1])));
                break;
            }
            default:
                fail("Rust GardenRunLoop 周常流程宿主无效");
            }
        });
}
void Garden::run() {
    std::optional<TaskScope> scope;
    std::optional<int> progress;
    rust::workflowRun(
        rust::WorkflowMethod::GardenRun, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::Now: {
                out[0] = c_.execution.now();
                break;
            }
            case rust::WorkflowOp::Sleep: {
                c_.sleep(a[0]);
                break;
            }
            case rust::WorkflowOp::Refresh: {
                c_.refresh();
                break;
            }
            case rust::WorkflowOp::HasInfo: {
                out[0] = bool(c_.info);
                break;
            }
            case rust::WorkflowOp::ClickRelative: {
                c_.clickRelative(a[0], a[1], a[2]);
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
            case rust::WorkflowOp::InvalidCondition: {
                fail("等待条件参数无效");
                break;
            }
            case rust::WorkflowOp::GardenTaskScope: {
                scope.emplace(c_, "GardenTask", config_);
                break;
            }
            case rust::WorkflowOp::IsGardenActive: {
                out[0] = active();
                break;
            }
            case rust::WorkflowOp::RunGardenLoopWithRetries: {
                runLoop();
                break;
            }
            case rust::WorkflowOp::NavEnsureMain: {
                navigation_.ensureMain(a[0]);
                break;
            }
            case rust::WorkflowOp::OpenGardenWeekly: {
                openWeekly();
                break;
            }
            case rust::WorkflowOp::ReadGardenWeeklyProgress: {
                progress = weeklyProgress();
                out[0] = progress.has_value();
                break;
            }
            case rust::WorkflowOp::GardenProgressExists: {
                out[0] = progress.has_value();
                break;
            }
            case rust::WorkflowOp::GardenProgressValue: {
                out[0] = *progress;
                break;
            }
            case rust::WorkflowOp::InvokeGardenWeeklyFailure: {
                const QStringList reasons{"无法确认周常页面或游历值，已保存识别现场，不自动开始乐园",
                                          "领奖后无法确认周任务进度，已保存识别现场，不自动重开乐园"};
                weeklyFailure(reasons.at(int(a[0])));
                break;
            }
            case rust::WorkflowOp::GardenPointsInfo: {
                c_.info("本周游历值", QString("%1/6000").arg(int(a[0])));
                break;
            }
            case rust::WorkflowOp::Throw: {
                throw NavigationFailure("周常达到单次运行上限，仍未确认本周游历值已满");
                break;
            }
            default:
                fail("Rust GardenRun 周常流程宿主无效");
            }
        });
}
} // namespace okww::runtime
