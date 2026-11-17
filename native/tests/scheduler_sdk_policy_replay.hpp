#pragma once
#include "runtime/workflow_abi.hpp"
namespace okww::runtime::scheduler_test {
inline QJsonObject sdkPolicy(int kind, int scene, bool original) {
    QJsonArray events;
    int ordinal = 0;
    const auto invoke = [&](rust::WorkflowOp op, const double *a, double *out) {
        QJsonArray args;
        if (op == rust::WorkflowOp::ScheduleCommandStarted ||
            op == rust::WorkflowOp::ScheduleCommandFinished || op == rust::WorkflowOp::ScheduleWaitCommand ||
            op == rust::WorkflowOp::ScheduleFailure)
            args.append(a[0]);
        events.append(QJsonObject{{"op", int(op)}, {"args", args}});
        if (++ordinal == scene % 13 + 1 && scene % 7 == 0)
            fail("injected-scheduler-sdk-error");
        switch (op) {
        case rust::WorkflowOp::ScheduleOpenToken:
            out[0] = scene % 3 != 0;
            break;
        case rust::WorkflowOp::ScheduleReadToken:
            out[0] = scene % 5 != 0;
            break;
        case rust::WorkflowOp::ScheduleConvertSid:
            out[0] = scene % 11 != 0;
            break;
        case rust::WorkflowOp::ScheduleCommandStarted:
            out[0] = scene % 3 != 0;
            break;
        case rust::WorkflowOp::ScheduleCommandFinished:
            out[0] = scene % 5 != 0;
            break;
        case rust::WorkflowOp::ScheduleCommandNormal:
            out[0] = scene % 11 != 0;
            break;
        case rust::WorkflowOp::ScheduleCommandExitCode:
            out[0] = scene % 13 == 0 ? 1 : 0;
            break;
        case rust::WorkflowOp::ScheduleTemporaryDirectoryValid:
            out[0] = scene % 3 != 0;
            break;
        case rust::WorkflowOp::ScheduleOpenFile:
            out[0] = scene % 5 != 0;
            break;
        case rust::WorkflowOp::ScheduleWriteFile:
            out[0] = scene % 11 != 0;
            break;
        case rust::WorkflowOp::ScheduleFlushFile:
            out[0] = scene % 13 != 0;
            break;
        case rust::WorkflowOp::ScheduleFailure:
            fail(QString("schedule-failure-%1").arg(int(a[0])));
            break;
        default:
            break;
        }
    };
    const auto action = [&](rust::WorkflowOp op, double value = 0) {
        double out[3]{};
        invoke(op, &value, out);
        return out[0];
    };
    QJsonObject result;
    try {
        if (!original) {
            const auto method = kind == 0   ? rust::WorkflowMethod::ScheduleCurrentUser
                                : kind == 1 ? rust::WorkflowMethod::ScheduleRunCommand
                                            : rust::WorkflowMethod::ScheduleRegister;
            rust::workflowRun(method, {}, invoke);
        } else if (kind == 0) {
            if (!action(rust::WorkflowOp::ScheduleOpenToken))
                action(rust::WorkflowOp::ScheduleFailure, 0);
            action(rust::WorkflowOp::ScheduleQueryTokenSize);
            action(rust::WorkflowOp::ScheduleAllocateToken);
            const bool read = action(rust::WorkflowOp::ScheduleReadToken);
            action(rust::WorkflowOp::ScheduleCloseToken);
            if (!read)
                action(rust::WorkflowOp::ScheduleFailure, 1);
            if (!action(rust::WorkflowOp::ScheduleConvertSid))
                action(rust::WorkflowOp::ScheduleFailure, 2);
            action(rust::WorkflowOp::ScheduleReadSidString);
            action(rust::WorkflowOp::ScheduleFreeSid);
        } else if (kind == 1) {
            action(rust::WorkflowOp::ScheduleHideCommandWindow);
            action(rust::WorkflowOp::ScheduleStartCommand);
            if (!action(rust::WorkflowOp::ScheduleCommandStarted, 5000))
                action(rust::WorkflowOp::ScheduleFailure, 3);
            if (!action(rust::WorkflowOp::ScheduleCommandFinished, 10000)) {
                action(rust::WorkflowOp::ScheduleKillCommand);
                action(rust::WorkflowOp::ScheduleWaitCommand, 1000);
                action(rust::WorkflowOp::ScheduleFailure, 4);
            }
            if (!action(rust::WorkflowOp::ScheduleCommandNormal) ||
                action(rust::WorkflowOp::ScheduleCommandExitCode) != 0)
                action(rust::WorkflowOp::ScheduleFailure, 5);
            action(rust::WorkflowOp::ScheduleReadCommandOutput);
        } else {
            action(rust::WorkflowOp::ScheduleCreateTemporaryDirectory);
            if (!action(rust::WorkflowOp::ScheduleTemporaryDirectoryValid))
                action(rust::WorkflowOp::ScheduleFailure, 11);
            action(rust::WorkflowOp::ScheduleTemporaryPath);
            action(rust::WorkflowOp::ScheduleCreateFile);
            action(rust::WorkflowOp::ScheduleEncodeXml);
            if (!action(rust::WorkflowOp::ScheduleOpenFile) || !action(rust::WorkflowOp::ScheduleWriteFile) ||
                !action(rust::WorkflowOp::ScheduleFlushFile))
                action(rust::WorkflowOp::ScheduleFailure, 12);
            action(rust::WorkflowOp::ScheduleDestroyFile);
            action(rust::WorkflowOp::ScheduleInvokeCreate);
        }
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
        result["type"] = typeid(e).name();
    }
    result["events"] = events;
    return result;
}
} // namespace okww::runtime::scheduler_test
