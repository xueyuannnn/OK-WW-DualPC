#pragma once
#include "workflow_types.hpp"
#include <array>
#include <exception>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
namespace okww::rust {
// Platform services distinguish standard exceptions from unknown exceptions;
// task-specific lifecycle types remain in runtime/workflow_abi.hpp.
template <class Action>
std::array<double, 3> platformWorkflowRun(WorkflowMethod method, std::span<const double> args,
                                          Action &&action) {
    struct Context {
        Action &action;
        std::exception_ptr error, handled;
    };
    Context context{action, {}, {}};
    const WorkflowHost host{
        &context, [](void *opaque, int32_t operation, const double *a, size_t, double *out) -> int32_t {
            auto &c = *static_cast<Context *>(opaque);
            const auto op = WorkflowOp(operation);
            try {
                if (op == WorkflowOp::ExceptionKind) {
                    try {
                        if (c.error)
                            std::rethrow_exception(c.error);
                        out[0] = 0;
                    } catch (const std::exception &) {
                        out[0] = 6;
                    } catch (...) {
                        out[0] = 7;
                    }
                } else if (op == WorkflowOp::ClearException) {
                    c.handled = std::exchange(c.error, {});
                } else if (op == WorkflowOp::RethrowHandled) {
                    if (c.handled)
                        std::rethrow_exception(c.handled);
                } else {
                    if constexpr (std::is_invocable_v<Action &, WorkflowOp, const double *, double *,
                                                      const std::exception_ptr &>)
                        c.action(op, a, out, c.error ? c.error : c.handled);
                    else
                        c.action(op, a, out);
                }
                return 1;
            } catch (...) {
                if (!c.error)
                    c.error = std::current_exception();
                return 0;
            }
        }};
    std::array<double, 3> out{};
    const auto status = okww_rs_workflow_run(&host, int(method), args.data(), args.size(), out.data());
    if (context.error)
        std::rethrow_exception(context.error);
    if (status != 1)
        throw std::runtime_error("Rust 平台流程接口异常");
    return out;
}
} // namespace okww::rust
