#include "window.hpp"
#include <QLocalServer>
#include <QLocalSocket>
#include <QCryptographicHash>
using namespace okww;
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setStyle("Fusion");
    QCoreApplication::setApplicationName("OKWW-DualPC-Native-Dev");
    QGuiApplication::setApplicationDisplayName(product::title);
    QCoreApplication::setApplicationVersion(product::version);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({{"root", "Native resources root", "path"},
                       {"data", "Isolated profile directory", "path"},
                       {"run-task", "Start a scheduled native task", "id"},
                       {"exit-after-task", "Exit after successful scheduled execution"},
                       {"limit-seconds", "Maximum scheduled execution time; 0 is unlimited", "seconds", "0"},
                       {"smoke", "Offline native UI check report", "path"}});
    parser.process(app);
    try {
        QString root = parser.value("root");
        if (root.isEmpty()) {
            QDir directory(QCoreApplication::applicationDirPath());
            for (int i = 0; i < 6; ++i) {
                if (directory.exists("native/resources/catalog.json")) {
                    root = directory.absolutePath();
                    break;
                }
                if (!directory.cdUp())
                    break;
            }
        }
        if (root.isEmpty() || !QFile::exists(root + "/assets/coco_annotations.json"))
            fail("缺少原生资源目录");
        QString data = parser.value("data");
        if (data.isEmpty())
            data = root + "/native-full-data";
        QDir().mkpath(data);
        if (!QFile::exists(data + "/dualpc.json"))
            writeJson(data + "/dualpc.json", Config::defaults().json);
        data = QDir(data).absolutePath();
        const auto socketName =
            "okww-native-" +
            QString::fromLatin1(
                QCryptographicHash::hash(data.toCaseFolded().toUtf8(), QCryptographicHash::Sha256).toHex());
        bool validLimit = false;
        const auto limit = parser.value("limit-seconds").toDouble(&validLimit);
        if (!validLimit || !std::isfinite(limit) || limit < 0 || limit > 604800)
            fail("任务运行时限无效");
        QLockFile lock(data + "/.native-session.lock");
        lock.setStaleLockTime(0);
        if (!lock.tryLock()) {
            QLocalSocket socket;
            socket.connectToServer(socketName);
            if (!socket.waitForConnected(3000))
                fail("此配置目录已打开，但现有窗口尚未就绪");
            socket.write(QJsonDocument(QJsonObject{{"op", parser.isSet("run-task") ? "run" : "show"},
                                                   {"task", parser.value("run-task")},
                                                   {"exit_after", parser.isSet("exit-after-task")},
                                                   {"limit", limit}})
                             .toJson(QJsonDocument::Compact) +
                         '\n');
            socket.flush();
            QElapsedTimer deadline;
            deadline.start();
            while (!socket.canReadLine()) {
                if (deadline.elapsed() >= 3000 || !socket.waitForReadyRead(int(3000 - deadline.elapsed())))
                    fail("现有窗口未确认计划请求");
                if (socket.bytesAvailable() > 65536)
                    fail("现有窗口返回内容过大");
            }
            const auto reply = QJsonDocument::fromJson(socket.readLine()).object();
            return reply["ok"].toBool() ? 0 : 3;
        }
        Window window(root, data, {});
        QObject::connect(&window, &Window::exitAccepted, &app, &QCoreApplication::quit, Qt::QueuedConnection);
        QLocalServer server;
        server.setSocketOptions(QLocalServer::UserAccessOption);
        if (!server.listen(socketName))
            fail("无法创建当前配置的本地任务入口");
        QObject::connect(&server, &QLocalServer::newConnection, &window, [&] {
            while (auto socket = server.nextPendingConnection()) {
                auto pending = std::make_shared<QByteArray>();
                QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QLocalSocket::readyRead, &window, [&, socket, pending] {
                    pending->append(socket->readAll());
                    if (pending->size() > 65536) {
                        socket->abort();
                        return;
                    }
                    if (!pending->contains('\n'))
                        return;
                    const auto request =
                        QJsonDocument::fromJson(pending->left(pending->indexOf('\n'))).object();
                    bool accepted = false;
                    if (request["op"] == "show") {
                        window.showNormal();
                        window.raise();
                        window.activateWindow();
                        accepted = true;
                    } else if (request["op"] == "run" && request["task"].isString() &&
                               request["exit_after"].isBool() && request["limit"].isDouble())
                        try {
                            accepted = window.scheduledStart(request["task"].toString(),
                                                             request["exit_after"].toBool(),
                                                             request["limit"].toDouble());
                        } catch (const std::exception &e) {
                            qWarning().noquote() << errorText(e);
                        }
                    socket->write(
                        QJsonDocument(QJsonObject{{"ok", accepted}}).toJson(QJsonDocument::Compact) + '\n');
                    socket->disconnectFromServer();
                });
            }
        });
        window.show();
        if (parser.isSet("smoke"))
            window.smoke(parser.value("smoke"), false);
        else if (parser.isSet("run-task"))
            QTimer::singleShot(0, &window, [&] {
                if (!window.scheduledStart(parser.value("run-task"), parser.isSet("exit-after-task"), limit))
                    qWarning() << "计划任务未能启动";
            });
        return app.exec();
    } catch (const std::exception &e) {
        qCritical().noquote() << errorText(e);
        if (!parser.isSet("smoke"))
            QMessageBox::critical(nullptr, product::name, errorText(e));
        return 1;
    }
}
