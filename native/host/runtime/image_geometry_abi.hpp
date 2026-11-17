#pragma once
#include "execution.hpp"
namespace okww::rust {
extern "C" int32_t okww_rs_image_geometry(int32_t kind, const double *args, size_t count, int32_t *out4);
inline QRect imageGeometry(int kind, std::initializer_list<double> args) {
    int32_t out[4]{};
    const auto status = okww_rs_image_geometry(kind, args.begin(), args.size(), out);
    if (status != 1) {
        const QStringList errors{"坐标或移动距离无效", "游戏界面坐标无效", "游戏界面识别范围无效",
                                 "模板缩放尺寸无效", "Rust 图像坐标接口无效"};
        fail(errors.at(-status - 1));
    }
    return {out[0], out[1], out[2], out[3]};
}
} // namespace okww::rust
