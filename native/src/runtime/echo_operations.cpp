#include "echo_operations.hpp"
#include "workflow_abi.hpp"
#include <opencv2/imgcodecs.hpp>
namespace okww::runtime {
namespace {
double number(QString value) {
    return rust::workflowRun(rust::WorkflowMethod::EchoStatNumber, {},
                             [&](rust::WorkflowOp op, const double *a, double *out) {
                                 switch (op) {
                                 case rust::WorkflowOp::NormalizeEchoNumberPercent: {
                                     value.replace(QChar(0xff05), '%');
                                     break;
                                 }
                                 case rust::WorkflowOp::ParseEchoStatNumber: {
                                     bool ok = false;
                                     out[0] = value.section('%', 0, 0).trimmed().toDouble(&ok);
                                     out[1] = ok;
                                     break;
                                 }
                                 default:
                                     fail("Rust 声骸词条宿主无效");
                                 }
                             })[0];
}
QString simple(QString value) {
    static_assert(sizeof(QChar) == sizeof(char16_t));
    // Borrow mutable storage only when there are characters to transform;
    // QString::data() would otherwise turn a null QString into an empty one.
    auto *words = value.isEmpty() ? nullptr : reinterpret_cast<char16_t *>(value.data());
    if (!rust::okww_rs_echo_simplify(words, size_t(value.size())))
        fail("声骸文字处理接口异常");
    return value;
}
QString fixedMain(QString text) {
    rust::workflowRun(rust::WorkflowMethod::EchoFixedMain, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::SimplifyEchoMain: {
                              text = simple(text);
                              break;
                          }
                          case rust::WorkflowOp::ReplaceEchoMainTerm: {
                              switch (int(a[0])) {
                              case 0:
                                  text.replace("凝夜自霜", "凝夜白霜");
                                  break;
                              case 1:
                                  text.replace("主属性灭伤害加成", "主属性湮灭伤害加成");
                                  break;
                              case 2:
                                  text.replace(QRegularExpression("(?<!湮)灭伤害加成"), "湮灭伤害加成");
                                  break;
                              case 3:
                                  text.replace("主属性行射伤害加成", "主属性衍射伤害加成");
                                  break;
                              }
                              break;
                          }
                          default:
                              fail("Rust 声骸词条宿主无效");
                          }
                      });
    return text;
}
} // namespace
QString normalizeEchoStat(QString p, const QString &value) {
    rust::workflowRun(rust::WorkflowMethod::EchoNormalizeStat, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::SimplifyEchoProperty: {
                              p = simple(p);
                              break;
                          }
                          case rust::WorkflowOp::EchoPropertyContains: {
                              out[0] = p.contains(QStringList{"暴击伤害", "暴击", "攻击", "生命", "防御",
                                                              "效率", "普攻", "重击", "解放", "技能"}
                                                      .at(int(a[0])));
                              break;
                          }
                          case rust::WorkflowOp::EchoValueHasPercent: {
                              out[0] = value.contains('%') || value.contains(QChar(0xff05));
                              break;
                          }
                          case rust::WorkflowOp::StoreCanonicalEchoProperty: {
                              p = QStringList{"暴击伤害",        "暴击",         "共鸣效率",
                                              "普攻伤害加成",    "重击伤害加成", "共鸣解放伤害加成",
                                              "共鸣技能伤害加成"}
                                      .at(int(a[0]));
                              break;
                          }
                          case rust::WorkflowOp::StoreBaseEchoProperty: {
                              p = QStringList{"攻击", "生命", "防御"}.at(int(a[0])) +
                                  (a[1] != 0 ? QString("百分比") : QString{});
                              break;
                          }
                          default:
                              fail("Rust 声骸词条宿主无效");
                          }
                      });
    return p;
}
QVector<EchoStat> pairEchoStats(const QVector<TextLine> &lines) {
    QVector<TextLine> properties, values;
    QVector<EchoStat> result;
    TextLine line;
    QString value;
    QRegularExpressionMatch match;
    const QRegularExpression property("[\\x{4e00}-\\x{9fff}]{2,}"), numeric("^[\\d.%％ ]+$");
    rust::workflowRun(
        rust::WorkflowMethod::EchoPairStats, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::EchoStatLineCount: {
                out[0] = double(lines.size());
                break;
            }
            case rust::WorkflowOp::ReadEchoStatLine: {
                line = lines.at(int(a[0]));
                break;
            }
            case rust::WorkflowOp::SimplifyEchoStatLine: {
                line.text = simple(line.text);
                break;
            }
            case rust::WorkflowOp::EchoStatPropertyMatches: {
                match = property.match(line.text);
                out[0] = match.hasMatch();
                break;
            }
            case rust::WorkflowOp::EchoStatAuxiliary: {
                out[0] = line.text.contains("辅音");
                break;
            }
            case rust::WorkflowOp::TruncateEchoStatProperty: {
                line.text = match.captured();
                break;
            }
            case rust::WorkflowOp::AppendEchoStatProperty: {
                properties.append(line);
                break;
            }
            case rust::WorkflowOp::EchoStatNumeric: {
                out[0] = numeric.match(line.text).hasMatch();
                break;
            }
            case rust::WorkflowOp::AppendEchoStatValue: {
                values.append(line);
                break;
            }
            case rust::WorkflowOp::EchoPropertyLineCount: {
                out[0] = double(properties.size());
                break;
            }
            case rust::WorkflowOp::DefaultEchoStatValue: {
                value = "0";
                break;
            }
            case rust::WorkflowOp::EchoValueLineCount: {
                out[0] = double(values.size());
                break;
            }
            case rust::WorkflowOp::EchoStatYDistance: {
                out[0] = std::abs(values.at(int(a[0])).box.y() - properties.at(int(a[1])).box.y());
                break;
            }
            case rust::WorkflowOp::SelectEchoStatValue: {
                value = values.at(int(a[0])).text;
                break;
            }
            case rust::WorkflowOp::RemoveEchoStatValue: {
                values.removeAt(int(a[0]));
                break;
            }
            case rust::WorkflowOp::AppendPairedEchoStat: {
                result.append({properties.at(int(a[0])).text, value});
                break;
            }
            default:
                fail("Rust 声骸词条宿主无效");
            }
        });
    return result;
}
EchoDecision evaluateEcho(const QVector<EchoStat> &stats, const QJsonObject &config) {
    EchoDecision result;
    QJsonArray valid;
    QString p;
    rust::workflowRun(
        rust::WorkflowMethod::EchoEvaluateStats, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::EchoStatsCount: {
                out[0] = double(stats.size());
                break;
            }
            case rust::WorkflowOp::StoreEchoDecisionCount: {
                result.count = int(a[0]);
                break;
            }
            case rust::WorkflowOp::ReadValidEchoStats: {
                valid = config.value("有效词条").toArray();
                break;
            }
            case rust::WorkflowOp::NormalizeCurrentEchoStat: {
                const auto &stat = stats.at(int(a[0]));
                p = normalizeEchoStat(stat.property, stat.value);
                break;
            }
            case rust::WorkflowOp::ReadCurrentEchoStatNumber: {
                out[0] = number(stats.at(int(a[0])).value);
                break;
            }
            case rust::WorkflowOp::CurrentEchoCritKind: {
                out[0] = QStringList{"暴击", "暴击伤害"}.indexOf(p);
                break;
            }
            case rust::WorkflowOp::EchoRuleAllBeforeCrit: {
                out[0] = config["双爆出现之前必须全有效词条"].toBool();
                break;
            }
            case rust::WorkflowOp::ValidEchoContains: {
                out[0] = valid.contains(int(a[0]) == 2 ? p : QStringList{"暴击", "暴击伤害"}.at(int(a[0])));
                break;
            }
            case rust::WorkflowOp::EchoFirstCritThreshold: {
                out[0] = config["首条双爆>="].toDouble();
                break;
            }
            case rust::WorkflowOp::StoreEchoInvalidCount: {
                result.invalid = int(a[0]);
                break;
            }
            case rust::WorkflowOp::EchoRuleRequiresBothCrit: {
                out[0] = config["必须有双爆"].toBool();
                break;
            }
            case rust::WorkflowOp::EchoTotalCritThreshold: {
                out[0] = config["双爆总计>="].toDouble();
                break;
            }
            case rust::WorkflowOp::EchoRuleFirstValid: {
                out[0] = config["第一条必须为有效词条"].toBool();
                break;
            }
            case rust::WorkflowOp::EchoValidThreshold: {
                out[0] = config["有效词条>="].toInt();
                break;
            }
            case rust::WorkflowOp::RejectEchoDecision: {
                result.keep = false;
                switch (int(a[0])) {
                case 0:
                    result.reason = "双爆前含无效_" + p;
                    break;
                case 1:
                    result.reason =
                        QString("首条%1不足_%2").arg(QStringList{"暴击", "爆伤"}.at(int(a[1]))).arg(a[2]);
                    break;
                case 2:
                    result.reason = QString("无法凑齐双爆_缺%1").arg(int(a[1]));
                    break;
                case 3:
                    result.reason = QString("双爆总计不足_%1").arg(a[1], 0, 'f', 1);
                    break;
                case 4:
                    result.reason = "首条无效";
                    break;
                case 5:
                    result.reason = QString("有效词条不足_上限%1").arg(int(a[1]));
                    break;
                }
                break;
            }
            default:
                fail("Rust 声骸词条宿主无效");
            }
        });
    return result;
}
EchoOperations::OperationResult EchoOperations::rustOperation(int method, std::initializer_list<double> args,
                                                              OperationRequest request) {
    OperationResult value;
    QVector<TextLine> lines;
    QVector<EchoStat> stats;
    EchoDecision decision;
    QJsonObject config;
    QString yes, no, directory, target;
    QRect box, crop, screen;
    std::optional<Match> markYes, markNo, selectAll;
    std::optional<TaskScope> scope;
    std::vector<uchar> buffer;
    std::optional<QSaveFile> file;
    const auto result = rust::workflowRun(
        rust::WorkflowMethod(method), std::span(args.begin(), args.size()),
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
            case rust::WorkflowOp::InTeam: {
                out[0] = c_.inTeam();
                break;
            }
            case rust::WorkflowOp::HasInfo: {
                out[0] = bool(c_.info);
                break;
            }
            case rust::WorkflowOp::HasNotify: {
                out[0] = bool(c_.notify);
                break;
            }
            case rust::WorkflowOp::ReadEchoWaitText: {
                lines = c_.text(request.area);
                break;
            }
            case rust::WorkflowOp::EchoOperationLineCount: {
                out[0] = double(lines.size());
                break;
            }
            case rust::WorkflowOp::FixEchoOperationLine: {
                lines[int(a[0])].text = fixedMain(lines.at(int(a[0])).text);
                break;
            }
            case rust::WorkflowOp::SimplifyEchoOperationLine: {
                lines[int(a[0])].text = simple(lines.at(int(a[0])).text);
                break;
            }
            case rust::WorkflowOp::MatchEchoWaitText: {
                value.text = matchingText(lines, request.pattern);
                out[0] = value.text.has_value();
                break;
            }
            case rust::WorkflowOp::ReadEchoOperationText: {
                lines = c_.text(a[0], a[1], a[2], a[3]);
                break;
            }
            case rust::WorkflowOp::MatchEchoCultivate: {
                value.text = matchingText(lines, "^培养$");
                break;
            }
            case rust::WorkflowOp::MatchEchoZeroLevel: {
                out[0] = bool(matchingText(lines, "声骸技能"));
                break;
            }
            case rust::WorkflowOp::ReadEchoCultivate: {
                value.text = cultivate();
                out[0] = value.text.has_value();
                break;
            }
            case rust::WorkflowOp::ClickEchoOperationText: {
                c_.click(*value.text, a[0]);
                break;
            }
            case rust::WorkflowOp::EchoOperationEscape: {
                c_.key("esc", a[0]);
                break;
            }
            case rust::WorkflowOp::PrepareEchoMarkBox: {
                const bool lock = a[0] != 0;
                yes = lock ? "echo_locked" : "echo_dropped";
                no = lock ? "echo_not_locked" : "echo_not_dropped";
                const QSize size(c_.frame.cols, c_.frame.rows);
                box = c_.templates.box(yes, size).united(c_.templates.box(no, size));
                box.adjust(-pixelRound(box.width() * .025), -pixelRound(box.height() * .025),
                           pixelRound(box.width() * .025), pixelRound(box.height() * .025));
                break;
            }
            case rust::WorkflowOp::ReadEchoMarkYes: {
                markYes = c_.find(yes, {.threshold = .7, .search = box});
                break;
            }
            case rust::WorkflowOp::ReadEchoMarkNo: {
                markNo = c_.find(no, {.threshold = .7, .search = box});
                break;
            }
            case rust::WorkflowOp::HasEchoMarkYes: {
                out[0] = markYes.has_value();
                break;
            }
            case rust::WorkflowOp::HasEchoMarkNo: {
                out[0] = markNo.has_value();
                break;
            }
            case rust::WorkflowOp::EchoMarkYesConfidence: {
                out[0] = markYes->confidence;
                break;
            }
            case rust::WorkflowOp::EchoMarkNoConfidence: {
                out[0] = markNo->confidence;
                break;
            }
            case rust::WorkflowOp::PressEchoMarkKey: {
                c_.key(a[0] != 0 ? "c" : "z", 1);
                break;
            }
            case rust::WorkflowOp::SanitizeEchoScreenshotReason: {
                request.reason.remove(QRegularExpression("[<>:\"/\\\\|?*]"));
                break;
            }
            case rust::WorkflowOp::PrepareEchoScreenshotDirectory: {
                directory = screenshots_ + "/" + request.category;
                break;
            }
            case rust::WorkflowOp::CreateEchoScreenshotDirectory: {
                out[0] = QDir().mkpath(directory);
                break;
            }
            case rust::WorkflowOp::PrepareEchoScreenshotCrop: {
                crop = gameBox(c_.frame, .09, .09, .37, .55);
                break;
            }
            case rust::WorkflowOp::EncodeEchoScreenshot: {
                out[0] = cv::imencode(
                    ".png", c_.frame(cv::Rect(crop.x(), crop.y(), crop.width(), crop.height())), buffer);
                break;
            }
            case rust::WorkflowOp::CreateEchoScreenshotFile: {
                file.emplace(directory + "/" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") +
                             QString("_%1_").arg(request.count) + request.reason.left(100) + ".png");
                break;
            }
            case rust::WorkflowOp::OpenEchoScreenshotFile: {
                out[0] = file->open(QIODevice::WriteOnly);
                break;
            }
            case rust::WorkflowOp::WriteEchoScreenshot: {
                out[0] = double(file->write(reinterpret_cast<const char *>(buffer.data()), buffer.size()));
                break;
            }
            case rust::WorkflowOp::EchoScreenshotSize: {
                out[0] = double(buffer.size());
                break;
            }
            case rust::WorkflowOp::CommitEchoScreenshot: {
                out[0] = file->commit();
                break;
            }
            case rust::WorkflowOp::ReadEchoStageAdd: {
                value.text = waitText(gameBox(c_.frame, .09, .6, .38, .86), "^阶段放入$", 1);
                out[0] = value.text.has_value();
                break;
            }
            case rust::WorkflowOp::ReadEchoEnhanceButton: {
                value.text = waitText(gameBox(c_.frame, .1, .88, .29, .96), "^强化并调谐$");
                break;
            }
            case rust::WorkflowOp::HasEchoOperationText: {
                out[0] = value.text.has_value();
                break;
            }
            case rust::WorkflowOp::ReadEchoUpgradeResult: {
                value.text = waitText(gameBox(c_.frame, .24, .18, .75, .98), "不再提示|^调谐成功$|点击任", 2);
                break;
            }
            case rust::WorkflowOp::EchoUpgradeDoNotAsk: {
                out[0] = value.text->text.contains("不再提示");
                break;
            }
            case rust::WorkflowOp::EchoOperationSkipConfirm: {
                out[0] = c_.skipConfirm();
                break;
            }
            case rust::WorkflowOp::CreateEchoOperationScope: {
                scope.emplace(
                    c_, QStringList{"EnhanceEchoTask", "ChangeEchoTask", "MergeEchoTask"}.at(int(a[0])));
                break;
            }
            case rust::WorkflowOp::EchoOperationLanguage: {
                out[0] = QStringList{"zh_CN", "zh_TW"}.indexOf(c_.gameLanguage);
                break;
            }
            case rust::WorkflowOp::LoadEchoOperationConfig: {
                config = c_.profile.group(c_.task);
                break;
            }
            case rust::WorkflowOp::ReadEchoZeroLevel: {
                out[0] = zeroLevel(a[0] != 0);
                break;
            }
            case rust::WorkflowOp::EnterEchoCultivate: {
                enterCultivate();
                break;
            }
            case rust::WorkflowOp::UpgradeEchoItem: {
                upgrade();
                break;
            }
            case rust::WorkflowOp::ReadEnhancedEchoStats: {
                stats = pairEchoStats(c_.text(.09, .3, .40, .53));
                break;
            }
            case rust::WorkflowOp::EnhancedEchoStatCount: {
                out[0] = double(stats.size());
                break;
            }
            case rust::WorkflowOp::EnhancedEchoStatNumber: {
                out[0] = number(stats.at(int(a[0])).value);
                break;
            }
            case rust::WorkflowOp::EvaluateEnhancedEcho: {
                decision = evaluateEcho(stats, config);
                break;
            }
            case rust::WorkflowOp::EnhancedEchoKeep: {
                out[0] = decision.keep;
                break;
            }
            case rust::WorkflowOp::MarkEchoItem: {
                mark(a[0] != 0);
                break;
            }
            case rust::WorkflowOp::ScreenshotRejectedEcho: {
                screenshot("failed", int(a[0]), decision.reason);
                break;
            }
            case rust::WorkflowOp::ScreenshotAcceptedEcho: {
                screenshot("success", int(a[0]));
                break;
            }
            case rust::WorkflowOp::EchoFailedInfo: {
                c_.info("失败声骸数量", QString::number(int(a[0])));
                break;
            }
            case rust::WorkflowOp::EchoSuccessInfo: {
                c_.info("成功声骸数量", QString::number(int(a[0])));
                break;
            }
            case rust::WorkflowOp::BackEchoToBag: {
                backToBag();
                break;
            }
            case rust::WorkflowOp::EchoPauseAfterSuccess: {
                out[0] = std::as_const(config)["Pause after Success"].toBool(true);
                break;
            }
            case rust::WorkflowOp::NotifyEchoSuccess: {
                c_.notify("符合条件的声骸", "已锁定并暂停任务，请查看强化结果", false);
                break;
            }
            case rust::WorkflowOp::ReadEchoTargetAttribute: {
                target = c_.setting("目标属性", "攻击").toString();
                break;
            }
            case rust::WorkflowOp::EchoTargetAttributeKind: {
                out[0] = QStringList{"攻击",         "暴击伤害",     "暴击",         "生命",
                                     "防御",         "共鸣效率",     "冷凝伤害加成", "热熔伤害加成",
                                     "导电伤害加成", "气动伤害加成", "衍射伤害加成", "湮灭伤害加成"}
                             .indexOf(target);
                break;
            }
            case rust::WorkflowOp::PrepareEchoOperationScreen: {
                screen = QRect(0, 0, c_.frame.cols, c_.frame.rows);
                break;
            }
            case rust::WorkflowOp::ReadEchoEnhanceHeading: {
                value.text = waitText(screen, "^声骸强化$");
                out[0] = value.text.has_value();
                break;
            }
            case rust::WorkflowOp::ReadEchoCurrentMain: {
                lines = c_.text(.10, .23, .35, .275);
                break;
            }
            case rust::WorkflowOp::EchoCurrentMainIsTarget: {
                out[0] = fixedMain(lines.at(int(a[0])).text).contains(target);
                break;
            }
            case rust::WorkflowOp::ReadEchoMainAttributeHeading: {
                value.text = waitText(screen, "^主音属性$");
                out[0] = value.text.has_value();
                break;
            }
            case rust::WorkflowOp::ReadEchoTargetOption: {
                value.text = waitText(screen, "^(?:主属性)?" + QRegularExpression::escape(target) + "$");
                out[0] = value.text.has_value();
                break;
            }
            case rust::WorkflowOp::ReadEchoAttributeConfirm: {
                value.text = waitText(screen, "^确认$");
                out[0] = value.text.has_value();
                break;
            }
            case rust::WorkflowOp::ReadEchoReconstruct: {
                value.text = waitText(gameBox(c_.frame, .37, .82, .64, .99), "^数据重构$");
                out[0] = value.text.has_value();
                break;
            }
            case rust::WorkflowOp::ReadEchoReconstructResult: {
                value.text = waitText(screen, "^获得声骸$", 10);
                out[0] = value.text.has_value();
                break;
            }
            case rust::WorkflowOp::EnsureEchoOperationMain: {
                navigation_.ensureMain();
                break;
            }
            case rust::WorkflowOp::OpenEchoBag: {
                c_.key(c_.hotkey("Bag Key", "b"));
                break;
            }
            case rust::WorkflowOp::EchoMergePrompt: {
                out[0] = bool(
                    c_.findAny({"cancel_button_hcenter_vcenter", "cancel_button_highlight_hcenter_vcenter"}));
                break;
            }
            case rust::WorkflowOp::EchoMergeNotEnoughInfo: {
                c_.info("声骸合成", "未达到 1000 个弃置声骸");
                break;
            }
            case rust::WorkflowOp::ConfirmEchoMergeDialog: {
                navigation_.confirmDialog(false);
                break;
            }
            case rust::WorkflowOp::ClickEchoCentered: {
                centered(a[0], a[1], a[2]);
                break;
            }
            case rust::WorkflowOp::ReadEchoSelectAll: {
                selectAll = c_.find("echo_select_all", {.horizontal = .3});
                out[0] = selectAll.has_value();
                break;
            }
            case rust::WorkflowOp::ClickEchoSelectAll: {
                c_.click(*selectAll, a[0]);
                break;
            }
            case rust::WorkflowOp::EchoMergeFullSelection: {
                out[0] = bool(matchingText(c_.text(.670, .660, .895, .958), "100\\s*/\\s*100"));
                break;
            }
            case rust::WorkflowOp::WaitEchoMergeResult: {
                awaitEchoMergeResult(c_);
                break;
            }
            case rust::WorkflowOp::EchoOperationFailure: {
                switch (int(a[0])) {
                case 0:
                    fail("必须在背包声骸界面过滤后开始");
                case 1:
                    fail("未能打开声骸培养界面");
                case 2:
                    fail("声骸处理后未能返回背包");
                case 3:
                    fail("无法确认声骸上锁状态");
                case 4:
                    fail("无法确认声骸弃置状态");
                case 5:
                    fail("声骸上锁失败");
                case 6:
                    fail("声骸弃置失败");
                case 7:
                    fail("无法创建声骸截图目录");
                case 8:
                    fail("声骸截图编码失败");
                case 9:
                    fail("声骸截图保存失败");
                case 10:
                    fail("强化设置需要开启阶段放入");
                case 11:
                    fail("找不到强化并调谐，请开启同步调谐并检查材料");
                case 12:
                    fail("强化确认界面未识别，停止继续消耗材料");
                case 13:
                    fail("强化结果未关闭，停止继续消耗材料");
                case 14:
                    fail("强化声骸仅支持简体或繁体中文游戏");
                case 15:
                    fail("强化后词条数量或数值未确认，停止继续消耗材料");
                case 16:
                    fail("声骸处理达到单次批量上限");
                case 17:
                    fail("修改声骸主属性仅支持简体中文游戏");
                case 18:
                    fail("目标主属性无效");
                case 19:
                    fail("声骸培养页面未确认");
                case 20:
                    fail("找不到当前主属性");
                case 21:
                    fail("目标属性和当前属性相同，请修改声骸过滤条件");
                case 22:
                    fail("主音属性页面未确认");
                case 23:
                    fail("没有找到目标主属性");
                case 24:
                    fail("没有找到主属性选择确认按钮");
                case 25:
                    fail("没有找到数据重构按钮");
                case 26:
                    fail("未能确认重构结果，停止继续消耗材料");
                case 27:
                    fail("主属性修改达到单次批量上限");
                case 28:
                    throw NavigationFailure("未能打开背包");
                case 29:
                    fail("未找到声骸全选按钮");
                default:
                    fail("声骸处理错误编号无效");
                }
                break;
            }
            default:
                fail("Rust 声骸操作宿主无效");
            }
        });
    value.value = result[0];
    return value;
}
std::optional<TextLine> EchoOperations::waitText(QRect area, const QString &pattern, double timeout) {
    OperationRequest request;
    request.area = area;
    request.pattern = pattern;
    return rustOperation(int(rust::WorkflowMethod::EchoWaitText), {timeout}, request).text;
}
std::optional<TextLine> EchoOperations::cultivate() {
    OperationRequest request;
    return rustOperation(int(rust::WorkflowMethod::EchoCultivate), {}, request).text;
}
bool EchoOperations::zeroLevel(bool change) {
    OperationRequest request;
    return rustOperation(int(rust::WorkflowMethod::EchoZeroLevel), {double(change)}, request).value != 0;
}
void EchoOperations::enterCultivate() {
    OperationRequest request;
    rustOperation(int(rust::WorkflowMethod::EchoEnterCultivate), {}, request);
}
void EchoOperations::backToBag() {
    OperationRequest request;
    rustOperation(int(rust::WorkflowMethod::EchoBackToBag), {}, request);
}
void EchoOperations::mark(bool lock) {
    OperationRequest request;
    rustOperation(int(rust::WorkflowMethod::EchoMark), {double(lock)}, request);
}
void EchoOperations::screenshot(const QString &category, int count, QString reason) {
    OperationRequest request;
    request.category = category;
    request.count = count;
    request.reason = std::move(reason);
    rustOperation(int(rust::WorkflowMethod::EchoScreenshot), {}, request);
}
void EchoOperations::upgrade() {
    OperationRequest request;
    rustOperation(int(rust::WorkflowMethod::EchoUpgrade), {}, request);
}
int EchoOperations::enhance() {
    OperationRequest request;
    return int(rustOperation(int(rust::WorkflowMethod::EchoEnhance), {}, request).value);
}
int EchoOperations::change() {
    OperationRequest request;
    return int(rustOperation(int(rust::WorkflowMethod::EchoChange), {}, request).value);
}
void EchoOperations::centered(double x, double y, double after) { c_.clickRelative(x, y, after, true); }
bool echoMergeResultConfirmed(const QVector<TextLine> &lines) {
    return rust::workflowRun(rust::WorkflowMethod::EchoMergeResult, {},
                             [&](rust::WorkflowOp op, const double *a, double *out) {
                                 switch (op) {
                                 case rust::WorkflowOp::EchoMergeTextConfirmed: {
                                     out[0] = bool(matchingText(
                                         lines, "^(?:获得声骸|獲得聲骸|数据融合成功|數據融合成功|Echoes? "
                                                "Obtained|Data (?:Merge|Fusion) Complete)[!！。:.：]*$"));
                                     break;
                                 }
                                 default:
                                     fail("Rust 声骸词条宿主无效");
                                 }
                             })[0] != 0;
}
void awaitEchoMergeResult(TaskContext &c) {
    rust::workflowRun(rust::WorkflowMethod::EchoAwaitMerge, {},
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
                          case rust::WorkflowOp::ReadEchoMergeResult: {
                              out[0] = echoMergeResultConfirmed(c.text(.12, .04, .88, .97, .7));
                              break;
                          }
                          case rust::WorkflowOp::EchoMergeSkipConfirm: {
                              out[0] = c.skipConfirm();
                              break;
                          }
                          case rust::WorkflowOp::EchoStatsFailure: {
                              fail("合成结果未确认，停止重复合成");
                              break;
                          }
                          default:
                              fail("Rust 声骸词条宿主无效");
                          }
                      });
}
int EchoOperations::merge() {
    OperationRequest request;
    return int(rustOperation(int(rust::WorkflowMethod::EchoMerge), {}, request).value);
}
} // namespace okww::runtime
