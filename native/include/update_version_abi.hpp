#pragma once
#include <cstddef>
#include <cstdint>
namespace okww::rust {
struct ParsedVersion {
    uint64_t numbers[3];
    size_t preStart, preLength;
};
struct WideView {
    const uint16_t *data;
    size_t length;
};
struct VersionView {
    uint64_t numbers[3];
    const WideView *pre;
    size_t count;
};
static_assert(sizeof(ParsedVersion) == 40);
static_assert(sizeof(WideView) == 16);
static_assert(sizeof(VersionView) == 40);
extern "C" int32_t okww_rs_version_parse(const uint16_t *, size_t, ParsedVersion *);
extern "C" int32_t okww_rs_version_compare(const VersionView *, const VersionView *, int32_t *);
} // namespace okww::rust
