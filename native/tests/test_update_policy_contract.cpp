#ifndef Q_MOC_RUN
#include "reference_update_policy.hpp"
#endif
#include <QtTest>
#include <array>
using namespace okww;
namespace {
template <class Release> QJsonObject serialized(const Release &v) {
    return {{"tag", v.tag},
            {"name", v.name},
            {"body", v.body},
            {"page", v.page.toString()},
            {"package", v.package.toString()},
            {"checksums", v.checksums.toString()},
            {"digest", QString::fromLatin1(v.digest)},
            {"size", QString::number(v.size)},
            {"prerelease", v.prerelease},
            {"installable", v.installable()}};
}
template <class F> QJsonObject releaseOutcome(F &&fn) {
    try {
        QJsonArray out;
        for (const auto &v : fn())
            out.append(serialized(v));
        return {{"releases", out}};
    } catch (const std::exception &e) {
        return {{"error", QString::fromUtf8(e.what())}};
    }
}
QString checksum(QByteArray text, bool rust) {
    try {
        return QString::fromLatin1(rust ? update::packageChecksum(text)
                                        : update::policy_reference::packageChecksum(text));
    } catch (const std::exception &e) {
        return "ERROR:" + QString::fromUtf8(e.what());
    }
}
} // namespace
class UpdatePolicyContracts final : public QObject {
    Q_OBJECT
  private slots:
    void urls_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 2048; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void urls() {
        QFETCH(int, scene);
        const QStringList hosts{"github.com",
                                "GITHUB.COM",
                                "release-assets.githubusercontent.com",
                                "objects.githubusercontent.com",
                                "github-releases.githubusercontent.com",
                                "github.com.evil.invalid",
                                "github.com.",
                                "api.github.com",
                                "127.0.0.1",
                                "",
                                "用户.测试"},
            schemes{"https", "http", "HTTPS", "file", ""}, ports{"", ":443", ":444", ":-1", ":abc"},
            users{"", "user@", "user:pass@"};
        const QUrl url(schemes[(scene / 7) % schemes.size()] + "://" + users[(scene / 13) % users.size()] +
                       hosts[scene % hosts.size()] + ports[(scene / 11) % ports.size()] + "/file?sig=abc");
        QCOMPARE(update::trustedDownload(url), update::policy_reference::trustedDownload(url));
    }
    void summaries_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 1024; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void summaries() {
        QFETCH(int, scene);
        const QStringList parts{"",
                                "# Heading",
                                "下载完整包",
                                "源码包",
                                "反馈邮箱",
                                "更新时备份",
                                "验证通过",
                                "修复队伍切换",
                                "- Improve capture",
                                "  界面调整  ",
                                QString(245, QChar('a')),
                                QString(QChar(char16_t(0xd800))),
                                "\t",
                                "\r"};
        QStringList lines;
        for (int i = 0; i < scene % 21; ++i)
            lines << parts[(scene + i * 5) % parts.size()];
        const auto text = lines.join('\n');
        QCOMPARE(update::summary(text), update::policy_reference::summary(text));
    }
    void checksums_data() { summaries_data(); }
    void checksums() {
        QFETCH(int, scene);
        const QByteArray hash(64, scene % 2 ? 'A' : 'b');
        const QList<QByteArray> lines{hash + "  OK-WW-DualPC-full.zip",
                                      hash + " *OK-WW-DualPC-full.zip",
                                      hash + "\tOK-WW-DualPC-full.zip",
                                      hash + "  other.zip",
                                      QByteArray(63, 'a') + "  OK-WW-DualPC-full.zip",
                                      hash + " OK-WW-DualPC-full.zip.extra",
                                      QByteArray("\xff invalid"),
                                      "",
                                      " \r"};
        QByteArray text;
        for (int i = 0; i < scene % 9; ++i)
            text += lines[(scene + i * 3) % lines.size()] + '\n';
        QCOMPARE(checksum(text, true), checksum(text, false));
    }
    void releases_data() { summaries_data(); }
    void releases() {
        QFETCH(int, scene);
        const QStringList tags{"v0.2.0-beta.8",   "v0.2.0-beta.9", "v0.2.0",  "v0.2.1",        "v1.0.0",
                               "v0.2.0+metadata", "0.2.0+other",   "invalid", "0.2.0-beta.01", "1.0.0\n"};
        QJsonArray entries;
        for (int i = 0; i < scene % 21; ++i) {
            const auto tag = tags[(scene + i * 7) % tags.size()],
                       base = product::github + "/releases/download/" + tag + '/';
            QJsonArray assets;
            for (int j = 0; j < 1 + (scene + i) % 5; ++j) {
                const auto name =
                    QStringList{"OK-WW-DualPC-full.zip", "SHA256SUMS.txt", "other.zip"}[(scene + i + j) % 3];
                QJsonObject asset{{"name", name},
                                  {"state", (scene + i + j) % 7 ? "uploaded" : "uploading"},
                                  {"browser_download_url", base + name},
                                  {"size", qint64((scene + i + j) % 11 ? 123 : -1)},
                                  {"digest", (scene + i + j) % 3
                                                 ? QString("sha256:") + QString(64, QChar('A'))
                                                 : QString("not-sha256")}};
                if ((scene + i + j) % 13 == 0)
                    asset["browser_download_url"] = "https://github.com/other/repo/file";
                assets.append(asset);
            }
            entries.append(QJsonObject{{"tag_name", tag},
                                       {"name", QString("版本 %1").arg(i)},
                                       {"body", "更新内容"},
                                       {"draft", (scene + i) % 17 == 0},
                                       {"published_at", (scene + i) % 19 ? "2026-11-17" : ""},
                                       {"prerelease", (scene + i) % 5 == 0},
                                       {"assets", assets}});
        }
        const auto current = scene % 31 ? QString("0.2.0-beta.8") : QString("invalid");
        const bool previews = scene % 2;
        QCOMPARE(releaseOutcome([&] { return update::newerReleases(entries, current, previews); }),
                 releaseOutcome(
                     [&] { return update::policy_reference::newerReleases(entries, current, previews); }));
    }
    void installable_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 128; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void installable() {
        QFETCH(int, scene);
        const std::array<qint64, 8> sizes{
            INT64_MIN, -1, 0, 1, 123, INT64_C(4294967296), INT64_C(9007199254740993), INT64_MAX};
        update::Release current;
        update::policy_reference::OriginalRelease old;
        current.package = old.package = scene & 1 ? QUrl("https://github.com/file") : QUrl{};
        current.checksums = old.checksums = scene & 2 ? QUrl("https://github.com/checksums") : QUrl{};
        current.size = old.size = sizes[(scene / 4) % sizes.size()];
        QCOMPARE(current.installable(), old.installable());
    }
    void lock() {
        QTemporaryDir root;
        QCOMPARE(update::installationBusy(root.path()),
                 update::policy_reference::installationBusy(root.path()));
        QDir().mkpath(root.path() + "/.updates");
        const auto path = root.path() + "/.updates/install.lock";
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("lock");
        }
        QCOMPARE(update::installationBusy(root.path()),
                 update::policy_reference::installationBusy(root.path()));
        auto file = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, 0, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(file != INVALID_HANDLE_VALUE);
        const auto current = update::installationBusy(root.path()),
                   original = update::policy_reference::installationBusy(root.path());
        CloseHandle(file);
        QCOMPARE(current, original);
        QVERIFY(current);
        QVERIFY(QFile::remove(path));
        QVERIFY(QDir().mkdir(path));
        QCOMPARE(update::installationBusy(root.path()),
                 update::policy_reference::installationBusy(root.path()));
    }
};
QTEST_GUILESS_MAIN(UpdatePolicyContracts)
#include "test_update_policy_contract.moc"
