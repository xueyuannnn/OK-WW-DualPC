#pragma once
#include "runtime/workflow_abi.hpp"
namespace okww::runtime::daily_stage_test {
struct Probe {
    int scene, retries_, calls = 0, completions = 0;
    QJsonArray events;
    QStringList failed_;
    QString error;
    struct Context {
        Probe &p;
        std::function<void(QString, QString)> info;
        std::function<void(QString, QString, bool)> notify;
        void workflowStep(QString name, QString state, QString detail = {}) {
            p.events.append(QJsonArray{"flow", name, state, detail});
            if (state == "completed" && p.scene % 17 == 0 && ++p.completions == 1)
                throw NavigationFailure("completed callback failed");
            if (state == "failed" && p.scene % 19 == 0)
                throw std::runtime_error("failed callback failed");
        }
    } c_;
    struct Nav {
        Probe &p;
        void ensureMain(double timeout) {
            p.events.append(QJsonArray{"main", timeout});
            if (p.scene % 31 == 0)
                throw std::runtime_error("navigation failed");
        }
    } navigation_;
    explicit Probe(int s) : scene(s), retries_(s % 5 - 1), c_{*this}, navigation_{*this} {
        if (s & 1)
            c_.info = [&](QString key, QString value) {
                events.append(QJsonArray{"info", key, value});
                if (scene % 23 == 0)
                    throw NavigationFailure("info failed");
            };
        if (s & 2)
            c_.notify = [&](QString title, QString body, bool fail) {
                events.append(QJsonArray{"notify", title, body, fail});
                if (scene % 29 == 0)
                    throw std::runtime_error("notify failed");
            };
    }
    void status(const QString &name) {
        if (c_.info)
            c_.info("当前任务", name);
    }
    bool referenceStage(const QString &name, const std::function<void()> &run, bool retryNavigation)
#include "reference_daily_stage_body.hpp"
        bool rustStage(const QString &name, const std::function<void()> &run, bool retryNavigation) {
        const std::array args{double(retryNavigation)};
        return rust::workflowRun(rust::WorkflowMethod::DailyStage, args,
                                 [&](rust::WorkflowOp op, const double *a, double *out,
                                     const std::exception_ptr &exception) {
                                     using O = rust::WorkflowOp;
                                     switch (op) {
                                     case O::DailyWorkflowRunning:
                                         c_.workflowStep(name, "running");
                                         break;
                                     case O::DailyStageStatus:
                                         status(name);
                                         break;
                                     case O::DailyRetries:
                                         out[0] = retries_;
                                         break;
                                     case O::DailyStageOperation:
                                         run();
                                         break;
                                     case O::DailyWorkflowCompleted:
                                         c_.workflowStep(name, "completed");
                                         break;
                                     case O::DailyExecutionFailed:
                                         c_.workflowStep(name, "failed",
                                                         "输入或执行通道异常，已停止后续操作");
                                         break;
                                     case O::DailyExceptionMessage:
                                         try {
                                             if (exception)
                                                 std::rethrow_exception(exception);
                                         } catch (const std::exception &e) {
                                             error = errorText(e);
                                         }
                                         break;
                                     case O::HasInfo:
                                         out[0] = bool(c_.info);
                                         break;
                                     case O::DailyStageErrorInfo:
                                         c_.info(name, error);
                                         break;
                                     case O::DailyWorkflowFailed:
                                         c_.workflowStep(name, "failed", error);
                                         break;
                                     case O::AppendDailyFailedStage:
                                         failed_.append(name + "：" + error);
                                         break;
                                     case O::HasNotify:
                                         out[0] = bool(c_.notify);
                                         break;
                                     case O::NotifyDailyFailedStage:
                                         c_.notify("每日任务步骤未完成", name + "：" + error, true);
                                         break;
                                     case O::NavEnsureMain:
                                         navigation_.ensureMain(a[0]);
                                         break;
                                     default:
                                         fail("Unexpected daily stage test operation");
                                     }
                                 })[0] != 0;
    }
    void operation() {
        ++calls;
        events.append(QJsonArray{"operation", calls});
        if (scene & 128)
            retries_ = 0;
        if (scene & 256) {
            c_.info = {};
            c_.notify = {};
        }
        switch ((scene / 5) % 7) {
        case 0:
            break;
        case 1:
            if (calls <= scene % 3 + 1)
                throw NavigationFailure("stage navigation failed");
            break;
        case 2:
            throw std::runtime_error("stage material uncertain");
        case 3:
            throw Cancelled();
        case 4:
            throw ExecutionFailure("stage input failure", "trace");
        case 5:
            throw CombatEnded("stage combat ended");
        case 6:
            throw QString("unknown failure");
        }
    }
};
inline QJsonObject trace(int scene, bool rust) {
    Probe p(scene);
    QJsonObject out;
    try {
        const auto op = [&] { p.operation(); };
        out["result"] =
            rust ? p.rustStage("步骤", op, (scene & 4) != 0) : p.referenceStage("步骤", op, (scene & 4) != 0);
        out["error"] = "";
    } catch (const std::exception &error) {
        out["error"] = errorText(error);
    } catch (const QString &error) {
        out["error"] = "QString:" + error;
    }
    out["events"] = p.events;
    out["calls"] = p.calls;
    out["failed"] = QJsonArray::fromStringList(p.failed_);
    out["retries"] = p.retries_;
    return out;
}
} // namespace okww::runtime::daily_stage_test
