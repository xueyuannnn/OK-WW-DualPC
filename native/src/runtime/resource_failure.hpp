#pragma once
#include "../platform_workflow.hpp"
#include "common.hpp"
#include <new>
#include <opencv2/core.hpp>
#include <optional>
#include <type_traits>
namespace okww::runtime {
inline bool memoryFailure(const std::exception &error) {
    const cv::Exception *cvError = nullptr;
    QByteArrayView text;
    return rust::platformWorkflowRun(rust::WorkflowMethod::ResourceMemoryFailure, {},
                                     [&](rust::WorkflowOp op, const double *a, double *out) {
                                         switch (op) {
                                         case rust::WorkflowOp::ResourceIsBadAlloc:
                                             out[0] = dynamic_cast<const std::bad_alloc *>(&error) != nullptr;
                                             break;
                                         case rust::WorkflowOp::ResourceIsCvException:
                                             cvError = dynamic_cast<const cv::Exception *>(&error);
                                             out[0] = cvError != nullptr;
                                             break;
                                         case rust::WorkflowOp::ResourceCvNoMemory:
                                             out[0] = cvError->code == cv::Error::StsNoMem;
                                             break;
                                         case rust::WorkflowOp::ResourceReadErrorText:
                                             text = QByteArrayView(error.what());
                                             break;
                                         case rust::WorkflowOp::ResourceTextContains: {
                                             static const char *const patterns[]{"(-4:Insufficient memory)",
                                                                                 "std::bad_alloc"};
                                             out[0] = text.contains(patterns[int(a[0])]);
                                             break;
                                         }
                                         default:
                                             fail("Rust 内存异常识别宿主无效");
                                         }
                                     })[0] != 0;
}
namespace resource_detail {
template <class R> struct ResultSlot {
    std::optional<R> value;
    template <class Action> void invoke(Action &action) { value.emplace(action()); }
    R take() { return std::move(*value); }
};
template <class R> struct ResultSlot<R &> {
    R *value = nullptr;
    template <class Action> void invoke(Action &action) {
        auto &&reference = action();
        value = std::addressof(reference);
    }
    R &take() { return *value; }
};
template <class R> struct ResultSlot<R &&> {
    R *value = nullptr;
    template <class Action> void invoke(Action &action) {
        auto &&reference = action();
        value = std::addressof(reference);
    }
    R &&take() { return std::move(*value); }
};
template <> struct ResultSlot<void> {
    template <class Action> void invoke(Action &action) { action(); }
    void take() {}
};
} // namespace resource_detail
// Committed emergency capacity; it is released once to retry a read-only step.
class EmergencyMemory {
    void *buffer_ = nullptr;

  public:
    static constexpr size_t capacity = 64 * 1024 * 1024;
    explicit EmergencyMemory(size_t bytes = capacity) noexcept {
        MEMORYSTATUSEX memory{sizeof(memory)};
        rust::platformWorkflowRun(rust::WorkflowMethod::ResourceReserveCreate, {},
                                  [&](rust::WorkflowOp op, const double *, double *out) {
                                      switch (op) {
                                      case rust::WorkflowOp::ResourceMemoryStatus:
                                          out[0] = GlobalMemoryStatusEx(&memory);
                                          break;
                                      case rust::WorkflowOp::ResourceCapacityAvailable:
                                          out[0] = memory.ullAvailPageFile / 4 >= bytes;
                                          break;
                                      case rust::WorkflowOp::ResourceAllocate:
                                          buffer_ = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT,
                                                                 PAGE_READWRITE);
                                          break;
                                      default:
                                          fail("Rust 应急内存分配宿主无效");
                                      }
                                  });
    }
    EmergencyMemory(const EmergencyMemory &) = delete;
    EmergencyMemory &operator=(const EmergencyMemory &) = delete;
    ~EmergencyMemory() { release(); }
    bool available() const { return buffer_ != nullptr; }
    bool release() noexcept {
        return rust::platformWorkflowRun(rust::WorkflowMethod::ResourceReserveRelease, {},
                                         [&](rust::WorkflowOp op, const double *, double *out) {
                                             switch (op) {
                                             case rust::WorkflowOp::ResourceHasBuffer:
                                                 out[0] = buffer_ != nullptr;
                                                 break;
                                             case rust::WorkflowOp::ResourceFree:
                                                 out[0] = VirtualFree(buffer_, 0, MEM_RELEASE);
                                                 break;
                                             case rust::WorkflowOp::ResourceClearBuffer:
                                                 buffer_ = nullptr;
                                                 break;
                                             default:
                                                 fail("Rust 应急内存释放宿主无效");
                                             }
                                         })[0] != 0;
    }
    template <class Read, class Report> auto read(Read &&action, Report &&report) -> decltype(action()) {
        using R = decltype(action());
        if constexpr (std::is_void_v<R> || std::is_reference_v<R> ||
                      std::is_nothrow_move_constructible_v<R>) {
            resource_detail::ResultSlot<R> result;
            rust::platformWorkflowRun(
                rust::WorkflowMethod::ResourceReserveRead, {},
                [&](rust::WorkflowOp op, const double *, double *out, const std::exception_ptr &error) {
                    switch (op) {
                    case rust::WorkflowOp::ResourceReadAction:
                        result.invoke(action);
                        break;
                    case rust::WorkflowOp::ResourceCaughtMemoryFailure:
                        try {
                            std::rethrow_exception(error);
                        } catch (const std::exception &e) {
                            out[0] = memoryFailure(e);
                        }
                        break;
                    case rust::WorkflowOp::ResourceRelease:
                        out[0] = release();
                        break;
                    case rust::WorkflowOp::ResourceReport:
                        report();
                        break;
                    default:
                        fail("Rust 应急读取宿主无效");
                    }
                });
            return result.take();
        } else {
            // Keep guaranteed native return-value elision for nonmovable or
            // throwing-move result types; the policy and report remain in Rust.
            try {
                return action();
            } catch (const std::exception &error) {
                const bool retry =
                    rust::platformWorkflowRun(rust::WorkflowMethod::ResourceRetryNonmovable, {},
                                              [&](rust::WorkflowOp op, const double *, double *out) {
                                                  switch (op) {
                                                  case rust::WorkflowOp::ResourceCaughtMemoryFailure:
                                                      out[0] = memoryFailure(error);
                                                      break;
                                                  case rust::WorkflowOp::ResourceRelease:
                                                      out[0] = release();
                                                      break;
                                                  case rust::WorkflowOp::ResourceReport:
                                                      report();
                                                      break;
                                                  default:
                                                      fail("Rust 原生返回值重试宿主无效");
                                                  }
                                              })[0] != 0;
                if (!retry)
                    throw;
                return action();
            }
        }
    }
};
inline QString taskFailureText(const std::exception &error) {
    QString result;
    rust::platformWorkflowRun(
        rust::WorkflowMethod::ResourceFailureText, {}, [&](rust::WorkflowOp op, const double *, double *out) {
            switch (op) {
            case rust::WorkflowOp::ResourceGivenMemoryFailure:
                out[0] = memoryFailure(error);
                break;
            case rust::WorkflowOp::ResourceLowMemoryText:
                result = "系统内存不足，当前任务已停止。请腾出系统内存或减少同时运行的程序后，再启动任务。";
                break;
            case rust::WorkflowOp::ResourceOriginalText:
                result = errorText(error);
                break;
            default:
                fail("Rust 任务异常文案宿主无效");
            }
        });
    return result;
}
} // namespace okww::runtime
