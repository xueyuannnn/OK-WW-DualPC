#pragma once
#include "character.hpp"
#include "scripts.hpp"
namespace okww::rust {
enum class EngineOp : int32_t {
#define OKWW_ENGINE_OP(name, id) name = id,
#define OKWW_ENGINE_METHOD(name, id, count)
#include "../../rust/abi/engine.def"
#undef OKWW_ENGINE_METHOD
#undef OKWW_ENGINE_OP
};
enum class EngineMethod : int32_t {
#define OKWW_ENGINE_OP(name, id)
#define OKWW_ENGINE_METHOD(name, id, count) name = id,
#include "../../rust/abi/engine.def"
#undef OKWW_ENGINE_METHOD
#undef OKWW_ENGINE_OP
};
struct EngineHost {
    void *context;
    int32_t (*invoke)(void *, int32_t, const double *, size_t, double *out3);
};
static_assert(sizeof(EngineHost) == 16);
extern "C" int32_t okww_rs_engine_run(const EngineHost *, int32_t, const double *, size_t, double *out3);
extern "C" int32_t okww_rs_engine_command_kind(const uint8_t *, size_t);
extern "C" int32_t okww_rs_engine_script_key(const uint8_t *, size_t);
// Synchronous SDK binding only. Exception identity never crosses allocators or
// unwinds into Rust; the scheduling policy decides which failures it consumes.
template <class Action> double engineRun(EngineMethod method, Action &&action) {
    struct Context {
        Action &action;
        std::exception_ptr error, handled;
    } context{action, {}, {}};
    const EngineHost host{
        &context, [](void *opaque, int32_t operation, const double *args, size_t, double *out) -> int32_t {
            auto &c = *static_cast<Context *>(opaque);
            const auto op = static_cast<EngineOp>(operation);
            try {
                if (op == EngineOp::ExceptionKind) {
                    try {
                        if (c.error)
                            std::rethrow_exception(c.error);
                        out[0] = 0;
                    } catch (const runtime::Cancelled &) {
                        out[0] = 1;
                    } catch (const runtime::CombatEnded &) {
                        out[0] = 2;
                    } catch (const runtime::ScriptDisabled &) {
                        out[0] = 4;
                    } catch (const std::exception &) {
                        out[0] = 3;
                    } catch (...) {
                        out[0] = 5;
                    }
                } else if (op == EngineOp::ClearException) {
                    c.handled = std::exchange(c.error, {});
                } else
                    c.action(op, args, out, c.error ? c.error : c.handled);
                return 1;
            } catch (...) {
                if (!c.error)
                    c.error = std::current_exception();
                return 0;
            }
        }};
    double out[3]{};
    const auto status = okww_rs_engine_run(&host, int(method), nullptr, 0, out);
    if (context.error)
        std::rethrow_exception(context.error);
    if (status != 1)
        throw std::runtime_error("Rust 执行器接口异常");
    return out[0];
}
} // namespace okww::rust
