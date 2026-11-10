#pragma once
#include "common.hpp"
#include <optional>

namespace okww {
// V4 exposes output routes. Older 3.2/3.7 firmware has a prompted mouse
// query instead; its identity alone never proves keyboard support.
inline std::optional<bool> makcuV4Keyboard(const QString &response) {
    const auto route = QRegularExpression("R:([MKC-]+);M:(\\d+)uf;K:(\\d+)uf;C:(\\d+)uf").match(response);
    if (!route.hasMatch()) return {};
    if (!route.captured(1).contains('M') || route.captured(2).toInt() <= 0)
        fail("MAKCU 未检测到鼠标输出路由。请检查 USB1 游戏机连接、USB3 鼠标及固件配置。");
    return route.captured(1).contains('K') && route.captured(3).toInt() > 0;
}

inline bool makcuLegacyMouseReply(const QString &response) {
    return QRegularExpression("(?:^|[\\r\\n]|>>>)\\s*km\\.left\\(\\)[\\r\\n]+[01][\\r\\n]+>>> ")
        .match(response).hasMatch();
}

inline void checkMakcuLegacyAcknowledgements(const QString &response, const QStringList &commands) {
    for (const auto &command : commands) {
        const auto pattern = QRegularExpression::escape(command) + "[\\r\\n]+>>> ";
        if (!QRegularExpression(pattern).match(response).hasMatch())
            fail("MAKCU 3.x 未确认指令，连接已中止；没有重发：" + command);
    }
}
} // namespace okww
