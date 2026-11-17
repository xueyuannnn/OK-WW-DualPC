#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <stdexcept>
namespace okww::rust {
enum class TaskOp : int32_t {
#define OKWW_TASKFLOW_OP(name, id) name = id,
#define OKWW_TASKFLOW_METHOD(name, id, count)
#include "../../rust/abi/taskflow.def"
#undef OKWW_TASKFLOW_METHOD
#undef OKWW_TASKFLOW_OP
};
enum class TaskMethod : int32_t {
#define OKWW_TASKFLOW_OP(name, id)
#define OKWW_TASKFLOW_METHOD(name, id, count) name = id,
#include "../../rust/abi/taskflow.def"
#undef OKWW_TASKFLOW_METHOD
#undef OKWW_TASKFLOW_OP
};
struct TaskHost {
    void *context;
    int32_t (*invoke)(void *, int32_t, const double *, size_t, double *out3);
    int32_t (*query)(void *, int32_t, const uint8_t *, size_t, const double *, size_t, double *out3);
};
static_assert(sizeof(TaskHost) == 24);
extern "C" int32_t okww_rs_taskflow_run(const TaskHost *, int32_t, const double *, size_t, double *out3);
extern "C" int32_t okww_rs_monthly_day(int32_t hour, int32_t valid, int32_t milliseconds);
extern "C" int32_t okww_rs_monthly_offset(int64_t seconds);
extern "C" int32_t okww_rs_continue_mask(const uint8_t *, size_t width, size_t height, size_t stride,
                                         uint8_t *, size_t outStride);
template <class Action>
std::array<double, 3> taskRun(TaskMethod method, std::span<const double> args, Action &&action) {
    struct Context {
        Action &action;
        std::exception_ptr error;
    } context{action, {}};
    const TaskHost host{&context,
                        [](void *opaque, int32_t op, const double *a, size_t, double *out) -> int32_t {
                            auto &c = *static_cast<Context *>(opaque);
                            try {
                                c.action(TaskOp(op), a, out);
                                return 1;
                            } catch (...) {
                                if (!c.error)
                                    c.error = std::current_exception();
                                return 0;
                            }
                        },
                        nullptr};
    std::array<double, 3> out{};
    const auto status = okww_rs_taskflow_run(&host, int(method), args.data(), args.size(), out.data());
    if (context.error)
        std::rethrow_exception(context.error);
    if (status != 1)
        throw std::runtime_error("Rust 任务适配接口异常");
    return out;
}
} // namespace okww::rust
