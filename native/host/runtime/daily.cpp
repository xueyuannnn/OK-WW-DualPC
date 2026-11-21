#include "daily.hpp"
#include "diagnostics.hpp"
#include "workflow_abi.hpp"
namespace okww::runtime {
namespace {
const QString additions = "Additional Tasks to Run After Daily Task";
const QString weekly = "Check Weekly Garden", allNests = "Auto Farm all Nightmare Nest",
              merge = "Merge Echo If discarded > 1000", echo = "Teleport and Farm 4C Echo",
              materials = "4C Echo and Advanced Skill Material";
QString countKey(DomainKind kind) {
    const QStringList keys{"Tacet Suppression Count", "Forgery Challenge Count",
                           "Simulation Challenge Count"};
    const std::array args{double(int(kind))};
    const auto result = rust::workflowRun(rust::WorkflowMethod::DailyCountKey, args,
                                          [&](rust::WorkflowOp op, const double *a, double *out) {
                                              switch (op) {
                                              default:
                                                  fail("Rust DailyCountKey 每日宿主无效");
                                              }
                                          });
    return keys.at(int(result[0]));
}
} // namespace
bool DailyProgress::ready() const {
    const std::array args{double(points)};
    return rust::workflowRun(rust::WorkflowMethod::DailyReady, args,
                             [&](rust::WorkflowOp op, const double *a, double *out) {
                                 switch (op) {
                                 default:
                                     fail("Rust DailyReady 每日宿主无效");
                                 }
                             })[0] != 0;
}
DailyProgress dailyProgress(const QVector<TextLine> &objectives, const QVector<TextLine> &points) {
    DailyProgress result;
    QRegularExpressionMatch match;
    QString point;
    QSet<int> activity;
    const QRegularExpression echoObjective(
        "(吸收|获得|獲得|获取|獲取).{0,20}(声骸|聲骸)|(Absorb|Obtain|Collect).{0,30}Echo",
        QRegularExpression::CaseInsensitiveOption);
    rust::workflowRun(
        rust::WorkflowMethod::DailyProgress, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::DailyObjectiveCount: {
                out[0] = double(objectives.size());
                break;
            }
            case rust::WorkflowOp::DailyEchoObjective: {
                out[0] = echoObjective.match(objectives.at(int(a[0])).text).hasMatch();
                break;
            }
            case rust::WorkflowOp::DailyObjectiveGeometry: {
                const auto &candidate = objectives.at(int(a[0]));
                const auto &line = objectives.at(int(a[1]));
                out[0] = candidate.center().y() - line.center().y();
                out[1] = std::max(candidate.box.height(), line.box.height());
                break;
            }
            case rust::WorkflowOp::DailyObjectiveCompleted: {
                out[0] = QRegularExpression("已完成|已领取|已領取|Completed|Claimed",
                                            QRegularExpression::CaseInsensitiveOption)
                             .match(objectives.at(int(a[0])).text)
                             .hasMatch();
                break;
            }
            case rust::WorkflowOp::DailyObjectiveCounter: {
                match = QRegularExpression("(\\d+)\\s*[/／]\\s*(\\d+)").match(objectives.at(int(a[0])).text);
                out[0] = match.hasMatch();
                break;
            }
            case rust::WorkflowOp::DailyObjectiveCounterValues: {
                out[0] = match.captured(1).toInt();
                out[1] = match.captured(2).toInt();
                break;
            }
            case rust::WorkflowOp::StoreDailyEchoNeeded: {
                result.echoNeeded = a[0] != 0;
                break;
            }
            case rust::WorkflowOp::DailyStaminaObjective: {
                match = QRegularExpression("^(\\d+)\\s*[/／]\\s*180$")
                            .match(objectives.at(int(a[0])).text.trimmed());
                out[0] = match.hasMatch();
                break;
            }
            case rust::WorkflowOp::DailyStaminaObjectiveValue: {
                bool ok = false;
                out[0] = match.captured(1).toInt(&ok);
                out[1] = ok;
                break;
            }
            case rust::WorkflowOp::StoreDailyStaminaProgress: {
                result.stamina = int(a[0]);
                break;
            }
            case rust::WorkflowOp::DailyPointsLineCount: {
                out[0] = double(points.size());
                break;
            }
            case rust::WorkflowOp::DailyPointMatches: {
                point = points.at(int(a[0])).text.trimmed();
                out[0] = QRegularExpression("^\\d{1,3}$").match(point).hasMatch();
                break;
            }
            case rust::WorkflowOp::InsertDailyPoint: {
                activity.insert(point.toInt());
                break;
            }
            case rust::WorkflowOp::DailyUniquePointsCount: {
                out[0] = double(activity.size());
                break;
            }
            case rust::WorkflowOp::StoreDailyUniquePoint: {
                result.points = *activity.cbegin();
                break;
            }
            default:
                fail("Rust DailyProgress 每日宿主无效");
            }
        });
    return result;
}
DailyProgress observeDailyProgress(TaskContext &c, QJsonObject *observation) {
    QVector<TextLine> heading, objectives, strip, points, labels;
    QRect counter, labelArea;
    QJsonArray headingLines, objectiveLines, stripLines;
    const QString dailyLabel = "^(?:活[跃躍]度|Daily\\s*Activity|Activity(?:\\s*Points?)?)$";
    DailyProgress result;
    rust::workflowRun(rust::WorkflowMethod::ObserveDailyProgress, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::ReadDailyHeading: {
                              heading = c.text(.09, .08, .65, .18, .75);
                              break;
                          }
                          case rust::WorkflowOp::ReadDailyObjectives: {
                              objectives = c.text(.1, .18, .96, .79, .75);
                              break;
                          }
                          case rust::WorkflowOp::ReadDailyStrip: {
                              strip = c.text(.05, .78, .98, .97, .75);
                              break;
                          }
                          case rust::WorkflowOp::PrepareDailyCounterAreas: {
                              counter = gameBox(c.frame, .18, .81, .30, .90);
                              labelArea = gameBox(c.frame, .17, .88, .34, .96);
                              break;
                          }
                          case rust::WorkflowOp::DailyHeadingCount: {
                              out[0] = double(heading.size());
                              break;
                          }
                          case rust::WorkflowOp::AppendDailyHeadingJson: {
                              headingLines.append(heading.at(int(a[0])).json());
                              break;
                          }
                          case rust::WorkflowOp::DailyObjectiveCount: {
                              out[0] = double(objectives.size());
                              break;
                          }
                          case rust::WorkflowOp::AppendDailyObjectiveJson: {
                              objectiveLines.append(objectives.at(int(a[0])).json());
                              break;
                          }
                          case rust::WorkflowOp::DailyStripCount: {
                              out[0] = double(strip.size());
                              break;
                          }
                          case rust::WorkflowOp::AppendDailyStripJson: {
                              stripLines.append(strip.at(int(a[0])).json());
                              break;
                          }
                          case rust::WorkflowOp::DailyCounterContains: {
                              out[0] = counter.contains(strip.at(int(a[0])).center().toPoint());
                              break;
                          }
                          case rust::WorkflowOp::AppendDailyCounterLine: {
                              points.append(strip.at(int(a[0])));
                              break;
                          }
                          case rust::WorkflowOp::DailyLabelContains: {
                              out[0] = labelArea.contains(strip.at(int(a[0])).center().toPoint());
                              break;
                          }
                          case rust::WorkflowOp::AppendDailyLabelLine: {
                              labels.append(strip.at(int(a[0])));
                              break;
                          }
                          case rust::WorkflowOp::DailyHeadingMatches: {
                              out[0] = bool(matchingText(heading, dailyLabel));
                              break;
                          }
                          case rust::WorkflowOp::DailyCounterLabelMatches: {
                              out[0] = bool(matchingText(labels, dailyLabel));
                              break;
                          }
                          case rust::WorkflowOp::ReadDailyProgress: {
                              result = dailyProgress(objectives, a[0] != 0 ? points : QVector<TextLine>{});
                              break;
                          }
                          case rust::WorkflowOp::HasDailyObservation: {
                              out[0] = observation != nullptr;
                              break;
                          }
                          case rust::WorkflowOp::StoreDailyObservation: {
                              *observation = {{"page_confirmed", a[0] != 0},
                                              {"points", result.points},
                                              {"heading_lines", headingLines},
                                              {"objective_lines", objectiveLines},
                                              {"reward_strip_lines", stripLines}};
                              break;
                          }
                          default:
                              fail("Rust ObserveDailyProgress 每日宿主无效");
                          }
                      });
    return result;
}
DomainKind dailyDomain(const QJsonObject &config) {
    return DomainKind(int(rust::workflowRun(
        rust::WorkflowMethod::DailyDomain, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::DailyForgeryFirst: {
                out[0] = config["Run Tacet or Forgery First"] == "Forgery First";
                break;
            }
            case rust::WorkflowOp::DailyPreferredDomain: {
                out[0] = QStringList{"Forgery Challenge", "Simulation Challenge"}.indexOf(
                    config["Which to Farm"].toString("Tacet Suppression"));
                break;
            }
            default:
                fail("Rust DailyDomain 每日宿主无效");
            }
        })[0]));
}
void Daily::status(const QString &step) {
    rust::workflowRun(rust::WorkflowMethod::DailyStatus, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::HasInfo: {
                              out[0] = bool(c_.info);
                              break;
                          }
                          case rust::WorkflowOp::DailyStatusInfo: {
                              c_.info("当前任务", step);
                              break;
                          }
                          default:
                              fail("Rust DailyStatus 每日宿主无效");
                          }
                      });
}
bool Daily::stage(const QString &name, const std::function<void()> &operation, bool retryNavigation) {
    QString error;
    const std::array args{double(retryNavigation)};
    return rust::workflowRun(
               rust::WorkflowMethod::DailyStage, args,
               [&](rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &exception) {
                   switch (op) {
                   case rust::WorkflowOp::DailyWorkflowRunning: {
                       c_.workflowStep(name, "running");
                       break;
                   }
                   case rust::WorkflowOp::DailyStageStatus: {
                       status(name);
                       break;
                   }
                   case rust::WorkflowOp::DailyRetries: {
                       out[0] = retries_;
                       break;
                   }
                   case rust::WorkflowOp::DailyStageOperation: {
                       operation();
                       break;
                   }
                   case rust::WorkflowOp::DailyWorkflowCompleted: {
                       c_.workflowStep(name, "completed");
                       break;
                   }
                   case rust::WorkflowOp::DailyExecutionFailed: {
                       c_.workflowStep(name, "failed", "输入或执行通道异常，已停止后续操作");
                       break;
                   }
                   case rust::WorkflowOp::DailyExceptionMessage: {
                       try {
                           if (exception)
                               std::rethrow_exception(exception);
                       } catch (const std::exception &e) {
                           error = errorText(e);
                       }
                       break;
                   }
                   case rust::WorkflowOp::HasInfo: {
                       out[0] = bool(c_.info);
                       break;
                   }
                   case rust::WorkflowOp::DailyStageErrorInfo: {
                       c_.info(name, error);
                       break;
                   }
                   case rust::WorkflowOp::DailyWorkflowFailed: {
                       c_.workflowStep(name, "failed", error);
                       break;
                   }
                   case rust::WorkflowOp::AppendDailyFailedStage: {
                       failed_.append(name + "：" + error);
                       break;
                   }
                   case rust::WorkflowOp::HasNotify: {
                       out[0] = bool(c_.notify);
                       break;
                   }
                   case rust::WorkflowOp::NotifyDailyFailedStage: {
                       c_.notify("每日任务步骤未完成", name + "：" + error, true);
                       break;
                   }
                   case rust::WorkflowOp::NavEnsureMain: {
                       navigation_.ensureMain(a[0]);
                       break;
                   }
                   default:
                       fail("Rust DailyStage 每日宿主无效");
                   }
               })[0] != 0;
}
void Daily::observationFailure(const QString &reason) {
    rust::workflowRun(rust::WorkflowMethod::DailyObservationFailure, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::HasDailyDiagnostic: {
                              out[0] = bool(c_.diagnostic);
                              break;
                          }
                          case rust::WorkflowOp::EmitDailyObservationFailure: {
                              c_.diagnostic(reason,
                                            {{"daily_points", dailyObservation_.value("points").toInt(-1)},
                                             {"daily_observation", dailyObservation_}});
                              break;
                          }
                          case rust::WorkflowOp::Throw: {
                              fail(reason);
                              break;
                          }
                          default:
                              fail("Rust DailyObservationFailure 每日宿主无效");
                          }
                      });
    fail(reason);
}
DailyProgress Daily::rustActions(int method, std::initializer_list<double> args) {
    DailyProgress progress;
    QJsonArray observations, pages, objectiveValues, box, selected;
    QJsonObject objective;
    QJsonValue previous, quota;
    QRect rect;
    TextLine target;
    DomainKind kind = DomainKind::Tacet;
    int remaining = 0;
    std::unique_ptr<CubeBudget> budget;
    std::unique_ptr<DomainFarm> farm;
    std::function<void(rust::WorkflowOp, const double *, double *, const std::exception_ptr &)> action;
    action = [&](rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &exception) {
        switch (op) {
        case rust::WorkflowOp::HasInfo: {
            out[0] = bool(c_.info);
            break;
        }
        case rust::WorkflowOp::HasNotify: {
            out[0] = bool(c_.notify);
            break;
        }
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
        case rust::WorkflowOp::InvalidCondition: {
            fail("等待条件参数无效");
            break;
        }
        case rust::WorkflowOp::Refresh: {
            c_.refresh();
            break;
        }
        case rust::WorkflowOp::InTeam: {
            out[0] = c_.inTeam();
            break;
        }
        case rust::WorkflowOp::Sleep: {
            c_.sleep(a[0]);
            break;
        }
        case rust::WorkflowOp::ClickRelative: {
            c_.clickRelative(a[0], a[1], a[2]);
            break;
        }
        case rust::WorkflowOp::DailyPageInfo: {
            c_.info("每日页面", int(a[0]) == 1 ? "打开每日目标" : "页面未确认，重新打开一次");
            break;
        }
        case rust::WorkflowOp::DailyOpenQuestBook: {
            navigation_.openBook("gray_book_quest");
            break;
        }
        case rust::WorkflowOp::ReadDailyPage: {
            progress = observeDailyProgress(c_, &dailyObservation_);
            break;
        }
        case rust::WorkflowOp::AppendDailyPageObservation: {
            observations.append(dailyObservation_);
            break;
        }
        case rust::WorkflowOp::DailyObservationCount: {
            out[0] = double(observations.size());
            break;
        }
        case rust::WorkflowOp::RemoveFirstDailyObservation: {
            observations.removeFirst();
            break;
        }
        case rust::WorkflowOp::DailyPageConfirmed: {
            out[0] = dailyObservation_["page_confirmed"].toBool();
            break;
        }
        case rust::WorkflowOp::StoreDailyFailedObservations: {
            dailyObservation_["recent_observations"] = observations;
            dailyObservation_["navigation_attempts"] = int(a[0]);
            break;
        }
        case rust::WorkflowOp::ReadDailyObjectiveValues: {
            objectiveValues = dailyObservation_["objective_lines"].toArray();
            break;
        }
        case rust::WorkflowOp::DailyObjectiveValueCount: {
            out[0] = double(objectiveValues.size());
            break;
        }
        case rust::WorkflowOp::DailyValidObjectiveBox: {
            objective = objectiveValues.at(int(a[0])).toObject();
            box = objective["box"].toArray();
            out[0] = box.size() == 4;
            break;
        }
        case rust::WorkflowOp::DailyObjectiveClaimText: {
            out[0] =
                QRegularExpression("^(?:领取|領取|Claim|Collect)$", QRegularExpression::CaseInsensitiveOption)
                    .match(objective["text"].toString().trimmed())
                    .hasMatch();
            break;
        }
        case rust::WorkflowOp::ReadDailyObjectiveRect: {
            rect = QRect(box[0].toInt(), box[1].toInt(), box[2].toInt(), box[3].toInt());
            break;
        }
        case rust::WorkflowOp::DailyObjectiveClaimInside: {
            out[0] = gameBox(c_.frame, .8, .18, .96, .79).contains(rect.center());
            break;
        }
        case rust::WorkflowOp::StoreDailyObjectiveTarget: {
            target = TextLine{objective["text"].toString(), rect, objective["confidence"].toDouble()};
            break;
        }
        case rust::WorkflowOp::StorePreviousDailyObjectives: {
            previous = dailyObservation_.value("objective_lines");
            break;
        }
        case rust::WorkflowOp::ClickDailyObjective: {
            c_.click(target, a[0]);
            break;
        }
        case rust::WorkflowOp::DailyObjectivesUnchanged: {
            out[0] = previous == dailyObservation_["objective_lines"];
            break;
        }
        case rust::WorkflowOp::ScrollDailyPage: {
            c_.input.scroll(int(a[0]), QPointF(c_.frame.cols * .65, c_.frame.rows * .4));
            break;
        }
        case rust::WorkflowOp::AppendDailyObjectivePage: {
            pages.append(dailyObservation_);
            break;
        }
        case rust::WorkflowOp::BeginDailyObjectivePages: {
            pages = QJsonArray{dailyObservation_};
            break;
        }
        case rust::WorkflowOp::LoadDailyProgress: {
            out[0] = progress.points;
            out[1] = progress.stamina.value_or(-1);
            out[2] = progress.echoNeeded ? double(*progress.echoNeeded) : -1.;
            break;
        }
        case rust::WorkflowOp::StoreDailyProgress: {
            progress.points = int(a[0]);
            progress.stamina = a[1] == -1 ? std::optional<int>{} : std::optional<int>{int(a[1])};
            progress.echoNeeded = a[2] == -1 ? std::optional<bool>{} : std::optional<bool>{a[2] != 0};
            break;
        }
        case rust::WorkflowOp::DailyEchoEnabled: {
            out[0] = config_.value("Farm Nightmare Nest for Daily Echo").toBool(true);
            break;
        }
        case rust::WorkflowOp::StoreDailyObjectivePages: {
            dailyObservation_["objective_pages"] = pages;
            dailyObservation_["stamina"] = progress.stamina.value_or(-1);
            dailyObservation_["echo_needed"] =
                progress.echoNeeded ? QJsonValue(*progress.echoNeeded) : QJsonValue();
            break;
        }
        case rust::WorkflowOp::DailyProgressInfo: {
            c_.info("每日活跃度", progress.points >= 0 ? QString::number(progress.points) : "未识别");
            c_.info("每日体力进度", progress.stamina ? QString("%1/180").arg(*progress.stamina) : "未识别");
            break;
        }
        case rust::WorkflowOp::ReportDailyProgress: {
            c_.reportProgress("完成每日目标", progress.points, 100, "活跃度",
                              progress.echoNeeded.value_or(false) ? "仍需获取声骸" : "检查目标与可领取奖励");
            break;
        }
        case rust::WorkflowOp::OpenDailyProgress: {
            progress = open(a[0] != 0);
            break;
        }
        case rust::WorkflowOp::DailyClaimedPoints: {
            out[0] = claimedAtPoints_;
            break;
        }
        case rust::WorkflowOp::StoreDailyClaimedPoints: {
            claimedAtPoints_ = int(a[0]);
            break;
        }
        case rust::WorkflowOp::DailyClaimInfo: {
            c_.info("当前任务", "领取每日奖励");
            break;
        }
        case rust::WorkflowOp::DailyRewardText: {
            out[0] = rewardTextConfirmed(c_.text(.2, .12, .8, .96, .7));
            break;
        }
        case rust::WorkflowOp::DailyEnsureMainDefault: {
            navigation_.ensureMain();
            break;
        }
        case rust::WorkflowOp::NavEnsureMain: {
            navigation_.ensureMain(a[0]);
            break;
        }
        case rust::WorkflowOp::DailyMailEscape: {
            c_.key("esc", 1.5);
            break;
        }
        case rust::WorkflowOp::DailyAltDown: {
            c_.input.keyDown("alt");
            break;
        }
        case rust::WorkflowOp::DailyAltUp: {
            c_.input.keyUp("alt");
            break;
        }
        case rust::WorkflowOp::DailyBattlePassText: {
            out[0] = bool(matchingText(c_.text(.2, .13, .32, .22), "\\d+"));
            break;
        }
        case rust::WorkflowOp::DailyBattlePassCollect: {
            c_.clickRelative(.68, .91, 2, true);
            break;
        }
        case rust::WorkflowOp::DailyBattlePassUnavailable: {
            c_.info("先约电台", "当前未识别到可领取页面，可能已结束");
            break;
        }
        case rust::WorkflowOp::ReadDailySelectedExtras: {
            selected = config_[additions].toArray();
            break;
        }
        case rust::WorkflowOp::DailyHasEchoExtra: {
            out[0] = selected.contains(int(a[0]) == 0 ? echo : materials);
            break;
        }
        case rust::WorkflowOp::DailyEchoExtraNeedsTarget: {
            out[0] = c_.profile.value(int(a[0]) == 0 ? "FarmEchoTask" : "FarmEchoTask2", "Teleport to Boss",
                                      "No") == "No";
            break;
        }
        case rust::WorkflowOp::DailyHasNestExtra: {
            out[0] = selected.contains(allNests);
            break;
        }
        case rust::WorkflowOp::DailyNestTargetsEmpty: {
            out[0] = c_.profile.value("NightmareNestTask", "Which to Farm").toArray().empty();
            break;
        }
        case rust::WorkflowOp::DailyNoProgress: {
            out[0] = noProgress_;
            break;
        }
        case rust::WorkflowOp::SetDailyNoProgress: {
            noProgress_ = true;
            break;
        }
        case rust::WorkflowOp::DailyUseCubes: {
            out[0] = config_["Use Stamina Cubes for Daily"].toBool();
            break;
        }
        case rust::WorkflowOp::CreateDailyCubeBudget: {
            budget = std::make_unique<CubeBudget>(dailyCubeJournal(data_ + "/configs-dualpc",cubeAccount_),
                                                  config_["Daily Stamina Cube Limit"].toInt(4));
            break;
        }
        case rust::WorkflowOp::DailyCubeErrorInfo: {
            try {
                std::rethrow_exception(exception);
            } catch (const std::exception &e) {
                c_.info("体力魔方记录待核对", errorText(e));
            }
            break;
        }
        case rust::WorkflowOp::DailyCubeErrorNotify: {
            try {
                std::rethrow_exception(exception);
            } catch (const std::exception &e) {
                c_.notify("体力魔方记录待核对", errorText(e), true);
            }
            break;
        }
        case rust::WorkflowOp::HasDailyCubeBudget: {
            out[0] = bool(budget);
            break;
        }
        case rust::WorkflowOp::DailyCubeUsageInfo: {
            c_.info("已用体力魔方", QString("%1/%2").arg(budget->used()).arg(budget->limit()));
            break;
        }
        case rust::WorkflowOp::ReadDailyDomainKind: {
            kind = dailyDomain(config_);
            out[0] = int(kind);
            break;
        }
        case rust::WorkflowOp::ReadDailyDomainCost: {
            out[0] = domainCost(kind);
            break;
        }
        case rust::WorkflowOp::DailyOpenBossBook: {
            navigation_.openBook("gray_book_boss", a[0] != 0);
            break;
        }
        case rust::WorkflowOp::DailyStaminaRetryInfo: {
            if(c_.info) c_.info("读取体力",QString("计数尚未确认，重新打开索拉指南后重读（%1/2）").arg(int(a[0])));
            break;
        }
        case rust::WorkflowOp::ReadDailyStamina: {
            const auto value = readStamina(c_);
            out[0] = value.current;
            out[1] = value.reserve;
            out[2] = value.total;
            break;
        }
        case rust::WorkflowOp::DailyCubeRemaining: {
            out[0] = budget->remaining();
            break;
        }
        case rust::WorkflowOp::UseDailyCubeBudget: {
            out[0] = useOneDailyCube(c_, *budget);
            break;
        }
        case rust::WorkflowOp::CreateDailyStaminaFarm: {
            farm = std::make_unique<DomainFarm>(c_, combat_);
            farm->onStaminaSpent = [&](int spent) { dailySpent_[countKey(kind)] += spent; };
            break;
        }
        case rust::WorkflowOp::DailyPreferDouble: {
            out[0] = config_["Reward Claim Mode"].toString("Prefer Double") != "Single";
            break;
        }
        case rust::WorkflowOp::FarmDailyStamina: {
            farm->farm(kind, int(a[0]), true, config_, true);
            break;
        }
        case rust::WorkflowOp::ClaimDailyProgress: {
            progress = claim();
            break;
        }
        case rust::WorkflowOp::DailyStaminaNoProgressInfo: {
            c_.info("每日体力进度", progress.stamina
                ? QString("多次复核后进度仍为 %1/180，未追加体力消耗").arg(*progress.stamina)
                : QString("多次复核后仍未识别到体力进度，未追加体力消耗"));
            break;
        }
        case rust::WorkflowOp::DailyStaminaNoProgressNotify: {
            c_.notify("每日体力进度待检查", "已等待页面更新并多次复核，仍未确认进度增加，未追加体力消耗", true);
            break;
        }
        case rust::WorkflowOp::ReadDailyDomainQuota: {
            kind = DomainKind(int(a[0]));
            quota = config_[countKey(kind)];
            break;
        }
        case rust::WorkflowOp::DailyDomainQuotaNumber: {
            out[0] = quota.isDouble();
            break;
        }
        case rust::WorkflowOp::DailyDomainQuota: {
            out[0] = quota.toDouble();
            break;
        }
        case rust::WorkflowOp::DailyDomainSpent: {
            out[0] = dailySpent_.value(countKey(DomainKind(int(a[0]))));
            break;
        }
        case rust::WorkflowOp::DailyKindCost: {
            out[0] = domainCost(DomainKind(int(a[0])));
            break;
        }
        case rust::WorkflowOp::DailyDomainCountLimit: {
            out[0] = domainCountLimit(int(a[0]), int(a[1]), int(a[2]));
            break;
        }
        case rust::WorkflowOp::StoreDailyRemainingQuota: {
            remaining = int(a[0]);
            break;
        }
        case rust::WorkflowOp::DailyRemainingQuota: {
            out[0] = remaining;
            break;
        }
        case rust::WorkflowOp::CreateDailyQuotaFarm: {
            farm = std::make_unique<DomainFarm>(c_, combat_);
            farm->onStaminaSpent = [&](int used) {
                const std::array values{double(used)};
                rust::workflowRun(rust::WorkflowMethod::DailyQuotaSpent, values, action);
            };
            break;
        }
        case rust::WorkflowOp::RunDailyQuotaStage: {
            const QStringList names{"清理体力 · 无音区", "清理体力 · 凝素领域", "清理体力 · 模拟领域"};
            stage(names.at(int(a[0])),
                  [&] { rust::workflowRun(rust::WorkflowMethod::DailyQuotaOperation, {}, action); });
            break;
        }
        case rust::WorkflowOp::FarmDailyQuota: {
            farm->farm(kind, remaining, true, config_, true);
            break;
        }
        case rust::WorkflowOp::DailyActionFailure: {
            const QStringList errors{
                "每日页面已打开，但活跃度连续识别失败，已保存页面与识别记录",
                "未能进入每日页面，已保存当前画面与识别记录，请检查游戏是否允许打开索拉指南",
                "每日目标领取后页面未更新，已保存现场，未重复点击",
                "每日目标领取次数异常，已保存现场",
                "每日奖励弹层关闭后未确认每日页面，已保存现场，未点击下一档奖励",
                "每日附加刷声骸任务需要先配置传送目标",
                "每日附加巢穴任务需要先选择刷取范围",
                "检查每日列表顶部后仍未识别到体力目标，已保存识别记录，未消耗体力",
                "无法确认体力，不使用魔方",
                "补充后无法确认体力，不继续使用魔方",
                "每日体力额度无效",
                "副本次数无效"};
            switch (int(a[0])) {
            case 0:
            case 1:
            case 2:
            case 3:
            case 4:
            case 7:
                observationFailure(errors.at(int(a[0])));
            default:
                fail(errors.at(int(a[0])));
            }
            break;
        }
        default:
            fail("Rust 每日动作宿主无效");
        }
    };
    rust::workflowRun(rust::WorkflowMethod(method), std::span(args.begin(), args.size()), action);
    return progress;
}
DailyProgress Daily::open(bool collectObjectives) {
    return rustActions(int(rust::WorkflowMethod::DailyOpen), {double(collectObjectives)});
}

int dailyStaminaBatch(int progress, int cost, int available, bool preferDouble) {
    const std::array args{double(progress), double(cost), double(available), double(preferDouble)};
    return int(rust::workflowRun(rust::WorkflowMethod::DailyStaminaBatch, args,
                                 [&](rust::WorkflowOp op, const double *, double *) {
                                     if (op == rust::WorkflowOp::DailyActionFailure)
                                         fail("每日体力额度无效");
                                     fail("Rust 每日体力额度宿主无效");
                                 })[0]);
}

void Daily::completeStamina() { rustActions(int(rust::WorkflowMethod::DailyCompleteStamina), {}); }
void Daily::farmCount(DomainKind kind) {
    rustActions(int(rust::WorkflowMethod::DailyFarmCount), {double(int(kind))});
}
DailyProgress Daily::claim() { return rustActions(int(rust::WorkflowMethod::DailyClaim), {}); }
void Daily::mail() { rustActions(int(rust::WorkflowMethod::DailyMail), {}); }
void Daily::battlePass() { rustActions(int(rust::WorkflowMethod::DailyBattlePass), {}); }
void Daily::validate() { rustActions(int(rust::WorkflowMethod::DailyValidate), {}); }
void Daily::run(bool enhanced, const QJsonObject &overrides, const QString &account) {
    cubeAccount_ = account;
    std::optional<TaskScope> scope;
    QJsonArray selected, steps;
    QVector<QPair<QString, std::function<void()>>> extras;
    DailyProgress progress;
    int echoAttempts = 0;
    bool captured = false;
    QString detail;
    const QStringList extraNames{"高级技能材料",
                                 "清理体力 · 无音区",
                                 "清理体力 · 凝素领域",
                                 "清理体力 · 模拟领域",
                                 "清理聚落",
                                 "邮件",
                                 "先约电台",
                                 "周常乐园",
                                 "弃置声骸合成",
                                 "4C 声骸"};
    const std::array args{double(enhanced)};
    rust::workflowRun(
        rust::WorkflowMethod::DailyRun, args, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::CreateDailyTaskScope: {
                scope.emplace(c_, a[0] != 0 ? "DailyTask2" : "DailyTask");
                c_.taskOverrides = overrides;
                break;
            }
            case rust::WorkflowOp::InitializeDailyRun: {
                config_ = c_.profile.group(c_.task);
                for (auto it = overrides.begin(); it != overrides.end(); ++it)
                    config_[it.key()] = it.value();
                noProgress_ = false;
                failed_.clear();
                dailySpent_.clear();
                claimedAtPoints_ = -1;
                extrasDone_ = extrasTotal_ = 0;
                break;
            }
            case rust::WorkflowOp::DailyRunConfiguredRetries: {
                out[0] = config_["Task Retry"].toInt(5);
                break;
            }
            case rust::WorkflowOp::StoreDailyRunRetries: {
                retries_ = int(a[0]);
                break;
            }
            case rust::WorkflowOp::ReadDailySelectedExtras: {
                selected = config_[additions].toArray();
                break;
            }
            case rust::WorkflowOp::DailyRunExtraSelected: {
                const QMap<int, QString> keys{
                    {0, materials}, {4, allNests}, {7, weekly}, {8, merge}, {9, echo}};
                out[0] = selected.contains(keys.value(int(a[0])));
                break;
            }
            case rust::WorkflowOp::DailyRunForgeryFirst: {
                out[0] = config_["Run Tacet or Forgery First"] == "Forgery First";
                break;
            }
            case rust::WorkflowOp::DailyRunQuota: {
                out[0] = config_[countKey(DomainKind(int(a[0])))].toInt();
                break;
            }
            case rust::WorkflowOp::AppendDailyRunExtra: {
                const int kind = int(a[0]);
                std::function<void()> operation;
                switch (kind) {
                case 0:
                    operation = [&] { EchoFarm(c_, combat_, detector_).run(true); };
                    break;
                case 1:
                case 2:
                case 3:
                    operation = [&, kind] { farmCount(DomainKind(kind - 1)); };
                    break;
                case 4:
                    operation = [&] { NightmareFarm(c_, combat_, detector_).run(); };
                    break;
                case 5:
                    operation = [&] { mail(); };
                    break;
                case 6:
                    operation = [&] { battlePass(); };
                    break;
                case 7:
                    operation = [&] { Garden(c_, root_).run(); };
                    break;
                case 8:
                    operation = [&] { EchoOperations(c_, data_ + "/screenshots").merge(); };
                    break;
                case 9:
                    operation = [&] { EchoFarm(c_, combat_, detector_).run(); };
                    break;
                default:
                    fail("每日附加任务编号无效");
                }
                extras.append({extraNames.at(kind), operation});
                break;
            }
            case rust::WorkflowOp::AppendDailyRunStep: {
                const QStringList names{"进入游戏", "检查每日目标与奖励", "完成每日目标", "领取每日奖励"};
                const auto id = names.at(int(a[0]));
                steps.append(
                    QJsonObject{{"id", id},
                                {"name", id},
                                {"estimate_seconds", a[1]},
                                {"detail", int(a[0]) == 2 ? "按需补做体力、获取声骸，已完成则跳过" : ""}});
                break;
            }
            case rust::WorkflowOp::DailyRunNestTargetCount: {
                out[0] = double(c_.profile.value("NightmareNestTask", "Which to Farm").toArray().size());
                break;
            }
            case rust::WorkflowOp::AppendDailyExtraStep: {
                const auto id = extras.at(int(a[0])).first;
                steps.append(
                    QJsonObject{{"id", id}, {"name", id}, {"estimate_seconds", a[1]}, {"detail", QString{}}});
                break;
            }
            case rust::WorkflowOp::BeginDailyRunWorkflow: {
                c_.beginWorkflow(steps, data_ + "/task-timings.json");
                break;
            }
            case rust::WorkflowOp::DailyEnterGameRunning: {
                c_.workflowStep("进入游戏", "running");
                break;
            }
            case rust::WorkflowOp::ResetDailyLoggedIn: {
                c_.state->loggedIn = false;
                break;
            }
            case rust::WorkflowOp::NavEnsureMain: {
                navigation_.ensureMain(a[0]);
                break;
            }
            case rust::WorkflowOp::DailyEnterGameCompleted: {
                c_.workflowStep("进入游戏", "completed");
                break;
            }
            case rust::WorkflowOp::StageDailyRunInitialClaim: {
                out[0] = stage("检查每日目标与奖励", [&] { progress = claim(); }, false);
                break;
            }
            case rust::WorkflowOp::ResetDailyEchoAttempts: {
                echoAttempts = 0;
                break;
            }
            case rust::WorkflowOp::DailyRunReady: {
                out[0] = progress.ready();
                break;
            }
            case rust::WorkflowOp::DailyRunPoints: {
                out[0] = progress.points;
                break;
            }
            case rust::WorkflowOp::DailyRunEchoEnabled: {
                out[0] = config_["Farm Nightmare Nest for Daily Echo"].toBool(true);
                break;
            }
            case rust::WorkflowOp::DailyEchoAttempts: {
                out[0] = echoAttempts;
                break;
            }
            case rust::WorkflowOp::DailyRunNeedsEcho: {
                out[0] = progress.echoNeeded.value_or(false);
                break;
            }
            case rust::WorkflowOp::IncrementDailyEchoAttempts: {
                ++echoAttempts;
                break;
            }
            case rust::WorkflowOp::ResetDailyEchoCaptured: {
                captured = false;
                break;
            }
            case rust::WorkflowOp::StageDailyEchoCapture: {
                out[0] = stage("获取每日声骸 · 残像聚落", [&] {
                    auto config = c_.profile.group("NightmareNestTask");
                    config["Which to Farm"] = QJsonArray{"Tacet Discord Nest"};
                    captured = NightmareFarm(c_, combat_, detector_).run(true, config);
                });
                break;
            }
            case rust::WorkflowOp::StageDailyRunReward: {
                out[0] = stage("每日奖励", [&] { progress = claim(); }, false);
                break;
            }
            case rust::WorkflowOp::DailyEchoCaptured: {
                out[0] = captured;
                break;
            }
            case rust::WorkflowOp::DailyGoalStart: {
                const bool done = a[0] != 0;
                c_.workflowStep("完成每日目标", done ? "skipped" : "running",
                                done ? "每日活跃度已满" : QString{});
                break;
            }
            case rust::WorkflowOp::StageDailyRunStamina: {
                stage("补做体力目标", [&] { completeStamina(); }, false);
                break;
            }
            case rust::WorkflowOp::StageDailyRunCheck: {
                stage("检查每日目标", [&] { progress = open(); });
                break;
            }
            case rust::WorkflowOp::DailyRewardRunning: {
                c_.workflowStep("领取每日奖励", "running");
                break;
            }
            case rust::WorkflowOp::DailyRewardFinished: {
                c_.workflowStep("领取每日奖励", a[0] != 0 ? "completed" : "failed");
                break;
            }
            case rust::WorkflowOp::DailyGoalFailed: {
                c_.workflowStep("完成每日目标", "failed", "每日目标尚未完成");
                break;
            }
            case rust::WorkflowOp::DailyRunStamina: {
                out[0] = progress.stamina.value_or(-1);
                out[1] = progress.stamina.has_value();
                break;
            }
            case rust::WorkflowOp::DailyMissingStaminaDetail: {
                detail += "\n体力目标未识别。";
                break;
            }
            case rust::WorkflowOp::DailyStaminaDetail: {
                detail += QString("\n体力目标：%1/180。").arg(*progress.stamina);
                break;
            }
            case rust::WorkflowOp::DailyEchoDetail: {
                detail += a[0] != 0 ? "\n声骸目标尚未完成。" : "\n声骸目标尚未完成，自动获取每日声骸已关闭。";
                break;
            }
            case rust::WorkflowOp::HasDailyDiagnostic: {
                out[0] = bool(c_.diagnostic);
                break;
            }
            case rust::WorkflowOp::EmitDailyRunDiagnostic: {
                c_.diagnostic("每日目标尚未完成", {{"daily_points", progress.points},
                                                   {"stamina", progress.stamina.value_or(-1)},
                                                   {"echo_needed", progress.echoNeeded.value_or(false)},
                                                   {"echo_farming_enabled", a[0] != 0},
                                                   {"daily_observation", dailyObservation_}});
                break;
            }
            case rust::WorkflowOp::DailyGoalCompleted: {
                c_.workflowStep("完成每日目标", "completed");
                break;
            }
            case rust::WorkflowOp::ValidateDailyRun: {
                validate();
                break;
            }
            case rust::WorkflowOp::StoreDailyExtrasTotal: {
                extrasTotal_ = int(a[0]);
                break;
            }
            case rust::WorkflowOp::StoreDailyExtrasDone: {
                extrasDone_ = int(a[0]);
                break;
            }
            case rust::WorkflowOp::ReportDailyExtra: {
                c_.reportProgress("每日已领奖 · 附加任务", int(a[0]), extras.size(), "项",
                                  extras.at(int(a[1])).first);
                break;
            }
            case rust::WorkflowOp::StageDailyExtra: {
                const auto &extra = extras.at(int(a[0]));
                stage(extra.first, extra.second, a[1] != 0);
                break;
            }
            case rust::WorkflowOp::DailyRunHasFailures: {
                out[0] = !failed_.empty();
                break;
            }
            case rust::WorkflowOp::DailyRunCompleteStatus: {
                status("每日任务已完成");
                break;
            }
            case rust::WorkflowOp::DailyRunFailure: {
                switch (int(a[0])) {
                case 0:
                    fail("每日目标检查未完成，本次未开始刷取或附加任务。\n" + failed_.join('\n'));
                case 1:
                    fail(QString("每日活跃度 %1/100，尚有目标未完成，未开始附加聚落和周任务。")
                             .arg(progress.points) +
                         detail + (failed_.isEmpty() ? QString{} : "\n" + failed_.join('\n')));
                case 2:
                    fail("每日流程已结束，以下项目未完成：\n" + failed_.join('\n'));
                default:
                    fail("每日失败编号无效");
                }
                break;
            }
            default:
                fail("Rust 每日流程宿主无效");
            }
        });
}
} // namespace okww::runtime
