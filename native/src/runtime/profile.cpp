#include "profile.hpp"
namespace okww::runtime {
Profile::Profile(QString directory, const QString &catalog)
    : directory_(std::move(directory)), schema_(readJson(catalog)) {
    const auto overridePath = QFileInfo(catalog).absolutePath() + "/native-overrides.json";
    if (QFile::exists(overridePath)) {
        const auto patches = readJson(overridePath);
        for (const auto *section : {"tasks", "globals"}) {
            QJsonArray groups;
            for (auto value : schema_[section].toArray()) {
                auto group = value.toObject();
                const auto id = group["id"].toString();
                const auto patch = patches[section].toObject()[id].toObject();
                if (patch["name"].isString() && QString(section) != "tasks")
                    group["name"] = patch["name"];
                if (patch["description_append"].isString())
                    group["description"] =
                        group["description"].toString() + "\n" + patch["description_append"].toString();
                const auto removed = patch["remove"].toArray();
                auto extras = patch["fields"].toObject();
                QJsonArray fields;
                for (auto f : group["fields"].toArray()) {
                    auto field = f.toObject();
                    const auto key = field["key"].toString();
                    if (removed.contains(key))
                        continue;
                    if (key == "Exit After Task") {
                        field["label"] = "完成后退出软件";
                        field["description"] = "成功执行后退出本软件。双机模式不关闭游戏机上的游戏。";
                    }
                    if (key == "Exit with Error")
                        field["description"] = "勾选“完成后退出软件”时，发生错误也退出本软件。";
                    const auto change = extras.take(key).toObject();
                    for (auto it = change.begin(); it != change.end(); ++it)
                        field[it.key()] = it.value();
                    fields.append(field);
                }
                for (auto it = extras.begin(); it != extras.end(); ++it) {
                    auto field = it.value().toObject();
                    field["key"] = it.key();
                    fields.append(field);
                }
                group["fields"] = fields;
                groups.append(group);
            }
            schema_[section] = groups;
        }
    }
    QDir().mkpath(directory_);
    for (const auto &section : {"tasks", "globals"})
        for (auto v : schema_[section].toArray()) {
            const auto g = v.toObject();
            const auto id = g["id"].toString();
            auto config = defaults(id);
            if (QFile::exists(filename(id))) {
                auto saved = readJson(filename(id));
                for (auto it = saved.begin(); it != saved.end(); ++it)
                    config[it.key()] = it.value();
            }
            values_[id] = config;
        }
}
QString Profile::filename(const QString &group) const {
    if (group.isEmpty() || group.contains('/') || group.contains('\\') || group == "..")
        fail("无效配置组");
    return directory_ + "/" + group + ".json";
}
QJsonObject Profile::defaults(const QString &group) const {
    for (const auto &section : {"tasks", "globals"})
        for (auto v : schema_[section].toArray()) {
            auto g = v.toObject();
            if (g["id"] != group)
                continue;
            QJsonObject result;
            for (auto f : g["fields"].toArray()) {
                auto field = f.toObject();
                result[field["key"].toString()] = field["value"];
            }
            if (g.contains("trigger"))
                result["_enabled"] = g["enabled"];
            return result;
        }
    fail("未知配置组：" + group);
}
QJsonObject Profile::group(const QString &id) const {
    std::lock_guard lock(mutex_);
    if (!values_.contains(id))
        fail("未知配置组：" + id);
    return values_[id];
}
QJsonValue Profile::value(const QString &id, const QString &key, QJsonValue fallback) const {
    const auto config = group(id);
    return config.contains(key) ? config[key] : fallback;
}
void Profile::set(const QString &id, const QString &key, const QJsonValue &value) {
    std::lock_guard lock(mutex_);
    auto config = group(id);
    QJsonObject spec;
    for (const auto &section : {"tasks", "globals"})
        for (auto v : schema_[section].toArray()) {
            auto g = v.toObject();
            if (g["id"] != id)
                continue;
            for (auto f : g["fields"].toArray())
                if (f.toObject()["key"] == key)
                    spec = f.toObject();
        }
    if (key == "_enabled") {
        if (!value.isBool())
            fail("启用状态需要开关值");
    } else {
        if (spec.isEmpty())
            fail("配置项不存在：" + key);
        const QString kind = spec["kind"].toString();
        if (kind == "boolean" && !value.isBool())
            fail("需要开关值");
        if (kind == "integer" || kind == "number") {
            if (!value.isDouble() || !std::isfinite(value.toDouble()))
                fail("需要有效数字");
            const double n = value.toDouble();
            if (kind == "integer" && std::floor(n) != n)
                fail("需要整数");
            if (spec["minimum"].isDouble() && n < spec["minimum"].toDouble())
                fail("数值低于下限");
            if (spec["maximum"].isDouble() && n > spec["maximum"].toDouble())
                fail("数值超过上限");
        }
        if (kind == "select" && !spec["options"].toArray().contains(value))
            fail("请选择有效选项");
        if ((kind == "text" || kind == "string" || kind == "multiline") && !value.isString())
            fail("需要文本");
        if ((kind == "list" || kind == "multiselect") && !value.isArray())
            fail("需要列表");
    }
    config[key] = value;
    writeJson(filename(id), config);
    values_[id] = config;
}
void Profile::reset(const QString &id) {
    std::lock_guard lock(mutex_);
    auto config = group(id);
    const auto initial = defaults(id);
    for (auto it = initial.begin(); it != initial.end(); ++it)
        if (it.key() != "_enabled")
            config[it.key()] = it.value();
    writeJson(filename(id), config);
    values_[id] = config;
}
QJsonObject Profile::catalog() const {
    std::lock_guard lock(mutex_);
    auto result = schema_;
    for (const auto &section : {"tasks", "globals"}) {
        QJsonArray groups;
        for (auto v : schema_[section].toArray()) {
            auto g = v.toObject();
            const auto config = group(g["id"].toString());
            QJsonArray fields;
            for (auto f : g["fields"].toArray()) {
                auto field = f.toObject();
                const auto conditions = field["visible_when"].toObject();
                bool visible = true;
                for (auto it = conditions.begin(); it != conditions.end(); ++it)
                    if (config[it.key()] != it.value())
                        visible = false;
                if (!visible)
                    continue;
                field["value"] = config[field["key"].toString()];
                fields.append(field);
            }
            g["fields"] = fields;
            if (g.contains("trigger"))
                g["enabled"] = config["_enabled"].toBool();
            groups.append(g);
        }
        result[section] = groups;
    }
    result["native_preferences"] = QJsonObject{
        {"close_to_tray", value("Basic Options", "Minimize Window to System Tray when Closing", false)},
        {"hotkey", value("Basic Options", "Start/Stop", "F9")}};
    return result;
}
} // namespace okww::runtime
