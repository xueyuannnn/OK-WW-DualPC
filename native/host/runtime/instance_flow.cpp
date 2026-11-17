#include "instance_flow.hpp"
#include "workflow_abi.hpp"
namespace okww::runtime {
namespace {
QString clean(QString text) { return text.remove(QRegularExpression("\\s+")); }
bool matches(const QString &pattern, const QString &text) {
    return QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption).match(text).hasMatch();
}
QString combine(const QVector<TextLine> &lines) {
    QString value;
    rust::workflowRun(rust::WorkflowMethod::CombineDialog, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::DialogLineCount: {
                              out[0] = double(lines.size());
                              break;
                          }
                          case rust::WorkflowOp::AppendCleanLine: {
                              value += clean(lines.at(int(a[0])).text);
                              break;
                          }
                          default:
                              fail("Rust CombineDialog 副本界面宿主操作无效");
                          }
                      });
    return value;
}
std::optional<TextLine> exact(const QVector<TextLine> &lines, const QString &pattern) {
    std::optional<TextLine> selected;
    rust::workflowRun(rust::WorkflowMethod::ExactDialog, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::DialogLineCount: {
                              out[0] = double(lines.size());
                              break;
                          }
                          case rust::WorkflowOp::ExactLineMatch: {
                              out[0] = matches(pattern, clean(lines.at(int(a[0])).text));
                              break;
                          }
                          case rust::WorkflowOp::StoreSelectedLine: {
                              selected = lines.at(int(a[0]));
                              break;
                          }
                          default:
                              fail("Rust ExactDialog 副本界面宿主操作无效");
                          }
                      });
    return selected;
}
} // namespace
RestartDialog restartDialog(const QVector<TextLine> &lines) {
    QString all;
    std::optional<TextLine> cancel, button;
    const auto result = rust::workflowRun(
        rust::WorkflowMethod::RestartDialog, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::JoinDialog: {
                all = combine(lines);
                break;
            }
            case rust::WorkflowOp::FindCancelLine: {
                cancel = exact(lines, "^(?:取消|cancel)$");
                break;
            }
            case rust::WorkflowOp::DialogResource: {
                out[0] = matches("结晶波片|結晶波片|体力|體力|领取奖励|領取獎勵|领取次数|領取次數|waveplate|"
                                 "claimreward|claimlimit",
                                 all);
                break;
            }
            case rust::WorkflowOp::DialogConfirm: {
                out[0] = matches("是否|确定|確定|确认|確認|sure|confirm", all);
                break;
            }
            case rust::WorkflowOp::CancelLineExists: {
                out[0] = cancel.has_value();
                break;
            }
            case rust::WorkflowOp::AdoptCancelLine: {
                button = cancel;
                break;
            }
            case rust::WorkflowOp::FindRestartLine: {
                button = exact(lines, "^(?:重新挑战|再次挑战|重新挑戰|再次挑戰|challengeagain|retrychallenge|"
                                      "restartchallenge)$");
                out[0] = button.has_value();
                break;
            }
            case rust::WorkflowOp::DialogRestart: {
                out[0] = matches("重新挑战|再次挑战|重新挑戰|再次挑戰|challengeagain|restartchallenge", all);
                break;
            }
            case rust::WorkflowOp::FindConfirmLine: {
                button = exact(lines, "^(?:确认|确定|確認|確定|confirm)$");
                out[0] = button.has_value();
                break;
            }
            default:
                fail("Rust RestartDialog 副本界面宿主操作无效");
            }
        });
    return {RestartKind(int(result[0])), button};
}
bool isNestExitText(QString text) {
    return rust::workflowRun(rust::WorkflowMethod::NestExitText, {},
                             [&](rust::WorkflowOp op, const double *a, double *out) {
                                 switch (op) {
                                 case rust::WorkflowOp::MatchExitText: {
                                     out[0] = matches(
                                         "^(?:F[:：]?)?(?:(?:离开|離開|退出)(?:当前|當前)?(?:副本|秘境|挑战|"
                                         "挑戰)|(?:leave|exit)(?:domain|instance|challenge))$",
                                         clean(text));
                                     break;
                                 }
                                 default:
                                     fail("Rust NestExitText 副本界面宿主操作无效");
                                 }
                             })[0] != 0;
}
bool isNestCompleteText(QString text) {
    return rust::workflowRun(rust::WorkflowMethod::NestCompleteText, {},
                             [&](rust::WorkflowOp op, const double *a, double *out) {
                                 switch (op) {
                                 case rust::WorkflowOp::MatchCompleteText: {
                                     out[0] =
                                         matches("(?:梦魔|夢魔|梦魇|夢魘).*(?:已被除|已破除|已净化|已淨化)|(?"
                                                 ":nightmare|nest).*(?:purified|cleared|defeated)",
                                                 clean(text));
                                     break;
                                 }
                                 default:
                                     fail("Rust NestCompleteText 副本界面宿主操作无效");
                                 }
                             })[0] != 0;
}
bool nestExitContext(const QVector<TextLine> &lines) {
    QString all;
    return rust::workflowRun(rust::WorkflowMethod::NestExitContext, {},
                             [&](rust::WorkflowOp op, const double *a, double *out) {
                                 switch (op) {
                                 case rust::WorkflowOp::JoinDialog: {
                                     all = combine(lines);
                                     break;
                                 }
                                 case rust::WorkflowOp::DialogExit: {
                                     out[0] = matches("(?:离开|離開|退出).{0,20}(?:副本|秘境|挑战|挑戰)|(?:"
                                                      "leave|exit).{0,20}(?:domain|instance|challenge)",
                                                      all);
                                     break;
                                 }
                                 case rust::WorkflowOp::DialogExitConfirm: {
                                     out[0] =
                                         matches("是否|确定|確定|确认|確認|进度|進度|sure|progress", all);
                                     break;
                                 }
                                 case rust::WorkflowOp::DialogForbidden: {
                                     out[0] = matches("消耗|结晶波片|結晶波片|贝币|貝幣|星声|星聲|重构|重構|"
                                                      "复苏|復甦|购买|購買|purchase|waveplate|revive",
                                                      all);
                                     break;
                                 }
                                 default:
                                     fail("Rust NestExitContext 副本界面宿主操作无效");
                                 }
                             })[0] != 0;
}
std::optional<TextLine> nestExitConfirmation(const QVector<TextLine> &lines, int width) {
    std::optional<TextLine> selected;
    const std::array args{double(width)};
    rust::workflowRun(rust::WorkflowMethod::NestExitConfirmation, args,
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::ExitContext: {
                              out[0] = nestExitContext(lines);
                              break;
                          }
                          case rust::WorkflowOp::DialogLineCount: {
                              out[0] = double(lines.size());
                              break;
                          }
                          case rust::WorkflowOp::IsExitConfirmLine: {
                              out[0] = matches("^(?:确认|確認|确定|確定|离开|離開|退出|confirm|leave|exit)$",
                                               clean(lines.at(int(a[0])).text));
                              break;
                          }
                          case rust::WorkflowOp::DialogCenterX: {
                              out[0] = lines.at(int(a[0])).center().x();
                              break;
                          }
                          case rust::WorkflowOp::SelectedLineExists: {
                              out[0] = selected.has_value();
                              break;
                          }
                          case rust::WorkflowOp::DialogLineX: {
                              out[0] = lines.at(int(a[0])).box.x();
                              break;
                          }
                          case rust::WorkflowOp::SelectedLineX: {
                              out[0] = selected->box.x();
                              break;
                          }
                          case rust::WorkflowOp::StoreSelectedLine: {
                              selected = lines.at(int(a[0]));
                              break;
                          }
                          default:
                              fail("Rust NestExitConfirmation 副本界面宿主操作无效");
                          }
                      });
    return selected;
}
RestartAction RestartTracker::step(RestartKind kind, bool hud, bool realm) {
    return RestartAction(rust::okww_rs_restart_step(&state_, int(kind), hud, realm));
}

void restartBossDomain(TaskContext &c) {
    Navigation navigation(c);
    RestartDialog dialog;
    rust::workflowRun(rust::WorkflowMethod::RestartBoss, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::Refresh: {
                              c.refresh();
                              break;
                          }
                          case rust::WorkflowOp::ReadRestartDialog: {
                              dialog = restartDialog(c.text(.16, .2, .86, .82, .7));
                              out[0] = int(dialog.kind);
                              break;
                          }
                          case rust::WorkflowOp::InTeam: {
                              out[0] = c.inTeam();
                              break;
                          }
                          case rust::WorkflowOp::NavInRealm: {
                              out[0] = navigation.inRealm();
                              break;
                          }
                          case rust::WorkflowOp::HasInfo: {
                              out[0] = bool(c.info);
                              break;
                          }
                          case rust::WorkflowOp::RestartCompleteInfo: {
                              c.info("再次挑战", "下一轮副本已确认加载完成");
                              break;
                          }
                          case rust::WorkflowOp::RestartButtonExists: {
                              out[0] = dialog.button.has_value();
                              break;
                          }
                          case rust::WorkflowOp::ClickRestartButton: {
                              c.click(*dialog.button, a[0]);
                              break;
                          }
                          case rust::WorkflowOp::Key: {
                              c.key("esc", a[1]);
                              break;
                          }
                          case rust::WorkflowOp::Sleep: {
                              c.sleep(a[0]);
                              break;
                          }
                          case rust::WorkflowOp::Throw: {
                              throw NavigationFailure("再次挑战未完成：尚未确认下一轮副本加载");
                              break;
                          }
                          default:
                              fail("Rust RestartBoss 副本界面宿主操作无效");
                          }
                      });
}
bool completedNest(TaskContext &c) {
    QVector<TextLine> lines;
    return rust::workflowRun(rust::WorkflowMethod::CompletedNest, {},
                             [&](rust::WorkflowOp op, const double *a, double *out) {
                                 switch (op) {
                                 case rust::WorkflowOp::ReadCompleteText: {
                                     lines = c.text(.015, .15, .38, .38, .7);
                                     out[0] = double(lines.size());
                                     break;
                                 }
                                 case rust::WorkflowOp::LineIsComplete: {
                                     out[0] = isNestCompleteText(lines.at(int(a[0])).text);
                                     break;
                                 }
                                 default:
                                     fail("Rust CompletedNest 副本界面宿主操作无效");
                                 }
                             })[0] != 0;
}
void leaveNest(TaskContext &c, bool sendEscape) {
    Navigation navigation(c);
    QVector<TextLine> lines, dialogText;
    std::optional<TextLine> button;
    std::optional<Match> feature;
    const std::array args{double(sendEscape)};
    rust::workflowRun(
        rust::WorkflowMethod::LeaveNest, args, [&](rust::WorkflowOp op, const double *a, double *out) {
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
            case rust::WorkflowOp::InvalidCondition: {
                fail("等待条件参数无效");
                break;
            }
            case rust::WorkflowOp::Refresh: {
                c.refresh();
                break;
            }
            case rust::WorkflowOp::NavInWorld: {
                out[0] = navigation.inWorld();
                break;
            }
            case rust::WorkflowOp::ReadCompleteText: {
                lines = c.text(.015, .15, .38, .38, .7);
                out[0] = double(lines.size());
                break;
            }
            case rust::WorkflowOp::LineIsComplete: {
                out[0] = isNestCompleteText(lines.at(int(a[0])).text);
                break;
            }
            case rust::WorkflowOp::ReadNestExitDialog: {
                dialogText = c.text(.2, .25, .82, .78, .7);
                break;
            }
            case rust::WorkflowOp::ReadExitConfirmation: {
                button = nestExitConfirmation(dialogText, c.frame.cols);
                break;
            }
            case rust::WorkflowOp::ExitButtonExists: {
                out[0] = button.has_value();
                break;
            }
            case rust::WorkflowOp::ReadExitContext: {
                out[0] = nestExitContext(dialogText);
                break;
            }
            case rust::WorkflowOp::FindNestConfirmFeature: {
                feature = c.findAny({"confirm_btn_hcenter_vcenter", "confirm_btn_highlight_hcenter_vcenter"},
                                    {.threshold = a[0]});
                out[0] = feature.has_value();
                break;
            }
            case rust::WorkflowOp::FeatureAsExitButton: {
                button = TextLine{feature->name, feature->box, feature->confidence};
                break;
            }
            case rust::WorkflowOp::ClickExitButton: {
                c.click(*button, a[0]);
                break;
            }
            case rust::WorkflowOp::FindExitPrompt: {
                out[0] =
                    bool(c.find("pick_up_f_hcenter_vcenter", {.threshold = a[0], .search = fSearchBox(c)}));
                break;
            }
            case rust::WorkflowOp::ReadExitPromptText: {
                lines = c.text(.59, .34, .94, .67, .7);
                out[0] = double(lines.size());
                break;
            }
            case rust::WorkflowOp::LineIsExit: {
                out[0] = isNestExitText(lines.at(int(a[0])).text);
                break;
            }
            case rust::WorkflowOp::Key: {
                c.key(a[0] == 6 ? "f" : "esc", a[1]);
                break;
            }
            case rust::WorkflowOp::ReadNestExitLines: {
                lines = c.text(.04, .18, .96, .92, .7);
                out[0] = double(lines.size());
                break;
            }
            case rust::WorkflowOp::SelectNestExitLine: {
                button = lines.at(int(a[0]));
                break;
            }
            case rust::WorkflowOp::InTeam: {
                out[0] = c.inTeam();
                break;
            }
            case rust::WorkflowOp::Sleep: {
                c.sleep(a[0]);
                break;
            }
            case rust::WorkflowOp::Throw: {
                throw NavigationFailure("巢穴退出未完成：尚未确认回到大世界");
                break;
            }
            default:
                fail("Rust LeaveNest 副本界面宿主操作无效");
            }
        });
}
} // namespace okww::runtime
