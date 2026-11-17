#pragma once
#include "common.hpp"
#include <span>
namespace okww::rust {
enum class SessionOp : int32_t {
#define OKWW_SESSION_OP(name, id) name = id,
#define OKWW_SESSION_METHOD(name, id, count)
#include "session.def"
#undef OKWW_SESSION_METHOD
#undef OKWW_SESSION_OP
};
enum class SessionMethod : int32_t {
#define OKWW_SESSION_OP(name, id)
#define OKWW_SESSION_METHOD(name, id, count) name = id,
#include "session.def"
#undef OKWW_SESSION_METHOD
#undef OKWW_SESSION_OP
};
struct SessionHost {
    void *context;
    int32_t (*invoke)(void *, int32_t, const double *, size_t, double *);
};
static_assert(sizeof(SessionHost) == 16);
extern "C" int32_t okww_rs_session_run(const SessionHost *, int32_t, const double *, size_t, double *);
int32_t sessionException(const std::exception_ptr &) noexcept;
template <class Action>
double sessionRun(SessionMethod method, std::span<const double> args, Action &&action) {
    struct Context {
        Action &action;
        std::exception_ptr error, handled;
    } context{action, {}, {}};
    const SessionHost host{
        &context, [](void *opaque, int32_t operation, const double *args, size_t, double *out) -> int32_t {
            auto &c = *static_cast<Context *>(opaque);
            const auto op = SessionOp(operation);
            try {
                if (op == SessionOp::ExceptionKind)
                    out[0] = sessionException(c.error);
                else if (op == SessionOp::ClearException)
                    c.handled = std::exchange(c.error, {});
                else if (op == SessionOp::RethrowHandled)
                    std::rethrow_exception(c.handled);
                else
                    c.action(op, args, out, c.error ? c.error : c.handled);
                return 1;
            } catch (...) {
                if (!c.error)
                    c.error = std::current_exception();
                return 0;
            }
        }};
    double out[3]{};
    const auto status = okww_rs_session_run(&host, int(method), args.data(), args.size(), out);
    if (context.error)
        std::rethrow_exception(context.error);
    if (status != 1)
        throw std::runtime_error("Rust 战斗会话接口异常");
    return out[0];
}
} // namespace okww::rust
