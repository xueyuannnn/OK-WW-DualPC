#pragma once
#include <QString>
#include "release_version.hpp"

namespace okww::product {
inline const QString name = QStringLiteral("OK-WW-DualPC");
inline const QString title = QStringLiteral("OK-WW-DualPC · OK-WW双机版");
inline const QString version = QStringLiteral("0.2.0");
inline const QString releaseVersion = QStringLiteral(OKWW_RELEASE_VERSION);
inline const QString github = QStringLiteral("https://github.com/xueyuannnn/okww-dualpc");
inline const QString legacyGithub = QStringLiteral("https://github.com/xueyuannnn/OK-WW-DualPC");
inline constexpr qint64 githubRepositoryId = 1452761111;
inline const QString supportEmail = QStringLiteral("lidada0202@gmail.com");
inline QString versionLabel() { return "版本 " + releaseVersion; }
} // namespace okww::product
