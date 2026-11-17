#pragma once
#include "common.hpp"
#include "platform_workflow.hpp"
#include <new>
#include <opencv2/core.hpp>
#include <optional>
#include <type_traits>
namespace okww::runtime::resource_test {
struct State {
    int scene, allocs = 0, frees = 0;
    QJsonArray trace;
    explicit State(int s) : scene(s) {}
};
inline thread_local State *active = nullptr;
inline BOOL GlobalMemoryStatusEx(LPMEMORYSTATUSEX memory) {
    active->trace.append(QJsonObject{{"status", int(memory->dwLength)}});
    memory->ullAvailPageFile = active->scene % 4 == 0   ? 0
                               : active->scene % 4 == 1 ? 4096
                               : active->scene % 4 == 2 ? (1ULL << 60)
                                                        : ~0ULL;
    return active->scene % 17 != 0;
}
inline LPVOID VirtualAlloc(LPVOID ptr, SIZE_T bytes, DWORD allocation, DWORD protection) {
    active->trace.append(QJsonObject{{"allocate", QString::number(quint64(bytes))},
                                     {"allocation", int(allocation)},
                                     {"protection", int(protection)},
                                     {"null", ptr == nullptr}});
    ++active->allocs;
    return bytes && active->scene % 11 ? reinterpret_cast<void *>(uintptr_t(1)) : nullptr;
}
inline BOOL VirtualFree(LPVOID ptr, SIZE_T bytes, DWORD mode) {
    active->trace.append(
        QJsonObject{{"free", double(uintptr_t(ptr))}, {"bytes", double(bytes)}, {"mode", int(mode)}});
    ++active->frees;
    return active->scene % 13 != 0 || active->frees > 1;
}
namespace original {

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
        if (resource_test::GlobalMemoryStatusEx(&memory) && memory.ullAvailPageFile / 4 >= bytes)
            buffer_ = resource_test::VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    }
    EmergencyMemory(const EmergencyMemory &) = delete;
    EmergencyMemory &operator=(const EmergencyMemory &) = delete;
    ~EmergencyMemory() { release(); }
    bool available() const { return buffer_ != nullptr; }
    bool release() noexcept {
        if (!buffer_ || !resource_test::VirtualFree(buffer_, 0, MEM_RELEASE))
            return false;
        buffer_ = nullptr;
        return true;
    }
    template <class Read, class Report> auto read(Read &&action, Report &&report) -> decltype(action()) {
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

} // namespace original
namespace current {

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
                                          out[0] = resource_test::GlobalMemoryStatusEx(&memory);
                                          break;
                                      case rust::WorkflowOp::ResourceCapacityAvailable:
                                          out[0] = memory.ullAvailPageFile / 4 >= bytes;
                                          break;
                                      case rust::WorkflowOp::ResourceAllocate:
                                          buffer_ = resource_test::VirtualAlloc(
                                              nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
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
                                                 out[0] = resource_test::VirtualFree(buffer_, 0, MEM_RELEASE);
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

} // namespace current
struct Nonmovable {
    int value;
    explicit Nonmovable(int v) : value(v) {}
    Nonmovable(const Nonmovable &) = delete;
    Nonmovable(Nonmovable &&) = delete;
};
struct ThrowingMove {
    int value;
    explicit ThrowingMove(int v) : value(v) {}
    ThrowingMove(const ThrowingMove &) = delete;
    ThrowingMove(ThrowingMove &&) { throw std::runtime_error("unexpected result move"); }
};
[[noreturn]] inline void throwFailure(int mode) {
    switch (mode) {
    case 0:
        throw std::bad_alloc();
    case 1:
        throw cv::Exception(cv::Error::StsNoMem, "fixture allocation", "read", "fixture", 1);
    case 2:
        throw std::runtime_error("std::bad_alloc fixture");
    case 3:
        throw std::runtime_error("ordinary fixture");
    case 4:
        throw 27;
    default:
        throw std::bad_alloc();
    }
}
template <class Reserve> QJsonObject outcome(int scene, int type) {
    State state(scene);
    active = &state;
    QJsonObject result;
    int calls = 0, reports = 0, value = 17;
    const size_t amounts[]{0, 4096, 64 * 1024 * 1024, size_t(1ULL << 54), std::numeric_limits<size_t>::max()};
    {
        Reserve reserve(amounts[scene % 5]);
        result["initial"] = reserve.available();
        auto checkpoint = [&] {
            state.trace.append("read");
            ++calls;
            const int failure = scene % 7;
            if (failure != 6 && (calls == 1 || failure == 5))
                throwFailure(failure);
        };
        auto report = [&] {
            state.trace.append("report");
            ++reports;
            if (scene % 19 == 0)
                throw std::runtime_error("report fixture");
        };
        try {
            switch (type) {
            case 0:
                result["value"] = reserve.read(
                    [&] {
                        checkpoint();
                        return 42;
                    },
                    report);
                break;
            case 1: {
                auto image = reserve.read(
                    [&] {
                        checkpoint();
                        return cv::Mat(2, 3, CV_8U, cv::Scalar(1));
                    },
                    report);
                result["value"] = QJsonArray{image.cols, image.rows, int(image.template at<uchar>(0, 0))};
                break;
            }
            case 2: {
                auto pointer = reserve.read(
                    [&] {
                        checkpoint();
                        return std::make_unique<int>(42);
                    },
                    report);
                result["value"] = *pointer;
                break;
            }
            case 3:
                reserve.read([&] { checkpoint(); }, report);
                result["value"] = true;
                break;
            case 4: {
                auto &reference = reserve.read(
                    [&]() -> int & {
                        checkpoint();
                        return value;
                    },
                    report);
                reference += 1;
                result["value"] = value;
                break;
            }
            case 5: {
                auto &&reference = reserve.read(
                    [&]() -> int && {
                        checkpoint();
                        return std::move(value);
                    },
                    report);
                reference += 1;
                result["value"] = value;
                break;
            }
            case 6: {
                auto object = reserve.read(
                    [&]() -> Nonmovable {
                        checkpoint();
                        return Nonmovable(42);
                    },
                    report);
                result["value"] = object.value;
                break;
            }
            case 7: {
                auto object = reserve.read(
                    [&]() -> ThrowingMove {
                        checkpoint();
                        return ThrowingMove(42);
                    },
                    report);
                result["value"] = object.value;
                break;
            }
            }
        } catch (const std::exception &e) {
            result["error"] = errorText(e);
        } catch (...) {
            result["error"] = "unknown";
        }
        result["after"] = reserve.available();
        result["reads"] = calls;
        result["reports"] = reports;
    }
    result["trace"] = state.trace;
    return result;
}
inline QJsonObject classify(int mode, bool rust) {
    std::exception_ptr error;
    switch (mode % 6) {
    case 0:
        error = std::make_exception_ptr(std::bad_alloc());
        break;
    case 1:
        error = std::make_exception_ptr(cv::Exception(cv::Error::StsNoMem, "fixture", "read", "fixture", 1));
        break;
    case 2:
        error = std::make_exception_ptr(
            cv::Exception(cv::Error::StsBadArg, "std::bad_alloc fixture", "read", "fixture", 1));
        break;
    case 3:
        error = std::make_exception_ptr(std::runtime_error("std::bad_alloc fixture"));
        break;
    case 4:
        error = std::make_exception_ptr(std::runtime_error("(-4:Insufficient memory) fixture"));
        break;
    default:
        error = std::make_exception_ptr(std::runtime_error("ordinary fixture"));
        break;
    }
    try {
        std::rethrow_exception(error);
    } catch (const std::exception &e) {
        return {{"memory", rust ? current::memoryFailure(e) : original::memoryFailure(e)},
                {"text", rust ? current::taskFailureText(e) : original::taskFailureText(e)}};
    }
}
} // namespace okww::runtime::resource_test
