#ifndef Q_MOC_RUN
#include "reference_notification.hpp"
#endif
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
namespace {
QJsonObject config(int scene) {
    QJsonObject out{{"Discord Webhook", "https://example.invalid/discord?fixture=1"},
                    {"Telegram Bot Token", "123:test_token"},
                    {"Telegram Chat ID", "-456"},
                    {"Enterprise WeChat Webhook URL", "https://example.invalid/wecom?fixture=1"},
                    {"QQ Bot API App ID", "123"},
                    {"QQ Bot API Token", "dummy_token"},
                    {"QQ Bot API Channel ID", "456"},
                    {"SMTP Default Sender", "sender@example.invalid"},
                    {"SMTP Default Recipient", "receiver@example.invalid"}};
    const QStringList keys{"Discord Webhook",       "Telegram Bot Token",
                           "Telegram Chat ID",      "Enterprise WeChat Webhook URL",
                           "QQ Bot API App ID",     "QQ Bot API Token",
                           "QQ Bot API Channel ID", "SMTP Default Sender",
                           "SMTP Default Recipient"};
    if (scene % 17 == 0)
        out.remove(keys[(scene / 17) % keys.size()]);
    if (scene % 19 == 0)
        out[keys[(scene / 19) % keys.size()]] = "injected\r\nvalue";
    if (scene % 23 == 0)
        out["Telegram Bot Token"] = "invalid";
    if (scene % 29 == 0)
        out["Discord Webhook"] = "http://example.invalid/webhook";
    if (scene % 31 == 0)
        out["Enterprise WeChat Webhook URL"] = "https://user:pass@example.invalid/webhook";
    if (scene % 37 == 0)
        out["QQ Bot API App ID"] = "not-a-number";
    if (scene % 41 == 0)
        out["SMTP Default Recipient"] = "two@example.invalid,other@example.invalid";
    return out;
}
Notice notice(int scene) {
    const QList<int> lengths{0,    1,    11,   12,   75,   76,   949,  950,  951, 1799,
                             1800, 1801, 1899, 1900, 1901, 3999, 4000, 4001, 8001};
    Notice out;
    out.title = scene % 3 ? QString("任务完成 ") + QString::fromUtf8("🙂").repeated(scene % 17) : QString{};
    const auto unit = QString::fromUtf8("中文🙂\r\n") + QString(QChar(char16_t(0xd800))) + QChar(0);
    const int length = lengths[scene % lengths.size()];
    out.text = unit.repeated((length + unit.size() - 1) / unit.size()).left(length);
    for (int i = 0; i < scene % 4; ++i)
        out.images.append(QByteArray((scene + i) % 81, char(13 + i)));
    if (scene == 255)
        out.images.append(QByteArray(2 * 1024 * 1024 + 1, 'x'));
    return out;
}
QJsonArray requests(const QVector<NoticeRequest> &values) {
    QJsonArray out;
    for (const auto &value : values) {
        auto type = value.contentType, body = value.body;
        const QByteArray prefix = "multipart/form-data; boundary=";
        if (type.startsWith(prefix)) {
            const auto boundary = type.mid(prefix.size());
            type.replace(boundary, "<boundary>");
            body.replace(boundary, "<boundary>");
        }
        QJsonObject headers;
        for (auto it = value.headers.cbegin(); it != value.headers.cend(); ++it)
            headers[QString::fromLatin1(it.key())] = QString::fromLatin1(it.value());
        out.append(QJsonObject{
            {"provider", value.provider},
            {"url", value.url.toString()},
            {"type", QString::fromLatin1(type)},
            {"body_sha256",
             QString::fromLatin1(QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex())},
            {"body_size", double(body.size())},
            {"body_null", body.isNull()},
            {"headers", headers}});
    }
    return out;
}
QJsonObject payload(const QString &provider, const QJsonObject &cfg, const Notice &n, bool rust) {
    try {
        return {{"requests", requests(rust ? noticeRequests(provider, cfg, n)
                                           : notice_reference::noticeRequests(provider, cfg, n))}};
    } catch (const std::exception &e) {
        return {{"error", errorText(e)}};
    }
}
QJsonObject mime(const QJsonObject &cfg, const Notice &n, const QByteArray &boundary, bool rust) {
    try {
        const auto bytes =
            rust ? noticeMime(cfg, n, boundary) : notice_reference::noticeMime(cfg, n, boundary);
        return {{"sha256",
                 QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
                {"size", double(bytes.size())},
                {"null", bytes.isNull()}};
    } catch (const std::exception &e) {
        return {{"error", errorText(e)}};
    }
}
QString response(const QString &provider, const QByteArray &body, bool rust) {
    try {
        if (rust)
            validateNoticeResponse(provider, body);
        else
            notice_reference::validateNoticeResponse(provider, body);
        return "ok";
    } catch (const std::exception &e) {
        return errorText(e);
    }
}
} // namespace
class NotificationContracts final : public QObject {
    Q_OBJECT
  private slots:
    void payloads_data() {
        QTest::addColumn<int>("scene");
        QTest::addColumn<QString>("provider");
        for (const auto &provider : {"Discord", "Telegram", "WeCom", "QQ Bot", "Unknown"})
            for (int i = 0; i < 256; ++i)
                QTest::newRow(qPrintable(QString("%1-%2").arg(provider).arg(i))) << i << QString(provider);
    }
    void payloads() {
        QFETCH(int, scene);
        QFETCH(QString, provider);
        const auto cfg = config(scene);
        const auto value = notice(scene);
        QCOMPARE(payload(provider, cfg, value, true), payload(provider, cfg, value, false));
    }
    void mime_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 512; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void mime() {
        QFETCH(int, scene);
        const QList<QByteArray> boundaries{"test_boundary",           "_x-123",    "",
                                           "bad\r\nheader",           "x\n",       "x\r\n",
                                           QByteArray::fromHex("ff"), "with space"};
        const auto cfg = config(scene);
        const auto value = notice(scene);
        QCOMPARE(::mime(cfg, value, boundaries[(scene / 7) % boundaries.size()], true),
                 ::mime(cfg, value, boundaries[(scene / 7) % boundaries.size()], false));
    }
    void responses_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 512; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void responses() {
        QFETCH(int, scene);
        const QStringList providers{"Discord", "QQ Bot", "Telegram", "WeCom", "Unknown"};
        const QList<QByteArray> bodies{"{}",
                                       "{\"ok\":true}",
                                       "{\"ok\":false}",
                                       "{\"ok\":\"true\"}",
                                       "{\"errcode\":0}",
                                       "{\"errcode\":0.5}",
                                       "{\"errcode\":\"0\"}",
                                       "{\"errcode\":-1}",
                                       "[]",
                                       "invalid json",
                                       "",
                                       QByteArray::fromHex("ff")};
        const auto &provider = providers[scene % providers.size()];
        const auto &body = bodies[(scene / 5) % bodies.size()];
        QCOMPARE(response(provider, body, true), response(provider, body, false));
    }
};
QTEST_GUILESS_MAIN(NotificationContracts)
#include "test_notification_contract.moc"
