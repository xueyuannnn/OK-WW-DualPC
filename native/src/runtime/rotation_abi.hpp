#pragma once
#include "../rust_core.hpp"

namespace okww::rust {
enum class RotationOp : int32_t {
#define OKWW_ROTATION_OP(name, id) name = id,
#define OKWW_ROTATION_METHOD(name, id, count)
#include "../../rust/abi/rotation.def"
#undef OKWW_ROTATION_OP
#undef OKWW_ROTATION_METHOD
};
enum class RotationMethod : int32_t {
#define OKWW_ROTATION_OP(name, id)
#define OKWW_ROTATION_METHOD(name, id, count) name = id,
#include "../../rust/abi/rotation.def"
#undef OKWW_ROTATION_OP
#undef OKWW_ROTATION_METHOD
};
} // namespace okww::rust
