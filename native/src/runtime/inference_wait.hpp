#pragma once
#include "execution.hpp"
#include <openvino/openvino.hpp>

namespace okww::runtime {
inline void inferWithCheckpoint(ov::InferRequest &request, const std::function<void()> &checkpoint,
                                const QString &name) {
    if (!checkpoint) {
        request.infer();
        return;
    }
    checkpoint();
    QElapsedTimer elapsed;
    elapsed.start();
    request.start_async();
    try {
        // Progress is checked on the task thread, including under CPU load.
        // A stuck model cannot keep the device watchdog alive indefinitely.
        while (!request.wait_for(std::chrono::milliseconds(20))) {
            checkpoint();
            if (elapsed.elapsed() > 10000)
                fail(name + "推理超过 10 秒，自动任务已停止");
        }
        checkpoint();
    } catch (...) {
        const auto cause = std::current_exception();
        try { request.cancel(); } catch (...) {}
        try { request.wait(); } catch (...) {}
        std::rethrow_exception(cause);
    }
}
} // namespace okww::runtime
