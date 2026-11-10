#include "scripts.hpp"
#include "workshop.hpp"
namespace okww::runtime {
namespace {
QProcessEnvironment scriptEnvironment() {
    auto env = QProcessEnvironment::systemEnvironment();
    for (auto key : {"QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "PYTHONHOME", "PYTHONPATH"})
        env.remove(key);
    env.insert("PYTHONUTF8", "1");
    env.insert("PYTHONIOENCODING", "utf-8");
    env.insert("PYTHONDONTWRITEBYTECODE", "1");
    return env;
}
QString literal(const QString &value) {
    auto encoded = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(encoded.mid(1, encoded.size() - 2));
}
} // namespace
QJsonObject scriptManage(const QString &root, const QString &data, QJsonObject request) {
    const auto python = workshopPython(root), engine = workshopRoot(root);
    if (python.isEmpty())
        fail("缺少 Python 脚本兼容引擎，请使用完整包");
    QProcess process;
    process.setProcessEnvironment(scriptEnvironment());
    process.setWorkingDirectory(data);
    process.start(python, {engine + "/native/bridge/script_manager.py", "--root", engine, "--data", data});
    if (!process.waitForStarted(10000))
        fail("脚本管理器启动失败");
    process.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    process.closeWriteChannel();
    if (!process.waitForFinished(60000)) {
        process.kill();
        process.waitForFinished(2000);
        fail("脚本文件操作超时");
    }
    const auto reply = QJsonDocument::fromJson(process.readAllStandardOutput().trimmed()).object();
    if (process.exitCode() || !reply["ok"].toBool())
        fail(reply["error"].toString("脚本管理器返回无效"));
    return reply["data"].toObject();
}
QJsonArray scriptCatalog(const QString &data) {
    const auto path = data + "/native-script-catalog.json";
    return QFile::exists(path) ? readJson(path)["scripts"].toArray() : QJsonArray{};
}
QString recordedScript(const QJsonArray &events, QString name) {
    if (!QRegularExpression("^[A-Za-z_][A-Za-z0-9_]*$").match(name).hasMatch())
        fail("录制脚本类名无效");
    QString result =
        "from ok import BaseTask\n\n\nclass " + name +
        "(BaseTask):\n    def __init__(self, *args, **kwargs):\n"
        "        super().__init__(*args, **kwargs)\n        self.name = \"录制脚本\"\n\n    def run(self):\n";
    double previous = 0;
    for (const auto &value : events) {
        const auto e = value.toObject();
        const auto time = e["time"].toDouble();
        if (!std::isfinite(time) || time < previous)
            fail("录制事件时间无效");
        if (time > previous + .001)
            result += QString("        self.sleep(%1)\n").arg(time - previous, 0, 'f', 4);
        previous = time;
        const auto op = e["op"].toString();
        const auto key = literal(e["key"].toString());
        const auto point =
            QString::number(e["x"].toDouble(), 'f', 6) + ", " + QString::number(e["y"].toDouble(), 'f', 6);
        if (op == "key_down" || op == "key_up")
            result += "        self.send_" + op + '(' + key + ")\n";
        else if (op == "mouse_down")
            result += "        self.move_relative(" + point + ")\n        self.mouse_down(key=" + key + ")\n";
        else if (op == "mouse_up")
            result += "        self.mouse_up(key=" + key + ")\n";
        else if (op == "move")
            result += "        self.move_relative(" + point + ")\n";
        else if (op == "wheel")
            result +=
                "        self.scroll_relative(" + point + ", " + QString::number(e["steps"].toInt()) + ")\n";
        else
            fail("未知录制事件");
    }
    if (events.isEmpty())
        result += "        pass\n";
    return result;
}
void ScriptRuntime::stop() noexcept {
    if (process_.state() != QProcess::NotRunning) {
        process_.kill();
        process_.waitForFinished(2000);
    }
    c_.input.release();
    if (mapped_)
        UnmapViewOfFile(mapped_);
    if (mapping_)
        CloseHandle(mapping_);
    mapped_ = nullptr;
    mapping_ = nullptr;
    buffer_.clear();
    errors_.clear();
}
void ScriptRuntime::write(QJsonObject message) {
    const auto bytes = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    if (process_.write(bytes) != bytes.size())
        fail("脚本执行通道已断开");
    process_.waitForBytesWritten(1000);
}
QJsonObject ScriptRuntime::rpc(QJsonObject request) {
    const auto op = request["op"].toString();
    if (op == "frame") {
        c_.refresh();
        auto frame = c_.frame;
        if (frame.empty())
            return {{"available", false}};
        if (frame.type() != CV_8UC3 || frame.total() * 3 > 64 * 1024 * 1024)
            fail("脚本采集画面尺寸无效");
        if (!frame.isContinuous())
            frame = frame.clone();
        const auto size = frame.total() * 3;
        memcpy(mapped_, frame.data, size);
        return {{"available", true},
                {"width", frame.cols},
                {"height", frame.rows},
                {"stride", frame.cols * 3},
                {"size", double(size)},
                {"format", "RGB3"},
                {"sequence", double(c_.frameVersion)}};
    }
    if (op == "script_info") {
        if (c_.info)
            c_.info(request["key"].toString(), request["value"].toString());
        return {};
    }
    if (op == "script_input") {
        const auto action = request["operation"].toString();
        const QPointF point(request["x"].toDouble(-1), request["y"].toDouble(-1));
        const bool positioned = point.x() >= 0 && point.y() >= 0;
        const auto key = request["key"].toString();
        if (action == "move")
            c_.input.move(point);
        else if (action == "click")
            c_.input.click(positioned ? std::optional<QPointF>(point) : std::nullopt,
                           request["seconds"].toDouble(.05), key);
        else if (action == "mouse_down") {
            if (positioned) {
                c_.input.move(point);
                c_.execution.wait(.06);
            }
            c_.input.buttonDown(key);
        } else if (action == "mouse_up")
            c_.input.buttonUp(key);
        else if (action == "key_down")
            c_.input.keyDown(key);
        else if (action == "key_up")
            c_.input.keyUp(key);
        else if (action == "key")
            c_.input.key(key, request["seconds"].toDouble(.02));
        else if (action == "scroll")
            c_.input.scroll(request["steps"].toInt(),
                            positioned ? std::optional<QPointF>(point) : std::nullopt);
        else if (action == "swipe")
            c_.input.swipe(point, {request["to_x"].toDouble(), request["to_y"].toDouble()},
                           request["seconds"].toDouble() * 1000, request["settle"].toDouble());
        else if (action == "text")
            c_.input.text(request["text"].toString());
        else
            fail("未知脚本输入动作");
        const auto state = c_.input.workshopCommand({{"op", "state"}});
        const auto box = state["box"].toObject();
        return box.contains("keys") ? box : state;
    }
    if (op == "script_builtin") {
        if (!builtin)
            fail("原生任务通道尚未就绪");
        const auto task = request["task"].toString();
        TaskScope scope(c_, task);
        return {{"handled", builtin(task)}};
    }
    return c_.input.workshopCommand(request);
}
QJsonObject ScriptRuntime::receive() {
    for (;;) {
        c_.execution.checkpoint();
        if (enabled_ && !enabled_())
            throw ScriptDisabled();
        errors_ += process_.readAllStandardError();
        errors_ = errors_.right(65536);
        buffer_ += process_.readAllStandardOutput();
        if (buffer_.size() > 16 * 1024 * 1024)
            fail("脚本返回数据过大");
        while (buffer_.contains('\n')) {
            const auto at = buffer_.indexOf('\n');
            const auto message = QJsonDocument::fromJson(buffer_.left(at)).object();
            buffer_.remove(0, at + 1);
            if (message["event"] == "rpc") {
                try {
                    write({{"ok", true}, {"data", rpc(message["request"].toObject())}});
                } catch (const Cancelled &) {
                    throw;
                } catch (const std::exception &error) {
                    write({{"ok", false}, {"error", errorText(error)}});
                }
            } else if (!message.isEmpty())
                return message;
        }
        if (process_.state() == QProcess::NotRunning)
            fail("脚本兼容引擎已退出：" + QString::fromUtf8(errors_.right(2000)));
        process_.waitForReadyRead(15);
    }
}
void ScriptRuntime::start() {
    if (process_.state() != QProcess::NotRunning)
        return;
    const auto python = workshopPython(root_), engine = workshopRoot(root_);
    if (python.isEmpty())
        fail("缺少 Python 脚本兼容引擎");
    const auto name = "Local\\OKWW-Script-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 64 * 1024 * 1024,
                                  reinterpret_cast<LPCWSTR>(name.utf16()));
    if (!mapping_)
        fail("无法创建脚本共享图像");
    mapped_ = MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, 64 * 1024 * 1024);
    if (!mapped_)
        fail("无法映射脚本共享图像");
    process_.setProcessEnvironment(scriptEnvironment());
    process_.setWorkingDirectory(data_);
    process_.start(python, {engine + "/native/bridge/script_worker.py", "--root", engine, "--data", data_,
                            "--mapping", name});
    if (!process_.waitForStarted(10000))
        fail("脚本兼容引擎启动失败");
    const auto reply = receive();
    if (reply["event"] != "ready")
        fail(reply["error"].toString("脚本引擎未就绪"));
}
QJsonObject ScriptRuntime::run(const QJsonObject &script, std::function<bool()> enabled) {
    enabled_ = std::move(enabled);
    try {
        start();
        QJsonObject values;
        for (auto item : script["fields"].toArray()) {
            const auto field = item.toObject();
            values[field["key"].toString()] = field["value"];
        }
        if (script["trigger"].toBool() && QFile::exists(data_ + "/native-scripts.json")) {
            const auto saved = readJson(data_ + "/native-scripts.json")[script["id"].toString()]
                                   .toObject()["values"].toObject();
            for (auto it = saved.begin(); it != saved.end(); ++it)
                if (values.contains(it.key())) values[it.key()] = it.value();
        }
        write({{"script", script["id"]}, {"values", values}});
        const auto reply = receive();
        if (!reply["ok"].toBool())
            fail(reply["error"].toString("脚本执行失败"));
        return reply["data"].toObject();
    } catch (...) {
        stop();
        throw;
    }
}
} // namespace okww::runtime
