#pragma once
#include "../workflow_types.hpp"
#include "combat_session.hpp"
#include "diagnostics.hpp"
#include "navigation.hpp"
#include <array>
#include <span>
#include <type_traits>
namespace okww::rust {
template <class Action>
std::array<double, 3> workflowRun(WorkflowMethod method, std::span<const double> args, Action &&action) {
    struct Context {
        Action &action;
        std::exception_ptr error, handled;
    } context{action, {}, {}};
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
                    } catch (const runtime::Cancelled &) {
                        out[0] = 1;
                    } catch (const runtime::NavigationFailure &) {
                        out[0] = 2;
                    } catch (const runtime::CharacterRevived &) {
                        out[0] = 3;
                    } catch (const runtime::CombatInterrupted &) {
                        out[0] = 4;
                    } catch (const runtime::CombatEnded &) {
                        out[0] = 5;
                    } catch (const runtime::ExecutionFailure &) {
                        out[0] = 8;
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
        throw std::runtime_error("Rust 任务流程接口异常");
    return out;
}
} // namespace okww::rust
