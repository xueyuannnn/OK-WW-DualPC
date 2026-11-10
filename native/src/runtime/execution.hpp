#pragma once
#include "common.hpp"
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
    std::atomic<uint64_t> epoch_{0};
    std::atomic<bool> running_{false};

  public:
    uint64_t start() {
        running_ = false;
        auto e = ++epoch_;
        running_ = true;
        return e;
    }
    void stop() {
        running_ = false;
        ++epoch_;
    }
    bool active(uint64_t epoch) const { return running_ && epoch_ == epoch; }
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
    void checkpoint() const {
        if (!gate_.active(epoch_))
            throw Cancelled();
        if (health_)
            health_();
        if (!gate_.active(epoch_))
            throw Cancelled();
    }
    void wait(double seconds) {
        if (!std::isfinite(seconds) || seconds < 0 || seconds > 86400)
            fail("等待时间无效");
        const double end = now() + seconds;
        checkpoint();
        while (now() < end) {
            clock_.sleep(std::min(.01, end - now()));
            checkpoint();
        }
    }
    bool until(const std::function<bool()> &condition, double timeout, double interval = .02) {
        if (!std::isfinite(timeout) || timeout < 0 || interval <= 0)
            fail("等待条件参数无效");
        const double end = now() + timeout;
        do {
            checkpoint();
            if (condition())
                return true;
            if (now() >= end)
                return false;
            wait(std::min(interval, end - now()));
        } while (true);
    }
};
// Matches Python round(), including its tie-to-even behavior, for HID coordinates.
inline int pixelRound(double value) {
    if (!std::isfinite(value) || value < INT_MIN || value > INT_MAX)
        fail("坐标或移动距离无效");
    const double floor = std::floor(value), fraction = value - floor;
    return int(fraction < .5                        ? floor
               : fraction > .5                      ? floor + 1
               : std::fmod(std::abs(floor), 2) == 0 ? floor
                                                    : floor + 1);
}
} // namespace okww::runtime
