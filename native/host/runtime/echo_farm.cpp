#include "echo_farm.hpp"
#include "workflow_abi.hpp"
namespace okww::runtime {
bool weeklyMaterialRewardConfirmed(const QVector<TextLine> &lines) {
    return rust::workflowRun(
               rust::WorkflowMethod::WeeklyMaterialReward, {},
               [&](rust::WorkflowOp op, const double *a, double *out) {
                   switch (op) {
                   case rust::WorkflowOp::EchoRewardResourceDialog: {
                       out[0] = restartDialog(lines).kind == RestartKind::Resource;
                       break;
                   }
                   case rust::WorkflowOp::EchoRewardText: {
                       out[0] = rewardTextConfirmed(lines);
                       break;
                   }
                   case rust::WorkflowOp::EchoRewardHeading: {
                       out[0] = bool(matchingText(lines, "^(?:获得奖励|獲得獎勵|获得物品|獲得物品|Rewards? "
                                                         "Obtained|Items? Obtained)[.:：]*$"));
                       break;
                   }
                   case rust::WorkflowOp::EchoRewardControls: {
                       out[0] = bool(matchingText(
                           lines, "^(?:离开|離開|退出|再次挑战|再次挑戰|重新挑战|重新挑戰|继续挑战|繼續挑戰|"
                                  "Leave|Exit|Challenge Again|Continue Challenge)$"));
                       break;
                   }
                   default:
                       fail("Rust 声骸刷取宿主无效");
                   }
               })[0] != 0;
}
void awaitWeeklyMaterialReward(TaskContext &context) {
    rust::workflowRun(rust::WorkflowMethod::AwaitWeeklyMaterialReward, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::Now: {
                              out[0] = context.execution.now();
                              break;
                          }
                          case rust::WorkflowOp::Checkpoint: {
                              context.execution.checkpoint();
                              break;
                          }
                          case rust::WorkflowOp::ExecutionWait: {
                              context.execution.wait(a[0]);
                              break;
                          }
                          case rust::WorkflowOp::Refresh: {
                              context.refresh();
                              break;
                          }
                          case rust::WorkflowOp::InvalidCondition: {
                              fail("等待条件参数无效");
                              break;
                          }
                          case rust::WorkflowOp::Sleep: {
                              context.sleep(a[0]);
                              break;
                          }
                          case rust::WorkflowOp::ClickRelative: {
                              context.clickRelative(a[0], a[1], a[2]);
                              break;
                          }
                          case rust::WorkflowOp::EchoFarmFailure: {
                              switch (int(a[0])) {
                              case 5:
                                  fail("未确认周本材料领奖结果，已停止，不会再次点击消耗按钮");
                              default:
                                  fail("声骸刷取失败编号无效");
                              }
                              break;
                          }
                          case rust::WorkflowOp::ReadEchoWeeklyReward: {
                              out[0] = weeklyMaterialRewardConfirmed(context.text(.08, .04, .92, .98, .7));
                              break;
                          }
                          default:
                              fail("Rust 声骸刷取宿主无效");
                          }
                      });
}
QVector<EchoTarget> echoRotationTargets(const QJsonObject &config, int bosses, int weekly) {
    QVector<EchoTarget> targets;
    QString scope;
    QJsonValue value;
    const std::array args{double(bosses), double(weekly)};
    rust::workflowRun(
        rust::WorkflowMethod::EchoRotationTargets, args,
        [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::EchoRotationScope: {
                scope = config["Cycle Scope"].toString("All Challenges");
                out[0] = QStringList{"All Challenges", "Boss Challenge", "Weekly Challenge"}.indexOf(scope);
                break;
            }
            case rust::WorkflowOp::EchoRotationStartCategory: {
                out[0] = QStringList{"Boss Challenge", "Weekly Challenge"}.indexOf(
                    config["Cycle Start Category"].toString("Boss Challenge"));
                break;
            }
            case rust::WorkflowOp::EchoRotationIndexAbsent: {
                value = config.value("Cycle Start Index");
                out[0] = value.isUndefined();
                break;
            }
            case rust::WorkflowOp::EchoRotationStartIndex: {
                out[0] = value.toDouble(-1);
                break;
            }
            case rust::WorkflowOp::AppendEchoRotationTarget: {
                targets.append(
                    {int(a[0]) == 0 ? QString("Boss Challenge") : QString("Weekly Challenge"), int(a[1])});
                break;
            }
            case rust::WorkflowOp::RotateEchoTargets: {
                std::rotate(targets.begin(), targets.begin() + int(a[0]), targets.end());
                break;
            }
            case rust::WorkflowOp::EchoFarmFailure: {
                switch (int(a[0])) {
                case 0:
                    fail("循环范围无效");
                case 1:
                    fail("循环起刷序号超出所选分类范围");
                default:
                    fail("声骸刷取失败编号无效");
                }
                break;
            }
            default:
                fail("Rust 声骸刷取宿主无效");
            }
        });
    return targets;
}
bool EchoFarm::teleportEnabled() const {
    return rust::workflowRun(rust::WorkflowMethod::EchoTeleportEnabled, {},
                             [&](rust::WorkflowOp op, const double *a, double *out) {
                                 switch (op) {
                                 case rust::WorkflowOp::EchoConfiguredTeleport: {
                                     out[0] = config_["Teleport to Boss"].toString("No") != "No";
                                     break;
                                 }
                                 default:
                                     fail("Rust 声骸刷取宿主无效");
                                 }
                             })[0] != 0;
}
void EchoFarm::parameters() {
    rust::workflowRun(
        rust::WorkflowMethod::EchoParameters, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::EchoConfiguredBoss: {
                out[0] = QStringList{"Sentry Construct",  "Lioness of Glory", "Fallacy of No Return",
                                     "Hyvatia",           "Lady of the Sea",  "Fenrico",
                                     "Nameless Explorer", "Nightmare: Hecate"}
                             .indexOf(config_["Boss"].toString("Other"));
                break;
            }
            case rust::WorkflowOp::ReadEchoCombatWait: {
                wait_ = config_["Combat Wait Time"].toDouble();
                break;
            }
            case rust::WorkflowOp::EchoCombatWait: {
                out[0] = wait_;
                break;
            }
            case rust::WorkflowOp::StoreEchoCombatWait: {
                wait_ = a[0];
                break;
            }
            case rust::WorkflowOp::StoreEchoRealm: {
                realm_ = a[0] != 0;
                break;
            }
            case rust::WorkflowOp::StoreEchoBypassWait: {
                bypassWait_ = a[0] != 0;
                break;
            }
            case rust::WorkflowOp::StoreEchoWorldMode: {
                treatAsWorld_ = a[0] != 0;
                break;
            }
            case rust::WorkflowOp::EchoRealm: {
                out[0] = realm_;
                break;
            }
            case rust::WorkflowOp::StoreEchoTargetTimeout: {
                combat_.targetEnemyTimeout = a[0];
                break;
            }
            default:
                fail("Rust 声骸刷取宿主无效");
            }
        });
}
void EchoFarm::checkRealm(double seconds) {
    const std::array args{seconds};
    rust::workflowRun(rust::WorkflowMethod::EchoCheckRealm, args,
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
                          case rust::WorkflowOp::InvalidCondition: {
                              fail("等待条件参数无效");
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
                          case rust::WorkflowOp::EchoRealm: {
                              out[0] = realm_;
                              break;
                          }
                          case rust::WorkflowOp::EchoWorldMode: {
                              out[0] = treatAsWorld_;
                              break;
                          }
                          case rust::WorkflowOp::EchoStartedAt: {
                              out[0] = started_;
                              break;
                          }
                          case rust::WorkflowOp::ReadEchoRealm: {
                              realm_ = navigation_.inRealm();
                              break;
                          }
                          case rust::WorkflowOp::StoreEchoTargetTimeout: {
                              combat_.targetEnemyTimeout = a[0];
                              break;
                          }
                          default:
                              fail("Rust 声骸刷取宿主无效");
                          }
                      });
}
void EchoFarm::bossLevel() {
    QString level;
    std::optional<TextLine> target;
    rust::workflowRun(
        rust::WorkflowMethod::EchoBossLevel, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
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
            case rust::WorkflowOp::InvalidCondition: {
                fail("等待条件参数无效");
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
            case rust::WorkflowOp::EchoFarmFailure: {
                switch (int(a[0])) {
                case 2:
                    fail("Boss 等级无效");
                case 3:
                    throw NavigationFailure("未找到所选 Boss 等级");
                default:
                    fail("声骸刷取失败编号无效");
                }
                break;
            }
            case rust::WorkflowOp::ReadEchoBossLevel: {
                level = config_["Boss Level"].toString("80");
                break;
            }
            case rust::WorkflowOp::EchoBossLevelValid: {
                out[0] = QStringList{"50", "60", "70", "80", "90"}.contains(level);
                break;
            }
            case rust::WorkflowOp::FindEchoBossLevel: {
                target = matchingText(c_.text(.030, .113, .323, .660), QRegularExpression::escape(level));
                out[0] = target.has_value();
                break;
            }
            case rust::WorkflowOp::ClickEchoBossLevel: {
                c_.click(*target);
                break;
            }
            default:
                fail("Rust 声骸刷取宿主无效");
            }
        });
}
void EchoFarm::enterFromF() {
    rust::workflowRun(rust::WorkflowMethod::EchoEnterF, {},
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
                          case rust::WorkflowOp::InvalidCondition: {
                              fail("等待条件参数无效");
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
                          case rust::WorkflowOp::EchoFarmFailure: {
                              switch (int(a[0])) {
                              case 4:
                                  throw NavigationFailure("进本前未找到 F 提示");
                              default:
                                  fail("声骸刷取失败编号无效");
                              }
                              break;
                          }
                          case rust::WorkflowOp::EchoHasInteract: {
                              out[0] = bool(findInteract(c_));
                              break;
                          }
                          case rust::WorkflowOp::EchoKeyF: {
                              c_.key("f", a[0]);
                              break;
                          }
                          case rust::WorkflowOp::SelectEchoBossLevel: {
                              bossLevel();
                              break;
                          }
                          case rust::WorkflowOp::EchoSkipConfirm: {
                              out[0] = c_.skipConfirm();
                              break;
                          }
                          case rust::WorkflowOp::EchoWaitTeam: {
                              navigation_.waitTeam(a[0]);
                              break;
                          }
                          case rust::WorkflowOp::StoreEchoRealm: {
                              realm_ = a[0] != 0;
                              break;
                          }
                          case rust::WorkflowOp::StoreEchoWorldMode: {
                              treatAsWorld_ = a[0] != 0;
                              break;
                          }
                          case rust::WorkflowOp::StoreEchoTreasure: {
                              treasure_ = a[0] != 0;
                              break;
                          }
                          case rust::WorkflowOp::StoreEchoJustEntered: {
                              justEntered_ = a[0] != 0;
                              break;
                          }
                          case rust::WorkflowOp::StoreEchoTargetTimeout: {
                              combat_.targetEnemyTimeout = a[0];
                              break;
                          }
                          default:
                              fail("Rust 声骸刷取宿主无效");
                          }
                      });
}
double EchoFarm::rustTravel(int method) {
    std::optional<CombatOptions> combatOptions;
    std::optional<Match> materialClaim;
    double last = -1;
    QString category;
    QRect area;
    std::optional<Match> marker, next;
    QVector<TextLine> lines;
    QRegularExpressionMatch match;
    const QVector<QPair<QString, QString>> bossNames{{"伪作的神王", "伪作的神王"},
                                                     {"异构武装", "异构武装|加尔古耶"},
                                                     {"荣耀狮像", "狮像|亚狮诺索"},
                                                     {"罗蕾莱", "罗蕾莱|夜之女皇"}};
    std::function<void(rust::WorkflowOp, const double *, double *)> action;
    action = [&](rust::WorkflowOp op, const double *a, double *out) {
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
        case rust::WorkflowOp::InvalidCondition: {
            fail("等待条件参数无效");
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
        case rust::WorkflowOp::ClearEchoWalkResult: {
            walkResult_.clear();
            break;
        }
        case rust::WorkflowOp::NavEnsureMain: {
            navigation_.ensureMain(a[0]);
            break;
        }
        case rust::WorkflowOp::EchoTeleportCategory: {
            category = config_["Teleport to Boss"].toString();
            out[0] = QStringList{"Weekly Challenge", "Boss Challenge", "Special Nightmare"}.indexOf(category);
            break;
        }
        case rust::WorkflowOp::EchoWeeklySerial: {
            out[0] = config_["Which Weekly Boss to Teleport"].toInt(1);
            break;
        }
        case rust::WorkflowOp::EchoBossSerial: {
            out[0] = config_["Which Boss Challenge to Teleport"].toInt(1);
            break;
        }
        case rust::WorkflowOp::EchoSpecialSerial: {
            out[0] = config_["Which Special Nightmare to Teleport"].toInt(1);
            break;
        }
        case rust::WorkflowOp::OpenEchoBossBook: {
            navigation_.openBook("gray_book_boss");
            break;
        }
        case rust::WorkflowOp::SelectEchoBookCategory: {
            navigation_.bookCategory(QStringList{"zhange", "qiangdi", "mengyan"}.at(int(a[0])));
            break;
        }
        case rust::WorkflowOp::SelectEchoBookEntry: {
            out[0] = navigation_.selectBookEntry(int(a[0]), int(a[1]),
                                                 a[2] != 0 ? QVector<int>{5, 10} : QVector<int>{});
            break;
        }
        case rust::WorkflowOp::SelectEchoBossLevel: {
            bossLevel();
            break;
        }
        case rust::WorkflowOp::EchoSkipConfirm: {
            out[0] = c_.skipConfirm();
            break;
        }
        case rust::WorkflowOp::StartEchoChallenge: {
            navigation_.startChallenge();
            break;
        }
        case rust::WorkflowOp::StoreEchoJustEntered: {
            justEntered_ = a[0] != 0;
            break;
        }
        case rust::WorkflowOp::StoreEchoEntryAtHeal: {
            entryAtHeal_ = a[0] != 0;
            break;
        }
        case rust::WorkflowOp::WaitEchoTravel: {
            navigation_.waitTravel();
            break;
        }
        case rust::WorkflowOp::EchoWaitTeam: {
            navigation_.waitTeam(a[0]);
            break;
        }
        case rust::WorkflowOp::StoreEchoWalkResult: {
            walkResult_ = a[0] == 0 ? "realm" : "combat";
            break;
        }
        case rust::WorkflowOp::EchoCombat: {
            out[0] = combat_.active();
            break;
        }
        case rust::WorkflowOp::EchoHasInteract: {
            out[0] = bool(findInteract(c_));
            break;
        }
        case rust::WorkflowOp::EchoMiddleClick: {
            c_.input.click({}, a[0], "middle");
            break;
        }
        case rust::WorkflowOp::EchoLastCameraAt: {
            out[0] = last;
            break;
        }
        case rust::WorkflowOp::StoreEchoLastCameraAt: {
            last = a[0];
            break;
        }
        case rust::WorkflowOp::WalkEchoTeleportEntry: {
            out[0] = world_.walkUntil(
                [&] { return rust::workflowRun(rust::WorkflowMethod::EchoTeleportSeek, {}, action)[0] != 0; },
                "w", 20, true);
            break;
        }
        case rust::WorkflowOp::EnterEchoFromF: {
            enterFromF();
            break;
        }
        case rust::WorkflowOp::EchoWalkResultRealm: {
            out[0] = walkResult_ == "realm";
            break;
        }
        case rust::WorkflowOp::StoreEchoRealm: {
            realm_ = a[0] != 0;
            break;
        }
        case rust::WorkflowOp::EchoRealm: {
            out[0] = realm_;
            break;
        }
        case rust::WorkflowOp::StoreEchoWorldMode: {
            treatAsWorld_ = a[0] != 0;
            break;
        }
        case rust::WorkflowOp::StoreEchoTreasure: {
            treasure_ = a[0] != 0;
            break;
        }
        case rust::WorkflowOp::StoreEchoTargetTimeout: {
            combat_.targetEnemyTimeout = a[0];
            break;
        }
        case rust::WorkflowOp::EchoAimBossEmpty: {
            out[0] = aimBoss_.isEmpty();
            break;
        }
        case rust::WorkflowOp::OpenEchoDefaultBook: {
            navigation_.openBook();
            break;
        }
        case rust::WorkflowOp::SelectEchoAimMonster: {
            navigation_.selectMonster(aimBoss_);
            break;
        }
        case rust::WorkflowOp::OpenEchoMap: {
            c_.key("m", 2);
            break;
        }
        case rust::WorkflowOp::EchoNamelessBoss: {
            out[0] = config_["Boss"].toString() == "Nameless Explorer";
            break;
        }
        case rust::WorkflowOp::ReadEchoMapArea: {
            area = gameBox(c_.frame, .3, .3, .7, .7);
            break;
        }
        case rust::WorkflowOp::FindEchoCheckedMarker: {
            marker = c_.find("boss_check_mark", {.search = area});
            break;
        }
        case rust::WorkflowOp::HasEchoMapMarker: {
            out[0] = marker.has_value();
            break;
        }
        case rust::WorkflowOp::FindEchoUncheckedMarker: {
            next = c_.find("boss_no_check_mark", {.threshold = .7, .search = area, .rotation = a[0]});
            break;
        }
        case rust::WorkflowOp::HasEchoNextMarker: {
            out[0] = next.has_value();
            break;
        }
        case rust::WorkflowOp::EchoNextMarkerConfidence: {
            out[0] = next->confidence;
            break;
        }
        case rust::WorkflowOp::EchoMarkerConfidence: {
            out[0] = marker->confidence;
            break;
        }
        case rust::WorkflowOp::StoreEchoMapMarker: {
            marker = next;
            break;
        }
        case rust::WorkflowOp::ClickEchoMapMarker: {
            c_.click(*marker);
            break;
        }
        case rust::WorkflowOp::EchoChineseGame: {
            out[0] = c_.gameLanguage == "zh_CN";
            break;
        }
        case rust::WorkflowOp::ReadEchoBossNames: {
            const auto box = Templates::anchored({1269, 10, 1264, 130}, {3840, 2160},
                                                 {c_.frame.cols, c_.frame.rows}, true);
            lines = c_.text(box);
            break;
        }
        case rust::WorkflowOp::EchoBossNameMatches: {
            out[0] = bool(matchingText(lines, bossNames.at(int(a[0])).second));
            break;
        }
        case rust::WorkflowOp::StoreEchoAimBoss: {
            aimBoss_ = bossNames.at(int(a[0])).first;
            break;
        }
        case rust::WorkflowOp::EchoTeleportEnabled: {
            out[0] = teleportEnabled();
            break;
        }
        case rust::WorkflowOp::ExitEchoRealm: {
            navigation_.exitRealm(a[0]);
            break;
        }
        case rust::WorkflowOp::EchoEntryAtHeal: {
            out[0] = entryAtHeal_;
            break;
        }
        case rust::WorkflowOp::HealEchoBeacon: {
            navigation_.healAtBeacon();
            break;
        }
        case rust::WorkflowOp::WalkEchoBackFromBeacon: {
            world_.walkUntil([] { return false; }, "s", 1, true);
            break;
        }
        case rust::WorkflowOp::FindEchoNearestBoss: {
            nearestBoss();
            break;
        }
        case rust::WorkflowOp::SeekEchoCombat: {
            out[0] = seekCombat(a[0], a[1] != 0, a[2] != 0);
            break;
        }
        case rust::WorkflowOp::HuntEchoTreasure: {
            treasureHunt();
            break;
        }
        case rust::WorkflowOp::EnsureEchoMainDefault: {
            navigation_.ensureMain();
            break;
        }
        case rust::WorkflowOp::ReadEchoWeeklyCountLines: {
            lines = c_.text(.36, .12, .96, .18);
            break;
        }
        case rust::WorkflowOp::EchoWeeklyCountLines: {
            out[0] = double(lines.size());
            break;
        }
        case rust::WorkflowOp::EchoWeeklyCountMatch: {
            match = QRegularExpression("(\\d)\\s*/\\s*(\\d)").match(lines.at(int(a[0])).text);
            out[0] = match.hasMatch();
            break;
        }
        case rust::WorkflowOp::EchoWeeklyCountValue: {
            out[0] = match.captured(1).toInt();
            break;
        }
        case rust::WorkflowOp::ReadEchoCurrentStamina: {
            out[0] = readStamina(c_).current;
            break;
        }
        case rust::WorkflowOp::EchoTravelFailure: {
            switch (int(a[0])) {
            case 0:
                fail("传送 Boss 分类无效");
            case 1:
                throw NavigationFailure("传送后未能接近战斗或副本入口");
            case 2:
                throw NavigationFailure("地图中未找到当前 Boss 标记");
            case 3:
                throw CharacterDead();
            case 4:
                fail("未确认周本体力，停止材料刷取");
            default:
                fail("声骸传送失败编号无效");
            }
            break;
        }
        case rust::WorkflowOp::EchoInteractionBoss: {
            out[0] = QStringList{"Fallacy of No Return", "Fenrico", "Lady of the Sea", "Nameless Explorer",
                                 "Lorelei"}
                         .indexOf(config_["Boss"].toString("Other"));
            break;
        }
        case rust::WorkflowOp::EchoLastNight: {
            out[0] = lastNight_;
            break;
        }
        case rust::WorkflowOp::ChangeEchoNight: {
            changeNight();
            break;
        }
        case rust::WorkflowOp::EchoTapKey: {
            c_.key(QStringList{"w", "a", "s", "d"}.at(int(a[0])), a[1], a[2]);
            break;
        }
        case rust::WorkflowOp::WalkEchoFallacyInteract: {
            out[0] = world_.walkToInteract("w", 20, true, 0, {}, true, true);
            break;
        }
        case rust::WorkflowOp::ScrollEchoInteractions: {
            scrollInteract();
            break;
        }
        case rust::WorkflowOp::EchoPickEchoOnly: {
            out[0] = world_.pick(true, true);
            break;
        }
        case rust::WorkflowOp::IncrementEchoDrops: {
            ++drops_;
            break;
        }
        case rust::WorkflowOp::EchoInWorld: {
            out[0] = navigation_.inWorld();
            break;
        }
        case rust::WorkflowOp::EchoPickDefault: {
            out[0] = world_.pick();
            break;
        }
        case rust::WorkflowOp::EchoTreasureFound: {
            out[0] = bool(world_.treasure());
            break;
        }
        case rust::WorkflowOp::EchoTreasureRestart: {
            out[0] = treasureRestart();
            break;
        }
        case rust::WorkflowOp::EchoTreasure: {
            out[0] = treasure_;
            break;
        }
        case rust::WorkflowOp::FollowEchoBossMinimap: {
            out[0] = world_.followBossMinimap(a[0]);
            break;
        }
        case rust::WorkflowOp::EchoEnhanced: {
            out[0] = enhanced_;
            break;
        }
        case rust::WorkflowOp::EchoMaterialMode: {
            out[0] = config_["Advanced Skill Material Mode"].toBool();
            break;
        }
        case rust::WorkflowOp::EchoRepeatCount: {
            out[0] = config_["Repeat Farm Count"].toInt();
            break;
        }
        case rust::WorkflowOp::EchoWeeklyCategory: {
            out[0] = config_["Teleport to Boss"] == "Weekly Challenge";
            break;
        }
        case rust::WorkflowOp::ReadEchoWeeklyMaterialCount: {
            out[0] = weeklyMaterialCount();
            break;
        }
        case rust::WorkflowOp::ReadEchoRealm: {
            realm_ = navigation_.inRealm();
            break;
        }
        case rust::WorkflowOp::ApplyEchoParameters: {
            parameters();
            break;
        }
        case rust::WorkflowOp::StoreEchoStartedAt: {
            started_ = a[0];
            break;
        }
        case rust::WorkflowOp::TeleportEcho: {
            teleport();
            break;
        }
        case rust::WorkflowOp::CheckEchoRealm: {
            checkRealm(a[0]);
            break;
        }
        case rust::WorkflowOp::RunEchoInteractions: {
            interactions();
            break;
        }
        case rust::WorkflowOp::EchoJustEntered: {
            out[0] = justEntered_;
            break;
        }
        case rust::WorkflowOp::RestartEchoDomain: {
            restartBossDomain(c_);
            break;
        }
        case rust::WorkflowOp::FollowEchoBossDefault: {
            out[0] = world_.followBossMinimap();
            break;
        }
        case rust::WorkflowOp::EchoCombatWait: {
            out[0] = wait_;
            break;
        }
        case rust::WorkflowOp::RecognizeEchoBoss: {
            recognizeBoss();
            break;
        }
        case rust::WorkflowOp::CreateEchoCombatOptions: {
            combatOptions.emplace();
            break;
        }
        case rust::WorkflowOp::SetEchoCombatWait: {
            combatOptions->waitSeconds = a[0];
            break;
        }
        case rust::WorkflowOp::SetEchoCombatRequired: {
            combatOptions->required = a[0] != 0;
            break;
        }
        case rust::WorkflowOp::EchoUseLiberation: {
            out[0] = config_["Use Liberation"].toBool(true);
            break;
        }
        case rust::WorkflowOp::SetEchoCombatLiberation: {
            combatOptions->useLiberation = a[0] != 0;
            break;
        }
        case rust::WorkflowOp::SetEchoCombatRecovery: {
            combatOptions->recover = [&] { heal(); };
            break;
        }
        case rust::WorkflowOp::RunEchoCombatOnce: {
            combat_.combatOnce(*combatOptions);
            break;
        }
        case rust::WorkflowOp::EchoPickupMethod: {
            out[0] =
                QStringList{"Yolo", "Run in Circle"}.indexOf(config_["Echo Pickup Method"].toString("Yolo"));
            break;
        }
        case rust::WorkflowOp::FindEchoDrop: {
            out[0] = world_.findEcho(false, a[0] != 0, a[1], a[2]).first;
            break;
        }
        case rust::WorkflowOp::CircleEchoDrop: {
            out[0] = world_.circleEcho(int(a[0]));
            break;
        }
        case rust::WorkflowOp::WalkEchoDrop: {
            out[0] = world_.walkToInteract("w", 3, false, 1, "吸收|Absorb", true);
            break;
        }
        case rust::WorkflowOp::WalkEchoMaterialTreasure: {
            world_.walkToTreasure();
            break;
        }
        case rust::WorkflowOp::PickEchoMaterialTreasure: {
            world_.pick(false);
            break;
        }
        case rust::WorkflowOp::ReadEchoMaterialClaim: {
            materialClaim = c_.find("gray_confirm_exit_button", {.threshold = .7});
            out[0] = materialClaim.has_value();
            break;
        }
        case rust::WorkflowOp::EchoMaterialClaimPoint: {
            out[0] = materialClaim->box.x();
            out[1] = materialClaim->box.width();
            out[2] = materialClaim->center().y();
            break;
        }
        case rust::WorkflowOp::ClickEchoMaterialPoint: {
            c_.click(QPointF(a[0], a[1]));
            break;
        }
        case rust::WorkflowOp::WaitEchoMaterialReward: {
            awaitWeeklyMaterialReward(c_);
            break;
        }
        case rust::WorkflowOp::IncrementEchoTotalCompleted: {
            ++totalCompleted_;
            break;
        }
        case rust::WorkflowOp::HasInfo: {
            out[0] = bool(c_.info);
            break;
        }
        case rust::WorkflowOp::EchoSingleCountsInfo: {
            c_.info("本副本轮数", QString("%1/%2").arg(int(a[0])).arg(int(a[1])));
            c_.info("声骸数量", QString::number(drops_));
            break;
        }
        case rust::WorkflowOp::EchoBypassWait: {
            out[0] = bypassWait_;
            break;
        }
        case rust::WorkflowOp::EchoWalkResultCombat: {
            out[0] = walkResult_ == "combat";
            break;
        }
        case rust::WorkflowOp::EchoCombatFailure: {
            switch (int(a[0])) {
            case 0:
                fail("刷取次数超出上限");
            case 1:
                fail("未确认周本材料领奖按钮，不重复消耗");
            default:
                fail("单轮刷取失败编号无效");
            }
            break;
        }
        default:
            fail("Rust 声骸传送宿主无效");
        }
    };
    return rust::workflowRun(rust::WorkflowMethod(method), {}, action)[0];
}
void EchoFarm::teleport() { rustTravel(int(rust::WorkflowMethod::EchoTeleport)); }
void EchoFarm::nearestBoss() { rustTravel(int(rust::WorkflowMethod::EchoNearestBoss)); }
bool EchoFarm::seekCombat(double timeout, bool move, bool run) {
    double last = -1;
    std::function<void(rust::WorkflowOp, const double *, double *)> action;
    action = [&](rust::WorkflowOp op, const double *a, double *out) {
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
        case rust::WorkflowOp::InvalidCondition: {
            fail("等待条件参数无效");
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
        case rust::WorkflowOp::EchoCombat: {
            out[0] = combat_.active();
            break;
        }
        case rust::WorkflowOp::EchoTreasureFound: {
            out[0] = bool(world_.treasure());
            break;
        }
        case rust::WorkflowOp::EchoLastCameraAt: {
            out[0] = last;
            break;
        }
        case rust::WorkflowOp::StoreEchoLastCameraAt: {
            last = a[0];
            break;
        }
        case rust::WorkflowOp::EchoMiddleClick: {
            c_.input.click({}, a[0], "middle");
            break;
        }
        case rust::WorkflowOp::WalkEchoSeekCombat: {
            out[0] = world_.walkUntil(
                [&] {
                    const std::array conditionArgs{double(move)};
                    return rust::workflowRun(rust::WorkflowMethod::EchoSeekCondition, conditionArgs,
                                             action)[0] != 0;
                },
                "w", a[0], a[1] != 0);
            break;
        }
        default:
            fail("Rust 接近战斗宿主无效");
        }
    };
    const std::array args{timeout, double(move), double(run)};
    return rust::workflowRun(rust::WorkflowMethod::EchoSeekCombat, args, action)[0] != 0;
}
void EchoFarm::scrollInteract() {
    rust::workflowRun(rust::WorkflowMethod::EchoScrollInteract, {},
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
                          case rust::WorkflowOp::InvalidCondition: {
                              fail("等待条件参数无效");
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
                          case rust::WorkflowOp::EchoTreasure: {
                              out[0] = treasure_;
                              break;
                          }
                          case rust::WorkflowOp::EchoHasInteract: {
                              out[0] = bool(findInteract(c_));
                              break;
                          }
                          case rust::WorkflowOp::EchoCombat: {
                              out[0] = combat_.active();
                              break;
                          }
                          case rust::WorkflowOp::ScrollEchoInteract: {
                              c_.input.scroll(1, QPointF(c_.frame.cols * .5, c_.frame.rows * .5));
                              break;
                          }
                          case rust::WorkflowOp::EchoKeyFDefault: {
                              c_.key("f");
                              break;
                          }
                          case rust::WorkflowOp::EchoDismissClaim: {
                              out[0] = world_.dismissClaim();
                              break;
                          }
                          case rust::WorkflowOp::StoreEchoTreasure: {
                              treasure_ = a[0] != 0;
                              break;
                          }
                          default:
                              fail("Rust 声骸刷取宿主无效");
                          }
                      });
}
bool EchoFarm::treasureRestart() {
    return rust::workflowRun(rust::WorkflowMethod::EchoTreasureRestart, {},
                             [&](rust::WorkflowOp op, const double *a, double *out) {
                                 switch (op) {
                                 case rust::WorkflowOp::EchoTreasureFound: {
                                     out[0] = bool(world_.treasure());
                                     break;
                                 }
                                 case rust::WorkflowOp::WalkEchoTreasureRestart: {
                                     out[0] =
                                         world_.walkToBox([&] { return world_.treasure(); },
                                                          [&] { return bool(findInteract(c_)); }, 30, .1);
                                     break;
                                 }
                                 default:
                                     fail("Rust 声骸刷取宿主无效");
                                 }
                             })[0] != 0;
}
void EchoFarm::treasureHunt() {
    rust::workflowRun(rust::WorkflowMethod::EchoTreasureHunt, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::EchoCombat: {
                              out[0] = combat_.active();
                              break;
                          }
                          case rust::WorkflowOp::EchoTreasureRestart: {
                              out[0] = treasureRestart();
                              break;
                          }
                          case rust::WorkflowOp::StoreEchoTreasure: {
                              treasure_ = a[0] != 0;
                              break;
                          }
                          case rust::WorkflowOp::EchoTeleportEnabled: {
                              out[0] = teleportEnabled();
                              break;
                          }
                          case rust::WorkflowOp::EchoRealm: {
                              out[0] = realm_;
                              break;
                          }
                          case rust::WorkflowOp::EnterEchoFromF: {
                              enterFromF();
                              break;
                          }
                          case rust::WorkflowOp::ScrollEchoInteractions: {
                              scrollInteract();
                              break;
                          }
                          default:
                              fail("Rust 声骸刷取宿主无效");
                          }
                      });
}
void EchoFarm::changeNight() {
    rust::workflowRun(rust::WorkflowMethod::EchoChangeNight, {},
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
                          case rust::WorkflowOp::InvalidCondition: {
                              fail("等待条件参数无效");
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
                          case rust::WorkflowOp::EchoEscape: {
                              c_.key("esc", a[0]);
                              break;
                          }
                          case rust::WorkflowOp::StoreEchoLastNight: {
                              lastNight_ = a[0];
                              break;
                          }
                          default:
                              fail("Rust 声骸刷取宿主无效");
                          }
                      });
}
void EchoFarm::interactions() { rustTravel(int(rust::WorkflowMethod::EchoInteractions)); }
void EchoFarm::recognizeBoss() { rustTravel(int(rust::WorkflowMethod::EchoRecognizeBoss)); }
void EchoFarm::heal() { rustTravel(int(rust::WorkflowMethod::EchoHeal)); }
int EchoFarm::weeklyMaterialCount() { return int(rustTravel(int(rust::WorkflowMethod::EchoWeeklyCount))); }
int EchoFarm::single() { return int(rustTravel(int(rust::WorkflowMethod::EchoSingle))); }
int EchoFarm::run(bool enhanced, const QJsonObject &supplied) {
    std::optional<TaskScope> scope;
    QVector<EchoTarget> targets;
    EchoTarget target;
    std::function<bool()> oldCheck, oldEnd;
    double oldTimeout = 0;
    std::function<void(rust::WorkflowOp, const double *, double *)> action;
    action = [&](rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::StoreEchoEnhanced: {
            enhanced_ = a[0] != 0;
            break;
        }
        case rust::WorkflowOp::EchoSuppliedConfigEmpty: {
            out[0] = supplied.isEmpty();
            break;
        }
        case rust::WorkflowOp::LoadEchoProfileConfig: {
            config_ = c_.profile.group(a[0] != 0 ? "FarmEchoTask2" : "FarmEchoTask");
            break;
        }
        case rust::WorkflowOp::CopyEchoSuppliedConfig: {
            config_ = supplied;
            break;
        }
        case rust::WorkflowOp::CreateEchoTaskScope: {
            scope.emplace(c_, a[0] != 0 ? "FarmEchoTask2" : "FarmEchoTask", config_);
            break;
        }
        case rust::WorkflowOp::ResetEchoRunCounters: {
            totalCompleted_ = drops_ = 0;
            break;
        }
        case rust::WorkflowOp::EchoRotationMode: {
            out[0] = config_["Farm Mode"].toString("Single Boss") == "Cycle All Dungeons";
            break;
        }
        case rust::WorkflowOp::ReadEchoRunCount: {
            const int total = config_["Repeat Farm Count"].toInt();
            const auto value = config_["Repeat Farm Count"];
            out[0] = total;
            out[1] = value.isDouble();
            out[2] = value.toDouble();
            break;
        }
        case rust::WorkflowOp::EchoEnhanced: {
            out[0] = enhanced_;
            break;
        }
        case rust::WorkflowOp::EchoMaterialMode: {
            out[0] = config_["Advanced Skill Material Mode"].toBool();
            break;
        }
        case rust::WorkflowOp::ReadEchoRotationRounds: {
            const auto rounds = config_["Rounds per Dungeon"];
            out[0] = rounds.toInt(1);
            out[1] = rounds.isDouble();
            out[2] = rounds.toDouble();
            break;
        }
        case rust::WorkflowOp::ReadEchoRotationTargets: {
            targets = echoRotationTargets(config_);
            break;
        }
        case rust::WorkflowOp::CaptureEchoCallbacks: {
            oldCheck = combat_.onCombatCheck;
            oldEnd = combat_.combatEndCondition;
            oldTimeout = combat_.targetEnemyTimeout;
            break;
        }
        case rust::WorkflowOp::InstallEchoCombatCheck: {
            combat_.onCombatCheck = [&] {
                return rust::workflowRun(rust::WorkflowMethod::EchoCombatCheck, {}, action)[0] != 0;
            };
            break;
        }
        case rust::WorkflowOp::InstallEchoCombatEnd: {
            combat_.combatEndCondition = [&] {
                return rust::workflowRun(rust::WorkflowMethod::EchoCombatEnd, {}, action)[0] != 0;
            };
            break;
        }
        case rust::WorkflowOp::RecoverEchoStartingPopup: {
            combat_.recoverStartingPopup();
            break;
        }
        case rust::WorkflowOp::Sleep: {
            c_.sleep(a[0]);
            break;
        }
        case rust::WorkflowOp::RunEchoSingle: {
            out[0] = single();
            break;
        }
        case rust::WorkflowOp::EchoTotalCompleted: {
            out[0] = totalCompleted_;
            break;
        }
        case rust::WorkflowOp::EchoRotationTargetCount: {
            out[0] = double(targets.size());
            break;
        }
        case rust::WorkflowOp::SelectEchoRotationTarget: {
            target = targets.at(int(a[0]));
            break;
        }
        case rust::WorkflowOp::ApplyEchoRotationTarget: {
            config_["Teleport to Boss"] = target.category;
            config_["Repeat Farm Count"] = int(a[0]);
            config_["Which Weekly Boss to Teleport"] = target.index;
            config_["Which Boss Challenge to Teleport"] = target.index;
            config_["Boss"] = "Other";
            c_.taskOverrides = config_;
            aimBoss_.clear();
            treatAsWorld_ = false;
            justEntered_ = false;
            break;
        }
        case rust::WorkflowOp::HasInfo: {
            out[0] = bool(c_.info);
            break;
        }
        case rust::WorkflowOp::EchoRotationInfo: {
            c_.info("循环副本",
                    QString("%1 · %2；本副本 %3 轮").arg(target.category).arg(target.index).arg(int(a[0])));
            break;
        }
        case rust::WorkflowOp::RestoreEchoCombatCheck: {
            combat_.onCombatCheck = oldCheck;
            break;
        }
        case rust::WorkflowOp::RestoreEchoCombatEnd: {
            combat_.combatEndCondition = oldEnd;
            break;
        }
        case rust::WorkflowOp::RestoreEchoTargetTimeout: {
            combat_.targetEnemyTimeout = oldTimeout;
            break;
        }
        case rust::WorkflowOp::EchoRealm: {
            out[0] = realm_;
            break;
        }
        case rust::WorkflowOp::EchoPickDefault: {
            out[0] = world_.pick();
            break;
        }
        case rust::WorkflowOp::IncrementEchoDrops: {
            ++drops_;
            break;
        }
        case rust::WorkflowOp::CheckEchoRealm: {
            checkRealm(a[0]);
            break;
        }
        case rust::WorkflowOp::EchoHasHealthBar: {
            out[0] = combat_.hasHealthBar();
            break;
        }
        case rust::WorkflowOp::EchoDetectedDrops: {
            out[0] = !world_.echoes().empty();
            break;
        }
        case rust::WorkflowOp::EchoRunFailure: {
            switch (int(a[0])) {
            case 0:
                fail("刷取次数必须为 0 到 100000 的整数");
            case 1:
                fail("循环刷声骸时请关闭技能进阶材料模式");
            case 2:
                fail("循环刷取次数无效");
            case 3:
                throw NavigationFailure("当前副本轮数未完成，不跳到下一副本");
            default:
                fail("循环刷取失败编号无效");
            }
            break;
        }
        default:
            fail("Rust 循环刷取宿主无效");
        }
    };
    const std::array args{double(enhanced)};
    return int(rust::workflowRun(rust::WorkflowMethod::EchoRun, args, action)[0]);
}
} // namespace okww::runtime
