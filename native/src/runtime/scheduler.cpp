#include "scheduler.hpp"
#include "scripts.hpp"
#include <sddl.h>
#include <QCryptographicHash>
#include <QXmlStreamWriter>
namespace okww::runtime {
namespace {
const QStringList days{"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
const QStringList months{"January", "February", "March",     "April",   "May",      "June",
                         "July",    "August",   "September", "October", "November", "December"};
int number(const QJsonObject &s, const char *key, int fallback, int low, int high) {
    const auto v = s.value(key);
    const double n = v.isUndefined() ? fallback : v.toDouble(-1);
    if (!std::isfinite(n) || n != std::floor(n) || n < low || n > high)
        fail(QString("计划参数无效：") + key);
    return int(n);
}
} // namespace
Scheduler::Scheduler(QString executable, QString root, QString data, QString user, ScheduleCommand command)
    : executable_(QFileInfo(executable).absoluteFilePath()), root_(QDir(root).absolutePath()),
      data_(QDir(data).absolutePath()), user_(user.isEmpty() ? currentUser() : std::move(user)),
      command_(command ? std::move(command) : runCommand) {
    prefix_ = "\\OKWW-Cpp-" +
              QString::fromLatin1(
                  QCryptographicHash::hash(data_.toCaseFolded().toUtf8(), QCryptographicHash::Sha256)
                      .toHex()
                      .left(12)) +
              "-";
}
QString Scheduler::currentUser() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        fail("无法读取当前 Windows 用户");
    DWORD required = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &required);
    QByteArray buffer(int(required), 0);
    const bool read = GetTokenInformation(token, TokenUser, buffer.data(), required, &required);
    CloseHandle(token);
    if (!read)
        fail("无法读取 Windows 用户标识");
    LPWSTR value = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid, &value))
        fail("无法转换 Windows 用户标识");
    const auto result = QString::fromWCharArray(value);
    LocalFree(value);
    return result;
}
QByteArray Scheduler::runCommand(const QStringList &arguments) {
    QProcess p;
    p.setCreateProcessArgumentsModifier([](auto *args) { args->flags |= CREATE_NO_WINDOW; });
    p.start(qEnvironmentVariable("SystemRoot") + "/System32/schtasks.exe", arguments);
    if (!p.waitForStarted(5000))
        fail("无法启动 Windows 任务计划工具");
    if (!p.waitForFinished(10000)) {
        p.kill();
        p.waitForFinished(1000);
        fail("Windows 任务计划操作超时");
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        fail("Windows 任务计划操作失败：" + QString::fromLocal8Bit(p.readAllStandardError()).trimmed());
    return p.readAllStandardOutput();
}
QString Scheduler::quoteArgument(const QString &arg) {
    QString result = "\"";
    int slashes = 0;
    for (auto c : arg) {
        if (c == '\\') {
            ++slashes;
            continue;
        }
        if (c == '\"')
            result += QString(slashes * 2 + 1, '\\') + c;
        else
            result += QString(slashes, '\\') + c;
        slashes = 0;
    }
    return result + QString(slashes * 2, '\\') + '\"';
}
QString Scheduler::xml(const QJsonObject &s) const {
    const auto task = s["task"].toString();
    const auto catalog = readJson(root_ + "/native/resources/catalog.json");
    bool found = false;
    for (auto value : catalog["tasks"].toArray()) {
        auto entry = value.toObject();
        if (entry["id"] == task && !entry["trigger"].toBool())
            found = true;
    }
    for (auto value : scriptCatalog(data_)) {
        const auto entry = value.toObject();
        if (entry["id"] == task && !entry["trigger"].toBool() && entry["native_supported"].toBool())
            found = true;
    }
    if (!found)
        fail("计划任务需要选择有效的一次性任务");
    const auto kind = s["kind"].toString("Daily");
    if (!QStringList{"Once", "Daily", "Weekly", "Monthly", "Days", "Hours"}.contains(kind))
        fail("计划重复方式无效");
    const auto start = QDateTime::fromString(s["start"].toString(), Qt::ISODate);
    if (!start.isValid())
        fail("计划开始日期和时间无效");
    const int interval = number(s, "interval", 1, 1, kind == "Hours" ? 744 : 365);
    const int weekday = number(s, "weekday", 1, 1, 7), monthday = number(s, "monthday", 1, 1, 31);
    const int limit = number(s, "limit_hours", 0, 0, 168);
    QString text;
    QXmlStreamWriter w(&text);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeStartElement("Task");
    w.writeDefaultNamespace("http://schemas.microsoft.com/windows/2004/02/mit/task");
    w.writeAttribute("version", "1.2");
    w.writeStartElement("RegistrationInfo");
    w.writeTextElement("Author", "OKWW C++");
    w.writeTextElement("Description", s["label"].toString(task));
    w.writeEndElement();
    w.writeStartElement("Triggers");
    w.writeStartElement(kind == "Once" || kind == "Hours" ? "TimeTrigger" : "CalendarTrigger");
    if (kind == "Hours") {
        w.writeStartElement("Repetition");
        w.writeTextElement("Interval", QString("PT%1H").arg(interval));
        w.writeTextElement("StopAtDurationEnd", "false");
        w.writeEndElement();
    }
    w.writeTextElement("StartBoundary", start.toString("yyyy-MM-ddTHH:mm:ss"));
    w.writeTextElement("Enabled", "true");
    if (kind == "Daily" || kind == "Days") {
        w.writeStartElement("ScheduleByDay");
        w.writeTextElement("DaysInterval", QString::number(kind == "Days" ? interval : 1));
        w.writeEndElement();
    } else if (kind == "Weekly") {
        w.writeStartElement("ScheduleByWeek");
        w.writeTextElement("WeeksInterval", "1");
        w.writeStartElement("DaysOfWeek");
        w.writeEmptyElement(days[weekday - 1]);
        w.writeEndElement();
        w.writeEndElement();
    } else if (kind == "Monthly") {
        w.writeStartElement("ScheduleByMonth");
        w.writeStartElement("DaysOfMonth");
        w.writeTextElement("Day", QString::number(monthday));
        w.writeEndElement();
        w.writeStartElement("Months");
        for (const auto &month : months)
            w.writeEmptyElement(month);
        w.writeEndElement();
        w.writeEndElement();
    }
    w.writeEndElement();
    w.writeEndElement();
    w.writeStartElement("Principals");
    w.writeStartElement("Principal");
    w.writeAttribute("id", "Author");
    w.writeTextElement("UserId", user_);
    w.writeTextElement("LogonType", "InteractiveToken");
    w.writeTextElement("RunLevel", "LeastPrivilege");
    w.writeEndElement();
    w.writeEndElement();
    w.writeStartElement("Settings");
    for (const auto &pair : QVector<QPair<QString, QString>>{{"MultipleInstancesPolicy", "IgnoreNew"},
                                                             {"DisallowStartIfOnBatteries", "false"},
                                                             {"StopIfGoingOnBatteries", "false"},
                                                             {"AllowHardTerminate", "false"},
                                                             {"StartWhenAvailable", "true"},
                                                             {"RunOnlyIfNetworkAvailable", "false"},
                                                             {"AllowStartOnDemand", "true"},
                                                             {"Enabled", "true"},
                                                             {"Hidden", "false"},
                                                             {"ExecutionTimeLimit", "PT0S"},
                                                             {"Priority", "7"}})
        w.writeTextElement(pair.first, pair.first == "Enabled"
                                           ? (s["enabled"].toBool(true) ? "true" : "false")
                                           : pair.second);
    w.writeEndElement();
    QStringList args{"--root",     root_, "--data",          data_,
                     "--run-task", task,  "--limit-seconds", QString::number(limit * 3600)};
    if (s["exit_after"].toBool(true))
        args.append("--exit-after-task");
    for (auto &arg : args)
        arg = quoteArgument(arg);
    w.writeStartElement("Actions");
    w.writeAttribute("Context", "Author");
    w.writeStartElement("Exec");
    w.writeTextElement("Command", executable_);
    w.writeTextElement("Arguments", args.join(' '));
    w.writeTextElement("WorkingDirectory", root_);
    w.writeEndElement();
    w.writeEndElement();
    w.writeEndElement();
    w.writeEndDocument();
    return text;
}
QJsonObject Scheduler::registry() const {
    const auto path = data_ + "/native-schedules.json";
    return QFile::exists(path) ? readJson(path) : QJsonObject{};
}
void Scheduler::save(QJsonObject value) const {
    QDir().mkpath(data_);
    writeJson(data_ + "/native-schedules.json", value);
}
void Scheduler::validateName(const QString &name) const {
    if (!name.startsWith(prefix_) ||
        !QRegularExpression("^[0-9a-f]{32}$").match(name.mid(prefix_.size())).hasMatch())
        fail("不能修改此配置以外的计划任务");
}
QJsonArray Scheduler::list() const {
    QJsonArray out;
    auto all = registry();
    for (auto it = all.begin(); it != all.end(); ++it) {
        validateName(it.key());
        auto s = it.value().toObject();
        s["name"] = it.key();
        out.append(s);
    }
    return out;
}
void Scheduler::registerTask(const QString &name, const QString &definition) const {
    QTemporaryDir directory(QDir::tempPath() + "/okww-schedule-XXXXXX");
    if (!directory.isValid())
        fail("无法创建计划任务临时目录");
    const auto path = directory.filePath("schedule.xml");
    // QTemporaryFile keeps its native handle open even after close(). Windows
    // schtasks needs to reopen this file; destroy an ordinary QFile first.
    {
        QFile file(path);
        const auto bytes = definition.toUtf8();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.flush())
            fail("无法写入计划任务文件");
    }
    command_({"/Create", "/TN", name, "/XML", path, "/F"});
}
QString Scheduler::add(QJsonObject s) {
    s["enabled"] = true;
    const auto definition = xml(s);
    const auto name = prefix_ + QUuid::createUuid().toString(QUuid::Id128);
    registerTask(name, definition);
    auto all = registry();
    all[name] = s;
    try {
        save(all);
    } catch (...) {
        try {
            command_({"/Delete", "/TN", name, "/F"});
        } catch (...) {
        }
        throw;
    }
    return name;
}
void Scheduler::update(const QString &name, QJsonObject s) {
    validateName(name);
    auto all = registry();
    if (!all.contains(name))
        fail("计划不存在");
    const auto previous = all[name].toObject();
    s.remove("name");
    s["enabled"] = previous["enabled"].toBool();
    const auto definition = xml(s);
    registerTask(name, definition);
    all[name] = s;
    try {
        save(all);
    } catch (...) {
        try {
            registerTask(name, xml(previous));
        } catch (...) {
        }
        throw;
    }
}
void Scheduler::remove(const QString &name) {
    validateName(name);
    auto all = registry();
    if (!all.contains(name))
        fail("计划不存在");
    command_({"/Delete", "/TN", name, "/F"});
    all.remove(name);
    save(all);
}
void Scheduler::setEnabled(const QString &name, bool enabled) {
    validateName(name);
    auto all = registry();
    if (!all.contains(name))
        fail("计划不存在");
    command_({"/Change", "/TN", name, enabled ? "/ENABLE" : "/DISABLE"});
    auto s = all[name].toObject();
    s["enabled"] = enabled;
    all[name] = s;
    save(all);
}
} // namespace okww::runtime
