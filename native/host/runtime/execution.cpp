#include "execution.hpp"
namespace okww::runtime {
namespace {
void result(int status, const std::exception_ptr &error) {
    if (error)
        std::rethrow_exception(error);
    if (status == -4)
        throw Cancelled();
    if (status == -2)
        fail("等待时间无效");
    if (status == -3)
        fail("等待条件参数无效");
    if (status < 0)
        fail("Rust 执行接口异常");
}
struct Host {
    const Execution *execution;
    Clock *clock;
    const std::function<bool()> *condition = nullptr;
    std::exception_ptr error;
    template <class F> int32_t run(F &&function) noexcept {
        if (error)
            return 0;
        try {
            function();
            return 1;
        } catch (...) {
            error = std::current_exception();
            return 0;
        }
    }
    rust::ExecutionHost abi() {
        return {this,
                [](void *value, double *out) -> int32_t {
                    auto &host = *static_cast<Host *>(value);
                    return host.run([&] { *out = host.clock->now(); });
                },
                [](void *value, double seconds) -> int32_t {
                    auto &host = *static_cast<Host *>(value);
                    return host.run([&] { host.clock->sleep(seconds); });
                },
                [](void *value) -> int32_t {
                    auto &host = *static_cast<Host *>(value);
                    return host.run([&] { host.execution->checkpoint(); });
                },
                [](void *value, int32_t *out) -> int32_t {
                    auto &host = *static_cast<Host *>(value);
                    return host.run([&] { *out = (*host.condition)(); });
                }};
    }
};
} // namespace
void Execution::checkpoint() const {
    struct Health {
        const Execution *execution;
        std::exception_ptr error;
    } state{this, {}};
    const auto status =
        rust::okww_rs_checkpoint(gate_.nativeHandle(), epoch_, &state, [](void *value) -> int32_t {
            auto &state = *static_cast<Health *>(value);
            try {
                if (state.execution->health_)
                    state.execution->health_();
                return 1;
            } catch (...) {
                state.error = std::current_exception();
                return 0;
            }
        });
    result(status, state.error);
}
void Execution::wait(double seconds) {
    Host state{this, &clock_, nullptr, {}};
    const auto host = state.abi();
    result(rust::okww_rs_execution_wait(&host, seconds), state.error);
}
bool Execution::until(const std::function<bool()> &condition, double timeout, double interval) {
    Host state{this, &clock_, &condition, {}};
    const auto host = state.abi();
    const auto status = rust::okww_rs_execution_until(&host, timeout, interval);
    result(status, state.error);
    return status != 0;
}
} // namespace okww::runtime
