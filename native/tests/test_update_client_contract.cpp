#ifndef Q_MOC_RUN
#include "reference_update_client.hpp"
#endif
#include <QUrlQuery>
#include <QtTest>
#include <type_traits>
using namespace okww;
namespace okww::update {
struct UpdateClientContractAccess {
    template <class T> static QJsonObject state(T &client) {
        return {{"busy", client.busy_},
                {"reply", bool(client.reply_)},
                {"output", client.output_ != nullptr},
                {"deadline", client.deadline_.isActive()},
                {"single_shot", client.deadline_.isSingleShot()},
                {"received", QString::number(client.received_)},
                {"buffer", QString::fromLatin1(
                               QCryptographicHash::hash(client.buffer_, QCryptographicHash::Sha256).toHex())},
                {"expected", QString::fromLatin1(client.expected_)},
                {"selected", client.selected_.tag},
                {"current", client.current_},
                {"previews", client.previews_},
                {"page", client.page_},
                {"releases", client.releases_}};
    }
    template <class T> static void get(T &client, QUrl url, qint64 limit, std::function<void()> done) {
        client.get(url, limit, std::move(done));
    }
    template <class T> static void timeout(T &client) {
        QMetaObject::invokeMethod(&client.deadline_, "timeout", Qt::DirectConnection);
    }
};
} // namespace okww::update
namespace {
QJsonValue normalized(QJsonValue value, const QString &root) {
    if (value.isString()) {
        auto out = value.toString();
        out.replace(root, "<fixture>");
        out.replace(QDir::toNativeSeparators(root), "<fixture>");
        // The endpoint moved from the old slug to the fixed repository ID;
        // callback order, pagination, errors and download state stay identical.
        const QString oldEndpoint = "https://api.github.com/repos/xueyuannnn/OK-WW-DualPC/releases?";
        if (out.startsWith(oldEndpoint))
            out = "https://api.github.com/repositories/" + QString::number(product::githubRepositoryId) +
                  "/releases?" + out.mid(oldEndpoint.size());
        return out;
    }
    if (value.isArray()) {
        QJsonArray out;
        for (auto v : value.toArray())
            out.append(normalized(v, root));
        return out;
    }
    if (value.isObject()) {
        auto out = value.toObject();
        for (auto it = out.begin(); it != out.end(); ++it)
            it.value() = normalized(it.value(), root);
        return out;
    }
    return value;
}
class OfflineReply final : public QNetworkReply {
    QByteArray body_;
    qint64 offset_ = 0;
    QJsonArray *trace_;

  public:
    OfflineReply(const QNetworkRequest &request, QByteArray body, int status, QUrl final, QJsonArray *trace)
        : body_(std::move(body)), trace_(trace) {
        setRequest(request);
        setUrl(final);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (status != 200)
            setError(QNetworkReply::ContentAccessDenied, "fixture HTTP error");
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] {
            emit readyRead();
            setFinished(true);
            emit finished();
        });
    }
    void abort() override { trace_->append("abort"); }
    qint64 bytesAvailable() const override {
        return body_.size() - offset_ + QNetworkReply::bytesAvailable();
    }
    qint64 readData(char *data, qint64 maximum) override {
        auto count = std::min(maximum, body_.size() - offset_);
        if (!count)
            return -1;
        memcpy(data, body_.constData() + offset_, size_t(count));
        offset_ += count;
        return count;
    }
};
QJsonObject releaseRecord(int i) {
    return {{"tag_name", QString("v0.2.%1").arg(1 + i % 3)},
            {"name", QString("版本%1").arg(i)},
            {"published_at", "2026-11-17"},
            {"body", "- 修复截图"},
            {"draft", false},
            {"prerelease", false},
            {"assets", QJsonArray{}}};
}
template <class T> QJsonObject clientReplay(int scene, int operation) {
    QTemporaryDir fixture;
    QJsonArray trace;
    bool terminal = false;
    QByteArray archive = "offline-package-" + QByteArray::number(scene);
    const auto digest = QCryptographicHash::hash(archive, QCryptographicHash::Sha256).toHex();
    auto checksum = digest + "  OK-WW-DualPC-full.zip\n";
    if (scene % 11 == 0)
        checksum = "invalid checksum";
    if (scene % 17 == 0)
        checksum += checksum;
    const auto transport = [&](const QNetworkRequest &request) -> QNetworkReply * {
        trace.append(
            QJsonObject{{"request", request.url().toString()},
                        {"user_agent", QString::fromUtf8(request.rawHeader("User-Agent"))},
                        {"accept", QString::fromUtf8(request.rawHeader("Accept"))},
                        {"redirect", request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt()}});
        QByteArray body;
        const auto url = request.url();
        if (operation == 1) {
            if (url.path().endsWith("SHA256SUMS.txt"))
                body = checksum;
            else {
                body = archive;
                if (scene % 7 == 0)
                    body[0] = 'X';
                if (scene % 29 == 0)
                    body += "extra";
            }
        } else {
            QJsonArray entries;
            const int page = QUrlQuery(url).queryItemValue("page").toInt();
            const int count = scene % 61 == 0 ? 100 : scene % 5 == 0 && page < 3 ? 100 : scene % 7;
            for (int i = 0; i < count; ++i)
                entries.append(releaseRecord(i));
            body = QJsonDocument(entries).toJson(QJsonDocument::Compact);
            if (scene % 19 == 0)
                body = "invalid json";
            if (scene % 23 == 0)
                body = "{}";
        }
        const int status = scene % 31 == 0 ? 403 : scene % 37 == 0 ? 429 : scene % 41 == 0 ? 500 : 200;
        const auto final = scene % 43 == 0   ? QUrl("https://untrusted.invalid/file")
                           : scene % 47 == 0 ? QUrl("https://release-assets.githubusercontent.com/file")
                                             : url;
        return new OfflineReply(request, body, status, final, &trace);
    };
    QJsonObject result;
    {
        T client(nullptr, transport);
        const auto sink = [&](QJsonObject event) {
            trace.append(normalized(event, fixture.path()));
            if (event.contains("checked") || event.contains("failed") || event.contains("downloaded"))
                terminal = true;
        };
        if constexpr (std::is_same_v<T, update::Client>) {
            QObject::connect(&client, &update::Client::checked, [&](QList<update::Release> releases) {
                QJsonArray list;
                for (const auto &v : releases)
                    list.append(QJsonObject{{"tag", v.tag},
                                            {"name", v.name},
                                            {"body", v.body},
                                            {"page", v.page.toString()},
                                            {"size", QString::number(v.size)}});
                sink({{"checked", list}});
            });
            QObject::connect(&client, &update::Client::failed,
                             [&](QString message) { sink({{"failed", message}}); });
            QObject::connect(&client, &update::Client::progress, [&](qint64 received, qint64 total) {
                sink({{"received", QString::number(received)}, {"total", QString::number(total)}});
            });
            QObject::connect(&client, &update::Client::downloaded, [&](QString path, QString hash) {
                sink({{"downloaded", path}, {"sha256", hash}});
            });
        } else
            client.sink = sink;
        if (operation == 1) {
            update::Release release;
            release.tag = "v0.2.1";
            release.package = QUrl(product::github + "/releases/download/v0.2.1/OK-WW-DualPC-full.zip");
            release.checksums = QUrl(product::github + "/releases/download/v0.2.1/SHA256SUMS.txt");
            release.size = archive.size() + (scene % 23 == 0 ? 1 : 0);
            release.digest = scene % 19 ? digest : QByteArray(64, 'f');
            if (scene % 53 == 0)
                release.package = QUrl("http://github.com/invalid");
            auto job = fixture.path();
            if (scene % 13 == 0) {
                job += "/blocked";
                QFile blocker(job);
                if (!blocker.open(QIODevice::WriteOnly))
                    throw std::runtime_error("Fixture write failed");
            }
            client.download(release, job);
            client.download(release, job);
        } else if (operation == 2) {
            update::UpdateClientContractAccess::get(
                client, QUrl("https://api.github.com/fixture"), scene % 2 ? 8 * 1024 * 1024 : 1, [&] {
                    if (scene % 3 == 0)
                        throw std::runtime_error("fixture callback failed");
                    sink({{"checked", QJsonArray{}}});
                });
        } else {
            client.check(scene % 29 ? "0.2.0-beta.8" : "invalid", scene % 2);
            client.check("9.9.9", false);
        }
        if (operation == 3) {
            client.cancel();
            terminal = true;
        }
        if (operation == 4)
            update::UpdateClientContractAccess::timeout(client);
        QElapsedTimer elapsed;
        elapsed.start();
        while (!terminal && elapsed.elapsed() < 5000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (!terminal)
            throw std::runtime_error("Offline update fixture timed out");
        result["state"] = update::UpdateClientContractAccess::state(client);
        const auto path = fixture.path() + "/package.zip";
        QFile package(path);
        result["package_exists"] = package.exists();
        if (package.open(QIODevice::ReadOnly))
            result["package"] = QString::fromLatin1(package.readAll().toHex());
    }
    // All replies are owned test objects; drain deferred deletes before their
    // trace sink is destroyed. No network access is performed by this fixture.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    result["trace"] = trace;
    return normalized(result, fixture.path()).toObject();
}
} // namespace
class UpdateClientContracts final : public QObject {
    Q_OBJECT
  private slots:
    void callbacks_data() {
        QTest::addColumn<int>("scene");
        QTest::addColumn<int>("operation");
        for (int op = 0; op < 5; ++op)
            for (int s = 1; s < 65; ++s)
                QTest::newRow(qPrintable(QString("%1-%2").arg(op).arg(s))) << s << op;
    }
    void callbacks() {
        QFETCH(int, scene);
        QFETCH(int, operation);
        QCOMPARE(clientReplay<update::Client>(scene, operation),
                 clientReplay<update::client_reference::OriginalClient>(scene, operation));
    }
};
QTEST_GUILESS_MAIN(UpdateClientContracts)
#include "test_update_client_contract.moc"
