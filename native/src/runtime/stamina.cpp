#include "stamina.hpp"
#include "navigation.hpp"
#include "workflow_abi.hpp"
#include <QLockFile>
namespace okww::runtime {
namespace {
[[noreturn]] void cubeError(int reason) {
    switch (reason) {
    case 400:
        fail("日常体力魔方记录损坏，不执行材料操作");
    case 401:
        fail("日常体力魔方状态无效");
    case 402:
        fail("上次魔方消耗尚未核实，停止自动使用");
    case 403:
        fail("日常体力魔方计数不一致");
    case 404:
        fail("另一任务正在处理体力魔方记录");
    case 405:
        fail("日常体力魔方次数已用尽或待核实");
    case 406:
        fail("当前执行没有可核实的魔方操作");
    case 407:
        fail("体力魔方记录正在使用");
    case 408:
        fail("魔方操作记录已变化，需要核对");
    case 409:
        fail("补充体力界面与已读数量不符，停止自动确认");
    case 410:
        fail("补充备用体力后未回到领奖界面");
    case 411:
        fail("体力领奖结果未确认，不重复点击消耗按钮");
    case 412:
        fail("魔方操作后体力变化未确认，已保留次数记录并停止继续消耗");
    case 413:
        fail("魔方操作后无法核对库存，已保留次数记录");
    case 414:
        fail("魔方库存变化未确认，已保留次数记录并停止继续消耗");
    case 415:
        throw NavigationFailure("未确认体力领奖界面");
    default:
        fail("魔方记录错误编号无效");
    }
}
int cubeLimit(int value) {
    const std::array args{double(value)};
    return int(rust::workflowRun(rust::WorkflowMethod::CubeLimit, args,
                                 [&](rust::WorkflowOp op, const double *a, double *out) {
                                     switch (op) {
                                     default:
                                         fail("Rust CubeLimit 体力宿主操作无效");
                                     }
                                 })[0]);
}
struct Counter {
    TextLine line;
    int current, maximum;
};
QVector<Counter> counters(const QVector<TextLine> &lines) {
    const QRegularExpression regex("^(\\d+)\\s*/\\s*(\\d+)$");
    QRegularExpressionMatch match;
    int current = 0, maximum = 0;
    bool valid = false;
    QVector<Counter> result;
    rust::workflowRun(
        rust::WorkflowMethod::StaminaCounters, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::StaminaLineCount: {
                out[0] = double(lines.size());
                break;
            }
            case rust::WorkflowOp::ParseStaminaCounter: {
                match = regex.match(QString(lines.at(int(a[0])).text).replace("／", "/").trimmed());
                out[0] = match.hasMatch();
                break;
            }
            case rust::WorkflowOp::ParsedStaminaCounter: {
                bool first = false, second = false;
                current = match.captured(1).toInt(&first);
                maximum = match.captured(2).toInt(&second);
                valid = first && second;
                out[0] = current;
                out[1] = maximum;
                out[2] = valid;
                break;
            }
            case rust::WorkflowOp::AppendStaminaCounter: {
                result.append({lines.at(int(a[0])), current, maximum});
                break;
            }
            default:
                fail("Rust StaminaCounters 体力宿主操作无效");
            }
        });
    return result;
}
int integerValue(const QJsonObject &data, const QString &key, int high) {
    const auto value = data[key];
    const std::array args{double(high)};
    return int(rust::workflowRun(rust::WorkflowMethod::JournalInteger, args,
                                 [&](rust::WorkflowOp op, const double *a, double *out) {
                                     switch (op) {
                                     case rust::WorkflowOp::JournalNumber: {
                                         out[0] = value.toDouble(-1);
                                         break;
                                     }
                                     case rust::WorkflowOp::JournalIsNumber: {
                                         out[0] = value.isDouble();
                                         break;
                                     }
                                     case rust::WorkflowOp::Throw: {
                                         fail("日常体力魔方记录损坏，不执行材料操作");
                                         break;
                                     }
                                     default:
                                         fail("Rust JournalInteger 体力宿主操作无效");
                                     }
                                 })[0]);
}
} // namespace
StaminaValues staminaValues(const QVector<TextLine> &lines) {
    QVector<Counter> entries;
    Counter selected;
    QRegularExpressionMatch match;
    const QRegularExpression reserve("^(?:MAX)?(\\d{1,4})(?:MAX)?$",
                                     QRegularExpression::CaseInsensitiveOption);
    const auto values = rust::workflowRun(
        rust::WorkflowMethod::StaminaValues, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::ReadStaminaCounters: {
                entries = counters(lines);
                out[0] = double(entries.size());
                break;
            }
            case rust::WorkflowOp::CounterMaximum: {
                out[0] = entries.at(int(a[0])).maximum;
                break;
            }
            case rust::WorkflowOp::SelectMainCounter: {
                selected = entries.at(int(a[0]));
                break;
            }
            case rust::WorkflowOp::StaminaLineCount: {
                out[0] = double(lines.size());
                break;
            }
            case rust::WorkflowOp::ParseReserveCounter: {
                match = reserve.match(QString(lines.at(int(a[0])).text).remove(' ').remove(','));
                out[0] = match.hasMatch();
                break;
            }
            case rust::WorkflowOp::ReserveLineBounds: {
                const auto &b = lines.at(int(a[0])).box;
                out[0] = b.x();
                out[1] = b.width();
                out[2] = b.height();
                break;
            }
            case rust::WorkflowOp::MainCounterBounds: {
                out[0] = selected.line.box.x();
                out[1] = selected.line.center().y();
                out[2] = selected.line.box.height();
                break;
            }
            case rust::WorkflowOp::ReserveCenterY: {
                out[0] = lines.at(int(a[0])).center().y();
                break;
            }
            case rust::WorkflowOp::ReserveNumber: {
                out[0] = match.captured(1).toInt();
                break;
            }
            case rust::WorkflowOp::MainCounterCurrent: {
                out[0] = selected.current;
                break;
            }
            default:
                fail("Rust StaminaValues 体力宿主操作无效");
            }
        });
    return {int(values[0]), int(values[1]), int(values[2])};
}
StaminaValues readStamina(TaskContext &c) {
    QVector<TextLine> lines;
    const auto values = rust::workflowRun(rust::WorkflowMethod::ReadStamina, {},
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
                                              case rust::WorkflowOp::ReadStaminaText: {
                                                  lines = c.text(.49, 0, .92, .1);
                                                  out[0] = bool(matchingText(lines, "\\d"));
                                                  break;
                                              }
                                              case rust::WorkflowOp::StaminaTextValues: {
                                                  const auto value = staminaValues(lines);
                                                  out[0] = value.current;
                                                  out[1] = value.reserve;
                                                  out[2] = value.total;
                                                  break;
                                              }
                                              case rust::WorkflowOp::HasInfo: {
                                                  out[0] = bool(c.info);
                                                  break;
                                              }
                                              case rust::WorkflowOp::CurrentStaminaInfo: {
                                                  c.info("当前体力", QString::number(int(a[0])));
                                                  break;
                                              }
                                              case rust::WorkflowOp::ReserveStaminaInfo: {
                                                  c.info("备用体力", QString::number(int(a[0])));
                                                  break;
                                              }
                                              default:
                                                  fail("Rust ReadStamina 体力宿主操作无效");
                                              }
                                          });
    return {int(values[0]), int(values[1]), int(values[2])};
}

StaminaSpend planStaminaSpend(StaminaValues before, int once, int remaining, bool exact, bool twice) {
    rust::Spend out{};
    if (!rust::okww_rs_plan_stamina(before.current, before.reserve, before.total, once, remaining, exact,
                                    twice, &out))
        fail("未确认体力数量，停止领奖");
    return {out.used, out.repeat != 0};
}

StaminaSpend claimStamina(TaskContext &c, int once, int remaining, bool exact) {
    StaminaValues before;
    std::optional<Match> button, retry;
    QPoint point;
    const std::array args{double(once), double(remaining), double(exact)};
    const auto result = rust::workflowRun(
        rust::WorkflowMethod::ClaimStamina, args, [&](rust::WorkflowOp op, const double *a, double *out) {
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
            case rust::WorkflowOp::HasInfo: {
                out[0] = bool(c.info);
                break;
            }
            case rust::WorkflowOp::Throw: {
                cubeError(int(a[0]));
                break;
            }
            case rust::WorkflowOp::ReadBeforeStamina: {
                before = readStamina(c);
                out[0] = before.current;
                out[1] = before.reserve;
                out[2] = before.total;
                break;
            }
            case rust::WorkflowOp::SingleRewardMode: {
                out[0] = c.setting("Reward Claim Mode", "Prefer Double").toString() == "Single";
                break;
            }
            case rust::WorkflowOp::PlanStaminaClaim: {
                const auto plan = planStaminaSpend(before, int(a[0]), int(a[1]), a[2] != 0, a[3] != 0);
                out[0] = plan.used;
                out[1] = plan.repeat;
                break;
            }
            case rust::WorkflowOp::FindRewardButton: {
                button = c.findAny(a[0] != 0 ? QStringList{"cancel_button_hcenter_vcenter",
                                                           "cancel_button_highlight_hcenter_vcenter"}
                                             : QStringList{"confirm_btn_hcenter_vcenter",
                                                           "confirm_btn_highlight_hcenter_vcenter"});
                out[0] = button.has_value();
                break;
            }
            case rust::WorkflowOp::ClaimSign: {
                out[0] = bool(c.find("claim_stamina_sign"));
                break;
            }
            case rust::WorkflowOp::RewardClaimInfo: {
                c.info("副本领奖", QString("%1倍领取 · %2体力").arg(int(a[0])).arg(int(a[1])));
                break;
            }
            case rust::WorkflowOp::ClickRewardButton: {
                c.click(*button, a[0]);
                break;
            }
            case rust::WorkflowOp::GemStaminaDialog: {
                out[0] = bool(c.find("gem_add_stamina", {.horizontal = .4, .vertical = .05}));
                break;
            }
            case rust::WorkflowOp::PrepareReservePoint: {
                point =
                    Templates::anchored({1344, 767, 1, 1}, {1920, 1080}, {c.frame.cols, c.frame.rows}, true)
                        .topLeft();
                break;
            }
            case rust::WorkflowOp::ClickReservePoint: {
                c.click(point, a[0]);
                break;
            }
            case rust::WorkflowOp::Key: {
                c.key("esc", a[1]);
                break;
            }
            case rust::WorkflowOp::FindRetryReward: {
                retry = c.find(button->name);
                out[0] = retry.has_value();
                break;
            }
            case rust::WorkflowOp::ClickRetryReward: {
                c.click(*retry, a[0]);
                break;
            }
            default:
                fail("Rust ClaimStamina 体力操作宿主无效");
            }
        });
    return {int(result[0]), result[1] != 0};
}
CubeBudget::CubeBudget(QString path, int limit, QDateTime now)
    : path_(std::move(path)), day_(now.addSecs(-4 * 3600).date().toString(Qt::ISODate)),
      limit_(cubeLimit(limit)) {
    data_ = read(false);
}
int CubeBudget::remaining() const {
    const std::array args{double(limit_), double(data_["reserved"].toInt())};
    return int(rust::workflowRun(rust::WorkflowMethod::CubeRemaining, args,
                                 [&](rust::WorkflowOp op, const double *a, double *out) {
                                     switch (op) {
                                     default:
                                         fail("Rust CubeRemaining 体力宿主操作无效");
                                     }
                                 })[0]);
}
QJsonObject CubeBudget::read(bool allowPending) const {
    QJsonObject fresh, data;
    const std::array args{double(allowPending)};
    rust::workflowRun(rust::WorkflowMethod::CubeRead, args,
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::FreshCubeJournal: {
                              fresh = {{"day", day_}, {"reserved", 0}, {"verified", 0}, {"pending", false}};
                              break;
                          }
                          case rust::WorkflowOp::CubeFileExists: {
                              out[0] = QFile::exists(path_);
                              break;
                          }
                          case rust::WorkflowOp::UseFreshCubeJournal: {
                              data = fresh;
                              break;
                          }
                          case rust::WorkflowOp::ReadCubeJournal: {
                              data = readJson(path_);
                              break;
                          }
                          case rust::WorkflowOp::ValidateReserved: {
                              out[0] = integerValue(data, "reserved", int(a[0]));
                              break;
                          }
                          case rust::WorkflowOp::JournalPendingIsBool: {
                              out[0] = std::as_const(data)["pending"].isBool();
                              break;
                          }
                          case rust::WorkflowOp::JournalPending: {
                              out[0] = std::as_const(data)["pending"].toBool();
                              break;
                          }
                          case rust::WorkflowOp::JournalDayMatches: {
                              out[0] = std::as_const(data)["day"].toString() == day_;
                              break;
                          }
                          case rust::WorkflowOp::ValidateVerified: {
                              out[0] = integerValue(data, "verified", int(a[0]));
                              break;
                          }
                          case rust::WorkflowOp::JournalReserved: {
                              out[0] = std::as_const(data)["reserved"].toInt();
                              break;
                          }
                          case rust::WorkflowOp::Throw: {
                              cubeError(int(a[0]));
                              break;
                          }
                          default:
                              fail("Rust CubeRead 体力宿主操作无效");
                          }
                      });
    return data;
}
void CubeBudget::reserve() {
    std::optional<QLockFile> lock;
    rust::workflowRun(rust::WorkflowMethod::CubeReserve, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::CreateJournalDirectory: {
                              QDir().mkpath(QFileInfo(path_).absolutePath());
                              break;
                          }
                          case rust::WorkflowOp::LockCubeJournal: {
                              lock.emplace(path_ + ".lock");
                              out[0] = lock->tryLock(0);
                              break;
                          }
                          case rust::WorkflowOp::ReloadCubeState: {
                              data_ = read(a[0] != 0);
                              break;
                          }
                          case rust::WorkflowOp::CubePending: {
                              out[0] = pending();
                              break;
                          }
                          case rust::WorkflowOp::CubeRemaining: {
                              out[0] = remaining();
                              break;
                          }
                          case rust::WorkflowOp::CubeOwned: {
                              out[0] = owned_;
                              break;
                          }
                          case rust::WorkflowOp::CubeUsed: {
                              out[0] = used();
                              break;
                          }
                          case rust::WorkflowOp::SetCubeReserved: {
                              data_["reserved"] = int(a[0]);
                              break;
                          }
                          case rust::WorkflowOp::SetCubePending: {
                              data_["pending"] = a[0] != 0;
                              break;
                          }
                          case rust::WorkflowOp::WriteCubeJournal: {
                              writeJson(path_, data_);
                              break;
                          }
                          case rust::WorkflowOp::SetCubeOwned: {
                              owned_ = a[0] != 0;
                              break;
                          }
                          case rust::WorkflowOp::Throw: {
                              cubeError(int(a[0]));
                              break;
                          }
                          default:
                              fail("Rust CubeReserve 体力宿主操作无效");
                          }
                      });
}
void CubeBudget::verify() {
    std::optional<QLockFile> lock;
    QJsonObject current;
    rust::workflowRun(rust::WorkflowMethod::CubeVerify, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::CubeOwned: {
                              out[0] = owned_;
                              break;
                          }
                          case rust::WorkflowOp::LockCubeJournal: {
                              lock.emplace(path_ + ".lock");
                              out[0] = lock->tryLock(0);
                              break;
                          }
                          case rust::WorkflowOp::ReadCurrentCubeJournal: {
                              current = read(a[0] != 0);
                              break;
                          }
                          case rust::WorkflowOp::CurrentCubeStateMatches: {
                              out[0] = current == data_;
                              break;
                          }
                          case rust::WorkflowOp::CubePending: {
                              out[0] = pending();
                              break;
                          }
                          case rust::WorkflowOp::SetCubePending: {
                              data_["pending"] = a[0] != 0;
                              break;
                          }
                          case rust::WorkflowOp::CubeVerified: {
                              out[0] = std::as_const(data_)["verified"].toInt();
                              break;
                          }
                          case rust::WorkflowOp::SetCubeVerified: {
                              data_["verified"] = int(a[0]);
                              break;
                          }
                          case rust::WorkflowOp::WriteCubeJournal: {
                              writeJson(path_, data_);
                              break;
                          }
                          case rust::WorkflowOp::SetCubeOwned: {
                              owned_ = a[0] != 0;
                              break;
                          }
                          case rust::WorkflowOp::Throw: {
                              cubeError(int(a[0]));
                              break;
                          }
                          default:
                              fail("Rust CubeVerify 体力宿主操作无效");
                          }
                      });
}
bool useOneDailyCube(TaskContext &c, CubeBudget &budget, bool consume) {
    StaminaValues before;
    QVector<Counter> entries;
    QVector<TextLine> descriptions, counts;
    QString description;
    std::optional<TextLine> cancel, confirm;
    const std::array args{double(consume)};
    return rust::workflowRun(
               rust::WorkflowMethod::UseDailyCube, args,
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
                   case rust::WorkflowOp::HasInfo: {
                       out[0] = bool(c.info);
                       break;
                   }
                   case rust::WorkflowOp::Throw: {
                       cubeError(int(a[0]));
                       break;
                   }
                   case rust::WorkflowOp::ReadBeforeStamina: {
                       before = readStamina(c);
                       out[0] = before.current;
                       out[1] = before.reserve;
                       out[2] = before.total;
                       break;
                   }
                   case rust::WorkflowOp::BudgetRemaining: {
                       out[0] = budget.remaining();
                       break;
                   }
                   case rust::WorkflowOp::BudgetPending: {
                       out[0] = budget.pending();
                       break;
                   }
                   case rust::WorkflowOp::CubeDialog: {
                       out[0] = bool(matchingText(c.text(.18, .17, .4, .23),
                                                  "补充结晶波片|補充結晶波片|replenish waveplates"));
                       break;
                   }
                   case rust::WorkflowOp::ReadCubeOpenCounters: {
                       entries = counters(c.text(.49, 0, .92, .1));
                       out[0] = double(entries.size());
                       break;
                   }
                   case rust::WorkflowOp::CounterMaximum: {
                       out[0] = entries.at(int(a[0])).maximum;
                       break;
                   }
                   case rust::WorkflowOp::CubeOpenCounterBox: {
                       const auto &b = entries.at(int(a[0])).line.box;
                       out[0] = b.x();
                       out[1] = b.width();
                       break;
                   }
                   case rust::WorkflowOp::ImageSize: {
                       out[0] = c.frame.cols;
                       out[1] = c.frame.rows;
                       break;
                   }
                   case rust::WorkflowOp::PixelRound: {
                       out[0] = pixelRound(a[0]);
                       break;
                   }
                   case rust::WorkflowOp::ClickCubeCounterPoint: {
                       c.click(QPointF(a[0], a[1]), a[2]);
                       break;
                   }
                   case rust::WorkflowOp::ClickRelative: {
                       c.clickRelative(a[0], a[1], a[2]);
                       break;
                   }
                   case rust::WorkflowOp::ReadCubeStockDescription: {
                       descriptions = c.text(.37, .53, .63, .585);
                       description.clear();
                       out[0] = double(descriptions.size());
                       break;
                   }
                   case rust::WorkflowOp::AppendCubeStockDescription: {
                       description += QString(descriptions.at(int(a[0])).text).remove(' ');
                       break;
                   }
                   case rust::WorkflowOp::CubeStockDescriptionValid: {
                       const QRegularExpression one("消耗1[结結]晶[溶熔][剂劑][补補]充60[结結]晶波片|"
                                                    "consume1crystalsolvent.*60waveplates",
                                                    QRegularExpression::CaseInsensitiveOption);
                       out[0] = one.match(description).hasMatch();
                       break;
                   }
                   case rust::WorkflowOp::ReadCubeStockCounts: {
                       counts = c.textScaled(gameBox(c.frame, .50, .49, .535, .53));
                       out[0] = double(counts.size());
                       break;
                   }
                   case rust::WorkflowOp::CubeStockCountMatches: {
                       out[0] =
                           QRegularExpression("^\\d+$").match(counts.at(int(a[0])).text.trimmed()).hasMatch();
                       break;
                   }
                   case rust::WorkflowOp::CubeStockCountValue: {
                       bool ok = false;
                       out[0] = counts.at(int(a[0])).text.trimmed().toInt(&ok);
                       out[1] = ok;
                       break;
                   }
                   case rust::WorkflowOp::FindCubeCancel: {
                       cancel = matchingText(c.text(.22, .68, .4, .76), "^(?:取消|cancel)$");
                       out[0] = cancel.has_value();
                       break;
                   }
                   case rust::WorkflowOp::ClickCubeCancel: {
                       c.click(*cancel, a[0]);
                       break;
                   }
                   case rust::WorkflowOp::FindCubeConfirm: {
                       confirm = matchingText(c.text(.60, .68, .79, .76), "^(?:确认|確認|confirm)$");
                       out[0] = confirm.has_value();
                       break;
                   }
                   case rust::WorkflowOp::CubeCheckInfo: {
                       c.info("体力魔方检查", QString("库存 %1；单次补充 60 体力；未消耗").arg(int(a[0])));
                       break;
                   }
                   case rust::WorkflowOp::ReserveCubeBudget: {
                       budget.reserve();
                       break;
                   }
                   case rust::WorkflowOp::ClickCubeConfirm: {
                       c.click(*confirm, a[0]);
                       break;
                   }
                   case rust::WorkflowOp::ReadAfterStamina: {
                       const auto after = readStamina(c);
                       out[0] = after.current;
                       out[1] = after.reserve;
                       out[2] = after.total;
                       break;
                   }
                   case rust::WorkflowOp::VerifyCubeBudget: {
                       budget.verify();
                       break;
                   }
                   case rust::WorkflowOp::CubeUsedInfo: {
                       c.info("今日体力魔方", QString("%1/%2").arg(budget.used()).arg(budget.limit()));
                       break;
                   }
                   default:
                       fail("Rust UseDailyCube 体力操作宿主无效");
                   }
               })[0] != 0;
}
} // namespace okww::runtime
