#pragma once
#include <cstdint>

namespace okww::runtime {
// Native modules share C++ object layouts with the host. Bump this independently
// of the package/manifest format when those layouts change.
inline constexpr uint32_t nativeTeamAbi = 2;
}
