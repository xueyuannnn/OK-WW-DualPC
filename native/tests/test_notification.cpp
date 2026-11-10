#include "runtime/notification.hpp"
#include "runtime/profile.hpp"
#include <QtTest>
#include <QCryptographicHash>
#include <QTcpServer>
#include <QTcpSocket>
using namespace okww;
using namespace okww::runtime;
class NotificationTests final : public QObject {
    Q_OBJECT
  private slots:
    void hangingDesktopCaptureCanBeCancelledAndCannotSpawnUnboundedWorkers() {
        bool called = false;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, boundedDesktopCapture(
                                                         [&] {
                                                             called = true;
                                                             return cv::Mat{};
                                                         },
                                                         [] { return true; }));
        QVERIFY(!called);
        auto release = std::make_shared<std::atomic<bool>>(false);
        auto finished = std::make_shared<std::atomic<bool>>(false);
        auto cleanup = qScopeGuard([release] { release->store(true); });
        const auto blocked = [release, finished] {
            while (!release->load())
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            finished->store(true);
            return cv::Mat(2, 2, CV_8UC3, cv::Scalar(1, 2, 3));
        };
        QElapsedTimer timer;
        timer.start();
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, boundedDesktopCapture(blocked, {}, 30));
        QVERIFY(timer.elapsed() < 500);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, boundedDesktopCapture([] { return cv::Mat{}; }, {}, 30));
        release->store(true);
        QTRY_VERIFY_WITH_TIMEOUT(finished->load(), 1000);
        // The completed worker releases the single capture slot.
        const auto available = [&] {
            try {
                return !boundedDesktopCapture([] { return cv::Mat::ones(2, 2, CV_8U); }, {}, 500).empty();
            } catch (...) {
                return false;
            }
        };
        QTRY_VERIFY_WITH_TIMEOUT(available(), 1000);
        release->store(false);
        finished->store(false);
        timer.restart();
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 boundedDesktopCapture(blocked, [&] { return timer.elapsed() >= 30; }, 2000));
        QVERIFY(timer.elapsed() < 500);
        release->store(true);
        QTRY_VERIFY_WITH_TIMEOUT(finished->load(), 1000);
    }
    void providerPayloadsAndErrors() {
        const Notice notice{"任务完成", "中文正文\n第二行", {QByteArray("PNG test bytes")}};
        const QJsonObject config{
            {"Discord Webhook", "https://discord.com/api/webhooks/test/test"},
            {"Telegram Bot Token", "123:test_token"},
            {"Telegram Chat ID", "-456"},
            {"Enterprise WeChat Webhook URL", "https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key=test"},
            {"QQ Bot API App ID", "123"},
            {"QQ Bot API Token", "test"},
            {"QQ Bot API Channel ID", "456"}};
        auto discord = noticeRequests("Discord", config, notice);
        QCOMPARE(discord.size(), 1);
        QVERIFY(discord[0].body.contains("files[0]"));
        QVERIFY(discord[0].body.contains("PNG test bytes"));
        QVERIFY(discord[0].body.contains("allowed_mentions"));
        QVERIFY(discord[0].body.contains(notice.text.left(4).toUtf8()));
        auto telegram = noticeRequests("Telegram", config, notice);
        QCOMPARE(telegram.size(), 2);
        auto json = QJsonDocument::fromJson(telegram[0].body).object();
        QCOMPARE(json["chat_id"].toString(), QString("-456"));
        QCOMPARE(json["text"].toString(), notice.title + '\n' + notice.text);
        QVERIFY(telegram[1].body.contains("name=\"photo\""));
        auto wecom = noticeRequests("WeCom", config, notice);
        QCOMPARE(wecom.size(), 2);
        json = QJsonDocument::fromJson(wecom[1].body).object()["image"].toObject();
        QCOMPARE(QByteArray::fromBase64(json["base64"].toString().toLatin1()), notice.images[0]);
        QCOMPARE(json["md5"].toString().toLatin1(),
                 QCryptographicHash::hash(notice.images[0], QCryptographicHash::Md5).toHex());
        auto qq = noticeRequests("QQ Bot", config, notice);
        QCOMPARE(qq[0].headers["Authorization"], QByteArray("Bot 123.test"));
        validateNoticeResponse("Telegram", R"({"ok":true})");
        validateNoticeResponse("WeCom", R"({"errcode":0})");
        for (auto provider : {"Telegram", "WeCom"}) {
            QVERIFY_EXCEPTION_THROWN(validateNoticeResponse(provider, "{}"), std::runtime_error);
            QVERIFY_EXCEPTION_THROWN(validateNoticeResponse(provider, "<html>"), std::runtime_error);
            QVERIFY_EXCEPTION_THROWN(noticeRequests(provider, {}, notice), std::runtime_error);
        }
        auto bad = config;
        bad["Discord Webhook"] = "http://localhost/test";
        QVERIFY_EXCEPTION_THROWN(noticeRequests("Discord", bad, notice), std::runtime_error);
        bad = config;
        bad["QQ Bot API Token"] = "secret\r\nX-Foo: bar";
        QVERIFY_EXCEPTION_THROWN(noticeRequests("QQ Bot", bad, notice), std::runtime_error);
        auto longNotice = notice;
        longNotice.title = {};
        longNotice.text = QString::fromUtf8("🙂").repeated(5000);
        auto parts = noticeRequests("Telegram", config, longNotice);
        QCOMPARE(parts.size(), 4);
        QString joined;
        for (int i = 0; i < 3; ++i) {
            auto text = QJsonDocument::fromJson(parts[i].body).object()["text"].toString();
            QVERIFY(text.size() <= 4000);
            QVERIFY(!text.back().isHighSurrogate());
            joined += text;
        }
        QCOMPARE(joined, longNotice.text);
    }
    void smtpMessageIsLocalizedAndInjectionSafe() {
        QJsonObject config{{"SMTP Default Sender", "sender@example.invalid"},
                           {"SMTP Default Recipient", "user@example.invalid"}};
        const Notice n{
            "很长的任务完成标题\r\nBcc: bad@example.invalid", "正文\r\n.\r\n测试", {"image bytes"}};
        auto mime = noticeMime(config, n, "test_boundary");
        QVERIFY(!mime.contains("\r\nBcc:"));
        QVERIFY(mime.contains("Content-Type: image/png"));
        QVERIFY(mime.contains(n.text.toUtf8().toBase64()));
        QVERIFY(mime.contains(QByteArray("image bytes").toBase64()));
        QVERIFY(mime.endsWith("--test_boundary--\r\n"));
        config["SMTP Default Recipient"] = "user@example.invalid\r\nRCPT TO:<other@example.invalid>";
        QVERIFY_EXCEPTION_THROWN(noticeMime(config, n, "test_boundary"), std::runtime_error);
    }
    void httpTransportUsesLocalFakeServerAndHonorsCancellation() {
        for (int scenario = 0; scenario < 3; ++scenario) {
            QTcpServer server;
            QVERIFY(server.listen(QHostAddress::LocalHost));
            QByteArray requestBytes;
            int requests = 0;
            connect(&server, &QTcpServer::newConnection, &server, [&] {
                auto socket = server.nextPendingConnection();
                auto handled = std::make_shared<bool>(false);
                connect(socket, &QTcpSocket::readyRead, &server, [&, socket, handled] {
                    requestBytes += socket->readAll();
                    if (*handled || !requestBytes.contains("\r\n\r\n"))
                        return;
                    *handled = true;
                    ++requests;
                    if (scenario == 0)
                        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                                      "11\r\nConnection: close\r\n\r\n{\"ok\":true}");
                    else if (scenario == 1)
                        socket->write("HTTP/1.1 302 Found\r\nLocation: "
                                      "https://example.invalid/secret-token\r\nContent-Length: "
                                      "0\r\nConnection: close\r\n\r\n");
                    if (scenario != 2)
                        socket->disconnectFromHost();
                });
            });
            NoticeRequest request{
                "Telegram",
                QUrl(QString("http://127.0.0.1:%1/test-secret-token").arg(server.serverPort())),
                "application/json",
                "{}",
                {}};
            std::atomic<bool> cancel = false, done = false;
            QByteArray received;
            QString failure;
            std::thread worker([&] {
                try {
                    received = sendHttpNotice(request, [&] { return bool(cancel); });
                } catch (const std::exception &e) {
                    failure = errorText(e);
                }
                done = true;
            });
            QElapsedTimer timer;
            timer.start();
            while (!done && timer.elapsed() < 3000) {
                QTest::qWait(10);
                if (scenario == 2 && requests > 0)
                    cancel = true;
            }
            cancel = true;
            worker.join();
            QCOMPARE(requests, 1);
            if (scenario == 0) {
                QVERIFY2(failure.isEmpty(), qPrintable(failure));
                validateNoticeResponse("Telegram", received);
            } else {
                QVERIFY(!failure.isEmpty());
                QVERIFY(!failure.contains("secret-token"));
            }
            QVERIFY(timer.elapsed() < 2000);
        }
    }
    void smtpTransportAgainstLocalFakeServerOnly() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QJsonObject config{{"SMTP Host", "127.0.0.1"},
                           {"SMTP Port", server.serverPort()},
                           {"SMTP Use TLS", false},
                           {"SMTP Username", "test-user"},
                           {"SMTP Password", "test-password"},
                           {"SMTP Default Sender", "sender@example.invalid"},
                           {"SMTP Default Recipient", "user@example.invalid"}};
        const Notice notice{"测试标题", "测试正文", {"fake image"}};
        QStringList commands;
        QByteArray received;
        int auth = 0;
        bool data = false, accepted = false;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto socket = server.nextPendingConnection();
            socket->write("220 local mock\r\n");
            connect(socket, &QTcpSocket::readyRead, &server, [&, socket] {
                while (socket->canReadLine()) {
                    auto line = socket->readLine();
                    auto trimmed = line.trimmed();
                    if (data) {
                        if (line == ".\r\n") {
                            data = false;
                            accepted = true;
                            socket->write("250 accepted\r\n");
                        } else
                            received += line;
                        continue;
                    }
                    commands << QString::fromLatin1(trimmed);
                    if (trimmed.startsWith("EHLO"))
                        socket->write("250-local\r\n250 AUTH LOGIN\r\n");
                    else if (trimmed == "AUTH LOGIN") {
                        auth = 1;
                        socket->write("334 username\r\n");
                    } else if (auth == 1) {
                        auth = 2;
                        socket->write("334 password\r\n");
                    } else if (auth == 2) {
                        auth = 0;
                        socket->write("235 authenticated\r\n");
                    } else if (trimmed == "DATA") {
                        data = true;
                        socket->write("354 enter data\r\n");
                    } else if (trimmed == "QUIT") {
                        socket->write("221 bye\r\n");
                        socket->disconnectFromHost();
                    } else
                        socket->write("250 ok\r\n");
                }
            });
        });
        std::atomic<bool> done = false, cancel = false;
        QString failure;
        std::thread worker([&] {
            try {
                sendSmtpNotice(config, notice, [&] { return bool(cancel); });
            } catch (const std::exception &e) {
                failure = errorText(e);
            }
            done = true;
        });
        QElapsedTimer timer;
        timer.start();
        while (!done && timer.elapsed() < 5000)
            QTest::qWait(10);
        cancel = true;
        worker.join();
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QVERIFY(accepted);
        QVERIFY(commands.contains("RCPT TO:<user@example.invalid>"));
        QVERIFY(commands.contains(QString::fromLatin1(QByteArray("test-user").toBase64())));
        QVERIFY(received.contains(notice.text.toUtf8().toBase64()));
    }
    void contactSelectionExcludesHistoryAndAmbiguousNames() {
        const auto line = [](QString text, int y) { return TextLine{text, {10, y, 100, 20}, 1}; };
        QVector<TextLine> lines{line("联系人", 10), line("收件人", 40), line("聊天记录", 80),
                                line("收件人", 110)};
        QCOMPARE(messengerContact(lines, " 收件人 ", true), std::optional<QPoint>(QPoint(60, 50)));
        QVERIFY(!messengerContact({line("聊天记录", 10), line("收件人", 40)}, "收件人", true));
        QVERIFY(!messengerContact({line("收件人 A", 10)}, "收件人", false));
        QCOMPARE(messengerContact({line("功能", 10), line("文件传输助手", 40)}, "文件传输助手", true),
                 std::optional<QPoint>(QPoint(60, 50)));
        QVERIFY(!messengerContact({line("收件人", 40), line("收件人", 80)}, "收件人", false));
    }
    void workerDoesNotSendDisabledChannelsAndCancelsPromptly() {
        std::atomic<int> sent = 0;
        std::atomic<bool> entered = false;
        Notifications queue({}, {}, [&](const NoticeRequest &r, const NoticeCancelled &cancel) {
            ++sent;
            entered = true;
            while (!cancel())
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return QByteArray{};
        });
        queue.submit({}, {"标题", "正文", {}});
        QTest::qWait(30);
        QCOMPARE(sent.load(), 0);
        queue.submit({{"Discord Notification", true},
                      {"Discord Webhook", "https://discord.com/api/webhooks/test/test"}},
                     {"标题", "正文", {}});
        QTRY_VERIFY_WITH_TIMEOUT(entered.load(), 1000);
        QVERIFY(!queue.idle());
        QElapsedTimer timer;
        timer.start();
        queue.stop();
        QVERIFY(timer.elapsed() < 500);
        QCOMPARE(sent.load(), 1);
    }
    void notificationFieldsFollowProviderSwitchAndPersist() {
        QTemporaryDir temp;
        Profile profile(temp.path(), QString(OKWW_SOURCE_ROOT) + "/native/resources/catalog.json");
        const auto field = [&](QString key) {
            for (auto g : profile.catalog()["globals"].toArray())
                if (g.toObject()["id"] == "Notification")
                    for (auto f : g.toObject()["fields"].toArray())
                        if (f.toObject()["key"] == key)
                            return f.toObject();
            return QJsonObject{};
        };
        QVERIFY(field("Telegram Bot Token").isEmpty());
        profile.set("Notification", "Telegram Notification", true);
        QVERIFY(field("Telegram Bot Token")["secret"].toBool());
        profile.set("Notification", "Telegram Bot Token", "123:local_test");
        profile.set("Notification", "Telegram Notification", false);
        QVERIFY(field("Telegram Bot Token").isEmpty());
        Profile reloaded(temp.path(), QString(OKWW_SOURCE_ROOT) + "/native/resources/catalog.json");
        QCOMPARE(reloaded.value("Notification", "Telegram Bot Token").toString(), QString("123:local_test"));
        QVERIFY_EXCEPTION_THROWN(profile.set("Notification", "SMTP Port", 70000), std::runtime_error);
    }
};
QTEST_GUILESS_MAIN(NotificationTests)
#include "test_notification.moc"
