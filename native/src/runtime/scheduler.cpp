#include "scheduler.hpp"
#include "scripts.hpp"
#include "workflow_abi.hpp"
#include <QCryptographicHash>
#include <QXmlStreamWriter>
#include <sddl.h>
namespace okww::runtime {
namespace {
const QStringList days{"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
const QStringList months{"January", "February", "March",     "April",   "May",      "June",
                         "July",    "August",   "September", "October", "November", "December"};
[[noreturn]] void scheduleFailure(int code, const QString &detail = {}) {
    const QStringList messages{"无法读取当前 Windows 用户",
                               "无法读取 Windows 用户标识",
                               "无法转换 Windows 用户标识",
                               "无法启动 Windows 任务计划工具",
                               "Windows 任务计划操作超时",
                               "Windows 任务计划操作失败：" + detail,
                               "计划参数无效：" + detail,
                               "计划任务需要选择有效的一次性任务",
                               "计划重复方式无效",
                               "计划开始日期和时间无效",
                               "不能修改此配置以外的计划任务",
                               "无法创建计划任务临时目录",
                               "无法写入计划任务文件",
                               "计划不存在"};
    fail(messages.at(code));
}
int number(const QJsonObject &s, const char *key, int fallback, int low, int high) {
    const auto value = s.value(key);
    const std::array args{double(fallback), double(low), double(high)};
    const auto result = rust::workflowRun(rust::WorkflowMethod::ScheduleNumber, args,
                                          [&](rust::WorkflowOp op, const double *a, double *out) {
                                              switch (op) {
                                              case rust::WorkflowOp::ScheduleNumberUndefined:
                                                  out[0] = value.isUndefined();
                                                  break;
                                              case rust::WorkflowOp::ScheduleNumberValue:
                                                  out[0] = value.toDouble(-1);
                                                  break;
                                              case rust::WorkflowOp::ScheduleFailure:
                                                  scheduleFailure(int(a[0]), QString::fromLatin1(key));
                                                  break;
                                              default:
                                                  fail("Rust 计划数值宿主无效");
                                              }
                                          });
    return int(result[0]);
}
} // namespace
Scheduler::Scheduler(QString executable, QString root, QString data, QString user, ScheduleCommand command)
    : executable_(QFileInfo(executable).absoluteFilePath()), root_(QDir(root).absolutePath()),
      data_(QDir(data).absolutePath()), user_(std::move(user)), command_(std::move(command)) {
    rust::workflowRun(
        rust::WorkflowMethod::ScheduleInitialize, {}, [&](rust::WorkflowOp op, const double *, double *out) {
            switch (op) {
            case rust::WorkflowOp::ScheduleUserEmpty:
                out[0] = user_.isEmpty();
                break;
            case rust::WorkflowOp::ScheduleFillCurrentUser:
                user_ = currentUser();
                break;
            case rust::WorkflowOp::ScheduleHasCommand:
                out[0] = bool(command_);
                break;
            case rust::WorkflowOp::ScheduleFillDefaultCommand:
                command_ = runCommand;
                break;
            case rust::WorkflowOp::ScheduleBuildPrefix:
                prefix_ = "\\OKWW-Cpp-" +
                          QString::fromLatin1(QCryptographicHash::hash(data_.toCaseFolded().toUtf8(),
                                                                       QCryptographicHash::Sha256)
                                                  .toHex()
                                                  .left(12)) +
                          "-";
                break;
            default:
                fail("Rust 计划初始化宿主无效");
            }
        });
}
QString Scheduler::currentUser() {
    HANDLE token = nullptr;
    DWORD required = 0;
    QByteArray buffer;
    LPWSTR value = nullptr;
    QString result;
    rust::workflowRun(rust::WorkflowMethod::ScheduleCurrentUser, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::ScheduleOpenToken:
                              out[0] = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token);
                              break;
                          case rust::WorkflowOp::ScheduleQueryTokenSize:
                              GetTokenInformation(token, TokenUser, nullptr, 0, &required);
                              break;
                          case rust::WorkflowOp::ScheduleAllocateToken:
                              buffer = QByteArray(int(required), 0);
                              break;
                          case rust::WorkflowOp::ScheduleReadToken:
                              out[0] =
                                  GetTokenInformation(token, TokenUser, buffer.data(), required, &required);
                              break;
                          case rust::WorkflowOp::ScheduleCloseToken:
                              CloseHandle(token);
                              break;
                          case rust::WorkflowOp::ScheduleConvertSid:
                              out[0] = ConvertSidToStringSidW(
                                  reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid, &value);
                              break;
                          case rust::WorkflowOp::ScheduleReadSidString:
                              result = QString::fromWCharArray(value);
                              break;
                          case rust::WorkflowOp::ScheduleFreeSid:
                              LocalFree(value);
                              break;
                          case rust::WorkflowOp::ScheduleFailure:
                              scheduleFailure(int(a[0]));
                              break;
                          default:
                              fail("Rust Windows 用户宿主无效");
                          }
                      });
    return result;
}
QByteArray Scheduler::runCommand(const QStringList &arguments) {
    QProcess p;
    QByteArray result;
    rust::workflowRun(
        rust::WorkflowMethod::ScheduleRunCommand, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::ScheduleHideCommandWindow:
                p.setCreateProcessArgumentsModifier([](auto *args) { args->flags |= CREATE_NO_WINDOW; });
                break;
            case rust::WorkflowOp::ScheduleStartCommand:
                p.start(qEnvironmentVariable("SystemRoot") + "/System32/schtasks.exe", arguments);
                break;
            case rust::WorkflowOp::ScheduleCommandStarted:
                out[0] = p.waitForStarted(int(a[0]));
                break;
            case rust::WorkflowOp::ScheduleCommandFinished:
                out[0] = p.waitForFinished(int(a[0]));
                break;
            case rust::WorkflowOp::ScheduleKillCommand:
                p.kill();
                break;
            case rust::WorkflowOp::ScheduleWaitCommand:
                p.waitForFinished(int(a[0]));
                break;
            case rust::WorkflowOp::ScheduleCommandNormal:
                out[0] = p.exitStatus() == QProcess::NormalExit;
                break;
            case rust::WorkflowOp::ScheduleCommandExitCode:
                out[0] = p.exitCode();
                break;
            case rust::WorkflowOp::ScheduleReadCommandOutput:
                result = p.readAllStandardOutput();
                break;
            case rust::WorkflowOp::ScheduleFailure:
                if (int(a[0]) == 5)
                    scheduleFailure(5, QString::fromLocal8Bit(p.readAllStandardError()).trimmed());
                else
                    scheduleFailure(int(a[0]));
                break;
            default:
                fail("Rust 计划进程宿主无效");
            }
        });
    return result;
}
QString Scheduler::quoteArgument(const QString &arg) {
    QString result;
    rust::workflowRun(rust::WorkflowMethod::ScheduleQuoteArgument, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::ScheduleQuoteStart:
                              result = "\"";
                              break;
                          case rust::WorkflowOp::ScheduleQuoteLength:
                              out[0] = double(arg.size());
                              break;
                          case rust::WorkflowOp::ScheduleQuoteUnit:
                              out[0] = arg.at(int(a[0])).unicode();
                              break;
                          case rust::WorkflowOp::ScheduleQuoteAppendUnit:
                              result += QString(int(a[1]), '\\') + arg.at(int(a[0]));
                              break;
                          case rust::WorkflowOp::ScheduleQuoteFinish:
                              result += QString(int(a[0]), '\\') + '\"';
                              break;
                          default:
                              fail("Rust 计划参数转义宿主无效");
                          }
                      });
    return result;
}
QString Scheduler::xml(const QJsonObject &s) const {
    const auto task = s["task"].toString();
    QJsonObject catalog, entry;
    QJsonArray nativeTasks, scripts;
    QDateTime start;
    QString text;
    std::optional<QXmlStreamWriter> w;
    QStringList arguments;
    const QStringList tags{"Task",
                           "RegistrationInfo",
                           "Author",
                           "Description",
                           "Triggers",
                           "TimeTrigger",
                           "CalendarTrigger",
                           "Repetition",
                           "Interval",
                           "StopAtDurationEnd",
                           "StartBoundary",
                           "Enabled",
                           "ScheduleByDay",
                           "DaysInterval",
                           "ScheduleByWeek",
                           "WeeksInterval",
                           "DaysOfWeek",
                           "ScheduleByMonth",
                           "DaysOfMonth",
                           "Day",
                           "Months",
                           "Principals",
                           "Principal",
                           "UserId",
                           "LogonType",
                           "RunLevel",
                           "Settings",
                           "MultipleInstancesPolicy",
                           "DisallowStartIfOnBatteries",
                           "StopIfGoingOnBatteries",
                           "AllowHardTerminate",
                           "StartWhenAvailable",
                           "RunOnlyIfNetworkAvailable",
                           "AllowStartOnDemand",
                           "Hidden",
                           "ExecutionTimeLimit",
                           "Priority",
                           "Actions",
                           "Exec",
                           "Command",
                           "Arguments",
                           "WorkingDirectory"};
    rust::workflowRun(
        rust::WorkflowMethod::ScheduleXml, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::ScheduleReadTaskCatalogue:
                catalog = readJson(root_ + "/native/resources/catalog.json");
                nativeTasks = catalog["tasks"].toArray();
                break;
            case rust::WorkflowOp::ScheduleNativeTaskCount:
                out[0] = double(nativeTasks.size());
                break;
            case rust::WorkflowOp::ScheduleLoadNativeTask:
                entry = nativeTasks.at(int(a[0])).toObject();
                break;
            case rust::WorkflowOp::ScheduleEntryMatches:
                out[0] = entry["id"] == task;
                break;
            case rust::WorkflowOp::ScheduleEntryTrigger:
                out[0] = entry["trigger"].toBool();
                break;
            case rust::WorkflowOp::ScheduleReadScriptCatalogue:
                scripts = scriptCatalog(data_);
                break;
            case rust::WorkflowOp::ScheduleScriptTaskCount:
                out[0] = double(scripts.size());
                break;
            case rust::WorkflowOp::ScheduleLoadScriptTask:
                entry = scripts.at(int(a[0])).toObject();
                break;
            case rust::WorkflowOp::ScheduleEntrySupported:
                out[0] = entry["native_supported"].toBool();
                break;
            case rust::WorkflowOp::ScheduleKind:
                out[0] = QStringList{"Once", "Daily", "Weekly", "Monthly", "Days", "Hours"}.indexOf(
                    s["kind"].toString("Daily"));
                break;
            case rust::WorkflowOp::ScheduleParseStart:
                start = QDateTime::fromString(s["start"].toString(), Qt::ISODate);
                break;
            case rust::WorkflowOp::ScheduleStartValid:
                out[0] = start.isValid();
                break;
            case rust::WorkflowOp::ScheduleReadNumber: {
                const std::array keys{"interval", "weekday", "monthday", "limit_hours"};
                out[0] = number(s, keys.at(size_t(a[0])), int(a[1]), int(a[2]), int(a[3]));
                break;
            }
            case rust::WorkflowOp::ScheduleCreateXmlWriter:
                w.emplace(&text);
                w->setAutoFormatting(true);
                break;
            case rust::WorkflowOp::ScheduleXmlDocument:
                w->writeStartDocument();
                break;
            case rust::WorkflowOp::ScheduleXmlBegin:
                w->writeStartElement(tags.at(int(a[0])));
                break;
            case rust::WorkflowOp::ScheduleXmlEnd:
                w->writeEndElement();
                break;
            case rust::WorkflowOp::ScheduleXmlNamespace:
                w->writeDefaultNamespace("http://schemas.microsoft.com/windows/2004/02/mit/task");
                break;
            case rust::WorkflowOp::ScheduleXmlAttribute:
                switch (int(a[0])) {
                case 0:
                    w->writeAttribute("version", "1.2");
                    break;
                case 1:
                    w->writeAttribute("id", "Author");
                    break;
                case 2:
                    w->writeAttribute("Context", "Author");
                    break;
                }
                break;
            case rust::WorkflowOp::ScheduleXmlText: {
                QString value;
                switch (int(a[1])) {
                case 0:
                    value = "OKWW C++";
                    break;
                case 1:
                    value = s["label"].toString(task);
                    break;
                case 2:
                    value = QString("PT%1H").arg(int(a[2]));
                    break;
                case 3:
                    value = "false";
                    break;
                case 4:
                    value = start.toString("yyyy-MM-ddTHH:mm:ss");
                    break;
                case 5:
                    value = "true";
                    break;
                case 6:
                    value = QString::number(int(a[2]));
                    break;
                case 7:
                    value = user_;
                    break;
                case 8:
                    value = "InteractiveToken";
                    break;
                case 9:
                    value = "LeastPrivilege";
                    break;
                case 10:
                    value = "IgnoreNew";
                    break;
                case 11:
                    value = s["enabled"].toBool(true) ? "true" : "false";
                    break;
                case 12:
                    value = "PT0S";
                    break;
                case 13:
                    value = executable_;
                    break;
                case 14:
                    value = arguments.join(' ');
                    break;
                case 15:
                    value = root_;
                    break;
                }
                w->writeTextElement(tags.at(int(a[0])), value);
                break;
            }
            case rust::WorkflowOp::ScheduleXmlWeekday:
                w->writeEmptyElement(days.at(int(a[0])));
                break;
            case rust::WorkflowOp::ScheduleXmlMonth:
                w->writeEmptyElement(months.at(int(a[0])));
                break;
            case rust::WorkflowOp::ScheduleBuildArguments:
                arguments = {"--root",     root_, "--data",          data_,
                             "--run-task", task,  "--limit-seconds", QString::number(int(a[0]))};
                break;
            case rust::WorkflowOp::ScheduleExitAfter:
                out[0] = s["exit_after"].toBool(true);
                break;
            case rust::WorkflowOp::ScheduleAppendExitArgument:
                arguments.append("--exit-after-task");
                break;
            case rust::WorkflowOp::ScheduleArgumentCount:
                out[0] = double(arguments.size());
                break;
            case rust::WorkflowOp::ScheduleQuoteArgument: {
                const auto index = int(a[0]);
                arguments[index] = quoteArgument(arguments[index]);
                break;
            }
            case rust::WorkflowOp::ScheduleXmlDocumentEnd:
                w->writeEndDocument();
                break;
            case rust::WorkflowOp::ScheduleFailure:
                scheduleFailure(int(a[0]));
                break;
            default:
                fail("Rust 计划 XML 宿主无效");
            }
        });
    return text;
}
QJsonObject Scheduler::registry() const {
    const auto path = data_ + "/native-schedules.json";
    QJsonObject result;
    rust::workflowRun(rust::WorkflowMethod::ScheduleRegistry, {},
                      [&](rust::WorkflowOp op, const double *, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::ScheduleRegistryExists:
                              out[0] = QFile::exists(path);
                              break;
                          case rust::WorkflowOp::ScheduleReadRegistry:
                              result = readJson(path);
                              break;
                          default:
                              fail("Rust 计划记录宿主无效");
                          }
                      });
    return result;
}
void Scheduler::save(QJsonObject value) const {
    rust::workflowRun(rust::WorkflowMethod::ScheduleSave, {},
                      [&](rust::WorkflowOp op, const double *, double *) {
                          switch (op) {
                          case rust::WorkflowOp::ScheduleMakeDirectory:
                              QDir().mkpath(data_);
                              break;
                          case rust::WorkflowOp::ScheduleWriteRegistry:
                              writeJson(data_ + "/native-schedules.json", value);
                              break;
                          default:
                              fail("Rust 计划保存宿主无效");
                          }
                      });
}
void Scheduler::validateName(const QString &name) const {
    rust::workflowRun(
        rust::WorkflowMethod::ScheduleValidateName, {},
        [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::ScheduleNamePrefix:
                out[0] = name.startsWith(prefix_);
                break;
            case rust::WorkflowOp::ScheduleNameId:
                out[0] = QRegularExpression("^[0-9a-f]{32}$").match(name.mid(prefix_.size())).hasMatch();
                break;
            case rust::WorkflowOp::ScheduleFailure:
                scheduleFailure(int(a[0]));
                break;
            default:
                fail("Rust 计划名称宿主无效");
            }
        });
}
QJsonArray Scheduler::list() const {
    QJsonObject all, s;
    QStringList keys;
    QString name;
    QJsonArray result;
    rust::workflowRun(rust::WorkflowMethod::ScheduleList, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::ScheduleLoadRegistry:
                              all = registry();
                              keys = all.keys();
                              break;
                          case rust::WorkflowOp::ScheduleRegistryCount:
                              out[0] = double(keys.size());
                              break;
                          case rust::WorkflowOp::ScheduleSelectEntry:
                              name = keys.at(int(a[0]));
                              s = all.value(name).toObject();
                              break;
                          case rust::WorkflowOp::ScheduleValidateSelectedName:
                              validateName(name);
                              break;
                          case rust::WorkflowOp::ScheduleAppendEntry:
                              s["name"] = name;
                              result.append(s);
                              break;
                          default:
                              fail("Rust 计划列表宿主无效");
                          }
                      });
    return result;
}
void Scheduler::registerTask(const QString &name, const QString &definition) const {
    std::optional<QTemporaryDir> directory;
    std::optional<QFile> file;
    QString path;
    QByteArray bytes;
    rust::workflowRun(rust::WorkflowMethod::ScheduleRegister, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::ScheduleCreateTemporaryDirectory:
                              directory.emplace(QDir::tempPath() + "/okww-schedule-XXXXXX");
                              break;
                          case rust::WorkflowOp::ScheduleTemporaryDirectoryValid:
                              out[0] = directory->isValid();
                              break;
                          case rust::WorkflowOp::ScheduleTemporaryPath:
                              path = directory->filePath("schedule.xml");
                              break;
                          case rust::WorkflowOp::ScheduleCreateFile:
                              file.emplace(path);
                              break;
                          case rust::WorkflowOp::ScheduleEncodeXml:
                              bytes = definition.toUtf8();
                              break;
                          case rust::WorkflowOp::ScheduleOpenFile:
                              out[0] = file->open(QIODevice::WriteOnly);
                              break;
                          case rust::WorkflowOp::ScheduleWriteFile:
                              out[0] = file->write(bytes) == bytes.size();
                              break;
                          case rust::WorkflowOp::ScheduleFlushFile:
                              out[0] = file->flush();
                              break;
                          case rust::WorkflowOp::ScheduleDestroyFile:
                              file.reset();
                              break;
                          case rust::WorkflowOp::ScheduleInvokeCreate:
                              command_({"/Create", "/TN", name, "/XML", path, "/F"});
                              break;
                          case rust::WorkflowOp::ScheduleFailure:
                              scheduleFailure(int(a[0]));
                              break;
                          default:
                              fail("Rust 计划注册宿主无效");
                          }
                      });
}
QString Scheduler::add(QJsonObject s) {
    QString definition, name;
    QJsonObject all;
    rust::workflowRun(rust::WorkflowMethod::ScheduleAdd, {},
                      [&](rust::WorkflowOp op, const double *, double *) {
                          switch (op) {
                          case rust::WorkflowOp::ScheduleSetNewEnabled:
                              s["enabled"] = true;
                              break;
                          case rust::WorkflowOp::ScheduleBuildDefinition:
                              definition = xml(s);
                              break;
                          case rust::WorkflowOp::ScheduleGenerateName:
                              name = prefix_ + QUuid::createUuid().toString(QUuid::Id128);
                              break;
                          case rust::WorkflowOp::ScheduleRegisterDefinition:
                              registerTask(name, definition);
                              break;
                          case rust::WorkflowOp::ScheduleLoadRegistry:
                              all = registry();
                              break;
                          case rust::WorkflowOp::ScheduleStoreSettings:
                              all[name] = s;
                              break;
                          case rust::WorkflowOp::ScheduleSaveRegistry:
                              save(all);
                              break;
                          case rust::WorkflowOp::ScheduleInvokeDelete:
                              command_({"/Delete", "/TN", name, "/F"});
                              break;
                          case rust::WorkflowOp::ScheduleReturnName:
                              break;
                          default:
                              fail("Rust 计划创建宿主无效");
                          }
                      });
    return name;
}
void Scheduler::update(const QString &name, QJsonObject s) {
    QJsonObject all, previous;
    QString definition;
    rust::workflowRun(rust::WorkflowMethod::ScheduleUpdate, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::ScheduleValidateRequestedName:
                              validateName(name);
                              break;
                          case rust::WorkflowOp::ScheduleLoadRegistry:
                              all = registry();
                              break;
                          case rust::WorkflowOp::ScheduleRequestedExists:
                              out[0] = all.contains(name);
                              break;
                          case rust::WorkflowOp::ScheduleReadPrevious:
                              previous = all[name].toObject();
                              break;
                          case rust::WorkflowOp::ScheduleRemoveSettingsName:
                              s.remove("name");
                              break;
                          case rust::WorkflowOp::ScheduleRestoreEnabled:
                              s["enabled"] = previous["enabled"].toBool();
                              break;
                          case rust::WorkflowOp::ScheduleBuildDefinition:
                              definition = xml(s);
                              break;
                          case rust::WorkflowOp::ScheduleRegisterDefinition:
                              registerTask(name, definition);
                              break;
                          case rust::WorkflowOp::ScheduleStoreSettings:
                              all[name] = s;
                              break;
                          case rust::WorkflowOp::ScheduleSaveRegistry:
                              save(all);
                              break;
                          case rust::WorkflowOp::ScheduleRollbackDefinition:
                              registerTask(name, xml(previous));
                              break;
                          case rust::WorkflowOp::ScheduleFailure:
                              scheduleFailure(int(a[0]));
                              break;
                          default:
                              fail("Rust 计划修改宿主无效");
                          }
                      });
}
void Scheduler::remove(const QString &name) {
    QJsonObject all;
    rust::workflowRun(rust::WorkflowMethod::ScheduleRemove, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::ScheduleValidateRequestedName:
                              validateName(name);
                              break;
                          case rust::WorkflowOp::ScheduleLoadRegistry:
                              all = registry();
                              break;
                          case rust::WorkflowOp::ScheduleRequestedExists:
                              out[0] = all.contains(name);
                              break;
                          case rust::WorkflowOp::ScheduleInvokeDelete:
                              command_({"/Delete", "/TN", name, "/F"});
                              break;
                          case rust::WorkflowOp::ScheduleRemoveEntry:
                              all.remove(name);
                              break;
                          case rust::WorkflowOp::ScheduleSaveRegistry:
                              save(all);
                              break;
                          case rust::WorkflowOp::ScheduleFailure:
                              scheduleFailure(int(a[0]));
                              break;
                          default:
                              fail("Rust 计划删除宿主无效");
                          }
                      });
}
void Scheduler::setEnabled(const QString &name, bool enabled) {
    QJsonObject all, previous;
    const std::array args{double(enabled)};
    rust::workflowRun(rust::WorkflowMethod::ScheduleSetEnabled, args,
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::ScheduleValidateRequestedName:
                              validateName(name);
                              break;
                          case rust::WorkflowOp::ScheduleLoadRegistry:
                              all = registry();
                              break;
                          case rust::WorkflowOp::ScheduleRequestedExists:
                              out[0] = all.contains(name);
                              break;
                          case rust::WorkflowOp::ScheduleInvokeEnabled:
                              command_({"/Change", "/TN", name, a[0] ? "/ENABLE" : "/DISABLE"});
                              break;
                          case rust::WorkflowOp::ScheduleReadPrevious:
                              previous = all[name].toObject();
                              break;
                          case rust::WorkflowOp::ScheduleUpdateEnabled:
                              previous["enabled"] = bool(a[0]);
                              break;
                          case rust::WorkflowOp::ScheduleStorePrevious:
                              all[name] = previous;
                              break;
                          case rust::WorkflowOp::ScheduleSaveRegistry:
                              save(all);
                              break;
                          case rust::WorkflowOp::ScheduleFailure:
                              scheduleFailure(int(a[0]));
                              break;
                          default:
                              fail("Rust 计划启停宿主无效");
                          }
                      });
}
} // namespace okww::runtime
