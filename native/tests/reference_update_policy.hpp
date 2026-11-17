#pragma once
#include "app_info.hpp"
#include "common.hpp"
#include "reference_update_version.hpp"
namespace okww::update::policy_reference {
using Version = version_reference::OriginalVersion;
struct OriginalRelease {
    QString tag, name, body;
    QUrl page, package, checksums;
    QByteArray digest;
    qint64 size = 0;
    bool prerelease = false;
    bool installable() const { return !package.isEmpty() && !checksums.isEmpty() && size > 0; }
};

using Release = OriginalRelease;
bool installationBusy(const QString &root) {
    const auto path = QDir::toNativeSeparators(root + "/.updates/install.lock");
    const auto file = CreateFileW(reinterpret_cast<const wchar_t *>(path.utf16()), GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
        return false;
    }
    const auto error = GetLastError();
    return error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION || error == ERROR_ACCESS_DENIED;
}
bool trustedDownload(const QUrl &url) {
    if (url.scheme() != "https" || !url.userInfo().isEmpty() || (url.port(-1) != -1 && url.port() != 443))
        return false;
    return QStringList{"github.com", "release-assets.githubusercontent.com", "objects.githubusercontent.com",
                       "github-releases.githubusercontent.com"}
        .contains(url.host());
}
QList<Release> newerReleases(const QJsonArray &json, const QString &current, bool includePreview) {
    const auto installed = Version::parse(current);
    if (!installed)
        throw std::runtime_error("当前软件版本标识无效");
    QList<Release> result;
    QSet<QString> seen;
    for (const auto &entry : json) {
        const auto object = entry.toObject();
        const auto tag = object["tag_name"].toString();
        const auto version = Version::parse(tag);
        if (!version || object["draft"].toBool() || object["published_at"].toString().isEmpty() ||
            (!includePreview && (object["prerelease"].toBool() || !version->pre.isEmpty())) ||
            version->compare(*installed) <= 0 || seen.contains(tag))
            continue;
        Release release;
        release.tag = tag;
        release.name = object["name"].toString(tag);
        release.body = object["body"].toString();
        release.page = QUrl(product::github + "/releases/tag/" + tag);
        release.prerelease = object["prerelease"].toBool() || !version->pre.isEmpty();
        const auto base = product::github + "/releases/download/" + tag + '/';
        for (const auto &asset : object["assets"].toArray()) {
            const auto item = asset.toObject();
            if (item["state"] != "uploaded")
                continue;
            const auto name = item["name"].toString();
            const QUrl url(item["browser_download_url"].toString());
            if (url != QUrl(base + name))
                continue;
            if (name == "OK-WW-DualPC-full.zip") {
                release.package = url;
                release.size = item["size"].toInteger();
                const auto digest = item["digest"].toString();
                if (digest.startsWith("sha256:"))
                    release.digest = digest.mid(7).toLatin1().toLower();
            } else if (name == "SHA256SUMS.txt")
                release.checksums = url;
        }
        seen.insert(tag);
        result.append(release);
    }
    std::sort(result.begin(), result.end(), [](const Release &a, const Release &b) {
        return Version::parse(a.tag)->compare(*Version::parse(b.tag)) > 0;
    });
    return result;
}
QString summary(const QString &body) {
    QStringList lines;
    for (auto line : body.split('\n')) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith("下载") || line.startsWith("源码") ||
            line.startsWith("反馈") || line.startsWith("更新时") || line.startsWith("验证"))
            continue;
        lines.append(line.left(240));
        if (lines.size() == 6)
            break;
    }
    return lines.isEmpty() ? "此版本未填写更新摘要，可在 GitHub 发布页查看。" : lines.join('\n');
}
QByteArray packageChecksum(const QByteArray &text) {
    QByteArray found;
    const QRegularExpression pattern(R"(^([0-9A-Fa-f]{64})\s+\*?OK-WW-DualPC-full\.zip$)");
    for (const auto &line : QString::fromUtf8(text).split('\n')) {
        const auto match = pattern.match(line.trimmed());
        if (!match.hasMatch())
            continue;
        if (!found.isEmpty())
            throw std::runtime_error("校验文件包含重复的完整包记录");
        found = match.captured(1).toLatin1().toLower();
    }
    if (found.isEmpty())
        throw std::runtime_error("发布版本缺少完整包 SHA256 校验值");
    return found;
}

} // namespace okww::update::policy_reference
