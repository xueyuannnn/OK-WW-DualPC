#include "workshop.hpp"
#include "diagnostics.hpp"
#include "workflow_abi.hpp"
#include <QProcessEnvironment>
#include <QThread>
namespace okww::runtime {
namespace {
const std::array<std::pair<const char *, double CharacterState::*>, 9> commonTimes{
    {{"last_res", &CharacterState::lastResonance},
     {"last_echo", &CharacterState::lastEcho},
     {"last_liberation", &CharacterState::lastLiberation},
     {"last_switch_in", &CharacterState::lastSwitchIn},
     {"last_outro", &CharacterState::lastOutro},
     {"last_switch", &CharacterState::lastSwitch},
     {"last_full_con_switch", &CharacterState::lastFullConSwitch},
     {"last_buff", &CharacterState::lastBuff},
     {"last_perform", &CharacterState::lastPerform}}};

[[noreturn]] void workshopStateFailure(int code) {
    static const QStringList messages{"工坊状态包含无效数值",
                                      "工坊状态包含无效开关值",
                                      "工坊队伍状态与当前队伍不一致",
                                      "工坊返回了不一致的角色位置",
                                      "工坊角色定位无效",
                                      "工坊协奏值无效",
                                      "工坊角色增益时间无效",
                                      "工坊冻结时间线过大",
                                      "工坊冻结时间无效",
                                      "工坊桥接版本不一致，请使用同一完整包中的主程序和兼容引擎",
                                      "工坊兼容引擎缺少必要接口能力，请更新完整包"};
    fail(messages.at(code));
}
double workshopNumberValue(QJsonValue value, bool boolean) {
    return rust::workflowRun(boolean ? rust::WorkflowMethod::WorkshopBoolean
                                     : rust::WorkflowMethod::WorkshopFinite,
                             {}, [&](rust::WorkflowOp op, const double *a, double *out) {
                                 switch (op) {
                                 case rust::WorkflowOp::WorkshopValueDouble:
                                     out[0] = value.isDouble();
                                     break;
                                 case rust::WorkflowOp::WorkshopValueNumber:
                                     out[0] = value.toDouble();
                                     break;
                                 case rust::WorkflowOp::WorkshopValueBool:
                                     out[0] = value.isBool();
                                     break;
                                 case rust::WorkflowOp::WorkshopValueBoolean:
                                     out[0] = value.toBool();
                                     break;
                                 case rust::WorkflowOp::WorkshopStateFailure:
                                     workshopStateFailure(int(a[0]));
                                     break;
                                 default:
                                     fail("Rust 工坊状态类型宿主无效");
                                 }
                             })[0];
}
double finiteNumber(QJsonValue value) { return workshopNumberValue(value, false); }
bool stateBoolean(QJsonValue value) { return workshopNumberValue(value, true) != 0; }
double percentile(QVector<double> values, double fraction) {
    const std::array<double, 1> args{fraction};
    return rust::workflowRun(rust::WorkflowMethod::WorkshopPercentile, args,
                             [&](rust::WorkflowOp op, const double *a, double *out) {
                                 switch (op) {
                                 case rust::WorkflowOp::WorkshopSampleCount:
                                     out[0] = double(values.size());
                                     break;
                                 case rust::WorkflowOp::WorkshopSortSamples:
                                     std::sort(values.begin(), values.end());
                                     break;
                                 case rust::WorkflowOp::WorkshopSampleAt:
                                     out[0] = values.at(qsizetype(a[0]));
                                     break;
                                 default:
                                     fail("Rust 工坊测量宿主无效");
                                 }
                             })[0];
}
QJsonObject workshopStateRun(rust::WorkflowMethod method, CombatHost *host, const QJsonObject &state) {
    QJsonObject result, value, freeze, snapshot;
    QVector<Character *> party;
    Character *character = nullptr;
    CharacterState preparedState;
    QJsonArray values, timeline, chars, freezes, capabilities;
    QSet<int> seen;
    QVector<QPair<Character *, CharacterState>> prepared;
    QVector<bool> checks;
    std::optional<FreezeTimeline> preparedFreezes;
    int role = 0;
    bool inLiberation = false;
    rust::workflowRun(method, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::WorkshopStateFailure:
            workshopStateFailure(int(a[0]));
            break;
        case rust::WorkflowOp::WorkshopHostNow:
            out[0] = host->now();
            break;
        case rust::WorkflowOp::WorkshopLoadParty:
            party = host->characters();
            break;
        case rust::WorkflowOp::WorkshopPartyCount:
            out[0] = double(party.size());
            break;
        case rust::WorkflowOp::WorkshopSelectCharacter:
            character = party.at(int(a[0]));
            break;
        case rust::WorkflowOp::WorkshopCharacterSnapshot: {
            const auto &s = character->state;
            snapshot = {{"index", s.index},
                        {"current", s.current},
                        {"intro", s.hasIntro},
                        {"sub_intro", s.hasSubDpsIntro},
                        {"current_con", s.currentCon},
                        {"check_f", character->checkFOnSwitch},
                        {"buff_time", s.buffTime},
                        {"role", QStringList{"MainDps", "SubDps", "Healer"}.value(int(s.role), "MainDps")}};
            break;
        }
        case rust::WorkflowOp::WorkshopCharacterTime:
            out[0] = character->state.*commonTimes.at(int(a[0])).second;
            break;
        case rust::WorkflowOp::WorkshopSnapshotTime:
            snapshot[commonTimes.at(int(a[0])).first] = a[1];
            break;
        case rust::WorkflowOp::WorkshopAppendCharacter:
            chars.append(snapshot);
            break;
        case rust::WorkflowOp::WorkshopFreezeCount:
            out[0] = double(host->freezes.entries().size());
            break;
        case rust::WorkflowOp::WorkshopAppendFreezeSnapshot: {
            const auto &f = host->freezes.entries().at(int(a[0]));
            freezes.append(
                QJsonObject{{"age", a[1] - f.start}, {"duration", f.duration}, {"threshold", f.threshold}});
            break;
        }
        case rust::WorkflowOp::WorkshopCombatStart:
            out[0] = host->combatStart;
            break;
        case rust::WorkflowOp::WorkshopReturnCombatState:
            result = {{"chars", chars},
                      {"freezes", freezes},
                      {"in_liberation", host->inLiberation},
                      {"combat_age", a[0]}};
            break;
        case rust::WorkflowOp::WorkshopLoadCharacterValues:
            values = state["chars"].toArray();
            break;
        case rust::WorkflowOp::WorkshopCharacterValueCount:
            out[0] = double(values.size());
            break;
        case rust::WorkflowOp::WorkshopSelectCharacterValue:
            value = values.at(int(a[0])).toObject();
            break;
        case rust::WorkflowOp::WorkshopReadFiniteIndex:
            out[0] = finiteNumber(std::as_const(value)["index"]);
            break;
        case rust::WorkflowOp::WorkshopPartyIndex:
            out[0] = party.at(int(a[0]))->state.index;
            break;
        case rust::WorkflowOp::WorkshopIndexSeen:
            out[0] = seen.contains(int(a[0]));
            break;
        case rust::WorkflowOp::WorkshopMarkIndex:
            seen.insert(int(a[0]));
            break;
        case rust::WorkflowOp::WorkshopCopyCharacterState:
            preparedState = character->state;
            break;
        case rust::WorkflowOp::WorkshopReadRole:
            role =
                QStringList{"MainDps", "SubDps", "Healer"}.indexOf(std::as_const(value)["role"].toString());
            break;
        case rust::WorkflowOp::WorkshopRoleValid:
            out[0] = role >= 0;
            break;
        case rust::WorkflowOp::WorkshopAssignRole:
            preparedState.role = std::array<Role, 3>{Role::MainDps, Role::SubDps, Role::Healer}.at(role);
            break;
        case rust::WorkflowOp::WorkshopReadStateFlag: {
            const bool flag = stateBoolean(
                std::as_const(value)[QStringList{"current", "intro", "sub_intro"}.at(int(a[0]))]);
            preparedState.*std::array<bool CharacterState::*, 3>{&CharacterState::current,
                                                                 &CharacterState::hasIntro,
                                                                 &CharacterState::hasSubDpsIntro}
                               .at(int(a[0])) = flag;
            break;
        }
        case rust::WorkflowOp::WorkshopReadConcerto:
            out[0] = preparedState.currentCon = finiteNumber(std::as_const(value)["current_con"]);
            break;
        case rust::WorkflowOp::WorkshopReadBuffTime:
            out[0] = preparedState.buffTime = finiteNumber(std::as_const(value)["buff_time"]);
            break;
        case rust::WorkflowOp::WorkshopReadStateAge:
            out[0] = finiteNumber(std::as_const(value)[commonTimes.at(int(a[0])).first]);
            break;
        case rust::WorkflowOp::WorkshopAssignStateTime:
            preparedState.*commonTimes.at(int(a[0])).second = a[1];
            break;
        case rust::WorkflowOp::WorkshopPrepareCharacter:
            prepared.append({character, preparedState});
            break;
        case rust::WorkflowOp::WorkshopPrepareCheckFlag:
            checks.append(stateBoolean(std::as_const(value)["check_f"]));
            break;
        case rust::WorkflowOp::WorkshopLoadFreezeValues:
            preparedFreezes.emplace();
            timeline = state["freezes"].toArray();
            break;
        case rust::WorkflowOp::WorkshopFreezeValueCount:
            out[0] = double(timeline.size());
            break;
        case rust::WorkflowOp::WorkshopSelectFreezeValue:
            freeze = timeline.at(int(a[0])).toObject();
            break;
        case rust::WorkflowOp::WorkshopReadFreezeNumber:
            out[0] = finiteNumber(
                std::as_const(freeze)[QStringList{"age", "duration", "threshold"}.at(int(a[0]))]);
            break;
        case rust::WorkflowOp::WorkshopPrepareFreeze:
            preparedFreezes->add(a[0], a[1], a[2], a[3]);
            break;
        case rust::WorkflowOp::WorkshopReadCombatAge:
            out[0] = finiteNumber(state["combat_age"]);
            break;
        case rust::WorkflowOp::WorkshopReadInLiberation:
            inLiberation = stateBoolean(state["in_liberation"]);
            break;
        case rust::WorkflowOp::WorkshopCommitCharacter:
            prepared.at(int(a[0])).first->state = prepared.at(int(a[0])).second;
            prepared.at(int(a[0])).first->checkFOnSwitch = checks.at(int(a[0]));
            break;
        case rust::WorkflowOp::WorkshopCommitFreezes:
            host->freezes = std::move(*preparedFreezes);
            break;
        case rust::WorkflowOp::WorkshopCommitCombatStart:
            host->combatStart = a[0];
            break;
        case rust::WorkflowOp::WorkshopCommitInLiberation:
            host->inLiberation = inLiberation;
            break;
        case rust::WorkflowOp::WorkshopHelloReady:
            out[0] = state["event"] == "ready";
            break;
        case rust::WorkflowOp::WorkshopHelloProtocol:
            out[0] = state["protocol"].toInt();
            break;
        case rust::WorkflowOp::WorkshopHelloApi:
            out[0] = state["api"].toInt();
            break;
        case rust::WorkflowOp::WorkshopReadCapabilities:
            capabilities = state["capabilities"].toArray();
            break;
        case rust::WorkflowOp::WorkshopHasCapability:
            out[0] = capabilities.contains(QStringList{"request-identity", "combat-state-ages",
                                                       "immutable-frame-cache", "bridge-metrics",
                                                       "character-policy", "task-config-context"}
                                               .at(int(a[0])));
            break;
        default:
            fail("Rust 工坊状态宿主无效");
        }
    });
    return result;
}
} // namespace
QJsonObject workshopCombatState(CombatHost &host) {
    return workshopStateRun(rust::WorkflowMethod::WorkshopCombatState, &host, {});
}
void applyWorkshopCombatState(CombatHost &host, const QJsonObject &state) {
    workshopStateRun(rust::WorkflowMethod::WorkshopApplyState, &host, state);
}
void validateWorkshopHandshake(const QJsonObject &hello) {
    workshopStateRun(rust::WorkflowMethod::WorkshopValidateHandshake, nullptr, hello);
}

QString workshopRoot(const QString &root) {
    QString result;
    rust::workflowRun(
        rust::WorkflowMethod::WorkshopRoot, {}, [&](rust::WorkflowOp op, const double *, double *out) {
            switch (op) {
            case rust::WorkflowOp::WorkshopBundledEngineExists:
                out[0] = QFile::exists(root + "/workshop-engine/workshop/bridge/workshop_worker.py");
                break;
            case rust::WorkflowOp::WorkshopReturnBundledRoot:
                result = root + "/workshop-engine";
                break;
            case rust::WorkflowOp::WorkshopReturnOriginalRoot:
                result = root;
                break;
            default:
                fail("Rust 工坊路径宿主无效");
            }
        });
    return result;
}
QString workshopPython(const QString &root) {
    QString result, engine;
    static const QStringList paths{"/runtime/python.exe", "/.venv/Scripts/python.exe"};
    rust::workflowRun(rust::WorkflowMethod::WorkshopPython, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::WorkshopResolveEngineRoot:
                              engine = workshopRoot(root);
                              break;
                          case rust::WorkflowOp::WorkshopPythonPathExists:
                              out[0] = QFile::exists(engine + paths.at(int(a[0])));
                              break;
                          case rust::WorkflowOp::WorkshopReturnPythonPath:
                              result = engine + paths.at(int(a[0]));
                              break;
                          default:
                              fail("Rust 工坊运行库路径宿主无效");
                          }
                      });
    return result;
}
static QProcessEnvironment environment() {
    auto result = QProcessEnvironment::systemEnvironment();
    rust::workflowRun(
        rust::WorkflowMethod::WorkshopEnvironment, {}, [&](rust::WorkflowOp op, const double *a, double *) {
            switch (op) {
            case rust::WorkflowOp::WorkshopInsertEnvironment: {
                const auto value = QVector<QPair<QString, QString>>{{"PYTHONUTF8", "1"},
                                                                    {"PYTHONIOENCODING", "utf-8"},
                                                                    {"PYTHONDONTWRITEBYTECODE", "1"}}
                                       .at(int(a[0]));
                result.insert(value.first, value.second);
                break;
            }
            case rust::WorkflowOp::WorkshopRemoveEnvironment:
                result.remove(
                    QStringList{"QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "PYTHONHOME", "PYTHONPATH"}
                        .at(int(a[0])));
                break;
            default:
                fail("Rust 工坊环境宿主无效");
            }
        });
    return result;
}
[[noreturn]] static void workshopFileFailure(int code, const QJsonObject &reply, QProcess *process) {
    switch (code) {
    case 1:
        fail("工坊管理器启动失败：" + process->errorString());
    case 3:
        fail(reply["error"].toString("工坊管理器返回无效，请查看兼容引擎日志"));
    case 7:
        fail(reply["error"].toString("工坊文件操作失败"));
    default:
        break;
    }
    static const QStringList messages{"缺少工坊 Python 兼容引擎，请使用完整包",
                                      "",
                                      "工坊代码检查超时",
                                      "",
                                      "工坊文件管理通道已断开",
                                      "工坊管理器返回数据过大",
                                      "工坊文件管理接口不匹配，请更新完整包",
                                      "",
                                      "工坊文件管理器已退出"};
    fail(messages.at(code));
}
QJsonObject workshopManage(const QString &root, const QString &data, QJsonObject request) {
    QProcess process;
    QString python, engine;
    QJsonObject reply, result;
    rust::workflowRun(
        rust::WorkflowMethod::WorkshopManage, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::WorkshopFileResolveRuntime:
                python = workshopPython(root);
                engine = workshopRoot(root);
                break;
            case rust::WorkflowOp::WorkshopFilePythonEmpty:
                out[0] = python.isEmpty();
                break;
            case rust::WorkflowOp::WorkshopFileSetEnvironment:
                process.setProcessEnvironment(environment());
                break;
            case rust::WorkflowOp::WorkshopFileSetWorkingDirectory:
                process.setWorkingDirectory(engine);
                break;
            case rust::WorkflowOp::WorkshopFileStartManager:
                process.start(python, {engine + "/workshop/bridge/workshop_manager.py", "--root", engine,
                                       "--data", data});
                break;
            case rust::WorkflowOp::WorkshopFileWaitStarted:
                out[0] = process.waitForStarted(int(a[0]));
                break;
            case rust::WorkflowOp::WorkshopFileWriteManagerRequest:
                process.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
                break;
            case rust::WorkflowOp::WorkshopFileCloseWriteChannel:
                process.closeWriteChannel();
                break;
            case rust::WorkflowOp::WorkshopFileLibraryRequest:
                out[0] = request["op"].toString().startsWith("library");
                break;
            case rust::WorkflowOp::WorkshopFileWaitFinished:
                out[0] = process.waitForFinished(int(a[0]));
                break;
            case rust::WorkflowOp::WorkshopFileKillProcess:
                process.kill();
                break;
            case rust::WorkflowOp::WorkshopFileWaitFinishedIgnored:
                process.waitForFinished(int(a[0]));
                break;
            case rust::WorkflowOp::WorkshopFileReadResponse:
                reply = QJsonDocument::fromJson(process.readAllStandardOutput().trimmed()).object();
                break;
            case rust::WorkflowOp::WorkshopFileExitCode:
                out[0] = process.exitCode();
                break;
            case rust::WorkflowOp::WorkshopFileReplyOk:
                out[0] = std::as_const(reply)["ok"].toBool();
                break;
            case rust::WorkflowOp::WorkshopFileReturnData:
                result = std::as_const(reply)["data"].toObject();
                break;
            case rust::WorkflowOp::WorkshopFileFailure:
                workshopFileFailure(int(a[0]), reply, &process);
                break;
            default:
                fail("Rust 工坊文件管理宿主无效");
            }
        });
    return result;
}
struct WorkshopFileSession::State {
    QString root, data;
    QThread thread;
    QObject *worker = new QObject;
    QProcess *process = nullptr;
    QByteArray buffer, errors;
    quint64 sequence = 0;
    std::atomic<qint64> pid{0};
    State(QString root, QString data) : root(std::move(root)), data(std::move(data)) {
        worker->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
        thread.start();
    }
    void stop() { run(rust::WorkflowMethod::WorkshopFileStop); }
    ~State() {
        QMetaObject::invokeMethod(worker, [this] { stop(); }, Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait();
    }
    QJsonObject run(rust::WorkflowMethod method, QJsonObject request = {}) {
        QString python, engine, id;
        QByteArray payload;
        QJsonObject reply, result;
        QElapsedTimer elapsed;
        qsizetype newline = -1;
        rust::workflowRun(method, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::WorkshopFileHasProcess:
                out[0] = process != nullptr;
                break;
            case rust::WorkflowOp::WorkshopFileProcessRunning:
                out[0] = process->state() != QProcess::NotRunning;
                break;
            case rust::WorkflowOp::WorkshopFileKillProcess:
                process->kill();
                break;
            case rust::WorkflowOp::WorkshopFileWaitFinishedIgnored:
                process->waitForFinished(int(a[0]));
                break;
            case rust::WorkflowOp::WorkshopFileResetPid:
                pid = 0;
                break;
            case rust::WorkflowOp::WorkshopFileClearBuffer:
                buffer.clear();
                break;
            case rust::WorkflowOp::WorkshopFileClearErrors:
                errors.clear();
                break;
            case rust::WorkflowOp::WorkshopFileCreateProcess:
                process = new QProcess(worker);
                break;
            case rust::WorkflowOp::WorkshopFileResolveRuntime:
                python = workshopPython(root);
                engine = workshopRoot(root);
                break;
            case rust::WorkflowOp::WorkshopFilePythonEmpty:
                out[0] = python.isEmpty();
                break;
            case rust::WorkflowOp::WorkshopFileSetEnvironment:
                process->setProcessEnvironment(environment());
                break;
            case rust::WorkflowOp::WorkshopFileSetWorkingDirectory:
                process->setWorkingDirectory(engine);
                break;
            case rust::WorkflowOp::WorkshopFileStartPersistentManager:
                process->start(python, {engine + "/workshop/bridge/workshop_manager.py", "--root", engine,
                                        "--data", data, "--persistent"});
                break;
            case rust::WorkflowOp::WorkshopFileWaitStarted:
                out[0] = process->waitForStarted(int(a[0]));
                break;
            case rust::WorkflowOp::WorkshopFileRememberPid:
                pid = process->processId();
                break;
            case rust::WorkflowOp::WorkshopFileAssignRequestIdentity:
                id = QString::number(++sequence);
                request["request_id"] = id;
                break;
            case rust::WorkflowOp::WorkshopFileEncodePayload:
                payload = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
                break;
            case rust::WorkflowOp::WorkshopFileWritePayload:
                out[0] = process->write(payload) == payload.size();
                break;
            case rust::WorkflowOp::WorkshopFileWaitWritten:
                process->waitForBytesWritten(int(a[0]));
                break;
            case rust::WorkflowOp::WorkshopFileStartTimer:
                elapsed.start();
                break;
            case rust::WorkflowOp::WorkshopFileElapsed:
                out[0] = double(elapsed.elapsed());
                break;
            case rust::WorkflowOp::WorkshopFileReadStderr:
                errors += process->readAllStandardError();
                break;
            case rust::WorkflowOp::WorkshopFileTrimStderr:
                errors = errors.right(qsizetype(a[0]));
                break;
            case rust::WorkflowOp::WorkshopFileReadStdout:
                buffer += process->readAllStandardOutput();
                break;
            case rust::WorkflowOp::WorkshopFileBufferSize:
                out[0] = double(buffer.size());
                break;
            case rust::WorkflowOp::WorkshopFileNewline:
                newline = buffer.indexOf('\n');
                out[0] = double(newline);
                break;
            case rust::WorkflowOp::WorkshopFileParseReply:
                reply = QJsonDocument::fromJson(buffer.left(newline)).object();
                break;
            case rust::WorkflowOp::WorkshopFileConsumeLine:
                buffer.remove(0, newline + 1);
                break;
            case rust::WorkflowOp::WorkshopFileReplyIdentityMatches:
                out[0] = std::as_const(reply)["request_id"] == id;
                break;
            case rust::WorkflowOp::WorkshopFileManagerProtocol:
                out[0] = std::as_const(reply)["manager_protocol"].toInt();
                break;
            case rust::WorkflowOp::WorkshopFileReplyOk:
                out[0] = std::as_const(reply)["ok"].toBool();
                break;
            case rust::WorkflowOp::WorkshopFileReturnData:
                result = std::as_const(reply)["data"].toObject();
                break;
            case rust::WorkflowOp::WorkshopFileWaitReadyRead:
                process->waitForReadyRead(int(a[0]));
                break;
            case rust::WorkflowOp::WorkshopFileFailure:
                workshopFileFailure(int(a[0]), reply, process);
                break;
            default:
                fail("Rust 工坊持久文件会话宿主无效");
            }
        });
        return result;
    }
    QJsonObject exchange(QJsonObject request) {
        return run(rust::WorkflowMethod::WorkshopFileExchange, std::move(request));
    }
};
WorkshopFileSession::WorkshopFileSession(QString root, QString data)
    : state_(std::make_unique<State>(std::move(root), std::move(data))) {}
WorkshopFileSession::~WorkshopFileSession() = default;
qint64 WorkshopFileSession::processId() const { return state_->pid.load(); }
QJsonObject WorkshopFileSession::command(QJsonObject request) {
    QJsonObject result;
    std::exception_ptr failure;
    QMetaObject::invokeMethod(
        state_->worker,
        [&] {
            try {
                rust::workflowRun(rust::WorkflowMethod::WorkshopFileCommand, {},
                                  [&](rust::WorkflowOp op, const double *, double *) {
                                      switch (op) {
                                      case rust::WorkflowOp::WorkshopFileExchangeNested:
                                          result = state_->exchange(std::move(request));
                                          break;
                                      case rust::WorkflowOp::WorkshopFileStopNested:
                                          state_->stop();
                                          break;
                                      default:
                                          fail("Rust 工坊文件命令宿主无效");
                                      }
                                  });
            } catch (...) {
                failure = std::current_exception();
            }
        },
        Qt::BlockingQueuedConnection);
    // Exceptions are transported on their owning C++ thread, never through Qt.
    if (failure)
        std::rethrow_exception(failure);
    return result;
}
PythonTeamMacro::PythonTeamMacro(CombatHost &host, QString root, QString data)
    : TeamMacro(host), root_(std::move(root)), data_(std::move(data)), pythonRoot_(workshopRoot(root_)) {}
PythonTeamMacro::~PythonTeamMacro() { stop(); }

namespace {
[[noreturn]] void workshopRuntimeFailure(int code, const QJsonObject &message, const QJsonObject &reply,
                                         const QJsonObject &result, const QByteArray &errors) {
    switch (code) {
    case 5:
        throw ExecutionFailure(message["error"].toString("工坊兼容引擎启动失败"),
                               message["traceback"].toString());
    case 6:
        fail("工坊兼容引擎已退出：" + QString::fromUtf8(errors.right(2000)));
    case 13:
        throw ExecutionFailure(reply["error"].toString("工坊引擎返回无效"), reply["traceback"].toString());
    case 15:
        throw CombatInterrupted(result["interrupted"].toString());
    default:
        break;
    }
    static const QStringList messages{"工坊兼容引擎连接已断开",
                                      "工坊图像格式无效",
                                      "工坊兼容引擎返回数据过大",
                                      "已丢弃过期工坊输入请求",
                                      "工坊完成接口校验前不能发送操作",
                                      "",
                                      "",
                                      "工坊连招执行超时",
                                      "工坊 Python 运行库缺失，请使用完整包",
                                      "无法创建工坊共享图像",
                                      "无法映射工坊共享图像",
                                      "工坊兼容引擎启动失败",
                                      "工坊返回了过期或不匹配的回复",
                                      "",
                                      "工坊缺少角色战斗规则",
                                      "",
                                      "工坊返回了无效的切人优先级",
                                      "工坊返回了无效的战斗检测规则"};
    fail(messages.at(code));
}
} // namespace
PythonTeamMacro::WorkshopResult PythonTeamMacro::runWorkshop(int method, std::span<const double> args,
                                                             QJsonObject request, const Character *selected) {
    QJsonObject result, reply, message, rpcResult, policy;
    QByteArray bytes, line;
    qsizetype newline = 0;
    cv::Mat image;
    uint64_t sequence = 0;
    QString python;
    QVector<Character *> party;
    QJsonArray loadParty;
    QHash<int, bool> healers, shortTargets;
    QElapsedTimer startup, ping, copyTimer, receiveTimer;
    const auto output = rust::workflowRun(
        rust::WorkflowMethod(method), args,
        [&](rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &error) {
            switch (op) {
            case rust::WorkflowOp::WorkshopRuntimeFailure:
                workshopRuntimeFailure(int(a[0]), message, reply, result, errors_);
                break;
            case rust::WorkflowOp::WorkshopProcessRunning:
                out[0] = process_.state() != QProcess::NotRunning;
                break;
            case rust::WorkflowOp::WorkshopKillProcess:
                process_.kill();
                break;
            case rust::WorkflowOp::WorkshopWaitFinishedIgnored:
                process_.waitForFinished(int(a[0]));
                break;
            case rust::WorkflowOp::WorkshopMappedPresent:
                out[0] = mapped_ != nullptr;
                break;
            case rust::WorkflowOp::WorkshopMappingPresent:
                out[0] = mapping_ != nullptr;
                break;
            case rust::WorkflowOp::WorkshopUnmapImage:
                UnmapViewOfFile(mapped_);
                break;
            case rust::WorkflowOp::WorkshopCloseMapping:
                CloseHandle(mapping_);
                break;
            case rust::WorkflowOp::WorkshopResetMappedPointer:
                mapped_ = nullptr;
                break;
            case rust::WorkflowOp::WorkshopResetMappingHandle:
                mapping_ = nullptr;
                break;
            case rust::WorkflowOp::WorkshopSetLoaded:
                loaded_ = a[0] != 0;
                break;
            case rust::WorkflowOp::WorkshopOriginalPolicyCount:
                out[0] = double(originalPolicies_.size());
                break;
            case rust::WorkflowOp::WorkshopRestoreOriginalPolicy: {
                const auto &p = originalPolicies_.at(int(a[0]));
                p.character->state.role = p.role;
                p.character->state.buffTime = p.buffTime;
                p.character->checkFOnSwitch = p.checkF;
                break;
            }
            case rust::WorkflowOp::WorkshopClearOriginalPolicies:
                originalPolicies_.clear();
                break;
            case rust::WorkflowOp::WorkshopClearHealerOverrides:
                healerOverrides_.clear();
                break;
            case rust::WorkflowOp::WorkshopClearShortOverrides:
                shortTargetOverrides_.clear();
                break;
            case rust::WorkflowOp::WorkshopClearBuffer:
                buffer_.clear();
                break;
            case rust::WorkflowOp::WorkshopClearErrors:
                errors_.clear();
                break;
            case rust::WorkflowOp::WorkshopResetFrameSequence:
                frameSequence_ = UINT64_MAX;
                break;
            case rust::WorkflowOp::WorkshopResetFrameSize:
                frameSize_ = {};
                break;
            case rust::WorkflowOp::WorkshopResetLastFrameTime:
                lastFrameAt_ = 0;
                break;
            case rust::WorkflowOp::WorkshopEncodeMessage:
                bytes = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
                break;
            case rust::WorkflowOp::WorkshopWriteMessage:
                out[0] = process_.write(bytes) == bytes.size();
                break;
            case rust::WorkflowOp::WorkshopWaitWritten:
                process_.waitForBytesWritten(int(a[0]));
                break;
            case rust::WorkflowOp::WorkshopRequestKind: {
                static const QHash<QString, int> kinds{{"frame", 0}, {"key", 1}, {"button", 2}};
                out[0] = kinds.value(request["op"].toString(), 3);
                break;
            }
            case rust::WorkflowOp::WorkshopCountFrameRequest:
                ++frameRequests_;
                break;
            case rust::WorkflowOp::WorkshopRefreshObservation:
                h_.refreshObservation();
                break;
            case rust::WorkflowOp::WorkshopReadFrameImage:
                image = h_.frameImage();
                break;
            case rust::WorkflowOp::WorkshopFrameEmpty:
                out[0] = image.empty();
                break;
            case rust::WorkflowOp::WorkshopReturnUnavailable:
                result = {{"available", false}};
                break;
            case rust::WorkflowOp::WorkshopFrameBgr:
                out[0] = image.type() == CV_8UC3;
                break;
            case rust::WorkflowOp::WorkshopFrameBytes:
                out[0] = double(image.total() * 3);
                break;
            case rust::WorkflowOp::WorkshopFrameContinuous:
                out[0] = image.isContinuous();
                break;
            case rust::WorkflowOp::WorkshopCloneFrame:
                image = image.clone();
                break;
            case rust::WorkflowOp::WorkshopReadObservationId:
                sequence = h_.observationId();
                break;
            case rust::WorkflowOp::WorkshopObservationIdZero:
                out[0] = sequence == 0;
                break;
            case rust::WorkflowOp::WorkshopFrameSequenceMatches:
                out[0] = frameSequence_ == sequence;
                break;
            case rust::WorkflowOp::WorkshopFrameSizeMatches:
                out[0] = frameSize_ == QSize(image.cols, image.rows);
                break;
            case rust::WorkflowOp::WorkshopStartCopyTimer:
                copyTimer.start();
                break;
            case rust::WorkflowOp::WorkshopCopyMappedFrame:
                memcpy(mapped_, image.data, image.total() * 3);
                break;
            case rust::WorkflowOp::WorkshopMeasureCopyTime:
                copyMs_ = copyTimer.nsecsElapsed() / 1e6;
                break;
            case rust::WorkflowOp::WorkshopCountFrameCopy:
                ++frameCopies_;
                break;
            case rust::WorkflowOp::WorkshopCacheFrameSequence:
                frameSequence_ = sequence;
                break;
            case rust::WorkflowOp::WorkshopCacheFrameSize:
                frameSize_ = {image.cols, image.rows};
                break;
            case rust::WorkflowOp::WorkshopStampFrameTime:
                lastFrameAt_ = nowNs();
                break;
            case rust::WorkflowOp::WorkshopTelemetryTimerValid:
                out[0] = telemetryClock_.isValid();
                break;
            case rust::WorkflowOp::WorkshopTelemetryElapsed:
                out[0] = double(telemetryClock_.elapsed());
                break;
            case rust::WorkflowOp::WorkshopReportNested:
                report();
                break;
            case rust::WorkflowOp::WorkshopRestartTelemetryTimer:
                telemetryClock_.restart();
                break;
            case rust::WorkflowOp::WorkshopReturnFrame:
                result = {{"available", true},
                          {"size", double(image.total() * 3)},
                          {"width", image.cols},
                          {"height", image.rows},
                          {"stride", image.cols * 3},
                          {"format", "RGB3"},
                          {"sequence", QString::number(sequence ? sequence : frameCopies_)}};
                break;
            case rust::WorkflowOp::WorkshopHasLastFrameTime:
                out[0] = lastFrameAt_ != 0;
                break;
            case rust::WorkflowOp::WorkshopRequestDown:
                out[0] = request["down"].toBool();
                break;
            case rust::WorkflowOp::WorkshopAppendFrameInputSample:
                frameInputSamples_.append((nowNs() - lastFrameAt_) / 1e6);
                break;
            case rust::WorkflowOp::WorkshopFrameInputSampleCount:
                out[0] = double(frameInputSamples_.size());
                break;
            case rust::WorkflowOp::WorkshopRemoveFirstInputSample:
                frameInputSamples_.removeFirst();
                break;
            case rust::WorkflowOp::WorkshopForwardCommand:
                result = h_.workshopCommand(std::move(request));
                break;
            case rust::WorkflowOp::WorkshopStartReceiveTimer:
                receiveTimer.start();
                break;
            case rust::WorkflowOp::WorkshopReceiveElapsed:
                out[0] = double(receiveTimer.elapsed());
                break;
            case rust::WorkflowOp::WorkshopCheckpoint:
                h_.workshopCheckpoint();
                break;
            case rust::WorkflowOp::WorkshopReadStderr:
                errors_ += process_.readAllStandardError();
                break;
            case rust::WorkflowOp::WorkshopStderrSize:
                out[0] = double(errors_.size());
                break;
            case rust::WorkflowOp::WorkshopTrimStderr:
                errors_ = errors_.right(qsizetype(a[0]));
                break;
            case rust::WorkflowOp::WorkshopReadStdout:
                buffer_ += process_.readAllStandardOutput();
                break;
            case rust::WorkflowOp::WorkshopBufferSize:
                out[0] = double(buffer_.size());
                break;
            case rust::WorkflowOp::WorkshopBufferHasLine:
                out[0] = buffer_.contains('\n');
                break;
            case rust::WorkflowOp::WorkshopReadLine:
                newline = buffer_.indexOf('\n');
                line = buffer_.left(newline);
                break;
            case rust::WorkflowOp::WorkshopConsumeLine:
                buffer_.remove(0, newline + 1);
                break;
            case rust::WorkflowOp::WorkshopParseLine:
                message = QJsonDocument::fromJson(line).object();
                break;
            case rust::WorkflowOp::WorkshopMessageRpc:
                out[0] = std::as_const(message)["event"] == "rpc";
                break;
            case rust::WorkflowOp::WorkshopLoaded:
                out[0] = loaded_;
                break;
            case rust::WorkflowOp::WorkshopMessageSessionMatches:
                out[0] = std::as_const(message)["session"] == session_;
                break;
            case rust::WorkflowOp::WorkshopMessageRequestMatches:
                out[0] = std::as_const(message)["request_id"] == requestId_;
                break;
            case rust::WorkflowOp::WorkshopPreHandshakeAllowed:
                out[0] = QStringList{"state", "heartbeat", "probe", "connect", "release"}.contains(
                    std::as_const(message)["request"].toObject()["op"].toString());
                break;
            case rust::WorkflowOp::WorkshopRpcNested:
                rpcResult = rpc(std::as_const(message)["request"].toObject());
                break;
            case rust::WorkflowOp::WorkshopWriteRpcSuccess:
                write({{"ok", true}, {"data", rpcResult}});
                break;
            case rust::WorkflowOp::WorkshopWriteRpcError:
                try {
                    std::rethrow_exception(error);
                } catch (const std::exception &e) {
                    write({{"ok", false}, {"error", errorText(e)}});
                }
                break;
            case rust::WorkflowOp::WorkshopMessageFatal:
                out[0] = std::as_const(message)["event"] == "fatal";
                break;
            case rust::WorkflowOp::WorkshopMessageEmpty:
                out[0] = message.isEmpty();
                break;
            case rust::WorkflowOp::WorkshopReturnMessage:
                result = message;
                break;
            case rust::WorkflowOp::WorkshopWaitReadyRead:
                process_.waitForReadyRead(int(a[0]));
                break;
            case rust::WorkflowOp::WorkshopStartStartupTimer:
                startup.start();
                break;
            case rust::WorkflowOp::WorkshopResetFrameCounters:
                frameCopies_ = frameRequests_ = 0;
                break;
            case rust::WorkflowOp::WorkshopResetStartupMetrics:
                startupMs_ = pingMs_ = copyMs_ = 0;
                break;
            case rust::WorkflowOp::WorkshopClearFrameInputSamples:
                frameInputSamples_.clear();
                break;
            case rust::WorkflowOp::WorkshopInvalidateTelemetryTimer:
                telemetryClock_.invalidate();
                break;
            case rust::WorkflowOp::WorkshopNewSession:
                session_ = QUuid::createUuid().toString(QUuid::Id128);
                break;
            case rust::WorkflowOp::WorkshopResolvePython:
                python = workshopPython(root_);
                break;
            case rust::WorkflowOp::WorkshopPythonMissing:
                out[0] = python.isEmpty();
                break;
            case rust::WorkflowOp::WorkshopNewMappingName:
                mappingName_ = "Local\\OKWW-workshop-" + QUuid::createUuid().toString(QUuid::Id128);
                break;
            case rust::WorkflowOp::WorkshopCreateMapping:
                mapping_ =
                    CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 64 * 1024 * 1024,
                                       reinterpret_cast<LPCWSTR>(mappingName_.utf16()));
                break;
            case rust::WorkflowOp::WorkshopMapImage:
                mapped_ = MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, 64 * 1024 * 1024);
                break;
            case rust::WorkflowOp::WorkshopSetEnvironment:
                process_.setProcessEnvironment(environment());
                break;
            case rust::WorkflowOp::WorkshopSetWorkingDirectory:
                process_.setWorkingDirectory(pythonRoot_);
                break;
            case rust::WorkflowOp::WorkshopStartProcess:
                process_.start(python, {pythonRoot_ + "/workshop/bridge/workshop_worker.py", "--root",
                                        pythonRoot_, "--data", data_, "--mapping", mappingName_});
                break;
            case rust::WorkflowOp::WorkshopWaitStarted:
                out[0] = process_.waitForStarted(int(a[0]));
                break;
            case rust::WorkflowOp::WorkshopReceiveNested:
                reply = receive(int(a[0]));
                break;
            case rust::WorkflowOp::WorkshopValidateHello:
                validateWorkshopHandshake(reply);
                break;
            case rust::WorkflowOp::WorkshopLoadRuntimeParty:
                party = h_.characters();
                break;
            case rust::WorkflowOp::WorkshopRuntimePartyCount:
                out[0] = double(party.size());
                break;
            case rust::WorkflowOp::WorkshopSaveOriginalPolicy: {
                auto *c = party.at(int(a[0]));
                originalPolicies_.append({c, c->state.role, c->state.buffTime, c->checkFOnSwitch});
                break;
            }
            case rust::WorkflowOp::WorkshopAppendLoadCharacter: {
                auto *c = party.at(int(a[0]));
                loadParty.append(QJsonObject{
                    {"class", c->state.className},
                    {"canonical", c->state.name},
                    {"index", c->state.index},
                    {"element", c->state.ringIndex},
                    {"role", QStringList{"MainDps", "SubDps", "Healer"}.value(int(c->state.role), "MainDps")},
                    {"buff", c->state.buffTime}});
                break;
            }
            case rust::WorkflowOp::WorkshopCallLoad:
                result = call({{"op", "load"}, {"party", loadParty}});
                break;
            case rust::WorkflowOp::WorkshopSyncNested:
                sync(result);
                break;
            case rust::WorkflowOp::WorkshopMeasureStartup:
                startupMs_ = startup.nsecsElapsed() / 1e6;
                break;
            case rust::WorkflowOp::WorkshopClearPingSamples:
                pingSamples_.clear();
                break;
            case rust::WorkflowOp::WorkshopStartPingTimer:
                ping.start();
                break;
            case rust::WorkflowOp::WorkshopCallPing:
                call({{"op", "ping"}});
                break;
            case rust::WorkflowOp::WorkshopAppendPingSample:
                pingSamples_.append(ping.nsecsElapsed() / 1e6);
                break;
            case rust::WorkflowOp::WorkshopSetMedianPing:
                pingMs_ = percentile(pingSamples_, .5);
                break;
            case rust::WorkflowOp::WorkshopStopNested:
                stop();
                break;
            case rust::WorkflowOp::WorkshopReadBusy:
                out[0] = h_.macroBusy;
                break;
            case rust::WorkflowOp::WorkshopSetBusy:
                h_.macroBusy = a[0] != 0;
                break;
            case rust::WorkflowOp::WorkshopFillTaskContext:
                switch (int(a[0])) {
                case 0:
                    request["use_liberation"] = h_.useLiberation;
                    break;
                case 1:
                    request["in_combat"] = h_.combatEngaged();
                    break;
                case 2:
                    request["has_levitator"] = h_.hasLevitator;
                    break;
                case 3:
                    request["current"] = h_.partyIndex();
                    break;
                case 4:
                    request["f_rearm"] = h_.macroRearmAt;
                    break;
                case 5:
                    request["f_age"] = h_.now() - h_.macroRearmAt;
                    break;
                }
                break;
            case rust::WorkflowOp::WorkshopAssignRequestId:
                requestId_ = QString::number(++requestSequence_);
                break;
            case rust::WorkflowOp::WorkshopFillRequestIdentity:
                request["protocol"] = workshopProtocolVersion;
                request["api"] = workshopApiVersion;
                request["session"] = session_;
                request["request_id"] = requestId_;
                break;
            case rust::WorkflowOp::WorkshopFillTaskId:
                request["task"] = h_.taskId();
                break;
            case rust::WorkflowOp::WorkshopFillTaskConfig:
                request["task_config"] = h_.workshopTaskConfig();
                break;
            case rust::WorkflowOp::WorkshopFillHostState:
                request["host_state"] = workshopCombatState(h_);
                break;
            case rust::WorkflowOp::WorkshopWriteRequest:
                write(request);
                break;
            case rust::WorkflowOp::WorkshopReplyEvent:
                out[0] = std::as_const(reply)["event"] == "reply";
                break;
            case rust::WorkflowOp::WorkshopReplySessionMatches:
                out[0] = std::as_const(reply)["session"] == session_;
                break;
            case rust::WorkflowOp::WorkshopReplyRequestMatches:
                out[0] = std::as_const(reply)["request_id"] == requestId_;
                break;
            case rust::WorkflowOp::WorkshopReplyOk:
                out[0] = std::as_const(reply)["ok"].toBool();
                break;
            case rust::WorkflowOp::WorkshopReturnReplyData:
                result = std::as_const(reply)["data"].toObject();
                break;
            case rust::WorkflowOp::WorkshopReleaseCommand:
                h_.workshopCommand({{"op", "release"}});
                break;
            case rust::WorkflowOp::WorkshopSyncCharacterCount:
                out[0] = double(request["chars"].toArray().size());
                break;
            case rust::WorkflowOp::WorkshopSelectPolicyCharacter:
                policy = request["chars"].toArray().at(int(a[0])).toObject();
                break;
            case rust::WorkflowOp::WorkshopPolicyHealerBool:
                out[0] = std::as_const(policy)["healer"].isBool();
                break;
            case rust::WorkflowOp::WorkshopPolicyShortTargetBool:
                out[0] = std::as_const(policy)["short_target"].isBool();
                break;
            case rust::WorkflowOp::WorkshopCollectHealerPolicy:
                healers[std::as_const(policy)["index"].toInt()] = std::as_const(policy)["healer"].toBool();
                break;
            case rust::WorkflowOp::WorkshopCollectShortPolicy:
                shortTargets[std::as_const(policy)["index"].toInt()] =
                    std::as_const(policy)["short_target"].toBool();
                break;
            case rust::WorkflowOp::WorkshopApplyStateNested:
                applyWorkshopCombatState(h_, request);
                break;
            case rust::WorkflowOp::WorkshopCommitHealerPolicies:
                healerOverrides_ = std::move(healers);
                break;
            case rust::WorkflowOp::WorkshopCommitShortPolicies:
                shortTargetOverrides_ = std::move(shortTargets);
                break;
            case rust::WorkflowOp::WorkshopPublishTelemetry:
                h_.workshopTelemetry({{"protocol", workshopProtocolVersion},
                                      {"api", workshopApiVersion},
                                      {"startup_ms", startupMs_},
                                      {"ping_ms", pingMs_},
                                      {"last_copy_ms", copyMs_},
                                      {"ping_p95_ms", percentile(pingSamples_, .95)},
                                      {"ping_samples", pingSamples_.size()},
                                      {"frame_input_p50_ms", percentile(frameInputSamples_, .5)},
                                      {"frame_input_p95_ms", percentile(frameInputSamples_, .95)},
                                      {"frame_input_samples", frameInputSamples_.size()},
                                      {"frame_requests", double(frameRequests_)},
                                      {"frame_copies", double(frameCopies_)}});
                break;
            case rust::WorkflowOp::WorkshopStartNested:
                startWorker();
                break;
            case rust::WorkflowOp::WorkshopCallReset:
                result = call({{"op", "reset"}});
                break;
            case rust::WorkflowOp::WorkshopCallEnd:
                result = call({{"op", "end"}});
                break;
            case rust::WorkflowOp::WorkshopCallPerform:
                result = call({{"op", "perform"}, {"index", selected->state.index}});
                break;
            case rust::WorkflowOp::WorkshopResultInterrupted:
                out[0] = result.contains("interrupted");
                break;
            case rust::WorkflowOp::WorkshopCallPriority:
                result = call({{"op", "priority"},
                               {"index", selected->state.index},
                               {"intro", a[0] != 0},
                               {"target_low_con", a[1] != 0}});
                break;
            case rust::WorkflowOp::WorkshopPriorityNumber:
                out[0] = std::as_const(result)["priority"].isDouble();
                break;
            case rust::WorkflowOp::WorkshopPriorityInteger:
                out[0] = std::as_const(result)["priority"].toInt();
                break;
            case rust::WorkflowOp::WorkshopCallPolicy:
                result = call({{"op", "policy"}, {"index", selected->state.index}});
                break;
            case rust::WorkflowOp::WorkshopSkipCheckBool:
                out[0] = std::as_const(result)["skip_combat_check"].isBool();
                break;
            case rust::WorkflowOp::WorkshopSkipCheckValue:
                out[0] = std::as_const(result)["skip_combat_check"].toBool();
                break;
            default:
                fail("Rust 工坊运行宿主无效");
            }
        });
    return {result, output};
}
void PythonTeamMacro::stop() noexcept { runWorkshop(int(rust::WorkflowMethod::WorkshopStop)); }
void PythonTeamMacro::write(QJsonObject value) {
    runWorkshop(int(rust::WorkflowMethod::WorkshopWrite), {}, std::move(value));
}
QJsonObject PythonTeamMacro::rpc(QJsonObject request) {
    return runWorkshop(int(rust::WorkflowMethod::WorkshopRpc), {}, std::move(request)).object;
}
QJsonObject PythonTeamMacro::receive(int timeoutMs) {
    const std::array<double, 1> args{double(timeoutMs)};
    return runWorkshop(int(rust::WorkflowMethod::WorkshopReceive), args).object;
}
void PythonTeamMacro::startWorker() { runWorkshop(int(rust::WorkflowMethod::WorkshopStart)); }
QJsonObject PythonTeamMacro::call(QJsonObject request) {
    return runWorkshop(int(rust::WorkflowMethod::WorkshopCall), {}, std::move(request)).object;
}
void PythonTeamMacro::sync(const QJsonObject &state) {
    runWorkshop(int(rust::WorkflowMethod::WorkshopSync), {}, state);
}
void PythonTeamMacro::report() { runWorkshop(int(rust::WorkflowMethod::WorkshopReport)); }
void PythonTeamMacro::configure() { startWorker(); }
void PythonTeamMacro::reset() { runWorkshop(int(rust::WorkflowMethod::WorkshopReset)); }
void PythonTeamMacro::combatEnd() { runWorkshop(int(rust::WorkflowMethod::WorkshopCombatEnd)); }
bool PythonTeamMacro::perform(Character &entry) {
    return runWorkshop(int(rust::WorkflowMethod::WorkshopPerform), {}, {}, &entry).values[0] != 0;
}
std::optional<int> PythonTeamMacro::priority(const Character &c, bool intro) {
    return priority(c, intro, false);
}
std::optional<int> PythonTeamMacro::priority(const Character &c, bool intro, bool low) {
    const std::array<double, 2> args{double(intro), double(low)};
    return int(runWorkshop(int(rust::WorkflowMethod::WorkshopPriority), args, {}, &c).values[0]);
}
bool PythonTeamMacro::skipsCombatCheck(const Character &c) {
    return runWorkshop(int(rust::WorkflowMethod::WorkshopSkipsCombatCheck), {}, {}, &c).values[0] != 0;
}
} // namespace okww::runtime
