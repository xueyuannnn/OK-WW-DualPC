#pragma once
#include "reference_scheduler.hpp"
namespace okww::runtime::scheduler_test {
struct Fixture {
    QTemporaryDir directory;
    QString root;
    explicit Fixture(int scene) {
        if (!directory.isValid())
            fail("Scheduler test fixture unavailable");
        root = directory.filePath("root");
        QDir().mkpath(root + "/native/resources");
        writeJson(root + "/native/resources/catalog.json",
                  {{"tasks", QJsonArray{QJsonObject{{"id", "DailyTask"}, {"trigger", false}},
                                        QJsonObject{{"id", "AutoCombatTask"}, {"trigger", true}}}}});
        Q_UNUSED(scene);
    }
};
inline QJsonObject settings(int scene) {
    const QStringList kinds{"Once", "Daily", "Weekly", "Monthly", "Days", "Hours"};
    QJsonObject result{{"task", "DailyTask"},
                       {"kind", kinds[scene % 6]},
                       {"start", "2030-02-28T23:59:58+08:00"},
                       {"interval", scene % 3 + 1},
                       {"weekday", scene % 7 + 1},
                       {"monthday", scene % 28 + 1},
                       {"limit_hours", scene % 169},
                       {"label", QStringLiteral("测试 <计划> & \"参数\"")},
                       {"enabled", scene % 2 == 0},
                       {"exit_after", scene % 3 == 0}};
    if (scene >= 512) {
        result["task"] = QStringList{"DailyTask", "AutoCombatTask", "ScriptDaily", "Unsupported",
                                     "Missing"}[(scene / 7) % 5];
        if (scene % 13 == 0)
            result["start"] = "2031-02-29T12:00:00";
        if (scene % 17 == 0)
            result["kind"] = "Invalid";
        if (scene % 19 == 0)
            result["interval"] = 744;
        if (scene % 23 == 0)
            result["weekday"] = 0;
        if (scene % 29 == 0)
            result["monthday"] = 32;
        if (scene % 31 == 0)
            result["limit_hours"] = 169;
        if (scene % 37 == 0)
            result["interval"] = 1.5;
        if (scene % 41 == 0)
            result["weekday"] = "2";
        if (scene % 43 == 0)
            result.remove("kind");
    }
    return result;
}
inline QJsonValue normalize(QJsonValue value, const QString &root, const QString &data) {
    const auto text = [&](QString s) {
        return s.replace(data, "$DATA")
            .replace(QDir::toNativeSeparators(data), "$DATA")
            .replace(root, "$ROOT")
            .replace(QDir::toNativeSeparators(root), "$ROOT")
            .replace(QRegularExpression("\\\\OKWW-Cpp-[0-9a-f]{12}-[0-9a-f]{32}"), "$PLAN");
    };
    if (value.isString())
        return text(value.toString());
    if (value.isArray()) {
        QJsonArray result;
        for (auto item : value.toArray())
            result.append(normalize(item, root, data));
        return result;
    }
    if (value.isObject()) {
        QJsonObject result;
        const auto object = value.toObject();
        for (auto i = object.begin(); i != object.end(); ++i)
            result[text(i.key())] = normalize(i.value(), root, data);
        return result;
    }
    return value;
}
template <class T> QJsonObject xml(Fixture &f, int scene, const QString &suffix) {
    const auto data = f.directory.filePath(suffix);
    QDir().mkpath(data);
    writeJson(
        data + "/native-script-catalog.json",
        {{"scripts",
          QJsonArray{QJsonObject{{"id", "ScriptDaily"}, {"trigger", false}, {"native_supported", true}},
                     QJsonObject{{"id", "Unsupported"}, {"trigger", false}, {"native_supported", false}}}}});
    T schedule(f.root + "/程序 with spaces.exe", f.root, data, "S-1-5-test", [](const QStringList &) {
        fail("Unexpected scheduler command during XML test");
        return QByteArray{};
    });
    QJsonObject result;
    try {
        result["xml"] = schedule.xml(settings(scene));
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    }
    return normalize(result, f.root, data).toObject();
}
template <class T> QJsonObject operations(Fixture &f, int scene, const QString &suffix) {
    const auto data = f.directory.filePath(suffix);
    QJsonArray calls;
    int commands = 0;
    if (scene % 13 == 0) {
        QFile file(data);
        if (!file.open(QIODevice::WriteOnly))
            fail("Cannot prepare failing scheduler data path");
        file.write("file");
        file.close();
    } else
        QDir().mkpath(data);
    T schedule(f.root + "/program.exe", f.root, data, "S-1-5-test", [&](const QStringList &args) {
        QJsonArray row;
        for (const auto &arg : args)
            row.append(arg);
        if (args.contains("/XML")) {
            const auto i = args.indexOf("/XML");
            QFile file(args.at(i + 1));
            if (!file.open(QIODevice::ReadOnly))
                fail("Schedule XML remained locked");
            row[i + 1] = "$TEMP_XML";
            row.append(QString::fromUtf8(file.readAll()));
        }
        calls.append(row);
        if (++commands == scene % 5 + 1 && scene % 7 == 0)
            fail("mock-scheduler-command-failure");
        return QByteArray{};
    });
    QJsonObject result;
    try {
        auto s = settings(scene % 512);
        const auto name = schedule.add(s);
        result["added"] = name;
        result["initial"] = schedule.list();
        schedule.setEnabled(name, scene % 2 == 0);
        s["label"] = "modified";
        s["kind"] = "Weekly";
        s["name"] = "untrusted";
        schedule.update(scene % 17 == 0 ? name + "x" : name, s);
        result["updated"] = schedule.list();
        if (scene % 3 == 0)
            schedule.remove(name);
        if (scene % 19 == 0)
            schedule.remove("\\OtherProject-deadbeef");
        result["final"] = schedule.list();
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
        result["type"] = typeid(e).name();
    }
    const auto path = data + "/native-schedules.json";
    if (QFile::exists(path))
        result["saved"] = readJson(path);
    result["calls"] = calls;
    return normalize(result, f.root, data).toObject();
}
} // namespace okww::runtime::scheduler_test
