#pragma once
#include "common.hpp"
#include "rust_core.hpp"
#include <functional>

namespace okww::runtime {
class Cancelled final : public std::runtime_error {
  public:
    Cancelled() : std::runtime_error("任务已取消或暂停") {}
};
class Clock {
  public:
    virtual ~Clock() = default;
    virtual double now() const = 0;
    virtual void sleep(double seconds) = 0;
};
class SteadyClock final : public Clock {
  public:
    double now() const override { return double(nowNs()) / 1e9; }
    void sleep(double seconds) override {
        std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    }
};
// Every execution owns an epoch. A paused stack cannot resume after a new start.
class ExecutionGate {
    rust::Gate *state_;

  public:
    ExecutionGate() : state_(nullptr) {
        rust::verifyAbi();
        state_=rust::okww_rs_gate_new();
        if (!state_) throw std::bad_alloc();
    }
    ~ExecutionGate() { rust::okww_rs_gate_free(state_); }
    ExecutionGate(const ExecutionGate &) = delete;
    ExecutionGate &operator=(const ExecutionGate &) = delete;
    uint64_t start() {
        return rust::okww_rs_gate_start(state_);
    }
    void stop() {
        rust::okww_rs_gate_stop(state_);
    }
    bool active(uint64_t epoch) const { return rust::okww_rs_gate_active(state_,epoch) != 0; }
    const rust::Gate *nativeHandle() const { return state_; }
};
class Execution {
    ExecutionGate &gate_;
    uint64_t epoch_;
    Clock &clock_;
    std::function<void()> health_;

  public:
    Execution(ExecutionGate &gate, uint64_t epoch, Clock &clock, std::function<void()> health = {})
        : gate_(gate), epoch_(epoch), clock_(clock), health_(std::move(health)) {}
    double now() const { return clock_.now(); }
    void checkpoint() const;
    void wait(double seconds);
    bool until(const std::function<bool()> &condition, double timeout, double interval = .02);
};
// Matches Python round(), including its tie-to-even behavior, for HID coordinates.
inline int pixelRound(double value) {
    int32_t result = 0;
    if (!rust::okww_rs_pixel_round(value,&result))
        fail("坐标或移动距离无效");
    return result;
}
} // namespace okww::runtime
