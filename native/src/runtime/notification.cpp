#include "notification.hpp"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSslSocket>
#include <QCryptographicHash>

namespace okww::runtime {
namespace {
QString required(const QJsonObject &c, const char *key) {
    auto v = c[key].toString().trimmed();
    if (v.isEmpty())
        fail(QString("通知配置不完整：") + key);
    if (v.contains('\r') || v.contains('\n') || v.contains(QChar(0)))
        fail("通知配置中不能包含换行或空字符");
    return v;
}
QString content(const Notice &n) { return n.title.isEmpty() ? n.text : n.title + '\n' + n.text; }
QStringList chunks(QString text, int length) {
    QStringList result;
    while (!text.isEmpty()) {
        int count = std::min(length, int(text.size()));
        if (count < text.size() && text[count - 1].isHighSurrogate())
            --count;
        result << text.left(count);
        text.remove(0, count);
    }
    return result;
}
QUrl webhook(QString value) {
    QUrl u(value);
    if (!u.isValid() || u.scheme() != "https" || u.host().isEmpty() || !u.userInfo().isEmpty())
        fail("通知 Webhook 必须是有效的 HTTPS 地址");
    return u;
}
NoticeRequest json(QString provider, QUrl url, QJsonObject body) {
    return {provider, url, "application/json", QJsonDocument(body).toJson(QJsonDocument::Compact), {}};
}
NoticeRequest multipart(QString provider, QUrl url, QMap<QString, QByteArray> fields,
                        QVector<QPair<QString, QByteArray>> files) {
    const auto boundary = "okww-" + QUuid::createUuid().toByteArray(QUuid::Id128);
    QByteArray body;
    for (auto it = fields.begin(); it != fields.end(); ++it)
        body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + it.key().toUtf8() +
                "\"\r\n\r\n" + it.value() + "\r\n";
    int i = 0;
    for (const auto &file : files)
        body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + file.first.toUtf8() +
                "\"; filename=\"notification_" + QByteArray::number(++i) +
                ".png\"\r\nContent-Type: image/png\r\n\r\n" + file.second + "\r\n";
    body += "--" + boundary + "--\r\n";
    return {provider, url, "multipart/form-data; boundary=" + boundary, body, {}};
}
QByteArray wrapped64(QByteArray data) {
    data = data.toBase64();
    QByteArray out;
    for (qsizetype i = 0; i < data.size(); i += 76)
        out += data.mid(i, 76) + "\r\n";
    return out;
}
QString address(QString text) {
    // One mailbox per envelope field. Reject command/header injection rather
    // than trying to reinterpret a display name as an SMTP recipient.
    if (!QRegularExpression("^[^\\s<>@,;\\x00-\\x1f]+@[^\\s<>@,;\\x00-\\x1f]+$").match(text).hasMatch())
        fail("发件人和收件人需填写单个完整邮箱地址");
    return text;
}
void checkpoint(const NoticeCancelled &cancel) {
    if (cancel && cancel())
        fail("通知已取消");
}
} // namespace
QVector<NoticeRequest> noticeRequests(const QString &provider, const QJsonObject &c, const Notice &n) {
    QVector<NoticeRequest> out;
    if (provider == "Discord") {
        const auto url = webhook(required(c, "Discord Webhook"));
        auto parts = chunks(content(n), 1900);
        if (parts.isEmpty())
            parts << QString{};
        for (int i = 0; i < parts.size(); ++i) {
            QJsonObject payload{{"content", parts[i]},
                                {"username", "OKWW"},
                                {"allowed_mentions", QJsonObject{{"parse", QJsonArray{}}}}};
            QVector<QPair<QString, QByteArray>> files;
            if (i == 0)
                for (int j = 0; j < n.images.size(); ++j)
                    files.append({QString("files[%1]").arg(j), n.images[j]});
            out << multipart(provider, url,
                             {{"payload_json", QJsonDocument(payload).toJson(QJsonDocument::Compact)}},
                             files);
        }
    } else if (provider == "Telegram") {
        const auto token = required(c, "Telegram Bot Token"), chat = required(c, "Telegram Chat ID");
        if (!QRegularExpression("^[0-9]+:[A-Za-z0-9_-]+$").match(token).hasMatch())
            fail("Telegram 机器人令牌格式错误");
        const auto base = "https://api.telegram.org/bot" + token;
        for (const auto &text : chunks(content(n), 4000))
            out << json(provider, QUrl(base + "/sendMessage"), {{"chat_id", chat}, {"text", text}});
        for (const auto &image : n.images)
            out << multipart(provider, QUrl(base + "/sendPhoto"), {{"chat_id", chat.toUtf8()}},
                             {{"photo", image}});
    } else if (provider == "WeCom") {
        const auto url = webhook(required(c, "Enterprise WeChat Webhook URL"));
        // A Chinese character may occupy four bytes; stay below 4096 bytes.
        for (const auto &text : chunks(content(n), 950))
            out << json(provider, url,
                        {{"msgtype", "markdown"}, {"markdown", QJsonObject{{"content", text}}}});
        for (const auto &image : n.images) {
            if (image.size() > 2 * 1024 * 1024)
                fail("企业微信图片超过 2 MB，请降低通知截图尺寸");
            out << json(
                provider, url,
                {{"msgtype", "image"},
                 {"image",
                  QJsonObject{
                      {"base64", QString::fromLatin1(image.toBase64())},
                      {"md5", QString::fromLatin1(
                                  QCryptographicHash::hash(image, QCryptographicHash::Md5).toHex())}}}});
        }
    } else if (provider == "QQ Bot") {
        const auto app = required(c, "QQ Bot API App ID"), token = required(c, "QQ Bot API Token"),
                   channel = required(c, "QQ Bot API Channel ID");
        if (!QRegularExpression("^[0-9]+$").match(app).hasMatch() ||
            !QRegularExpression("^[0-9]+$").match(channel).hasMatch())
            fail("QQ 应用和频道 ID 应为数字");
        auto text = content(n);
        if (!n.images.isEmpty())
            text += QString("\n[%1 张截图；旧版频道接口仅发送文字]").arg(n.images.size());
        for (const auto &part : chunks(text, 1800)) {
            auto request = json(provider, QUrl("https://api.sgroup.qq.com/channels/" + channel + "/messages"),
                                {{"content", part}});
            request.headers["Authorization"] = "Bot " + app.toUtf8() + '.' + token.toUtf8();
            out << request;
        }
    } else
        fail("未知网络通知渠道");
    return out;
}
void validateNoticeResponse(const QString &provider, const QByteArray &body) {
    if (provider == "Discord" || provider == "QQ Bot")
        return;
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        fail("通知服务返回了无法识别的结果");
    auto o = doc.object();
    // Never log raw server responses: some services echo credential-bearing URLs.
    if (provider == "Telegram" && !o["ok"].toBool())
        fail("Telegram 拒绝通知，请检查令牌和接收权限");
    if (provider == "WeCom" && (!o["errcode"].isDouble() || o["errcode"].toInt(-1) != 0))
        fail("企业微信拒绝通知，请检查 Webhook 和消息限制");
}
QByteArray sendHttpNotice(const NoticeRequest &r, const NoticeCancelled &cancel) {
    checkpoint(cancel);
    QNetworkAccessManager manager;
    QNetworkRequest request(r.url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, r.contentType);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    for (auto it = r.headers.begin(); it != r.headers.end(); ++it)
        request.setRawHeader(it.key(), it.value());
    QScopedPointer<QNetworkReply> reply(manager.post(request, r.body));
    QEventLoop loop;
    QTimer timer;
    timer.setInterval(50);
    QElapsedTimer elapsed;
    elapsed.start();
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        if ((cancel && cancel()) || elapsed.elapsed() > 30000)
            reply->abort();
    });
    QObject::connect(reply.data(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start();
    if (!reply->isFinished())
        loop.exec();
    checkpoint(cancel);
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300)
        fail(QString("通知发送失败（HTTP %1，网络状态 %2）").arg(status).arg(int(reply->error())));
    if (reply->bytesAvailable() > 1024 * 1024)
        fail("通知返回内容过大");
    return reply->readAll();
}
QByteArray noticeMime(const QJsonObject &c, const Notice &n, QByteArray boundary) {
    if (!QRegularExpression("^[a-zA-Z0-9_-]+$").match(QString::fromLatin1(boundary)).hasMatch())
        fail("无效邮件边界");
    const auto sender = address(required(c, "SMTP Default Sender")),
               recipient = address(required(c, "SMTP Default Recipient"));
    QByteArray out = "From: <" + sender.toUtf8() + ">\r\nTo: <" + recipient.toUtf8() + ">\r\nSubject: ";
    // Fold RFC 2047 words, including very long localized titles.
    for (const auto &part : chunks(n.title.isEmpty() ? QString("OKWW 通知") : n.title, 12))
        out += "=?UTF-8?B?" + part.toUtf8().toBase64() + "?=\r\n ";
    out += "\r\nMIME-Version: 1.0\r\nContent-Type: multipart/mixed; boundary=\"" + boundary + "\"\r\n\r\n";
    out += "--" + boundary +
           "\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: base64\r\n\r\n" +
           wrapped64(n.text.toUtf8());
    int i = 0;
    for (const auto &image : n.images)
        out += "--" + boundary +
               "\r\nContent-Type: image/png\r\nContent-Disposition: attachment; filename=\"notification_" +
               QByteArray::number(++i) + ".png\"\r\nContent-Transfer-Encoding: base64\r\n\r\n" +
               wrapped64(image);
    return out + "--" + boundary + "--\r\n";
}
void sendSmtpNotice(const QJsonObject &c, const Notice &n, const NoticeCancelled &cancel) {
    const auto host = required(c, "SMTP Host");
    const auto sender = address(required(c, "SMTP Default Sender")),
               recipient = address(required(c, "SMTP Default Recipient"));
    int port = c["SMTP Port"].toInt(587);
    if (port < 1 || port > 65535)
        fail("邮件服务器端口无效");
    QSslSocket socket;
    socket.setPeerVerifyMode(QSslSocket::VerifyPeer);
    socket.setProtocol(QSsl::TlsV1_2OrLater);
    auto until = [&](auto predicate, auto poll) {
        QElapsedTimer elapsed;
        elapsed.start();
        while (!predicate()) {
            checkpoint(cancel);
            if (elapsed.elapsed() > 30000)
                fail("邮件服务器响应超时");
            poll();
            if (socket.state() == QAbstractSocket::UnconnectedState)
                fail("邮件连接已断开");
        }
    };
    const bool tls = c["SMTP Use TLS"].toBool(true);
    if (tls && port == 465)
        socket.connectToHostEncrypted(host, quint16(port));
    else
        socket.connectToHost(host, quint16(port));
    until([&] { return socket.state() == QAbstractSocket::ConnectedState; },
          [&] { socket.waitForConnected(100); });
    auto encrypted = [&] {
        until([&] { return socket.isEncrypted(); }, [&] { socket.waitForEncrypted(100); });
    };
    if (tls && port == 465)
        encrypted();
    auto response = [&](std::initializer_list<int> expected) {
        QByteArray line;
        int code = 0, total = 0;
        do {
            until([&] { return socket.canReadLine(); }, [&] { socket.waitForReadyRead(100); });
            line = socket.readLine(8192);
            total += int(line.size());
            if (line.size() < 4 || !line.endsWith('\n') || total > 65536)
                fail("邮件服务器返回无效响应");
            bool valid = false;
            const auto next = line.left(3).toInt(&valid);
            if (!valid || (code && next != code))
                fail("邮件响应码不一致");
            code = next;
        } while (line[3] == '-');
        if (line[3] != ' ' || std::find(expected.begin(), expected.end(), code) == expected.end())
            fail(QString("邮件服务拒绝操作（%1）").arg(code));
    };
    auto write = [&](QByteArray bytes) {
        checkpoint(cancel);
        if (socket.write(bytes) < 0)
            fail("邮件发送失败");
        until([&] { return socket.bytesToWrite() == 0; }, [&] { socket.waitForBytesWritten(100); });
    };
    auto command = [&](QByteArray bytes, std::initializer_list<int> codes) {
        write(bytes + "\r\n");
        response(codes);
    };
    response({220});
    command("EHLO okww.local", {250});
    if (tls && port != 465) {
        command("STARTTLS", {220});
        socket.startClientEncryption();
        encrypted();
        command("EHLO okww.local", {250});
    }
    const auto user = c["SMTP Username"].toString();
    if (!user.isEmpty()) {
        command("AUTH LOGIN", {334});
        command(user.toUtf8().toBase64(), {334});
        command(c["SMTP Password"].toString().toUtf8().toBase64(), {235});
    }
    command("MAIL FROM:<" + sender.toUtf8() + ">", {250});
    command("RCPT TO:<" + recipient.toUtf8() + ">", {250, 251});
    command("DATA", {354});
    // MIME body is base64, so no line can begin with a dot.
    write(noticeMime(c, n, "okww-" + QUuid::createUuid().toByteArray(QUuid::Id128)) + ".\r\n");
    response({250});
    // Delivery is accepted; a disconnected QUIT must not change it to failure.
    socket.write("QUIT\r\n");
    socket.waitForBytesWritten(100);
    socket.disconnectFromHost();
}
Notifications::Notifications(QString models, std::function<void(QString)> report, NoticeTransport transport)
    : models_(std::move(models)), transport_(transport ? std::move(transport) : sendHttpNotice),
      report_(std::move(report)), worker_(&Notifications::run, this) {}
Notifications::~Notifications() { stop(); }
void Notifications::stop() {
    stopping_ = true;
    ready_.notify_all();
    if (worker_.joinable())
        worker_.join();
}
bool Notifications::idle() {
    std::lock_guard lock(mutex_);
    return queue_.empty() && !busy_;
}
void Notifications::submit(QJsonObject config, Notice notice) {
    static const QStringList flags{"Discord Notification",
                                   "Telegram Notification",
                                   "Enterprise WeChat Webhook Notification",
                                   "QQ Bot API Notification",
                                   "SMTP Notification",
                                   "QQ Desktop Notification (Not Reliable)",
                                   "WeChat Desktop Notification (Not Reliable)"};
    if (std::none_of(flags.begin(), flags.end(), [&](const auto &f) { return config[f].toBool(); }))
        return;
    std::lock_guard lock(mutex_);
    if (stopping_)
        return;
    if (queue_.size() >= 32) {
        if (report_)
            report_("通知队列已满，此条未发送");
        return;
    }
    queue_.push_back({std::move(config), std::move(notice)});
    ready_.notify_one();
}
void Notifications::run() {
    const NoticeCancelled cancel = [&] { return bool(stopping_); };
    while (!stopping_) {
        Pending item;
        {
            std::unique_lock lock(mutex_);
            ready_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
            if (stopping_)
                break;
            item = std::move(queue_.front());
            queue_.pop_front();
            busy_ = true;
        }
        const QVector<QPair<QString, QString>> channels{
            {"Discord", "Discord Notification"},
            {"Telegram", "Telegram Notification"},
            {"WeCom", "Enterprise WeChat Webhook Notification"},
            {"QQ Bot", "QQ Bot API Notification"},
            {"SMTP", "SMTP Notification"},
            {"QQ Desktop", "QQ Desktop Notification (Not Reliable)"},
            {"WeChat Desktop", "WeChat Desktop Notification (Not Reliable)"}};
        for (const auto &[provider, flag] : channels) {
            if (stopping_)
                break;
            if (!item.config[flag].toBool())
                continue;
            try {
                if (provider == "SMTP")
                    sendSmtpNotice(item.config, item.notice, cancel);
                else if (provider.endsWith("Desktop"))
                    sendDesktopNotice(provider.startsWith("WeChat"), item.config, item.notice, models_,
                                      cancel);
                else
                    for (const auto &request : noticeRequests(provider, item.config, item.notice))
                        validateNoticeResponse(provider, transport_(request, cancel));
                if (report_)
                    report_(provider + " 通知已提交");
            } catch (const std::exception &e) {
                if (!stopping_ && report_)
                    report_(provider + "：" + errorText(e));
            }
        }
        busy_ = false;
        std::unique_lock lock(mutex_);
        ready_.wait_for(lock, std::chrono::seconds(5), [&] { return bool(stopping_); });
    }
    busy_ = false;
}
} // namespace okww::runtime
