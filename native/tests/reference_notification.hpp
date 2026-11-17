#pragma once
#include "runtime/notification.hpp"
#include <QCryptographicHash>
namespace okww::runtime::notice_reference {
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

} // namespace okww::runtime::notice_reference
