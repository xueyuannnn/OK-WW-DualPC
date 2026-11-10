#include "window.hpp"
using namespace okww;
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("OKWW-DualPC-Cpp");
    QCoreApplication::setApplicationVersion("0.2.0");
    QGuiApplication::setApplicationDisplayName(product::title);
    QCommandLineParser p;
    p.addHelpOption();
    p.addVersionOption();
    p.addOptions({{"root", "Compatibility engine source root", "path"},
                  {"data", "Independent profile directory", "path"},
                  {"python", "Compatibility Python interpreter", "path"},
                  {"smoke", "Offscreen GUI smoke report", "path"},
                  {"capture-check", "Read-only native capture diagnostic report", "path"},
                  {"export-catalog", "Export legacy metadata during the offline integration test", "path"},
                  {"integration-test", "Synthetic native/Python bridge test report", "path"}});
    p.process(app);
    try {
        QString exeDir = QCoreApplication::applicationDirPath(), root = p.value("root");
        if (root.isEmpty()) {
            if (QFile::exists(exeDir + "/engine/main_dualpc.py"))
                root = exeDir + "/engine";
            else {
                QDir d(exeDir);
                for (int i = 0; i < 5; ++i) {
                    if (d.exists("main_dualpc.py")) {
                        root = d.absolutePath();
                        break;
                    }
                    if (!d.cdUp())
                        break;
                }
            }
        }
        root = QDir(root).absolutePath();
        if (!QFile::exists(root + "/main_dualpc.py"))
            fail("找不到兼容引擎，请使用完整交付目录或 --root 参数");
        QString data = p.value("data");
        if (data.isEmpty())
            data =
                QFile::exists(exeDir + "/engine/main_dualpc.py") ? exeDir + "/data" : root + "/native-data";
        data = QDir(data).absolutePath();
        QDir().mkpath(data);
        if (!QFile::exists(data + "/dualpc.json"))
            writeJson(data + "/dualpc.json", Config::defaults().json);
        QLockFile lock(data + "/.native-session.lock");
        lock.setStaleLockTime(0);
        if (!lock.tryLock())
            fail("此 C++ 配置已经被另一个实例使用");
        auto python = p.value("python");
        if (python.isEmpty())
            python = QFile::exists(root + "/.venv/Scripts/python.exe") ? root + "/.venv/Scripts/python.exe"
                                                                       : root + "/runtime/python.exe";
        if (!QFile::exists(python))
            fail("找不到兼容引擎 Python 运行时");
        if (p.isSet("capture-check")) {
            QJsonObject report;
            auto cfg = Config::load(data + "/dualpc.json");
            try {
                auto available = modes(cfg);
                QJsonArray choices;
                for (auto m : available)
                    if (m.width == cfg.n("capture.width") && m.height == cfg.n("capture.height") &&
                        m.fourcc == cfg.s("capture.fourcc"))
                        choices.append(m.json());
                report["advertised_modes"] = choices;
                Capture capture(cfg);
                capture.start();
                QElapsedTimer clock;
                clock.start();
                uint64_t last = 0;
                int count = 0;
                while (clock.elapsed() < 5000) {
                    auto frame = capture.next(last);
                    if (frame) {
                        last = frame->sequence;
                        ++count;
                    }
                }
                report["actual"] = capture.actual();
                report["measured_fps"] = capture.fps();
                report["frames_observed"] = count;
                report["error"] = capture.error();
                report["ok"] = count > 0;
                const auto sample = capture.latest();
                capture.stop();
                if (sample) {
                    const auto samplePath = QFileInfo(p.value("capture-check")).absolutePath() + "/capture-frame.png";
                    report["sample_saved"] = frameImage(*sample).save(samplePath);
                    report["sample_path"] = samplePath;
                    report["preview_valid"] = !previewImage(*sample).isNull();
                }
            } catch (const std::exception &e) {
                report["ok"] = false;
                report["error"] = errorText(e);
            }
            report["hardware_input_sent"] = false;
            writeJson(p.value("capture-check"), report);
            return report["ok"].toBool() ? 0 : 1;
        }
        if (p.isSet("integration-test")) {
            Backend backend;
            auto cfg = Config::load(data + "/dualpc.json");
            cfg.set("kmbox.dry_run", true);
            cfg.save(data + "/dualpc.json");
            backend.start(cfg, true);
            QProcess worker;
            auto env = QProcessEnvironment::systemEnvironment();
            env.insert("OKWW_NATIVE_PORT", QString::number(backend.port()));
            env.insert("OKWW_NATIVE_TOKEN", backend.token());
            env.insert("OKWW_NATIVE_MAPPING", backend.mappingName());
            env.insert("PYTHONUTF8", "1");
            env.remove("QT_PLUGIN_PATH");
            env.remove("QT_QPA_PLATFORM_PLUGIN_PATH");
            worker.setProcessEnvironment(env);
            worker.setWorkingDirectory(root);
            worker.setStandardErrorFile(QFileInfo(p.value("integration-test")).absolutePath() +
                                        "/native-engine-test.log");
            QJsonObject report;
            QByteArray buffered;
            QObject::connect(&worker, &QProcess::readyReadStandardOutput, &app, [&] {
                buffered += worker.readAllStandardOutput();
                int n;
                while ((n = int(buffered.indexOf('\n'))) >= 0) {
                    auto o = QJsonDocument::fromJson(buffered.left(n)).object();
                    buffered.remove(0, n + 1);
                    if (o["event"] == "ready") {
                        if (p.isSet("export-catalog"))
                            writeJson(p.value("export-catalog"), o["data"].toObject());
                        report["catalog_tasks"] = o["data"].toObject()["tasks"].toArray().size();
                        backend.arm(true);
                    }
                    if (o["event"] == "self_test")
                        report["bridge"] = o["data"];
                }
            });
            QObject::connect(&worker, &QProcess::finished, &app, [&](int code, QProcess::ExitStatus) {
                report["exit_code"] = code;
                report["native_state"] = backend.snapshot();
                writeJson(p.value("integration-test"), report);
                app.exit(code == 0 && report["bridge"].toObject()["ok"].toBool() ? 0 : 1);
            });
            QTimer::singleShot(90000, &app, [&] {
                if (worker.state() != QProcess::NotRunning) {
                    worker.kill();
                    report["timeout"] = true;
                }
            });
            worker.start(python,
                         {root + "/native/bridge/worker.py", "--root", root, "--data", data, "--self-test"});
            if (!worker.waitForStarted(10000))
                fail("测试兼容引擎无法启动");
            return app.exec();
        }
        Window window(root, data, python);
        window.show();
        QObject::connect(&window, &Window::exitAccepted, &app, &QCoreApplication::quit, Qt::QueuedConnection);
        if (p.isSet("smoke"))
            window.smoke(p.value("smoke"), false);
        return app.exec();
    } catch (const std::exception &e) {
        QString error = errorText(e);
        qCritical().noquote() << error;
        if (!p.isSet("smoke") && !p.isSet("integration-test") && !p.isSet("capture-check"))
            QMessageBox::critical(nullptr, product::name, error);
        return 1;
    }
}
