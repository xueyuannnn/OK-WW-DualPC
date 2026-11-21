#include "scripts.hpp"
#include "diagnostics.hpp"
#include "workflow_abi.hpp"
#include "workshop.hpp"
namespace okww::runtime {
namespace {
[[noreturn]] void scriptFailure(int code, const QJsonObject &reply = {}, const QByteArray &errors = {}) {
    switch (code) {
    case 3:
        fail(reply["error"].toString("脚本管理器返回无效"));
    case 9:
        throw ScriptDisabled();
    case 11:
        fail("脚本兼容引擎已退出：" + QString::fromUtf8(errors.right(2000)));
    case 16:
        throw ExecutionFailure(reply["error"].toString("脚本引擎未就绪"), reply["traceback"].toString());
    case 19:
        throw ExecutionFailure(reply["error"].toString("脚本执行失败"), reply["traceback"].toString());
    default:
        break;
    }
    const QStringList messages{"缺少 Python 脚本兼容引擎，请使用完整包",
                               "脚本管理器启动失败",
                               "脚本文件操作超时",
                               "",
                               "录制脚本类名无效",
                               "录制事件时间无效",
                               "脚本采集画面尺寸无效",
                               "未知脚本输入动作",
                               "原生任务通道尚未就绪",
                               "",
                               "脚本返回数据过大",
                               "",
                               "缺少 Python 脚本兼容引擎",
                               "无法创建脚本共享图像",
                               "无法映射脚本共享图像",
                               "脚本兼容引擎启动失败",
                               "",
                               "未知录制事件",
                               "脚本执行通道已断开",
                               ""};
    fail(messages.at(code));
}
QProcessEnvironment scriptEnvironment() {
    auto env = QProcessEnvironment::systemEnvironment();
    rust::workflowRun(
        rust::WorkflowMethod::ScriptEnvironment, {}, [&](rust::WorkflowOp op, const double *a, double *) {
            switch (op) {
            case rust::WorkflowOp::ScriptRemoveEnvironment:
                env.remove(
                    QStringList{"QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "PYTHONHOME", "PYTHONPATH"}
                        .at(int(a[0])));
                break;
            case rust::WorkflowOp::ScriptInsertEnvironment: {
                const QVector<QPair<QString, QString>> values{
                    {"PYTHONUTF8", "1"}, {"PYTHONIOENCODING", "utf-8"}, {"PYTHONDONTWRITEBYTECODE", "1"}};
                const auto &v = values.at(int(a[0]));
                env.insert(v.first, v.second);
                break;
            }
            default:
                fail("Rust 脚本环境宿主无效");
            }
        });
    return env;
}
QString literal(const QString &value) {
    auto encoded = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(encoded.mid(1, encoded.size() - 2));
}

} // namespace
QJsonObject scriptManage(const QString &root, const QString &data, QJsonObject request) {
    QString python, engine;
    QProcess process;
    QJsonObject reply, result;
    rust::workflowRun(
        rust::WorkflowMethod::ScriptManage, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::ScriptResolveRuntime:
                python = workshopPython(root);
                engine = workshopRoot(root);
                break;
            case rust::WorkflowOp::ScriptPythonEmpty:
                out[0] = python.isEmpty();
                break;
            case rust::WorkflowOp::ScriptSetEnvironment:
                process.setProcessEnvironment(scriptEnvironment());
                break;
            case rust::WorkflowOp::ScriptSetWorkingDirectory:
                process.setWorkingDirectory(data);
                break;
            case rust::WorkflowOp::ScriptStartManager:
                process.start(
                    python, {engine + "/workshop/bridge/script_manager.py", "--root", engine, "--data", data});
                break;
            case rust::WorkflowOp::ScriptWaitStarted:
                out[0] = process.waitForStarted(int(a[0]));
                break;
            case rust::WorkflowOp::ScriptWriteManagerRequest:
                process.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
                break;
            case rust::WorkflowOp::ScriptCloseWriteChannel:
                process.closeWriteChannel();
                break;
            case rust::WorkflowOp::ScriptWaitFinished:
                out[0] = process.waitForFinished(int(a[0]));
                break;
            case rust::WorkflowOp::ScriptKillProcess:
                process.kill();
                break;
            case rust::WorkflowOp::ScriptWaitFinishedIgnored:
                process.waitForFinished(int(a[0]));
                break;
            case rust::WorkflowOp::ScriptReadManagerReply:
                reply = QJsonDocument::fromJson(process.readAllStandardOutput().trimmed()).object();
                break;
            case rust::WorkflowOp::ScriptExitCode:
                out[0] = process.exitCode();
                break;
            case rust::WorkflowOp::ScriptManagerReplyOk:
                out[0] = std::as_const(reply)["ok"].toBool();
                break;
            case rust::WorkflowOp::ScriptReturnManagerData:
                result = std::as_const(reply)["data"].toObject();
                break;
            case rust::WorkflowOp::ScriptFailure:
                scriptFailure(int(a[0]), reply);
                break;
            default:
                fail("Rust 脚本管理宿主无效");
            }
        });
    return result;
}
QJsonArray scriptCatalog(const QString &data) {
    const auto path = data + "/native-script-catalog.json";
    QJsonArray result;
    rust::workflowRun(rust::WorkflowMethod::ScriptCatalogue, {},
                      [&](rust::WorkflowOp op, const double *, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::ScriptCatalogueExists:
                              out[0] = QFile::exists(path);
                              break;
                          case rust::WorkflowOp::ScriptReadCatalogue:
                              result = readJson(path)["scripts"].toArray();
                              break;
                          default:
                              fail("Rust 脚本目录宿主无效");
                          }
                      });
    return result;
}
QString recordedScript(const QJsonArray &events, QString name) {
    QString result, op, key, point;
    QJsonObject event;
    rust::workflowRun(rust::WorkflowMethod::RecordedScript, {},
                      [&](rust::WorkflowOp operation, const double *a, double *out) {
                          switch (operation) {
                          case rust::WorkflowOp::RecordedClassNameValid:
                              out[0] = QRegularExpression("^[A-Za-z_][A-Za-z0-9_]*$").match(name).hasMatch();
                              break;
                          case rust::WorkflowOp::RecordedScriptHeader: {
                              result = "from ok import BaseTask\n\n\nclass " + name +
                                       "(BaseTask):\n    def __init__(self, *args, **kwargs):\n"
                                       "        super().__init__(*args, **kwargs)\n        self.name = "
                                       "\"录制脚本\"\n\n    def run(self):\n";

                              break;
                          }
                          case rust::WorkflowOp::RecordedEventCount:
                              out[0] = double(events.size());
                              break;
                          case rust::WorkflowOp::LoadRecordedEvent:
                              event = events.at(int(a[0])).toObject();
                              break;
                          case rust::WorkflowOp::RecordedEventTime:
                              out[0] = std::as_const(event)["time"].toDouble();
                              break;
                          case rust::WorkflowOp::RecordedAppendWait:
                              result += QString("        self.sleep(%1)\n").arg(a[0], 0, 'f', 4);
                              break;
                          case rust::WorkflowOp::RecordedPrepareFields:
                              op = std::as_const(event)["op"].toString();
                              key = literal(std::as_const(event)["key"].toString());
                              point = QString::number(std::as_const(event)["x"].toDouble(), 'f', 6) + ", " +
                                      QString::number(std::as_const(event)["y"].toDouble(), 'f', 6);
                              break;
                          case rust::WorkflowOp::RecordedEventKind: {
                              static const QHash<QString, int> kinds{{"key_down", 0},   {"key_up", 0},
                                                                     {"mouse_down", 1}, {"mouse_up", 2},
                                                                     {"move", 3},       {"wheel", 4}};
                              out[0] = kinds.value(op, -1);
                              break;
                          }
                          case rust::WorkflowOp::RecordedAppendKey:
                              result += "        self.send_" + op + '(' + key + ")\n";
                              break;
                          case rust::WorkflowOp::RecordedAppendMouseDown:
                              result += "        self.move_relative(" + point +
                                        ")\n        self.mouse_down(key=" + key + ")\n";
                              break;
                          case rust::WorkflowOp::RecordedAppendMouseUp:
                              result += "        self.mouse_up(key=" + key + ")\n";
                              break;
                          case rust::WorkflowOp::RecordedAppendMove:
                              result += "        self.move_relative(" + point + ")\n";
                              break;
                          case rust::WorkflowOp::RecordedAppendWheel:
                              result += "        self.scroll_relative(" + point + ", " +
                                        QString::number(std::as_const(event)["steps"].toInt()) + ")\n";
                              break;
                          case rust::WorkflowOp::RecordedAppendPass:
                              result += "        pass\n";
                              break;
                          case rust::WorkflowOp::ScriptFailure:
                              scriptFailure(int(a[0]));
                              break;
                          default:
                              fail("Rust 录制脚本宿主无效");
                          }
                      });
    return result;
}
QJsonObject ScriptRuntime::rustScript(int method, QJsonObject request, QJsonObject script) {
    QJsonObject result, reply, message, rpcResult, state, box, values, saved;
    QString python, engine, name, action, key, task, savedKey;
    QStringList savedKeys;
    cv::Mat frame;
    QPointF point;
    QByteArray bytes;
    qsizetype lineAt = 0;
    std::optional<TaskScope> scope;
    rust::workflowRun(
        rust::WorkflowMethod(method), {},
        [&](rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &error) {
            switch (op) {
            case rust::WorkflowOp::Checkpoint:
                c_.execution.checkpoint();
                break;
            case rust::WorkflowOp::ScriptFailure:
                scriptFailure(int(a[0]), reply, errors_);
                break;
            case rust::WorkflowOp::ScriptProcessRunning:
                out[0] = process_.state() != QProcess::NotRunning;
                break;
            case rust::WorkflowOp::ScriptKillProcess:
                process_.kill();
                break;
            case rust::WorkflowOp::ScriptWaitFinishedIgnored:
                process_.waitForFinished(int(a[0]));
                break;
            case rust::WorkflowOp::ScriptReleaseInput:
                c_.input.release();
                break;
            case rust::WorkflowOp::ScriptMappedPresent:
                out[0] = mapped_ != nullptr;
                break;
            case rust::WorkflowOp::ScriptMappingPresent:
                out[0] = mapping_ != nullptr;
                break;
            case rust::WorkflowOp::ScriptUnmapImage:
                UnmapViewOfFile(mapped_);
                break;
            case rust::WorkflowOp::ScriptCloseMapping:
                CloseHandle(mapping_);
                break;
            case rust::WorkflowOp::ScriptResetMappedPointer:
                mapped_ = nullptr;
                break;
            case rust::WorkflowOp::ScriptResetMappingHandle:
                mapping_ = nullptr;
                break;
            case rust::WorkflowOp::ScriptClearBuffer:
                buffer_.clear();
                break;
            case rust::WorkflowOp::ScriptClearErrors:
                errors_.clear();
                break;
            case rust::WorkflowOp::ScriptEncodeMessage:
                bytes = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
                break;
            case rust::WorkflowOp::ScriptWriteMessage:
                out[0] = process_.write(bytes) == bytes.size();
                break;
            case rust::WorkflowOp::ScriptWaitWritten:
                process_.waitForBytesWritten(int(a[0]));
                break;
            case rust::WorkflowOp::ScriptRpcKind: {
                static const QHash<QString, int> kinds{{"frame", 0},
                                                       {"script_progress", 1},
                                                       {"script_info", 2},
                                                       {"script_input", 3},
                                                       {"script_builtin", 4},
                                                       {"activity_ocr", 6}};
                out[0] = kinds.value(request["op"].toString(), 5);
                break;
            }
            case rust::WorkflowOp::ScriptRefreshFrame:
                c_.refresh();
                frame = c_.frame;
                break;
            case rust::WorkflowOp::ScriptActivityOcr: {
                // Reuse the image just published to Python; never capture a different frame here.
                QJsonArray lines;
                for (const auto &line : c_.text(request["x"].toDouble(), request["y"].toDouble(),
                                               request["to_x"].toDouble(1), request["to_y"].toDouble(1),
                                               request["threshold"].toDouble(.5)))
                    lines.append(line.json());
                result = {{"lines", lines}, {"sequence", double(c_.frameVersion)}};
                break;
            }
            case rust::WorkflowOp::ScriptFrameEmpty:
                out[0] = frame.empty();
                break;
            case rust::WorkflowOp::ScriptReturnUnavailableFrame:
                result = {{"available", false}};
                break;
            case rust::WorkflowOp::ScriptFrameBgr:
                out[0] = frame.type() == CV_8UC3;
                break;
            case rust::WorkflowOp::ScriptFrameBytes:
                out[0] = double(frame.total() * 3);
                break;
            case rust::WorkflowOp::ScriptFrameContinuous:
                out[0] = frame.isContinuous();
                break;
            case rust::WorkflowOp::ScriptCloneFrame:
                frame = frame.clone();
                break;
            case rust::WorkflowOp::ScriptCopyMappedFrame:
                memcpy(mapped_, frame.data, frame.total() * 3);
                break;
            case rust::WorkflowOp::ScriptReturnFrame:
                result = {{"available", true},
                          {"width", frame.cols},
                          {"height", frame.rows},
                          {"stride", frame.cols * 3},
                          {"size", double(frame.total() * 3)},
                          {"format", "RGB3"},
                          {"sequence", double(c_.frameVersion)}};
                break;
            case rust::WorkflowOp::ScriptReportProgress:
                c_.reportProgress(request["stage"].toString("执行脚本"), request["current"].toDouble(-1),
                                  request["total"].toDouble(-1), request["unit"].toString(),
                                  request["detail"].toString());
                break;
            case rust::WorkflowOp::ScriptHasInfo:
                out[0] = bool(c_.info);
                break;
            case rust::WorkflowOp::ScriptReportInfo:
                c_.info(request["key"].toString(), request["value"].toString());
                break;
            case rust::WorkflowOp::ScriptReadInputAction:
                action = request["operation"].toString();
                break;
            case rust::WorkflowOp::ScriptHasProgress:
                out[0] = bool(c_.progress);
                break;
            case rust::WorkflowOp::ScriptReportInputAction: {
                static const QMap<QString, QString> labels{
                    {"move", "移动鼠标"},     {"click", "点击"},        {"mouse_down", "按住鼠标"},
                    {"mouse_up", "松开鼠标"}, {"key_down", "按住按键"}, {"key_up", "松开按键"},
                    {"key", "按键"},          {"text", "输入文本"},     {"scroll", "滚动鼠标"},
                    {"swipe", "拖动鼠标"}};
                c_.progress(
                    {{"detail", labels.value(action, "执行动作") + " · " + request["key"].toString()}});
                break;
            }
            case rust::WorkflowOp::ScriptReadInputPoint:
                point = {request["x"].toDouble(-1), request["y"].toDouble(-1)};
                break;
            case rust::WorkflowOp::ScriptInputPoint:
                out[0] = point.x();
                out[1] = point.y();
                break;
            case rust::WorkflowOp::ScriptReadInputKey:
                key = request["key"].toString();
                break;
            case rust::WorkflowOp::ScriptInputKind: {
                static const QHash<QString, int> kinds{
                    {"move", 0},   {"click", 1}, {"mouse_down", 2}, {"mouse_up", 3}, {"key_down", 4},
                    {"key_up", 5}, {"key", 6},   {"scroll", 7},     {"swipe", 8},    {"text", 9}};
                out[0] = kinds.value(action, -1);
                break;
            }
            case rust::WorkflowOp::ScriptInputMove:
                c_.input.move(point);
                break;
            case rust::WorkflowOp::ScriptInputClick:
                c_.input.click(a[0] ? std::optional<QPointF>(point) : std::nullopt,
                               request["seconds"].toDouble(.05), key);
                break;
            case rust::WorkflowOp::ScriptInputMouseDown:
                c_.input.buttonDown(key);
                break;
            case rust::WorkflowOp::ScriptInputMouseUp:
                c_.input.buttonUp(key);
                break;
            case rust::WorkflowOp::ScriptInputKeyDown:
                c_.input.keyDown(key);
                break;
            case rust::WorkflowOp::ScriptInputKeyUp:
                c_.input.keyUp(key);
                break;
            case rust::WorkflowOp::ScriptInputKey:
                c_.input.key(key, request["seconds"].toDouble(.02));
                break;
            case rust::WorkflowOp::ScriptInputScroll:
                c_.input.scroll(request["steps"].toInt(),
                                a[0] ? std::optional<QPointF>(point) : std::nullopt);
                break;
            case rust::WorkflowOp::ScriptInputSwipe:
                c_.input.swipe(point, {request["to_x"].toDouble(), request["to_y"].toDouble()},
                               request["seconds"].toDouble() * 1000, request["settle"].toDouble());
                break;
            case rust::WorkflowOp::ScriptInputText:
                c_.input.text(request["text"].toString());
                break;
            case rust::WorkflowOp::ScriptReadInputState:
                state = c_.input.workshopCommand({{"op", "state"}});
                box = std::as_const(state)["box"].toObject();
                break;
            case rust::WorkflowOp::ScriptInputBoxHasKeys:
                out[0] = box.contains("keys");
                break;
            case rust::WorkflowOp::ScriptReturnBoxState:
                result = box;
                break;
            case rust::WorkflowOp::ScriptReturnInputState:
                result = state;
                break;
            case rust::WorkflowOp::ScriptHasBuiltin:
                out[0] = bool(builtin);
                break;
            case rust::WorkflowOp::ScriptReadBuiltinTask:
                task = request["task"].toString();
                break;
            case rust::WorkflowOp::ScriptCreateBuiltinScope:
                scope.emplace(c_, task);
                break;
            case rust::WorkflowOp::ScriptInvokeBuiltin:
                result = {{"handled", builtin(task)}};
                break;
            case rust::WorkflowOp::ScriptForwardInputRequest:
                result = c_.input.workshopCommand(request);
                break;
            case rust::WorkflowOp::ScriptHasEnabledPredicate:
                out[0] = bool(enabled_);
                break;
            case rust::WorkflowOp::ScriptEnabled:
                out[0] = enabled_();
                break;
            case rust::WorkflowOp::ScriptReadStderr:
                errors_ += process_.readAllStandardError();
                break;
            case rust::WorkflowOp::ScriptTrimStderr:
                errors_ = errors_.right(qsizetype(a[0]));
                break;
            case rust::WorkflowOp::ScriptReadStdout:
                buffer_ += process_.readAllStandardOutput();
                break;
            case rust::WorkflowOp::ScriptBufferBytes:
                out[0] = double(buffer_.size());
                break;
            case rust::WorkflowOp::ScriptBufferHasLine:
                out[0] = buffer_.contains('\n');
                break;
            case rust::WorkflowOp::ScriptParseLine:
                lineAt = buffer_.indexOf('\n');
                message = QJsonDocument::fromJson(buffer_.left(lineAt)).object();
                break;
            case rust::WorkflowOp::ScriptConsumeLine:
                buffer_.remove(0, lineAt + 1);
                break;
            case rust::WorkflowOp::ScriptMessageRpc:
                out[0] = std::as_const(message)["event"] == "rpc";
                break;
            case rust::WorkflowOp::ScriptInvokeRpc:
                rpcResult = rpc(std::as_const(message)["request"].toObject());
                break;
            case rust::WorkflowOp::ScriptWriteRpcSuccess:
                write({{"ok", true}, {"data", rpcResult}});
                break;
            case rust::WorkflowOp::ScriptWriteRpcError:
                try {
                    std::rethrow_exception(error);
                } catch (const std::exception &e) {
                    write({{"ok", false}, {"error", errorText(e)}});
                }
                break;
            case rust::WorkflowOp::ScriptMessageEmpty:
                out[0] = message.isEmpty();
                break;
            case rust::WorkflowOp::ScriptReturnMessage:
                result = message;
                break;
            case rust::WorkflowOp::ScriptWaitReadyRead:
                process_.waitForReadyRead(int(a[0]));
                break;
            case rust::WorkflowOp::ScriptResolveRuntime:
                python = workshopPython(root_);
                engine = workshopRoot(root_);
                break;
            case rust::WorkflowOp::ScriptPythonEmpty:
                out[0] = python.isEmpty();
                break;
            case rust::WorkflowOp::ScriptCreateMappingName:
                name = "Local\\OKWW-Script-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
                break;
            case rust::WorkflowOp::ScriptCreateMapping:
                mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                              64 * 1024 * 1024, reinterpret_cast<LPCWSTR>(name.utf16()));
                break;
            case rust::WorkflowOp::ScriptMapImage:
                mapped_ = MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, 64 * 1024 * 1024);
                break;
            case rust::WorkflowOp::ScriptSetEnvironment:
                process_.setProcessEnvironment(scriptEnvironment());
                break;
            case rust::WorkflowOp::ScriptSetWorkingDirectory:
                process_.setWorkingDirectory(data_);
                break;
            case rust::WorkflowOp::ScriptStartWorker: {
                QStringList arguments{engine + "/workshop/bridge/script_worker.py", "--root", engine,
                                      "--data", data_, "--mapping", name};
                if (activityOnly_) arguments.append("--activities");
                process_.start(python, arguments);
                break;
            }
            case rust::WorkflowOp::ScriptWaitStarted:
                out[0] = process_.waitForStarted(int(a[0]));
                break;
            case rust::WorkflowOp::ScriptReceiveNested:
                reply = receive();
                break;
            case rust::WorkflowOp::ScriptReplyReady:
                out[0] = std::as_const(reply)["event"] == "ready";
                break;
            case rust::WorkflowOp::ScriptStartNested:
                start();
                break;
            case rust::WorkflowOp::ScriptFieldCount:
                out[0] = double(std::as_const(script)["fields"].toArray().size());
                break;
            case rust::WorkflowOp::ScriptCopyFieldDefault: {
                const auto field = std::as_const(script)["fields"].toArray().at(int(a[0])).toObject();
                values[field["key"].toString()] = field["value"];
                break;
            }
            case rust::WorkflowOp::ScriptDefinitionTrigger:
                out[0] = std::as_const(script)["trigger"].toBool();
                break;
            case rust::WorkflowOp::ScriptSavedValuesExist:
                out[0] = QFile::exists(data_ + "/native-scripts.json");
                break;
            case rust::WorkflowOp::ScriptReadSavedValues:
                saved = readJson(data_ + "/native-scripts.json")[std::as_const(script)["id"].toString()]
                            .toObject()["values"]
                            .toObject();
                savedKeys = saved.keys();
                break;
            case rust::WorkflowOp::ScriptSavedValueCount:
                out[0] = double(savedKeys.size());
                break;
            case rust::WorkflowOp::ScriptSelectSavedValue:
                savedKey = savedKeys.at(int(a[0]));
                break;
            case rust::WorkflowOp::ScriptDefaultContainsSavedKey:
                out[0] = values.contains(savedKey);
                break;
            case rust::WorkflowOp::ScriptOverlaySavedValue:
                values[savedKey] = saved.value(savedKey);
                break;
            case rust::WorkflowOp::ScriptWriteRunRequest:
                write({{"script", std::as_const(script)["id"]}, {"values", values}});
                break;
            case rust::WorkflowOp::ScriptReplyOk:
                out[0] = std::as_const(reply)["ok"].toBool();
                break;
            case rust::WorkflowOp::ScriptReturnReplyData:
                result = std::as_const(reply)["data"].toObject();
                break;
            case rust::WorkflowOp::ScriptStopNested:
                stop();
                break;
            default:
                fail("Rust 脚本执行宿主无效");
            }
        });
    return result;
}
void ScriptRuntime::stop() noexcept { rustScript(int(rust::WorkflowMethod::ScriptStop)); }
void ScriptRuntime::write(QJsonObject message) {
    rustScript(int(rust::WorkflowMethod::ScriptWrite), std::move(message));
}
QJsonObject ScriptRuntime::rpc(QJsonObject request) {
    return rustScript(int(rust::WorkflowMethod::ScriptRpc), std::move(request));
}
QJsonObject ScriptRuntime::receive() { return rustScript(int(rust::WorkflowMethod::ScriptReceive)); }
void ScriptRuntime::start() { rustScript(int(rust::WorkflowMethod::ScriptStart)); }
QJsonObject ScriptRuntime::run(const QJsonObject &script, std::function<bool()> enabled) {
    enabled_ = std::move(enabled);
    return rustScript(int(rust::WorkflowMethod::ScriptRun), {}, script);
}
} // namespace okww::runtime
