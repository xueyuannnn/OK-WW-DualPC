#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <stdexcept>
namespace okww::rust {
enum class VisionOp : int32_t {
#define OKWW_VISION_OP(name, id) name = id,
#define OKWW_VISION_METHOD(name, id, count)
#include "../../rust/abi/visionflow.def"
#undef OKWW_VISION_METHOD
#undef OKWW_VISION_OP
};
enum class VisionMethod : int32_t {
#define OKWW_VISION_OP(name, id)
#define OKWW_VISION_METHOD(name, id, count) name = id,
#include "../../rust/abi/visionflow.def"
#undef OKWW_VISION_METHOD
#undef OKWW_VISION_OP
};
struct VisionHost {
    void *context;
    int32_t (*invoke)(void *, int32_t, const double *, size_t, double *);
};
static_assert(sizeof(VisionHost) == 16);
extern "C" int32_t okww_rs_vision_run(const VisionHost *, int32_t, const double *, size_t, double *);
template <class Action>
std::array<double, 3> visionRun(VisionMethod method, std::span<const double> args, Action &&action) {
    struct Context {
        Action &action;
        std::exception_ptr error;
    } context{action, {}};
    const VisionHost host{&context,
                          [](void *opaque, int32_t op, const double *a, size_t, double *out) -> int32_t {
                              auto &c = *static_cast<Context *>(opaque);
                              try {
                                  c.action(VisionOp(op), a, out);
                                  return 1;
                              } catch (...) {
                                  if (!c.error)
                                      c.error = std::current_exception();
                                  return 0;
                              }
                          }};
    std::array<double, 3> out{};
    const auto status = okww_rs_vision_run(&host, int(method), args.data(), args.size(), out.data());
    if (context.error)
        std::rethrow_exception(context.error);
    if (status != 1)
        throw std::runtime_error("Rust 识别接口异常");
    return out;
}
} // namespace okww::rust
