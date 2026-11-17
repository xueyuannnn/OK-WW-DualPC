#pragma once
#include "reference_notification.hpp"
#include "runtime/workflow_abi.hpp"
#include <QSslSocket>
namespace okww::runtime::notice_transport_test {
using notice_reference::address;
using notice_reference::required;
QJsonObject configuration(int scene) {
    QJsonObject c{{"SMTP Host", "fixture.invalid"},
                  {"SMTP Default Sender", "sender@example.invalid"},
                  {"SMTP Default Recipient", "recipient@example.invalid"},
                  {"SMTP Port", scene % 3 ? 587 : 465},
                  {"SMTP Use TLS", scene % 2 != 0},
                  {"SMTP Username", scene % 5 ? "test-user" : ""},
                  {"SMTP Password", "test-password"}};
    if (scene % 41 == 0)
        c["SMTP Port"] = 0;
    if (scene % 43 == 0)
        c.remove("SMTP Host");
    if (scene % 47 == 0)
        c["SMTP Default Recipient"] = "bad recipient";
    return c;
}
struct Socket {
    QJsonArray &trace;
    int scene;
    QAbstractSocket::SocketState state_ = QAbstractSocket::UnconnectedState;
    bool encrypted = false, accepted = false, data = false;
    int auth = 0;
    qint64 pending = 0;
    QList<QByteArray> lines;
    Socket(QJsonArray &t, int s) : trace(t), scene(s) { trace.append("socket"); }
    void setPeerVerifyMode(QSslSocket::PeerVerifyMode mode) {
        trace.append(QJsonObject{{"verify", int(mode)}});
    }
    void setProtocol(QSsl::SslProtocol protocol) { trace.append(QJsonObject{{"protocol", int(protocol)}}); }
    void connect(bool tls, QString host, quint16 port) {
        trace.append(QJsonObject{{"connect_tls", tls}, {"host", host}, {"port", port}});
        state_ = QAbstractSocket::ConnectingState;
        if (scene % 19 == 0)
            lines = {"BAD invalid\r\n"};
        else if (scene % 7 == 0)
            lines = {scene % 2 ? QByteArray("220 short") : QByteArray("2\n")};
        else
            lines = {"220 fixture\r\n"};
    }
    void connectToHostEncrypted(QString host, quint16 port) { connect(true, host, port); }
    void connectToHost(QString host, quint16 port) { connect(false, host, port); }
    QAbstractSocket::SocketState state() {
        trace.append("state");
        return state_;
    }
    bool isEncrypted() {
        trace.append("encrypted");
        return encrypted;
    }
    bool canReadLine() {
        trace.append("can-read");
        return !lines.isEmpty();
    }
    qint64 bytesToWrite() {
        trace.append("pending");
        return pending;
    }
    bool waitForConnected(int ms) {
        trace.append(QJsonObject{{"wait-connect", ms}});
        if (scene % 31 == 0)
            state_ = QAbstractSocket::UnconnectedState;
        else if (scene % 23)
            state_ = QAbstractSocket::ConnectedState;
        return state_ == QAbstractSocket::ConnectedState;
    }
    bool waitForEncrypted(int ms) {
        trace.append(QJsonObject{{"wait-encrypt", ms}});
        encrypted = true;
        return true;
    }
    bool waitForReadyRead(int ms) {
        trace.append(QJsonObject{{"wait-read", ms}});
        return !lines.empty();
    }
    bool waitForBytesWritten(int ms) {
        trace.append(QJsonObject{{"wait-write", ms}});
        pending = 0;
        return state_ != QAbstractSocket::UnconnectedState;
    }
    void startClientEncryption() { trace.append("start-encryption"); }
    QByteArray readLine(qint64 limit) {
        trace.append(QJsonObject{{"read-line", double(limit)}});
        return lines.isEmpty() ? QByteArray{} : lines.takeFirst();
    }
    qint64 write(QByteArray bytes) {
        auto text = QString::fromLatin1(bytes);
        text.replace(QRegularExpression("okww-[0-9a-f]{32}"), "<boundary>");
        trace.append(QJsonObject{{"write", text}});
        pending = 1;
        if (bytes == "QUIT\r\n") {
            if (scene % 53 == 0) {
                state_ = QAbstractSocket::UnconnectedState;
                return -1;
            }
            return bytes.size();
        }
        if (scene % 37 == 0)
            return -1;
        if (data) {
            data = false;
            accepted = true;
            lines.append("250 accepted\r\n");
        } else if (bytes.startsWith("EHLO")) {
            if (scene % 13 == 0)
                lines.append({"250-first\r\n", "251 mismatch\r\n"});
            else if (scene % 11 == 0) {
                for (int i = 0; i < 9; ++i)
                    lines.append("250-" + QByteArray(7980, 'x') + "\r\n");
                lines.append("250 ok\r\n");
            } else
                lines.append({"250-fixture\r\n", "250 AUTH LOGIN\r\n"});
        } else if (bytes == "STARTTLS\r\n")
            lines.append("220 tls\r\n");
        else if (bytes == "AUTH LOGIN\r\n") {
            auth = 1;
            lines.append("334 user\r\n");
        } else if (auth == 1) {
            auth = 2;
            lines.append("334 password\r\n");
        } else if (auth == 2) {
            auth = 0;
            lines.append("235 authenticated\r\n");
        } else if (bytes == "DATA\r\n") {
            data = true;
            lines.append("354 data\r\n");
        } else if (bytes.startsWith("RCPT"))
            lines.append(scene % 2 ? "251 forwarded\r\n" : "250 recipient\r\n");
        else
            lines.append("250 ok\r\n");
        return bytes.size();
    }
    void disconnectFromHost() {
        trace.append("disconnect");
        state_ = QAbstractSocket::UnconnectedState;
    }
};
struct Timer {
    QJsonArray &trace;
    int scene, reads = 0;
    void start() { trace.append("timer-start"); }
    qint64 elapsed() {
        trace.append("elapsed");
        return ++reads * 6000;
    }
};
[[noreturn]] void noticeTransportFailure(int code, int detail = 0) {
    if (code == 7)
        fail(QString("邮件服务拒绝操作（%1）").arg(detail));
    static const QStringList messages{"",
                                      "通知返回内容过大",
                                      "邮件服务器端口无效",
                                      "邮件服务器响应超时",
                                      "邮件连接已断开",
                                      "邮件服务器返回无效响应",
                                      "邮件响应码不一致",
                                      "",
                                      "邮件发送失败"};
    fail(messages.at(code));
}
struct Scenario {

    int scene;
    QJsonArray trace;
    QJsonObject config;
    Notice notice;
    int cancellations = 0;
    NoticeCancelled cancelled;
    explicit Scenario(int s)
        : scene(s), config(configuration(s)), notice{"标题", "正文", {QByteArray("PNG")}},
          cancelled([this] { return scene % 29 == 0 && ++cancellations >= 2; }), c(config), n(notice),
          cancel(cancelled) {}
    void checkpoint(const NoticeCancelled &callback) {
        trace.append("checkpoint");
        notice_reference::checkpoint(callback);
    }
    void original() {
        const auto host = required(c, "SMTP Host");
        const auto sender = address(required(c, "SMTP Default Sender")),
                   recipient = address(required(c, "SMTP Default Recipient"));
        int port = c["SMTP Port"].toInt(587);
        if (port < 1 || port > 65535)
            fail("邮件服务器端口无效");
        Socket socket{trace, scene};
        socket.setPeerVerifyMode(QSslSocket::VerifyPeer);
        socket.setProtocol(QSsl::TlsV1_2OrLater);
        auto until = [&](auto predicate, auto poll) {
            Timer elapsed{trace, scene};
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
        write(notice_reference::noticeMime(c, n, "okww-" + QUuid::createUuid().toByteArray(QUuid::Id128)) +
              ".\r\n");
        response({250});
        // Delivery is accepted; a disconnected QUIT must not change it to failure.
        socket.write("QUIT\r\n");
        socket.waitForBytesWritten(100);
        socket.disconnectFromHost();
    }
    QJsonObject outcome(bool rust) {
        QJsonObject result;
        try {
            if (rust)
                run(rust::WorkflowMethod::NoticeSmtp);
            else
                original();
            result["ok"] = true;
        } catch (const std::exception &e) {
            result["error"] = errorText(e);
        }
        result["trace"] = trace;
        return result;
    }

    const QJsonObject &c;
    const Notice &n;
    const NoticeCancelled &cancel;
    QString host, sender, recipient, user;
    int port = 0;
    std::optional<Socket> socket;
    void run(rust::WorkflowMethod method, std::span<const double> args = {}, const QByteArray &payload = {},
             std::span<const int> expected = {}) {
        Timer elapsed{trace, scene};
        QByteArray line;
        rust::workflowRun(method, args, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::NoticeTransportCheckpoint:
                checkpoint(cancel);
                break;
            case rust::WorkflowOp::NoticeSmtpReadHost:
                host = required(c, "SMTP Host");
                break;
            case rust::WorkflowOp::NoticeSmtpReadSender:
                sender = address(required(c, "SMTP Default Sender"));
                break;
            case rust::WorkflowOp::NoticeSmtpReadRecipient:
                recipient = address(required(c, "SMTP Default Recipient"));
                break;
            case rust::WorkflowOp::NoticeSmtpReadPort:
                out[0] = port = c["SMTP Port"].toInt(587);
                break;
            case rust::WorkflowOp::NoticeSmtpCreateSocket:
                socket.emplace(trace, scene);
                break;
            case rust::WorkflowOp::NoticeSmtpVerifyPeer:
                socket->setPeerVerifyMode(QSslSocket::VerifyPeer);
                break;
            case rust::WorkflowOp::NoticeSmtpTlsProtocol:
                socket->setProtocol(QSsl::TlsV1_2OrLater);
                break;
            case rust::WorkflowOp::NoticeSmtpUseTls:
                out[0] = c["SMTP Use TLS"].toBool(true);
                break;
            case rust::WorkflowOp::NoticeSmtpConnectEncrypted:
                socket->connectToHostEncrypted(host, quint16(port));
                break;
            case rust::WorkflowOp::NoticeSmtpConnectPlain:
                socket->connectToHost(host, quint16(port));
                break;
            case rust::WorkflowOp::NoticeSmtpUntilNested:
                until(int(a[0]));
                break;
            case rust::WorkflowOp::NoticeSmtpWelcome:
                response({220});
                break;
            case rust::WorkflowOp::NoticeSmtpEhlo:
                command("EHLO okww.local", {250});
                break;
            case rust::WorkflowOp::NoticeSmtpStartTlsCommand:
                command("STARTTLS", {220});
                break;
            case rust::WorkflowOp::NoticeSmtpStartEncryption:
                socket->startClientEncryption();
                break;
            case rust::WorkflowOp::NoticeSmtpReadUser:
                user = c["SMTP Username"].toString();
                break;
            case rust::WorkflowOp::NoticeSmtpUserEmpty:
                out[0] = user.isEmpty();
                break;
            case rust::WorkflowOp::NoticeSmtpAuthLogin:
                command("AUTH LOGIN", {334});
                break;
            case rust::WorkflowOp::NoticeSmtpAuthUser:
                command(user.toUtf8().toBase64(), {334});
                break;
            case rust::WorkflowOp::NoticeSmtpAuthPassword:
                command(c["SMTP Password"].toString().toUtf8().toBase64(), {235});
                break;
            case rust::WorkflowOp::NoticeSmtpMailFrom:
                command("MAIL FROM:<" + sender.toUtf8() + ">", {250});
                break;
            case rust::WorkflowOp::NoticeSmtpRecipient:
                command("RCPT TO:<" + recipient.toUtf8() + ">", {250, 251});
                break;
            case rust::WorkflowOp::NoticeSmtpData:
                command("DATA", {354});
                break;
            case rust::WorkflowOp::NoticeSmtpWriteMime:
                write(notice_reference::noticeMime(c, n,
                                                   "okww-" + QUuid::createUuid().toByteArray(QUuid::Id128)) +
                      ".\r\n");
                break;
            case rust::WorkflowOp::NoticeSmtpAccepted:
                response({250});
                break;
            case rust::WorkflowOp::NoticeSmtpWriteQuit:
                socket->write("QUIT\r\n");
                break;
            case rust::WorkflowOp::NoticeSmtpWaitQuitWritten:
                socket->waitForBytesWritten(int(a[0]));
                break;
            case rust::WorkflowOp::NoticeSmtpDisconnect:
                socket->disconnectFromHost();
                break;
            case rust::WorkflowOp::NoticeSmtpStartElapsed:
                elapsed.start();
                break;
            case rust::WorkflowOp::NoticeSmtpElapsed:
                out[0] = double(elapsed.elapsed());
                break;
            case rust::WorkflowOp::NoticeSmtpWaitPredicate:
                switch (int(a[0])) {
                case 0:
                    out[0] = socket->state() == QAbstractSocket::ConnectedState;
                    break;
                case 1:
                    out[0] = socket->isEncrypted();
                    break;
                case 2:
                    out[0] = socket->canReadLine();
                    break;
                case 3:
                    out[0] = socket->bytesToWrite() == 0;
                    break;
                }
                break;
            case rust::WorkflowOp::NoticeSmtpWaitPoll:
                switch (int(a[0])) {
                case 0:
                    socket->waitForConnected(int(a[1]));
                    break;
                case 1:
                    socket->waitForEncrypted(int(a[1]));
                    break;
                case 2:
                    socket->waitForReadyRead(int(a[1]));
                    break;
                case 3:
                    socket->waitForBytesWritten(int(a[1]));
                    break;
                }
                break;
            case rust::WorkflowOp::NoticeSmtpDisconnected:
                out[0] = socket->state() == QAbstractSocket::UnconnectedState;
                break;
            case rust::WorkflowOp::NoticeSmtpReadLine:
                line = socket->readLine(qint64(a[0]));
                break;
            case rust::WorkflowOp::NoticeSmtpLineSize:
                out[0] = double(line.size());
                break;
            case rust::WorkflowOp::NoticeSmtpLineEndsLf:
                out[0] = line.endsWith('\n');
                break;
            case rust::WorkflowOp::NoticeSmtpParseCode: {
                bool valid = false;
                out[0] = line.left(3).toInt(&valid);
                out[1] = valid;
                break;
            }
            case rust::WorkflowOp::NoticeSmtpLineContinues:
                out[0] = line[3] == '-';
                break;
            case rust::WorkflowOp::NoticeSmtpLineSpace:
                out[0] = line[3] == ' ';
                break;
            case rust::WorkflowOp::NoticeSmtpExpectedCount:
                out[0] = double(expected.size());
                break;
            case rust::WorkflowOp::NoticeSmtpExpectedAt:
                out[0] = expected[size_t(a[0])];
                break;
            case rust::WorkflowOp::NoticeSmtpWritePayload:
                out[0] = double(socket->write(payload));
                break;
            case rust::WorkflowOp::NoticeSmtpWriteCommand:
                write(payload + "\r\n");
                break;
            case rust::WorkflowOp::NoticeSmtpCommandResponse:
                run(rust::WorkflowMethod::NoticeSmtpResponse, {}, {}, expected);
                break;
            case rust::WorkflowOp::NoticeTransportFailure:
                noticeTransportFailure(int(a[0]), int(a[1]));
                break;
            default:
                fail("Rust 通知 SMTP 宿主无效");
            }
        });
    }
    void until(int kind) {
        const std::array<double, 1> args{double(kind)};
        run(rust::WorkflowMethod::NoticeSmtpUntil, args);
    }
    void response(std::initializer_list<int> expected) {
        run(rust::WorkflowMethod::NoticeSmtpResponse, {}, {}, {expected.begin(), expected.size()});
    }
    void write(const QByteArray &payload) { run(rust::WorkflowMethod::NoticeSmtpWrite, {}, payload); }
    void command(const QByteArray &payload, std::initializer_list<int> expected) {
        run(rust::WorkflowMethod::NoticeSmtpCommand, {}, payload, {expected.begin(), expected.size()});
    }
};
} // namespace okww::runtime::notice_transport_test
