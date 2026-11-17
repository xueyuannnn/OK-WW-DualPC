#pragma once
#include "reference_profile.hpp"
namespace okww::runtime::profile_test {
struct Fixture {
    QTemporaryDir directory;
    QString catalog;
    explicit Fixture(int scene) {
        if (!directory.isValid())
            fail("Profile fixture unavailable");
        catalog = directory.filePath("catalog.json");
        QJsonArray fields;
        const QStringList kinds{"boolean", "integer",   "number", "select",      "text",
                                "string",  "multiline", "list",   "multiselect", "unknown"};
        for (int i = 0; i < kinds.size(); ++i) {
            QJsonValue initial;
            if (i == 0)
                initial = true;
            else if (i == 1 || i == 2)
                initial = 2;
            else if (i == 3)
                initial = "first";
            else if (i >= 7 && i <= 8)
                initial = QJsonArray{"a"};
            else
                initial = "example";
            QJsonObject field{{"key", QString("key%1").arg(i)},
                              {"kind", kinds[i]},
                              {"value", initial},
                              {"minimum", 0},
                              {"maximum", scene % 7 + 3},
                              {"options", QJsonArray{"first", "second", QJsonObject{{"nested", true}}}}};
            if (i == 4 || i == 5)
                field["visible_when"] = QJsonObject{{"key0", true}, {"key1", scene % 5}};
            fields.append(field);
        }
        fields.append(QJsonObject{{"key", "Exit After Task"}, {"kind", "boolean"}, {"value", false}});
        fields.append(QJsonObject{{"key", "Exit with Error"}, {"kind", "boolean"}, {"value", false}});
        QJsonArray tasks{QJsonObject{
            {"id", "task"}, {"name", "old task"}, {"enabled", true}, {"trigger", false}, {"fields", fields}}};
        if (scene % 11 == 0)
            tasks.append(QJsonObject{
                {"id", "task"},
                {"fields", QJsonArray{QJsonObject{
                               {"key", "key1"}, {"kind", "integer"}, {"value", 99}, {"minimum", 10}}}}});
        writeJson(
            catalog,
            {{"tasks", tasks},
             {"globals",
              QJsonArray{
                  QJsonObject{{"id", "Basic Options"},
                              {"name", "basic"},
                              {"fields",
                               QJsonArray{QJsonObject{{"key", "Minimize Window to System Tray when Closing"},
                                                      {"kind", "boolean"},
                                                      {"value", false}},
                                          QJsonObject{{"key", "Start/Stop"},
                                                      {"kind", "select"},
                                                      {"value", "F9"},
                                                      {"options", QJsonArray{"F9", "F10"}}}}}},
                  QJsonObject{{"id", "global"}, {"fields", fields}}}}});
        if (scene % 3 != 0) {
            writeJson(
                directory.filePath("native-overrides.json"),
                {{"tasks",
                  QJsonObject{
                      {"task",
                       QJsonObject{
                           {"name", "new task"},
                           {"description_append", "extra"},
                           {"remove", QJsonArray{"key5"}},
                           {"fields",
                            QJsonObject{{"key1", QJsonObject{{"maximum", scene % 7 + 5}}},
                                        {"key5", QJsonObject{{"kind", "string"}, {"value", "restored"}}},
                                        {"extra", QJsonObject{{"kind", "number"}, {"value", .5}}}}}}}}},
                 {"globals", QJsonObject{{"Basic Options", QJsonObject{{"name", "new basic"}}}}}});
        }
    }
};
template <class T> QJsonObject replay(Fixture &f, const QString &subdirectory, int scene) {
    const auto data = f.directory.filePath(subdirectory);
    QDir().mkpath(data);
    writeJson(data + "/task.json",
              {{"key0", scene % 2 == 0}, {"key1", scene % 7}, {"unknown_saved", 123}, {"_enabled", false}});
    QJsonObject result;
    QJsonArray results;
    try {
        T profile(data, f.catalog);
        const QString id = scene % 17 == 0 ? "missing" : scene % 3 == 0 ? "global" : "task";
        result["initial"] = profile.catalog();
        const QJsonArray values{true,
                                false,
                                3,
                                -1,
                                2.5,
                                "first",
                                "second",
                                QStringLiteral("文字"),
                                QJsonArray{"a", "b"},
                                QJsonObject{{"nested", true}},
                                QJsonValue(QJsonValue::Null)};
        const QString key = scene % 13 == 0   ? "_enabled"
                            : scene % 19 == 0 ? "extra"
                                              : QString("key%1").arg(scene % 11);
        try {
            profile.set(id, key, values[(scene / 5) % values.size()]);
            results.append("set");
        } catch (const std::exception &e) {
            results.append(errorText(e));
        }
        try {
            result["after"] = profile.group(id);
            result["value"] = profile.value(id, key, "fallback");
            profile.reset(id);
            result["reset"] = profile.group(id);
        } catch (const std::exception &e) {
            results.append(errorText(e));
        }
        result["final"] = profile.catalog();
        for (const auto &name : QDir(data).entryList({"*.json"}, QDir::Files))
            result["file:" + name] = readJson(data + '/' + name);
    } catch (const std::exception &e) {
        result["fatal"] = errorText(e).replace(data, "$DATA");
    }
    result["operations"] = results;
    return result;
}
} // namespace okww::runtime::profile_test
