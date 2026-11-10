#pragma once
#include "common.hpp"
#include <opencv2/core.hpp>
#include <new>

namespace okww::runtime {
inline bool memoryFailure(const std::exception &error) {
    if (dynamic_cast<const std::bad_alloc *>(&error))
        return true;
    const auto cvError = dynamic_cast<const cv::Exception *>(&error);
    if (cvError)
        return cvError->code == cv::Error::StsNoMem;
    const QByteArrayView text(error.what());
    return text.contains("(-4:Insufficient memory)") || text.contains("std::bad_alloc");
}
// This is committed emergency capacity, not locked physical RAM. Release it
// once per task to retry a read-only recognition step after an allocation error.
class EmergencyMemory {
    void *buffer_ = nullptr;
  public:
    static constexpr size_t capacity = 64 * 1024 * 1024;
    explicit EmergencyMemory(size_t bytes = capacity) noexcept {
        MEMORYSTATUSEX memory{sizeof(memory)};
        if (GlobalMemoryStatusEx(&memory) && memory.ullAvailPageFile / 4 >= bytes)
            buffer_ = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    }
    EmergencyMemory(const EmergencyMemory &) = delete;
    EmergencyMemory &operator=(const EmergencyMemory &) = delete;
    ~EmergencyMemory() { release(); }
    bool available() const { return buffer_ != nullptr; }
    bool release() noexcept {
        if (!buffer_ || !VirtualFree(buffer_, 0, MEM_RELEASE))
            return false;
        buffer_ = nullptr;
        return true;
    }
    template<class Read, class Report> auto read(Read &&action, Report &&report) -> decltype(action()) {
        try {
            return action();
        } catch (const std::exception &error) {
            if (!memoryFailure(error) || !release())
                throw;
            report();
            return action();
        }
    }
};
inline QString taskFailureText(const std::exception &error) {
    if (memoryFailure(error))
        return "系统内存不足，当前任务已停止。请腾出系统内存或减少同时运行的程序后，再启动任务。";
    return errorText(error);
}
} // namespace okww::runtime
