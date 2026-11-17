#pragma once
#include <QtCore>
#include <QtNetwork>
#include <optional>
#include <span>

namespace okww::update {
struct Version {
    quint64 major = 0, minor = 0, patch = 0;
    QStringList pre;
    static std::optional<Version> parse(QString text);
    int compare(const Version &other) const;
};
struct Release {
    QString tag, name, body;
    QUrl page, package, checksums;
    QByteArray digest;
    qint64 size = 0;
    bool prerelease = false;
    bool installable() const;
};
QList<Release> newerReleases(const QJsonArray &json, const QString &current, bool includePreview);
QString summary(const QString &body);
QByteArray packageChecksum(const QByteArray &text);
bool trustedDownload(const QUrl &url);
bool installationBusy(const QString &root);

class Client final : public QObject {
    Q_OBJECT
    QNetworkAccessManager network_;
    std::function<QNetworkReply *(const QNetworkRequest &)> transport_;
    QPointer<QNetworkReply> reply_;
    QTimer deadline_;
    QSaveFile *output_ = nullptr;
    QCryptographicHash hash_{QCryptographicHash::Sha256};
    QByteArray buffer_, expected_;
    QJsonArray releases_;
    Release selected_;
    QString current_, job_;
    bool busy_ = false, previews_ = true;
    int page_ = 1;
    qint64 received_ = 0;
    friend struct UpdateClientContractAccess;
    struct UpdateCall {
        std::span<const double> args{};
        const QUrl *url = nullptr;
        std::function<void()> *ownedDone = nullptr;
        const std::function<void()> *callback = nullptr;
        const QString *text = nullptr;
        const Release *release = nullptr;
    };
    void runUpdate(int method, UpdateCall call);
    void get(const QUrl &url, qint64 limit, std::function<void()> done, bool file = false);
    void getPage();
    void fail(const QString &message);

  public:
    explicit Client(QObject *parent = nullptr,
                    std::function<QNetworkReply *(const QNetworkRequest &)> transport = {});
    ~Client() override { cancel(); }
    bool busy() const { return busy_; }
    void check(const QString &current, bool includePreview = true);
    void download(const Release &release, const QString &job);
    void cancel();
  signals:
    void checked(QList<okww::update::Release> releases);
    void failed(QString error);
    void progress(qint64 received, qint64 total);
    void downloaded(QString archive, QString sha256);
};
} // namespace okww::update
Q_DECLARE_METATYPE(okww::update::Release)
