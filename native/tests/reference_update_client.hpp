#pragma once
#include "app_info.hpp"
#include "common.hpp"
#include "update.hpp"
namespace okww::update::client_reference {
class OriginalClient final : public QObject {
  public:
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
    void get(const QUrl &url, qint64 limit, std::function<void()> done, bool file = false);
    void getPage();
    void fail(const QString &message);

  public:
    explicit OriginalClient(QObject *parent = nullptr,
                            std::function<QNetworkReply *(const QNetworkRequest &)> transport = {});
    ~OriginalClient() override { cancel(); }
    bool busy() const { return busy_; }
    void check(const QString &current, bool includePreview = true);
    void download(const Release &release, const QString &job);
    void cancel();

  public:
    std::function<void(QJsonObject)> sink;
    void checked(QList<okww::update::Release> releases);
    void failed(QString error);
    void progress(qint64 received, qint64 total);
    void downloaded(QString archive, QString sha256);
};
OriginalClient::OriginalClient(QObject *parent,
                               std::function<QNetworkReply *(const QNetworkRequest &)> transport)
    : QObject(parent), network_(this), transport_(std::move(transport)) {
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] { fail("连接 GitHub 超时，请检查网络后重试。"); });
}
void OriginalClient::fail(const QString &message) {
    cancel();
    emit failed(message);
}
void OriginalClient::cancel() {
    busy_ = false;
    deadline_.stop();
    if (reply_) {
        auto reply = reply_.data();
        reply_ = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (output_) {
        output_->cancelWriting();
        delete output_;
        output_ = nullptr;
    }
}
void OriginalClient::get(const QUrl &url, qint64 limit, std::function<void()> done, bool file) {
    buffer_.clear();
    received_ = 0;
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", (product::name + '/' + product::releaseVersion).toUtf8());
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);
    reply_ = transport_ ? transport_(request) : network_.get(request);
    reply_->setReadBufferSize(1024 * 1024);
    deadline_.start(file ? 30 * 60 * 1000 : 30000);
    connect(reply_, &QNetworkReply::readyRead, this, [this, limit, file] {
        const auto chunk = reply_->readAll();
        received_ += chunk.size();
        if (received_ > limit) {
            fail("GitHub 返回的文件超过预期大小。");
            return;
        }
        if (file) {
            if (!output_ || output_->write(chunk) != chunk.size()) {
                fail("无法写入更新包，请检查剩余磁盘空间。");
                return;
            }
            hash_.addData(chunk);
            emit progress(received_, selected_.size);
        } else
            buffer_.append(chunk);
    });
    connect(reply_, &QNetworkReply::finished, this, [this, done = std::move(done)] {
        auto reply = reply_.data();
        if (!reply)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto error = reply->error();
        const auto message = reply->errorString();
        const auto finalUrl = reply->url();
        reply_ = nullptr;
        reply->deleteLater();
        deadline_.stop();
        if (status != 200 || error != QNetworkReply::NoError) {
            fail(status == 403 || status == 429
                     ? "GitHub 请求次数已达上限，请稍后重试。"
                     : QString("无法获取更新（HTTP %1）：%2").arg(status).arg(message));
            return;
        }
        if (finalUrl.host() != "api.github.com" && !trustedDownload(finalUrl)) {
            fail("更新下载被重定向到非 GitHub 地址。");
            return;
        }
        try {
            done();
        } catch (const std::exception &e) {
            fail(QString::fromUtf8(e.what()));
        }
    });
}
void OriginalClient::check(const QString &current, bool includePreview) {
    if (busy_)
        return;
    busy_ = true;
    current_ = current;
    previews_ = includePreview;
    releases_ = {};
    page_ = 1;
    getPage();
}
void OriginalClient::getPage() {
    get(QUrl("https://api.github.com/repos/xueyuannnn/OK-WW-DualPC/releases?per_page=100&page=" +
             QString::number(page_)),
        8 * 1024 * 1024, [this] {
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(buffer_, &error);
            if (error.error != QJsonParseError::NoError || !document.isArray())
                throw std::runtime_error("GitHub 版本列表格式无效");
            const auto entries = document.array();
            for (const auto &entry : entries)
                releases_.append(entry);
            if (entries.size() == 100 && page_ < 10) {
                ++page_;
                getPage();
                return;
            }
            const auto result = newerReleases(releases_, current_, previews_);
            busy_ = false;
            emit checked(result);
        });
}
void OriginalClient::download(const Release &release, const QString &job) {
    if (busy_)
        return;
    if (!release.installable() || !trustedDownload(release.package) || !trustedDownload(release.checksums) ||
        release.size > 4LL * 1024 * 1024 * 1024) {
        emit failed("此版本未提供可用的完整包和校验文件，请前往 GitHub 下载。");
        return;
    }
    selected_ = release;
    job_ = job;
    busy_ = true;
    get(release.checksums, 128 * 1024, [this] {
        expected_ = packageChecksum(buffer_);
        if (!selected_.digest.isEmpty() && selected_.digest != expected_)
            throw std::runtime_error("发布附件与校验文件不一致，已取消更新");
        output_ = new QSaveFile(job_ + "/package.zip");
        if (!output_->open(QIODevice::WriteOnly))
            throw std::runtime_error("无法创建更新包文件");
        hash_.reset();
        get(
            selected_.package, selected_.size,
            [this] {
                if (received_ != selected_.size || hash_.result().toHex() != expected_)
                    throw std::runtime_error("更新包大小或 SHA256 校验失败，请重新下载");
                if (!output_->commit())
                    throw std::runtime_error("保存更新包失败，请检查剩余空间");
                delete output_;
                output_ = nullptr;
                busy_ = false;
                emit downloaded(job_ + "/package.zip", QString::fromLatin1(expected_));
            },
            true);
    });
}

void OriginalClient::checked(QList<okww::update::Release> releases) {
    QJsonArray list;
    for (const auto &v : releases)
        list.append(QJsonObject{{"tag", v.tag},
                                {"name", v.name},
                                {"body", v.body},
                                {"page", v.page.toString()},
                                {"size", QString::number(v.size)}});
    sink({{"checked", list}});
}
void OriginalClient::failed(QString error) { sink({{"failed", error}}); }
void OriginalClient::progress(qint64 received, qint64 total) {
    sink({{"received", QString::number(received)}, {"total", QString::number(total)}});
}
void OriginalClient::downloaded(QString archive, QString sha256) {
    sink({{"downloaded", archive}, {"sha256", sha256}});
}

} // namespace okww::update::client_reference
