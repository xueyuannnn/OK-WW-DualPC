#pragma once
#include <QString>

namespace okww::product {
inline const QString name = QStringLiteral("OK-WW-DualPC");
inline const QString title = QStringLiteral("OK-WW-DualPC · OK-WW双机版");
inline const QString version = QStringLiteral("0.2.0");
inline const QString supportEmail = QStringLiteral("lidada0202@gmail.com");
inline QString versionLabel() { return "版本 " + version + " · 预览版"; }
} // namespace okww::product
