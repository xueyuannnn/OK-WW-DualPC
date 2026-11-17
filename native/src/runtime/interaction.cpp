#include "interaction.hpp"
#include "workflow_abi.hpp"
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
        rust::workflowRun(rust::WorkflowMethod::WorldMoveDrop, {},
                          [&](rust::WorkflowOp op, const double *a, double *out) {
                              switch (op) {
                              case rust::WorkflowOp::WorldOldDirectionEmpty: {
                                  out[0] = key_.isEmpty();
                                  break;
                              }
                              case rust::WorkflowOp::WorldMovementRunning: {
                                  out[0] = running_;
                                  break;
                              }
                              case rust::WorkflowOp::WorldReleaseInput: {
                                  c_.input.release();
                                  break;
                              }
                              default:
                                  fail("Rust 世界交互宿主无效");
                              }
                          });
    }
    void direction(const QString &key) {
        rust::workflowRun(rust::WorkflowMethod::WorldMoveDirection, {},
                          [&](rust::WorkflowOp op, const double *a, double *out) {
                              switch (op) {
                              case rust::WorkflowOp::WorldSameDirection: {
                                  out[0] = key == key_;
                                  break;
                              }
                              case rust::WorkflowOp::WorldOldDirectionEmpty: {
                                  out[0] = key_.isEmpty();
                                  break;
                              }
                              case rust::WorkflowOp::WorldOldDirectionUp: {
                                  c_.input.keyUp(key_);
                                  break;
                              }
                              case rust::WorkflowOp::WorldClearOldDirection: {
                                  key_.clear();
                                  break;
                              }
                              case rust::WorkflowOp::Sleep: {
                                  c_.sleep(a[0]);
                                  break;
                              }
                              case rust::WorkflowOp::WorldNewDirectionEmpty: {
                                  out[0] = key.isEmpty();
                                  break;
                              }
                              case rust::WorkflowOp::WorldNewDirectionDown: {
                                  c_.input.keyDown(key);
                                  break;
                              }
                              case rust::WorkflowOp::WorldStoreDirection: {
                                  key_ = key;
                                  break;
                              }
                              default:
                                  fail("Rust 世界交互宿主无效");
                              }
                          });
    }
    void run() {
        rust::workflowRun(rust::WorkflowMethod::WorldMoveRun, {},
                          [&](rust::WorkflowOp op, const double *a, double *out) {
                              switch (op) {
                              case rust::WorkflowOp::Sleep: {
                                  c_.sleep(a[0]);
                                  break;
                              }
                              case rust::WorkflowOp::WorldRightDown: {
                                  c_.input.buttonDown("right");
                                  break;
                              }
                              case rust::WorkflowOp::WorldStoreRunning: {
                                  running_ = a[0] != 0;
                                  break;
                              }
                              default:
                                  fail("Rust 世界交互宿主无效");
                              }
                          });
    }
    void stopRunning() {
        rust::workflowRun(rust::WorkflowMethod::WorldMoveStopRunning, {},
                          [&](rust::WorkflowOp op, const double *a, double *out) {
                              switch (op) {
                              case rust::WorkflowOp::WorldMovementRunning: {
                                  out[0] = running_;
                                  break;
                              }
                              case rust::WorkflowOp::WorldRightUp: {
                                  c_.input.buttonUp("right");
                                  break;
                              }
                              case rust::WorkflowOp::WorldStoreRunning: {
                                  running_ = a[0] != 0;
                                  break;
                              }
                              default:
                                  fail("Rust 世界交互宿主无效");
                              }
                          });
    }
    void stop() {
        rust::workflowRun(rust::WorkflowMethod::WorldMoveStop, {},
                          [&](rust::WorkflowOp op, const double *a, double *out) {
                              switch (op) {
                              case rust::WorkflowOp::WorldMovementClearDirection: {
                                  direction({});
                                  break;
                              }
                              case rust::WorkflowOp::WorldMovementRunning: {
                                  out[0] = running_;
                                  break;
                              }
                              case rust::WorkflowOp::Sleep: {
                                  c_.sleep(a[0]);
                                  break;
                              }
                              case rust::WorkflowOp::WorldRightUp: {
                                  c_.input.buttonUp("right");
                                  break;
                              }
                              case rust::WorkflowOp::WorldStoreRunning: {
                                  running_ = a[0] != 0;
                                  break;
                              }
                              default:
                                  fail("Rust 世界交互宿主无效");
                              }
                          });
    }
};
} // namespace
QRect fSearchBox(TaskContext &c) {
    QRect result;
    QRect box;
    rust::workflowRun(
        rust::WorkflowMethod::WorldFSearchBox, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::WorldInteractBox: {
                box = c.templates.box("pick_up_f_hcenter_vcenter", {c.frame.cols, c.frame.rows});
                out[0] = box.x();
                out[1] = box.y();
                out[2] = box.width();
                break;
            }
            case rust::WorkflowOp::WorldInteractBoxHeight: {
                out[0] = box.height();
                break;
            }
            case rust::WorkflowOp::WorldStoreSearchBox: {
                result = QRect(int(a[0]), int(a[1]), int(a[2]), int(a[3]));
                break;
            }
            default:
                fail("Rust 世界交互宿主无效");
            }
        });
    return result;
}
std::optional<Match> findInteract(TaskContext &c, const QString &pattern) {
    std::optional<Match> found;
    std::optional<TextLine> line;
    QRect region;
    double white = 0;
    rust::workflowRun(
        rust::WorkflowMethod::WorldFindInteract, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::Now: {
                out[0] = c.execution.now();
                break;
            }
            case rust::WorkflowOp::Checkpoint: {
                c.execution.checkpoint();
                break;
            }
            case rust::WorkflowOp::ExecutionWait: {
                c.execution.wait(a[0]);
                break;
            }
            case rust::WorkflowOp::Refresh: {
                c.refresh();
                break;
            }
            case rust::WorkflowOp::InvalidCondition: {
                fail("等待条件参数无效");
                break;
            }
            case rust::WorkflowOp::Sleep: {
                c.sleep(a[0]);
                break;
            }
            case rust::WorkflowOp::WorldFindInteract: {
                found = c.find("pick_up_f_hcenter_vcenter", {.search = fSearchBox(c)});
                break;
            }
            case rust::WorkflowOp::WorldHasMatch: {
                out[0] = found.has_value();
                break;
            }
            case rust::WorkflowOp::WorldPatternEmpty: {
                out[0] = pattern.isEmpty();
                break;
            }
            case rust::WorkflowOp::WorldReadInteractWhite: {
                white = colorPercentage(c.frame, found->box, {235, 235, 235}, {255, 255, 255});
                break;
            }
            case rust::WorkflowOp::WorldInteractWhite: {
                out[0] = white;
                break;
            }
            case rust::WorkflowOp::WorldClearMatch: {
                found.reset();
                break;
            }
            case rust::WorkflowOp::WorldReadInteractText: {
                const auto r = found->box;
                region = QRect(pixelRound(r.x() + r.width() * 5.2), pixelRound(r.y() - .8 * r.height()),
                               r.width() * 7, pixelRound(r.height() * 5.5));
                line = matchingText(c.text(region), pattern);
                break;
            }
            case rust::WorkflowOp::WorldHasInteractText: {
                out[0] = line.has_value();
                break;
            }
            case rust::WorkflowOp::WorldInteractTextBelow: {
                out[0] = line->box.y() > region.y() + found->box.height();
                break;
            }
            case rust::WorkflowOp::WorldScrollInteract: {
                c.input.scroll(1, QPointF(c.frame.cols * .5, c.frame.rows * .5));
                break;
            }
            default:
                fail("Rust 世界交互宿主无效");
            }
        });
    return found;
}
bool isDialogInteract(TaskContext &c) {
    std::optional<Match> found, dots;
    return rust::workflowRun(rust::WorkflowMethod::WorldDialogInteract, {},
                             [&](rust::WorkflowOp op, const double *a, double *out) {
                                 switch (op) {
                                 case rust::WorkflowOp::WorldReadUnfilteredInteract: {
                                     found = findInteract(c);
                                     break;
                                 }
                                 case rust::WorkflowOp::WorldHasMatch: {
                                     out[0] = found.has_value();
                                     break;
                                 }
                                 case rust::WorkflowOp::WorldFindDialogDots: {
                                     const auto r = found->box;
                                     const QRect search(r.x() + r.width() * 3, r.y() - r.height(),
                                                        pixelRound(r.width() * 2.8), r.height() * 3);
                                     dots = c.find("dialog_3_dots", {.threshold = .6, .search = search});
                                     break;
                                 }
                                 case rust::WorkflowOp::WorldHasDialogDots: {
                                     out[0] = dots.has_value();
                                     break;
                                 }
                                 default:
                                     fail("Rust 世界交互宿主无效");
                                 }
                             })[0] != 0;
}
bool WorldInteraction::claimDialog() {
    return rust::workflowRun(rust::WorkflowMethod::WorldClaimDialog, {},
                             [&](rust::WorkflowOp op, const double *a, double *out) {
                                 switch (op) {
                                 case rust::WorkflowOp::InTeam: {
                                     out[0] = c_.inTeam();
                                     break;
                                 }
                                 case rust::WorkflowOp::WorldClaimCancelFound: {
                                     out[0] = bool(c_.find("claim_cancel_button_hcenter_vcenter",
                                                           {.horizontal = .05, .vertical = .1}));
                                     break;
                                 }
                                 default:
                                     fail("Rust 世界交互宿主无效");
                                 }
                             })[0] != 0;
}
bool WorldInteraction::dismissClaim() {
    return rust::workflowRun(rust::WorkflowMethod::WorldDismissClaim, {},
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
                                 case rust::WorkflowOp::WorldClaimDialog: {
                                     out[0] = claimDialog();
                                     break;
                                 }
                                 case rust::WorkflowOp::WorldEscape: {
                                     c_.key("esc", a[0]);
                                     break;
                                 }
                                 default:
                                     fail("Rust 世界交互宿主无效");
                                 }
                             })[0] != 0;
}
bool WorldInteraction::pick(bool handleClaim, bool echoOnly) {
    const std::array args{double(handleClaim), double(echoOnly)};
    return rust::workflowRun(rust::WorkflowMethod::WorldPick, args,
                             [&](rust::WorkflowOp op, const double *a, double *out) {
                                 switch (op) {
                                 case rust::WorkflowOp::WorldFindPick: {
                                     out[0] = bool(findInteract(c_, a[0] != 0 ? "吸收|Absorb" : ""));
                                     break;
                                 }
                                 case rust::WorkflowOp::WorldKeyF: {
                                     c_.key("f", a[0]);
                                     break;
                                 }
                                 case rust::WorkflowOp::WorldDismissClaim: {
                                     out[0] = dismissClaim();
                                     break;
                                 }
                                 default:
                                     fail("Rust 世界交互宿主无效");
                                 }
                             })[0] != 0;
}
bool WorldInteraction::walkUntil(const std::function<bool()> &condition, const QString &direction,
                                 double timeout, bool running, bool required, double postWalk) {
    std::optional<Movement> movement;
    const std::array args{timeout, double(running), double(required), postWalk};
    return rust::workflowRun(rust::WorkflowMethod::WorldWalkUntil, args,
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
                                 case rust::WorkflowOp::CreateWorldMovement: {
                                     movement.emplace(c_);
                                     break;
                                 }
                                 case rust::WorkflowOp::WorldMovementRequestedDirection: {
                                     movement->direction(direction);
                                     break;
                                 }
                                 case rust::WorkflowOp::WorldMovementRun: {
                                     movement->run();
                                     break;
                                 }
                                 case rust::WorkflowOp::WorldMovementStop: {
                                     movement->stop();
                                     break;
                                 }
                                 case rust::WorkflowOp::WorldCondition: {
                                     out[0] = condition();
                                     break;
                                 }
                                 case rust::WorkflowOp::WorldFailure: {
                                     switch (int(a[0])) {
                                     case 0:
                                         fail("移动等待参数无效");
                                     case 1:
                                         throw NavigationFailure("移动后未找到目标交互提示");
                                     default:
                                         fail("移动失败编号无效");
                                     }
                                     break;
                                 }
                                 default:
                                     fail("Rust 世界交互宿主无效");
                                 }
                             })[0] != 0;
}
bool WorldInteraction::walkToInteract(const QString &direction, double timeout, bool required,
                                      double backward, const QString &pattern, bool checkCombat,
                                      bool running) {
    std::optional<Movement> movement;
    const QStringList directions{"w", "a", "s", "d"};
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
        case rust::WorkflowOp::CreateWorldMovement: {
            movement.emplace(c_);
            break;
        }
        case rust::WorkflowOp::WorldMovementStop: {
            movement->stop();
            break;
        }
        case rust::WorkflowOp::WorldMovementRun: {
            movement->run();
            break;
        }
        case rust::WorkflowOp::WorldMoveDirectionId: {
            movement->direction(directions.at(int(a[0])));
            break;
        }
        case rust::WorkflowOp::WorldHasCombat: {
            out[0] = bool(inCombat_);
            break;
        }
        case rust::WorkflowOp::WorldCombat: {
            out[0] = inCombat_();
            break;
        }
        case rust::WorkflowOp::WorldCenterCamera: {
            centerCamera();
            break;
        }
        case rust::WorkflowOp::WorldReleaseInput: {
            c_.input.release();
            break;
        }
        case rust::WorkflowOp::WorldFindRequestedInteract: {
            out[0] = bool(findInteract(c_, pattern));
            break;
        }
        case rust::WorkflowOp::WorldMiddleClick: {
            c_.input.click({}, a[0], "middle");
            break;
        }
        case rust::WorkflowOp::WorldWalkInteractUntil: {
            out[0] = walkUntil(
                [&] {
                    const std::array conditionArgs{a[4]};
                    return rust::workflowRun(rust::WorkflowMethod::WorldInteractCondition, conditionArgs,
                                             action)[0] != 0;
                },
                a[0] != 0 ? QString("s") : direction, a[1], a[2] != 0, a[3] != 0);
            break;
        }
        default:
            fail("Rust 移动宿主无效");
        }
    };
    const std::array args{timeout, double(required), backward, double(checkCombat), double(running)};
    return rust::workflowRun(rust::WorkflowMethod::WorldWalkInteract, args, action)[0] != 0;
}
QVector<Match> WorldInteraction::echoes(double threshold) {
    QVector<Match> result;
    const std::array args{threshold};
    rust::workflowRun(rust::WorkflowMethod::WorldEchoes, args,
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::WorldHasDetector: {
                              out[0] = bool(detect_);
                              break;
                          }
                          case rust::WorkflowOp::WorldFailure: {
                              fail("声骸识别模型尚未初始化");
                              break;
                          }
                          case rust::WorkflowOp::Checkpoint: {
                              c_.execution.checkpoint();
                              break;
                          }
                          case rust::WorkflowOp::WorldDetectEchoes: {
                              result = detect_(c_.frame, a[0]);
                              break;
                          }
                          case rust::WorkflowOp::WorldEchoCount: {
                              out[0] = double(result.size());
                              break;
                          }
                          case rust::WorkflowOp::WorldFlattenEcho: {
                              auto &box = result[int(a[0])];
                              box.box.setY(box.box.y() + box.box.height() / 3);
                              box.box.setHeight(1);
                              break;
                          }
                          default:
                              fail("Rust 世界交互宿主无效");
                          }
                      });
    return result;
}
bool WorldInteraction::walkToEcho(double timeout, double threshold, const std::function<void()> &update) {
    std::optional<Movement> movement;
    const QStringList directions{"w", "a", "s", "d"};
    std::function<void(rust::WorkflowOp, const double *, double *)> action;
    QVector<Match> found;
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
        case rust::WorkflowOp::CreateWorldMovement: {
            movement.emplace(c_);
            break;
        }
        case rust::WorkflowOp::WorldMovementStop: {
            movement->stop();
            break;
        }
        case rust::WorkflowOp::WorldMovementRun: {
            movement->run();
            break;
        }
        case rust::WorkflowOp::WorldMoveDirectionId: {
            movement->direction(directions.at(int(a[0])));
            break;
        }
        case rust::WorkflowOp::WorldHasCombat: {
            out[0] = bool(inCombat_);
            break;
        }
        case rust::WorkflowOp::WorldCombat: {
            out[0] = inCombat_();
            break;
        }
        case rust::WorkflowOp::WorldCenterCamera: {
            centerCamera();
            break;
        }
        case rust::WorkflowOp::WorldReleaseInput: {
            c_.input.release();
            break;
        }
        case rust::WorkflowOp::WorldPickDefault: {
            out[0] = pick();
            break;
        }
        case rust::WorkflowOp::WorldReadEchoes: {
            found = echoes(a[0]);
            break;
        }
        case rust::WorkflowOp::WorldEchoCount: {
            out[0] = double(found.size());
            break;
        }
        case rust::WorkflowOp::WorldMoveEchoDirection: {
            movement->direction(echoWalkDirection(found, {c_.frame.cols, c_.frame.rows}, a[0] != 0));
            break;
        }
        case rust::WorkflowOp::WorldHasUpdate: {
            out[0] = bool(update);
            break;
        }
        case rust::WorkflowOp::WorldUpdate: {
            update();
            break;
        }
        default:
            fail("Rust 移动宿主无效");
        }
    };
    const std::array args{timeout, threshold};
    return rust::workflowRun(rust::WorkflowMethod::WorldWalkEcho, args, action)[0] != 0;
}
void WorldInteraction::centerCamera() {
    rust::workflowRun(rust::WorkflowMethod::WorldCenterCamera, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::WorldCenterMiddleClick: {
                              c_.input.click(QPointF(c_.frame.cols * .5, c_.frame.rows * .5), .2, "middle");
                              break;
                          }
                          case rust::WorkflowOp::Sleep: {
                              c_.sleep(a[0]);
                              break;
                          }
                          default:
                              fail("Rust 世界交互宿主无效");
                          }
                      });
}
std::pair<bool, bool> WorldInteraction::findEcho(bool useColor, bool turn, double timeout, double threshold,
                                                 const std::function<void()> &update) {
    std::optional<Movement> movement;
    const QStringList directions{"w", "a", "s", "d"};
    std::function<void(rust::WorkflowOp, const double *, double *)> action;
    QVector<Match> found;
    QRect front;
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
        case rust::WorkflowOp::CreateWorldMovement: {
            movement.emplace(c_);
            break;
        }
        case rust::WorkflowOp::WorldMovementStop: {
            movement->stop();
            break;
        }
        case rust::WorkflowOp::WorldMovementRun: {
            movement->run();
            break;
        }
        case rust::WorkflowOp::WorldMoveDirectionId: {
            movement->direction(directions.at(int(a[0])));
            break;
        }
        case rust::WorkflowOp::WorldHasCombat: {
            out[0] = bool(inCombat_);
            break;
        }
        case rust::WorkflowOp::WorldCombat: {
            out[0] = inCombat_();
            break;
        }
        case rust::WorkflowOp::WorldCenterCamera: {
            centerCamera();
            break;
        }
        case rust::WorkflowOp::WorldReleaseInput: {
            c_.input.release();
            break;
        }
        case rust::WorkflowOp::WorldPickEchoOnly: {
            out[0] = pick(true, true);
            break;
        }
        case rust::WorkflowOp::WorldReadEchoes: {
            found = echoes(a[0]);
            break;
        }
        case rust::WorkflowOp::WorldEchoCount: {
            out[0] = double(found.size());
            break;
        }
        case rust::WorkflowOp::WorldWalkEcho: {
            out[0] = walkToEcho(a[0], a[1], update);
            break;
        }
        case rust::WorkflowOp::WorldReadFrontBox: {
            front =
                Templates::anchored({672, 378, 576, 194}, {1920, 1080}, {c_.frame.cols, c_.frame.rows}, true);
            break;
        }
        case rust::WorkflowOp::WorldFrontColor: {
            out[0] = colorPercentage(c_.frame, front, {130, 150, 200}, {170, 220, 255});
            break;
        }
        case rust::WorkflowOp::WorldTapDirection: {
            c_.key(directions.at(int(a[0])), a[1], a[2]);
            break;
        }
        default:
            fail("Rust 移动宿主无效");
        }
    };
    const std::array args{double(useColor), double(turn), timeout, threshold};
    const auto result = rust::workflowRun(rust::WorkflowMethod::WorldFindEcho, args, action);
    return {result[0] != 0, result[1] != 0};
}
bool WorldInteraction::circleEcho(int circles) {
    const std::array args{double(circles)};
    return rust::workflowRun(rust::WorkflowMethod::WorldCircleEcho, args,
                             [&](rust::WorkflowOp op, const double *a, double *out) {
                                 switch (op) {
                                 case rust::WorkflowOp::WorldFailure: {
                                     fail("声骸搜索圈数无效");
                                     break;
                                 }
                                 case rust::WorkflowOp::WorldCircleWalk: {
                                     const QStringList keys{"w", "a", "s", "d"};
                                     out[0] = walkUntil([&] { return bool(findInteract(c_, "吸收|Absorb")); },
                                                        keys.at(int(a[0])), a[1], true);
                                     break;
                                 }
                                 case rust::WorkflowOp::WorldPickDefault: {
                                     out[0] = pick();
                                     break;
                                 }
                                 default:
                                     fail("Rust 世界交互宿主无效");
                                 }
                             })[0] != 0;
}
std::optional<Match> WorldInteraction::treasure() {
    std::optional<Match> found;
    rust::workflowRun(
        rust::WorkflowMethod::WorldTreasure, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::WorldFindTreasure: {
                const auto region = Templates::anchored({58, 108, 1804, 767}, {1920, 1080},
                                                        {c_.frame.cols, c_.frame.rows}, true, true);
                found = c_.find("treasure_icon", {.search = region, .targetHeight = 720});
                break;
            }
            default:
                fail("Rust 世界交互宿主无效");
            }
        });
    return found;
}
bool WorldInteraction::walkToBox(const std::function<std::optional<Match>()> &find,
                                 const std::function<bool()> &end, double timeout, double yOffset,
                                 double xThreshold, bool hook) {
    std::optional<Movement> movement;
    const QStringList directions{"w", "a", "s", "d"};
    std::function<void(rust::WorkflowOp, const double *, double *)> action;
    std::optional<Match> candidate, target;
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
        case rust::WorkflowOp::CreateWorldMovement: {
            movement.emplace(c_);
            break;
        }
        case rust::WorkflowOp::WorldMovementStop: {
            movement->stop();
            break;
        }
        case rust::WorkflowOp::WorldMovementRun: {
            movement->run();
            break;
        }
        case rust::WorkflowOp::WorldMoveDirectionId: {
            movement->direction(directions.at(int(a[0])));
            break;
        }
        case rust::WorkflowOp::WorldHasCombat: {
            out[0] = bool(inCombat_);
            break;
        }
        case rust::WorkflowOp::WorldCombat: {
            out[0] = inCombat_();
            break;
        }
        case rust::WorkflowOp::WorldCenterCamera: {
            centerCamera();
            break;
        }
        case rust::WorkflowOp::WorldReleaseInput: {
            c_.input.release();
            break;
        }
        case rust::WorkflowOp::WorldHasFind: {
            out[0] = bool(find);
            break;
        }
        case rust::WorkflowOp::WorldHasEnd: {
            out[0] = bool(end);
            break;
        }
        case rust::WorkflowOp::WorldEnd: {
            out[0] = end();
            break;
        }
        case rust::WorkflowOp::WorldProbeFind: {
            out[0] = bool(find());
            break;
        }
        case rust::WorkflowOp::WorldReadFind: {
            candidate = find();
            break;
        }
        case rust::WorkflowOp::WorldHasCandidate: {
            out[0] = candidate.has_value();
            break;
        }
        case rust::WorkflowOp::WorldStoreWalkTarget: {
            target = candidate;
            break;
        }
        case rust::WorkflowOp::WorldHasWalkTarget: {
            out[0] = target.has_value();
            break;
        }
        case rust::WorkflowOp::WorldWalkTargetPoint: {
            const auto point = target->center();
            out[0] = point.x();
            out[1] = point.y();
            out[2] = c_.frame.rows;
            break;
        }
        case rust::WorkflowOp::WorldFrameWidth: {
            out[0] = c_.frame.cols;
            break;
        }
        case rust::WorkflowOp::WorldOnWall: {
            out[0] = bool(c_.find("on_the_wall", {.threshold = .7}));
            break;
        }
        case rust::WorkflowOp::WorldMovementStopRunning: {
            movement->stopRunning();
            break;
        }
        case rust::WorkflowOp::WorldHookFound: {
            out[0] = bool(c_.find("tool_teleport", {.threshold = .75}));
            break;
        }
        case rust::WorkflowOp::WorldUseHook: {
            c_.key(c_.hotkey("Tool Key", "t"), 3);
            break;
        }
        case rust::WorkflowOp::WorldMovementFailure: {
            switch (int(a[0])) {
            case 0:
                fail("目标移动参数无效");
            case 1:
                throw NavigationFailure("未能接近奖励目标");
            case 2:
                fail("小地图寻路缺少战斗状态");
            default:
                fail("寻路失败编号无效");
            }
            break;
        }
        default:
            fail("Rust 移动宿主无效");
        }
    };
    const std::array args{timeout, yOffset, xThreshold, double(hook)};
    return rust::workflowRun(rust::WorkflowMethod::WorldWalkBox, args, action)[0] != 0;
}
void WorldInteraction::walkToTreasure(bool requireF) {
    std::optional<Movement> movement;
    const QStringList directions{"w", "a", "s", "d"};
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
        case rust::WorkflowOp::CreateWorldMovement: {
            movement.emplace(c_);
            break;
        }
        case rust::WorkflowOp::WorldMovementStop: {
            movement->stop();
            break;
        }
        case rust::WorkflowOp::WorldMovementRun: {
            movement->run();
            break;
        }
        case rust::WorkflowOp::WorldMoveDirectionId: {
            movement->direction(directions.at(int(a[0])));
            break;
        }
        case rust::WorkflowOp::WorldHasCombat: {
            out[0] = bool(inCombat_);
            break;
        }
        case rust::WorkflowOp::WorldCombat: {
            out[0] = inCombat_();
            break;
        }
        case rust::WorkflowOp::WorldCenterCamera: {
            centerCamera();
            break;
        }
        case rust::WorkflowOp::WorldReleaseInput: {
            c_.input.release();
            break;
        }
        case rust::WorkflowOp::WorldWalkClaimTreasure: {
            out[0] = walkToBox([&] { return treasure(); },
                               [&] { return bool(findInteract(c_, "领取奖励|領取獎勵|Claim Rewards")); });
            break;
        }
        case rust::WorkflowOp::WorldWalkAnyTreasure: {
            out[0] = walkToBox([&] { return treasure(); }, [&] { return bool(findInteract(c_)); });
            break;
        }
        case rust::WorkflowOp::WorldRequireTreasureInteract: {
            walkToInteract("w", 2, true);
            break;
        }
        case rust::WorkflowOp::WorldMovementFailure: {
            switch (int(a[0])) {
            case 0:
                fail("目标移动参数无效");
            case 1:
                throw NavigationFailure("未能接近奖励目标");
            case 2:
                fail("小地图寻路缺少战斗状态");
            default:
                fail("寻路失败编号无效");
            }
            break;
        }
        default:
            fail("Rust 移动宿主无效");
        }
    };
    const std::array args{double(requireF)};
    rust::workflowRun(rust::WorkflowMethod::WorldWalkTreasure, args, action);
}
double WorldInteraction::arrowAngle() {
    QRect box;
    std::optional<Match> found;
    return rust::workflowRun(
        rust::WorkflowMethod::WorldArrowAngle, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::WorldReadArrowBox: {
                box = c_.templates.box("arrow", {c_.frame.cols, c_.frame.rows});
                break;
            }
            case rust::WorkflowOp::Checkpoint: {
                c_.execution.checkpoint();
                break;
            }
            case rust::WorkflowOp::WorldFindRotatedArrow: {
                found = c_.find("arrow", {.threshold = .01, .search = box, .rotation = a[0]});
                break;
            }
            case rust::WorkflowOp::WorldHasMatch: {
                out[0] = found.has_value();
                break;
            }
            case rust::WorkflowOp::WorldMatchConfidence: {
                out[0] = found->confidence;
                break;
            }
            default:
                fail("Rust 世界交互宿主无效");
            }
        })[0];
}
std::optional<double> WorldInteraction::miniMapTurn(const QString &feature, double threshold, double xOffset,
                                                    double yOffset) {
    QRect box;
    std::optional<Match> target;
    const std::array args{threshold, xOffset, yOffset};
    const auto result = rust::workflowRun(
        rust::WorkflowMethod::WorldMiniMapTurn, args, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::WorldReadMinimapBox: {
                box = c_.templates.box("box_minimap", {c_.frame.cols, c_.frame.rows});
                break;
            }
            case rust::WorkflowOp::WorldFindMinimapTarget: {
                target = c_.find(feature, {.threshold = a[0], .search = box});
                break;
            }
            case rust::WorkflowOp::WorldHasMatch: {
                out[0] = target.has_value();
                break;
            }
            case rust::WorkflowOp::WorldMinimapDelta: {
                const auto point =
                    target->center() + QPointF(target->box.width() * a[0], target->box.height() * a[1]);
                const QPointF center(box.x() + box.width() * .5, box.y() + box.height() * .5);
                out[0] = point.x() - center.x();
                out[1] = point.y() - center.y();
                break;
            }
            case rust::WorkflowOp::WorldReadArrowAngle: {
                out[0] = arrowAngle();
                break;
            }
            default:
                fail("Rust 世界交互宿主无效");
            }
        });
    return result[1] != 0 ? std::optional<double>{result[0]} : std::optional<double>{};
}
bool WorldInteraction::followBossMinimap(double timeout, double threshold) {
    std::optional<Movement> movement;
    const QStringList directions{"w", "a", "s", "d"};
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
        case rust::WorkflowOp::CreateWorldMovement: {
            movement.emplace(c_);
            break;
        }
        case rust::WorkflowOp::WorldMovementStop: {
            movement->stop();
            break;
        }
        case rust::WorkflowOp::WorldMovementRun: {
            movement->run();
            break;
        }
        case rust::WorkflowOp::WorldMoveDirectionId: {
            movement->direction(directions.at(int(a[0])));
            break;
        }
        case rust::WorkflowOp::WorldHasCombat: {
            out[0] = bool(inCombat_);
            break;
        }
        case rust::WorkflowOp::WorldCombat: {
            out[0] = inCombat_();
            break;
        }
        case rust::WorkflowOp::WorldCenterCamera: {
            centerCamera();
            break;
        }
        case rust::WorkflowOp::WorldReleaseInput: {
            c_.input.release();
            break;
        }
        case rust::WorkflowOp::WorldMovementFailure: {
            switch (int(a[0])) {
            case 0:
                fail("目标移动参数无效");
            case 1:
                throw NavigationFailure("未能接近奖励目标");
            case 2:
                fail("小地图寻路缺少战斗状态");
            default:
                fail("寻路失败编号无效");
            }
            break;
        }
        case rust::WorkflowOp::WorldBossMinimapTurn: {
            out[0] = miniMapTurn("boss_check_mark_minimap", a[0], -1, 1).value_or(0);
            break;
        }
        case rust::WorkflowOp::WorldDirectionDown: {
            c_.input.keyDown(directions.at(int(a[0])));
            break;
        }
        case rust::WorkflowOp::WorldDirectionUp: {
            c_.input.keyUp(directions.at(int(a[0])));
            break;
        }
        case rust::WorkflowOp::WorldMiddleClick: {
            c_.input.click({}, a[0], "middle");
            break;
        }
        case rust::WorkflowOp::WorldTapDirection: {
            c_.key(directions.at(int(a[0])), a[1], a[2]);
            break;
        }
        default:
            fail("Rust 移动宿主无效");
        }
    };
    const std::array args{timeout, threshold};
    return rust::workflowRun(rust::WorkflowMethod::WorldFollowBoss, args, action)[0] != 0;
}
} // namespace okww::runtime
