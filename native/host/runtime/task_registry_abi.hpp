#pragma once
#include "common.hpp"
namespace okww::rust {
extern "C" int32_t okww_rs_business_task_kind(const uint8_t *, size_t);
}
namespace okww::runtime {
inline int businessTaskKind(const QString &id) {
    const auto bytes = id.toUtf8();
    return rust::okww_rs_business_task_kind(reinterpret_cast<const uint8_t *>(bytes.constData()),
                                            size_t(bytes.size()));
}
} // namespace okww::runtime
