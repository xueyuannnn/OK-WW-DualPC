#include "multi_account.hpp"
#include "workflow_abi.hpp"
namespace okww::runtime {
QString normalizeAccount(QString value) {
    rust::workflowRun(rust::WorkflowMethod::NormalizeAccount, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::LowerTrimAccount: {
                              value = value.toLower().trimmed();
                              break;
                          }
                          case rust::WorkflowOp::RemoveAccountWhitespace: {
                              value.remove(QRegularExpression("\\s+"));
                              break;
                          }
                          case rust::WorkflowOp::ReplaceAccountZeros: {
                              value.replace('0', 'o');
                              break;
                          }
                          case rust::WorkflowOp::ReplaceAccountDomain: {
                              value.replace(".con", ".com");
                              break;
                          }
                          default:
                              fail("Rust 账号宿主无效");
                          }
                      });
    return value;
}
void AccountCycle::complete(const QString &account) {
    QString name;
    rust::workflowRun(rust::WorkflowMethod::CompleteAccount, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::NormalizeCycleAccount: {
                              name = normalizeAccount(account);
                              break;
                          }
                          case rust::WorkflowOp::NormalizedAccountEmpty: {
                              out[0] = name.isEmpty();
                              break;
                          }
                          case rust::WorkflowOp::InsertCompletedAccount: {
                              completed_.insert(name);
                              break;
                          }
                          case rust::WorkflowOp::AccountFailure: {
                              fail("未识别到账号，停止切换以免重复执行日常");
                              break;
                          }
                          default:
                              fail("Rust 账号宿主无效");
                          }
                      });
}
bool AccountCycle::completed(const QString &account) const {
    QString name;
    return rust::workflowRun(rust::WorkflowMethod::AccountCompleted, {},
                             [&](rust::WorkflowOp op, const double *a, double *out) {
                                 switch (op) {
                                 case rust::WorkflowOp::NormalizeCycleAccount: {
                                     name = normalizeAccount(account);
                                     break;
                                 }
                                 case rust::WorkflowOp::CycleContainsAccount: {
                                     out[0] = completed_.contains(name);
                                     break;
                                 }
                                 default:
                                     fail("Rust 账号宿主无效");
                                 }
                             })[0] != 0;
}
std::optional<TextLine> AccountCycle::next(const QVector<TextLine> &accounts) const {
    const std::array args{double(accounts.size())};
    const auto result = rust::workflowRun(
        rust::WorkflowMethod::NextAccount, args, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::NextAccountNameEmpty: {
                out[0] = normalizeAccount(accounts.at(int(a[0])).text).isEmpty();
                break;
            }
            case rust::WorkflowOp::NextAccountCompleted: {
                out[0] = completed(accounts.at(int(a[0])).text);
                break;
            }
            default:
                fail("Rust 账号宿主无效");
            }
        });
    return result[0] < 0 ? std::optional<TextLine>{} : std::optional<TextLine>{accounts.at(int(result[0]))};
}
int AccountCycle::count() const {
    return int(rust::workflowRun(rust::WorkflowMethod::AccountCount, {},
                                 [&](rust::WorkflowOp op, const double *a, double *out) {
                                     switch (op) {
                                     case rust::WorkflowOp::AccountCycleCount: {
                                         out[0] = completed_.size();
                                         break;
                                     }
                                     default:
                                         fail("Rust 账号宿主无效");
                                     }
                                 })[0]);
}
std::optional<TextLine> MultiAccountDaily::rustRun(int method, AccountCycle *cycle,
                                                   const std::function<void()> &daily,
                                                   QVector<TextLine> *result) {
    QVector<TextLine> text, lines;
    std::optional<TextLine> selected, login;
    TextLine displayed;
    QStringList names;
    QString previous, fingerprint, expected;
    std::optional<AccountCycle> owned;
    const QRegularExpression pattern("(?:\\*{3,}|[•·]{3,}|[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,})");
    rust::workflowRun(
        rust::WorkflowMethod(method), {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::ReadAccountText: {
                text = c_.text(.15, .12, .85, .85, .65);
                break;
            }
            case rust::WorkflowOp::AccountTextCount: {
                out[0] = double(text.size());
                break;
            }
            case rust::WorkflowOp::AccountTextMatches: {
                out[0] = pattern.match(text.at(int(a[0])).text).hasMatch();
                break;
            }
            case rust::WorkflowOp::AppendAccountText: {
                lines.append(text.at(int(a[0])));
                break;
            }
            case rust::WorkflowOp::SortAccountText: {
                std::sort(lines.begin(), lines.end(),
                          [](const auto &a, const auto &b) { return a.box.y() < b.box.y(); });
                *result = lines;
                break;
            }
            case rust::WorkflowOp::ReadAccounts: {
                lines = accounts();
                break;
            }
            case rust::WorkflowOp::AccountLineCount: {
                out[0] = double(lines.size());
                break;
            }
            case rust::WorkflowOp::SelectFirstAccountLine: {
                selected = lines.first();
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
            case rust::WorkflowOp::Refresh: {
                c_.refresh();
                break;
            }
            case rust::WorkflowOp::InvalidCondition: {
                fail("等待条件参数无效");
                break;
            }
            case rust::WorkflowOp::NavEnsureMain: {
                navigation_.ensureMain(a[0]);
                break;
            }
            case rust::WorkflowOp::AccountEscape: {
                c_.key("esc", a[0]);
                break;
            }
            case rust::WorkflowOp::AccountSettingsFound: {
                out[0] = bool(c_.find("esc_setting"));
                break;
            }
            case rust::WorkflowOp::ClickRelative: {
                c_.clickRelative(a[0], a[1], a[2]);
                break;
            }
            case rust::WorkflowOp::ConfirmAccountLogout: {
                navigation_.confirmDialog();
                break;
            }
            case rust::WorkflowOp::ResetAccountLoggedIn: {
                c_.state->loggedIn = false;
                break;
            }
            case rust::WorkflowOp::ReadCurrentAccount: {
                displayed = currentAccount();
                break;
            }
            case rust::WorkflowOp::ClickAccountLine: {
                c_.click(displayed, a[0]);
                break;
            }
            case rust::WorkflowOp::AccountPageLimit: {
                out[0] = c_.setting("Account List Pages", 12).toInt();
                break;
            }
            case rust::WorkflowOp::ReadNextAccount: {
                selected = cycle->next(lines);
                break;
            }
            case rust::WorkflowOp::HasSelectedAccount: {
                out[0] = selected.has_value();
                break;
            }
            case rust::WorkflowOp::ClearAccountNames: {
                names.clear();
                break;
            }
            case rust::WorkflowOp::AppendNormalizedAccountName: {
                names.append(normalizeAccount(lines.at(int(a[0])).text));
                break;
            }
            case rust::WorkflowOp::JoinAccountNames: {
                fingerprint = names.join('|');
                break;
            }
            case rust::WorkflowOp::AccountFingerprintUnchanged: {
                out[0] = fingerprint == previous;
                break;
            }
            case rust::WorkflowOp::StoreAccountFingerprint: {
                previous = fingerprint;
                break;
            }
            case rust::WorkflowOp::ClearSelectedAccount: {
                selected.reset();
                break;
            }
            case rust::WorkflowOp::ScrollAccountList: {
                c_.input.scroll(-3, lines.last().center());
                break;
            }
            case rust::WorkflowOp::Sleep: {
                c_.sleep(a[0]);
                break;
            }
            case rust::WorkflowOp::ClickSelectedAccount: {
                c_.click(*selected, a[0]);
                break;
            }
            case rust::WorkflowOp::ReadExpectedAccount: {
                expected = normalizeAccount(selected->text);
                break;
            }
            case rust::WorkflowOp::AccountSelectionMatches: {
                out[0] = normalizeAccount(lines.first().text) == expected;
                break;
            }
            case rust::WorkflowOp::CreateAccountCycle: {
                owned.emplace();
                cycle = &*owned;
                break;
            }
            case rust::WorkflowOp::MaximumAccountCount: {
                out[0] = c_.setting("Maximum Accounts", 20).toInt();
                break;
            }
            case rust::WorkflowOp::RunAccountDaily: {
                daily();
                break;
            }
            case rust::WorkflowOp::LogoutAccount: {
                logout();
                break;
            }
            case rust::WorkflowOp::CompleteCurrentAccount: {
                cycle->complete(currentAccount().text);
                break;
            }
            case rust::WorkflowOp::HasInfo: {
                out[0] = bool(c_.info);
                break;
            }
            case rust::WorkflowOp::AccountCountInfo: {
                c_.info("多账号日常", QString("已完成 %1 个账号").arg(cycle->count()));
                break;
            }
            case rust::WorkflowOp::ChooseAccount: {
                selected = choose(*cycle);
                break;
            }
            case rust::WorkflowOp::AccountCycleCount: {
                out[0] = cycle->count();
                break;
            }
            case rust::WorkflowOp::ReadAccountLogin: {
                login = matchingText(c_.text(.25, .25, .75, .85, .65),
                                     "登录|登入|登錄|Log\\s*[Ii]n|Sign\\s*[Ii]n");
                break;
            }
            case rust::WorkflowOp::HasAccountLogin: {
                out[0] = login.has_value();
                break;
            }
            case rust::WorkflowOp::ClickAccountLogin: {
                c_.click(*login, 2);
                break;
            }
            case rust::WorkflowOp::CompleteSelectedAccount: {
                cycle->complete(selected->text);
                break;
            }
            case rust::WorkflowOp::LoggedOutAccountMatches: {
                out[0] = normalizeAccount(displayed.text) == normalizeAccount(selected->text);
                break;
            }
            case rust::WorkflowOp::AccountFailure: {
                const QStringList errors{
                    "未识别到账号，停止切换以免重复执行日常",
                    "无法唯一识别登录框当前账号，请收起账号列表并确认已保存账号；不会尝试输入密码",
                    "未确认进入设置菜单，停止切换账号",
                    "账号列表不可读，停止切换账号",
                    "账号列表达到翻页上限，未确认全部完成，已停止",
                    "选择后显示的账号不一致，已停止以免重复执行",
                    "达到账号数量上限，剩余账号未运行",
                    "未识别到登录按钮，停止切换账号",
                    "日常完成后登录框显示的账号发生变化，已停止"};
                fail(errors.at(int(a[0])));
                break;
            }
            default:
                fail("Rust 多账号宿主无效");
            }
        });
    return selected;
}
QVector<TextLine> MultiAccountDaily::accounts() {
    QVector<TextLine> result;
    rustRun(int(rust::WorkflowMethod::ReadAccounts), nullptr, {}, &result);
    return result;
}
TextLine MultiAccountDaily::currentAccount() { return *rustRun(int(rust::WorkflowMethod::CurrentAccount)); }
void MultiAccountDaily::logout() { rustRun(int(rust::WorkflowMethod::LogoutAccount)); }
std::optional<TextLine> MultiAccountDaily::choose(AccountCycle &cycle) {
    return rustRun(int(rust::WorkflowMethod::ChooseAccount), &cycle);
}
void MultiAccountDaily::run(const std::function<void()> &daily) {
    rustRun(int(rust::WorkflowMethod::RunAccounts), nullptr, daily);
}
} // namespace okww::runtime
