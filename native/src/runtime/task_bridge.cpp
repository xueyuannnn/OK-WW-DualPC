#include "task.hpp"
#include "task_abi.hpp"
#include <opencv2/imgproc.hpp>

namespace okww::runtime {
std::array<double, 3> TaskContext::rustRun(int method, std::initializer_list<double> args,
                                           const QJsonObject &request) {
    struct Context {
        TaskContext *task = nullptr;
        cv::Mat next;
        QString monthlyKey;
        std::exception_ptr error;
        std::array<QRect, 4> areas;
        std::array<std::optional<Match>, 4> matches;
        std::array<QVector<TextLine>, 4> lines;
        std::array<std::optional<TextLine>, 4> texts;
        QJsonArray configured;
        const QJsonObject *request = nullptr;
        QJsonObject step;
        QJsonArray samples, outputSteps;
    } context;
    context.task = this;
    context.request = &request;
    const rust::TaskHost host{
        &context,
        [](void *opaque, int32_t operation, const double *a, size_t, double *out) -> int32_t {
            auto &context = *static_cast<Context *>(opaque);
            auto &c = *context.task;
            try {
                switch (static_cast<rust::TaskOp>(operation)) {
                case rust::TaskOp::Checkpoint:
                    c.execution.checkpoint();
                    break;
                case rust::TaskOp::Now:
                    out[0] = c.execution.now();
                    break;
                case rust::TaskOp::ReadFrame:
                    context.next = c.source_();
                    out[0] = !context.next.empty();
                    break;
                case rust::TaskOp::HasRecovery:
                    out[0] = bool(c.sourceRecovering);
                    break;
                case rust::TaskOp::Recovering:
                    out[0] = c.sourceRecovering();
                    break;
                case rust::TaskOp::ReleaseInput:
                    c.input.release();
                    break;
                case rust::TaskOp::RecoveryInfo:
                    out[0] = bool(c.info);
                    if (c.info)
                        switch (int(a[0])) {
                        case 0:
                            c.info("自动恢复", QString("等待采集恢复 · %1 秒").arg(int(a[1])));
                            break;
                        case 1:
                            c.info("自动恢复", "采集画面已恢复，继续当前任务");
                            break;
                        case 2:
                            c.info("月卡", "到达设定的弹出时段，等待领取后继续当前步骤");
                            break;
                        default:
                            fail("Rust 恢复提示类型无效");
                        }
                    break;
                case rust::TaskOp::Wait:
                    c.execution.wait(a[0]);
                    break;
                case rust::TaskOp::CommitFrame:
                    c.frame = std::move(context.next);
                    if (c.sourceSequence)
                        c.frameVersion = c.sourceSequence();
                    else
                        ++c.frameVersion;
                    break;
                case rust::TaskOp::MonthlyWindow:
                    c.monthlyWindow();
                    break;
                case rust::TaskOp::FrameEmpty:
                    out[0] = c.frame.empty();
                    break;
                case rust::TaskOp::HandlingMonthly:
                    out[0] = c.handlingMonthlyWindow_;
                    break;
                case rust::TaskOp::SetHandlingMonthly:
                    c.handlingMonthlyWindow_ = a[0] != 0;
                    break;
                case rust::TaskOp::InCombat:
                    out[0] = c.state->inCombat;
                    break;
                case rust::TaskOp::InputIdle:
                    out[0] = c.input.idle();
                    break;
                case rust::TaskOp::MonthlyEnabled:
                    out[0] = c.profile.value("Monthly Card Config", "Check Monthly Card", true).toBool();
                    break;
                case rust::TaskOp::MonthlyKey:
                    context.monthlyKey = monthlyWindowKey(
                        c.wallTime(),
                        c.profile.value("Monthly Card Config", "Monthly Card Time", 4).toInt(4));
                    out[0] = !context.monthlyKey.isEmpty();
                    break;
                case rust::TaskOp::MonthlyHandled:
                    out[0] = c.state->monthlyWindowHandled == context.monthlyKey;
                    break;
                case rust::TaskOp::InTeam:
                    out[0] = c.inTeam();
                    break;
                case rust::TaskOp::RememberMonthly:
                    c.state->monthlyWindowHandled = context.monthlyKey;
                    break;
                case rust::TaskOp::Refresh:
                    c.refresh();
                    break;
                case rust::TaskOp::MonthlyCard:
                    out[0] = c.monthlyCard();
                    break;
                case rust::TaskOp::LoggedIn:
                    out[0] = c.state->loggedIn;
                    break;
                case rust::TaskOp::SetLoggedIn:
                    c.state->loggedIn = a[0] != 0;
                    break;
                case rust::TaskOp::PartyProbe: {
                    static const QStringList names{"char_1_text", "char_2_text", "char_3_text"};
                    out[0] = a[1] == 0
                                 ? bool(c.find(names.at(int(a[0])), {.horizontal = .006, .vertical = .012}))
                                 : bool(c.find(names.at(int(a[0])), {.threshold = .84,
                                                                     .horizontal = .006,
                                                                     .vertical = .012,
                                                                     .grayscale = true,
                                                                     .blurSigma = .9}));
                    break;
                }
                case rust::TaskOp::MonthlyTemplate:
                    out[0] =
                        bool(c.find("monthly_card", {.threshold = .65, .horizontal = .05, .vertical = .05}));
                    break;
                case rust::TaskOp::MonthlyText:
                    out[0] = monthlyTextConfirmed(c.text(.28, .70, .72, .96, .7));
                    break;
                case rust::TaskOp::RewardText:
                    out[0] = rewardTextConfirmed(c.text(.2, .12, .8, .96, .7));
                    break;
                case rust::TaskOp::MonthlyInfo:
                    if (c.info)
                        c.info("月卡", a[0] != 0 ? "已确认领取界面" : "关闭领取结果");
                    break;
                case rust::TaskOp::ClickRelative:
                    c.clickRelative(a[0], a[1], a[2], a[3] != 0);
                    break;
                case rust::TaskOp::Sleep:
                    c.sleep(a[0]);
                    break;
                case rust::TaskOp::SkipConfirm:
                    out[0] = c.skipConfirm();
                    break;
                case rust::TaskOp::TravelButton:
                    out[0] = c.travelButton();
                    break;
                case rust::TaskOp::FrameSize:
                    out[0] = c.frame.cols;
                    out[1] = c.frame.rows;
                    break;
                case rust::TaskOp::SetArea:
                    context.areas.at(size_t(a[0])) =
                        a[7] != 0 ? relativeBox(c.frame, a[1], a[2], a[3], a[4])
                                  : gameBox(c.frame, a[1], a[2], a[3], a[4], a[5] != 0, a[6] != 0);
                    break;
                case rust::TaskOp::ClickMatch:
                    c.click(context.matches.at(size_t(a[0])).value(), a[1]);
                    break;
                case rust::TaskOp::ClickText:
                    c.click(context.texts.at(size_t(a[0])).value(), a[1]);
                    break;
                case rust::TaskOp::ClickPoint:
                    c.click(gamePoint({c.frame.cols, c.frame.rows}, a[0], a[1], a[3] != 0, a[4] != 0), a[2]);
                    break;
                case rust::TaskOp::SetPixelArea:
                    context.areas.at(size_t(a[0])) = {int(a[1]), int(a[2]), int(a[3]), int(a[4])};
                    break;
                case rust::TaskOp::MatchField: {
                    const auto box = context.matches.at(size_t(a[0])).value().box;
                    const int values[]{box.x(), box.y(), box.width(), box.height()};
                    out[0] = values[int(a[1])];
                    break;
                }
                case rust::TaskOp::AreaField: {
                    const auto box = context.areas.at(size_t(a[0]));
                    const int values[]{box.x(), box.y(), box.width(), box.height()};
                    out[0] = values[int(a[1])];
                    break;
                }
                case rust::TaskOp::Scroll:
                    c.input.scroll(int(a[0]), QPointF(c.frame.cols * .5, c.frame.rows * .5));
                    break;
                case rust::TaskOp::LineField: {
                    const auto &line = context.lines.at(size_t(a[0])).at(int(a[1]));
                    const double values[]{double(line.box.x()), double(line.box.y()),
                                          double(line.box.width()), double(line.box.height()),
                                          double(line.text.trimmed().size())};
                    out[0] = values[int(a[2])];
                    break;
                }
                case rust::TaskOp::ContinueMarker:
                    out[0] = hasContinueMarker(c.frame);
                    break;
                case rust::TaskOp::HsvCount: {
                    const auto r = context.areas.at(size_t(a[0]));
                    cv::Mat hsv, mask;
                    cv::cvtColor(c.frame(cv::Rect(r.x(), r.y(), r.width(), r.height())), hsv,
                                 cv::COLOR_BGR2HSV);
                    cv::inRange(hsv, cv::Scalar(10, 90, 110), cv::Scalar(45, 255, 255), mask);
                    out[0] = cv::countNonZero(mask);
                    break;
                }
                case rust::TaskOp::ClickAbsolute:
                    c.click({a[0], a[1]}, a[2]);
                    break;
                case rust::TaskOp::ClearError:
                    context.error = {};
                    break;
                case rust::TaskOp::WorkflowReset:
                    c.workflow_ = {};
                    c.workflowTask_ = c.task;
                    break;
                case rust::TaskOp::WorkflowStart:
                    c.workflowBegan_ = a[0];
                    break;
                case rust::TaskOp::TimingReset:
                    c.timingPath_ = context.request->value("timing_path").toString();
                    c.timingHistory_ = {};
                    break;
                case rust::TaskOp::TimingRead:
                    if (QFile::exists(c.timingPath_))
                        c.timingHistory_ = readJson(c.timingPath_);
                    break;
                case rust::TaskOp::InputStepsCount:
                    out[0] = double(context.request->value("steps").toArray().size());
                    break;
                case rust::TaskOp::LoadStep:
                    switch (int(a[0])) {
                    case 0:
                        context.step = context.request->value("steps").toArray().at(int(a[1])).toObject();
                        break;
                    case 1:
                        context.step = c.workflow_.at(int(a[1])).toObject();
                        break;
                    case 2:
                        context.step = context.outputSteps.at(int(a[1])).toObject();
                        break;
                    default:
                        fail("Rust 流程步骤来源无效");
                    }
                    break;
                case rust::TaskOp::LoadSamples:
                    context.samples =
                        c.timingHistory_.value(c.workflowTask_ + ':' + context.step["id"].toString())
                            .toArray();
                    break;
                case rust::TaskOp::SampleCount:
                    out[0] = double(context.samples.size());
                    break;
                case rust::TaskOp::Sample: {
                    const auto value = context.samples.at(int(a[0]));
                    out[0] = value.toDouble();
                    out[1] = value.isDouble();
                    break;
                }
                case rust::TaskOp::AppendStep:
                    c.workflow_.append(context.step);
                    break;
                case rust::TaskOp::PublishWorkflow:
                    c.publishWorkflow();
                    break;
                case rust::TaskOp::WorkflowCount:
                    out[0] = double(c.workflow_.size());
                    break;
                case rust::TaskOp::StepMatchesId:
                    out[0] = context.step["id"] == context.request->value("id").toString();
                    break;
                case rust::TaskOp::StepMatchesState:
                    out[0] = context.step["state"].toString() == context.request->value("state").toString();
                    break;
                case rust::TaskOp::StoreStepState:
                    context.step["state"] = context.request->value("state").toString();
                    break;
                case rust::TaskOp::StoreDetail:
                    context.step["detail"] = context.request->value("detail").toString().left(500);
                    break;
                case rust::TaskOp::StoreWorkflowStep:
                    c.workflow_[int(a[0])] = context.step;
                    break;
                case rust::TaskOp::HasTimingPath:
                    out[0] = !c.timingPath_.isEmpty();
                    break;
                case rust::TaskOp::AppendSample:
                    context.samples.append(context.step["elapsed_seconds"]);
                    break;
                case rust::TaskOp::RemoveSample:
                    context.samples.removeFirst();
                    break;
                case rust::TaskOp::StoreSamples:
                    c.timingHistory_[c.workflowTask_ + ':' + context.step["id"].toString()] = context.samples;
                    break;
                case rust::TaskOp::TimingWrite:
                    writeJson(c.timingPath_, c.timingHistory_);
                    break;
                case rust::TaskOp::HasProgress:
                    out[0] = bool(c.progress);
                    break;
                case rust::TaskOp::OutputReset:
                    context.outputSteps = c.workflow_;
                    break;
                case rust::TaskOp::StoreOutputStep:
                    context.outputSteps[int(a[0])] = context.step;
                    break;
                case rust::TaskOp::WorkflowBegan:
                    out[0] = c.workflowBegan_;
                    break;
                case rust::TaskOp::OutputPublish:
                    c.progress({{"flow", context.outputSteps},
                                {"flow_task", c.workflowTask_},
                                {"flow_elapsed_seconds", a[0]},
                                {"flow_observed_at", double(QDateTime::currentMSecsSinceEpoch())}});
                    break;
                case rust::TaskOp::ReportProgress:
                    c.progress({{"stage", context.request->value("stage").toString().left(160)},
                                {"current", a[0]},
                                {"total", a[1]},
                                {"unit", context.request->value("unit").toString().left(16)},
                                {"detail", context.request->value("detail").toString().left(500)},
                                {"source_task", c.task}});
                    break;
                default:
                    fail("Rust 任务宿主操作无效");
                }
                return 1;
            } catch (...) {
                if (!context.error)
                    context.error = std::current_exception();
                return 0;
            }
        },
        [](void *opaque, int32_t kind, const uint8_t *name, size_t length, const double *a, size_t count,
           double *out) -> int32_t {
            auto &context = *static_cast<Context *>(opaque);
            auto &c = *context.task;
            try {
                const auto key = QString::fromUtf8(reinterpret_cast<const char *>(name), qsizetype(length));
                switch (kind) {
                case 0: {
                    if (count != 7)
                        fail("Rust 任务模板参数无效");
                    MatchOptions options{.threshold = a[1],
                                         .horizontal = a[2],
                                         .vertical = a[3],
                                         .filter = Filter(int(a[4])),
                                         .templateScale = a[6]};
                    if (a[5] >= 0)
                        options.search = context.areas.at(size_t(a[5]));
                    auto &match = context.matches.at(size_t(a[0]));
                    match = c.find(key, options);
                    out[0] = bool(match);
                    break;
                }
                case 2: {
                    if (count != 3)
                        fail("Rust 任务文字区域参数无效");
                    auto &lines = context.lines.at(size_t(a[0]));
                    lines = c.text(context.areas.at(size_t(a[1])), a[2]);
                    out[0] = double(lines.size());
                    break;
                }
                case 3: {
                    if (count != 2)
                        fail("Rust 任务文本匹配参数无效");
                    auto &line = context.texts.at(size_t(a[1]));
                    line = matchingText(context.lines.at(size_t(a[0])), key);
                    out[0] = bool(line);
                    break;
                }
                case 4:
                    if (count != 1)
                        fail("Rust 任务模板名称参数无效");
                    out[0] = context.matches.at(size_t(a[0])).value().name == key;
                    break;
                case 5:
                    if (count != 1)
                        fail("Rust 任务模板区域参数无效");
                    context.areas.at(size_t(a[0])) = c.templates.box(key, {c.frame.cols, c.frame.rows});
                    break;
                case 6:
                    if (count != 0)
                        fail("Rust 任务列表设置参数无效");
                    context.configured = c.setting(key).toArray();
                    out[0] = double(context.configured.size());
                    break;
                case 7:
                    if (count != 3)
                        fail("Rust 任务名单匹配参数无效");
                    out[0] = context.lines.at(size_t(a[0]))
                                 .at(int(a[1]))
                                 .text.contains(context.configured.at(int(a[2])).toString());
                    break;
                case 8:
                    if (count != 2)
                        fail("Rust 任务按键参数无效");
                    c.key(key, a[0], a[1]);
                    break;
                case 9:
                    if (count != 1)
                        fail("Rust 任务布尔设置参数无效");
                    out[0] = c.setting(key, a[0] != 0).toBool();
                    break;
                case 11:
                    if (count != 7)
                        fail("Rust 任务颜色比例参数无效");
                    out[0] = colorPercentage(c.frame, context.matches.at(size_t(a[0])).value().box,
                                             {a[1], a[2], a[3]}, {a[4], a[5], a[6]});
                    break;
                case 12:
                    if (count != 0)
                        fail("Rust 流程请求状态参数无效");
                    out[0] = context.request->value("state").toString() == key;
                    break;
                case 13:
                    if (count != 0)
                        fail("Rust 流程步骤状态参数无效");
                    out[0] = context.step["state"].toString() == key;
                    break;
                case 14: {
                    if (count != 0)
                        fail("Rust 流程字符串写入参数无效");
                    const auto values = key.split(QChar('\0'));
                    if (values.size() != 2)
                        fail("Rust 流程字符串字段无效");
                    context.step[values[0]] = values[1];
                    break;
                }
                case 15:
                    if (count != 1)
                        fail("Rust 流程数值写入参数无效");
                    context.step[key] = a[0];
                    break;
                case 17:
                    if (count != 0)
                        fail("Rust 流程数值读取参数无效");
                    out[0] = context.step[key].toDouble();
                    break;
                case 18:
                    if (count != 0)
                        fail("Rust 流程字段删除参数无效");
                    context.step.remove(key);
                    break;
                case 19:
                    if (count != 0)
                        fail("Rust 流程请求字段参数无效");
                    out[0] = !context.request->value(key).toString().isEmpty();
                    break;
                default:
                    fail("Rust 任务查询类型无效");
                }
                return 1;
            } catch (...) {
                if (!context.error)
                    context.error = std::current_exception();
                return 0;
            }
        }};
    std::array<double, 3> out{};
    const auto status = rust::okww_rs_taskflow_run(&host, method, args.begin(), args.size(), out.data());
    if (context.error)
        std::rethrow_exception(context.error);
    if (status == -2)
        fail("等待时间无效");
    if (status == -4)
        fail("没有新鲜采集画面，自动任务已停止");
    if (status != 1)
        fail("Rust 任务执行接口异常");
    return out;
}
} // namespace okww::runtime
