#include "profile.hpp"
#include "workflow_abi.hpp"
namespace okww::runtime {
Profile::RustResult Profile::rustProfile(int method, RustRequest request) {
    RustResult result;
    std::optional<std::unique_lock<std::recursive_mutex>> lock;
    QString overridePath;
    QString section, currentId, fieldKey;
    QJsonObject patches, groupSchema, patch, extras, field, change, config, saved, spec, initial, conditions;
    QJsonArray groups, fields, removed;
    QStringList changeKeys, extraKeys, savedKeys, initialKeys, conditionKeys;
    const auto sectionGroups = [&]() {
        return method == int(rust::WorkflowMethod::ProfileInitialize)
                   ? schema_[section].toArray()
                   : std::as_const(schema_)[section].toArray();
    };
    rust::workflowRun(
        rust::WorkflowMethod(method), {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::ProfileFailure: {
                const QStringList errors{"无效配置组",         "未知配置组：" + request.id,
                                         "启用状态需要开关值", "配置项不存在：" + request.key,
                                         "需要开关值",         "需要有效数字",
                                         "需要整数",           "数值低于下限",
                                         "数值超过上限",       "请选择有效选项",
                                         "需要文本",           "需要列表"};
                fail(errors.at(int(a[0])));
                break;
            }
            case rust::WorkflowOp::LockProfile:
                lock.emplace(mutex_);
                break;
            case rust::WorkflowOp::ProfileOverridesExist:
                overridePath = QFileInfo(request.catalog).absolutePath() + "/native-overrides.json";
                out[0] = QFile::exists(overridePath);
                break;
            case rust::WorkflowOp::ProfileReadOverrides:
                patches = readJson(overridePath);
                break;
            case rust::WorkflowOp::ProfileSelectSection:
                section = QStringList{"tasks", "globals"}.at(int(a[0]));
                break;
            case rust::WorkflowOp::ProfileGroupCount:
                out[0] = double(sectionGroups().size());
                break;
            case rust::WorkflowOp::ProfileLoadGroup:
                groupSchema = sectionGroups().at(int(a[0])).toObject();
                break;
            case rust::WorkflowOp::ProfileClearGroups:
                groups = {};
                break;
            case rust::WorkflowOp::ProfileLoadPatch:
                patch = patches.value(section).toObject().value(groupSchema["id"].toString()).toObject();
                break;
            case rust::WorkflowOp::ProfilePatchNameString:
                out[0] = patch["name"].isString();
                break;
            case rust::WorkflowOp::ProfilePatchName:
                groupSchema["name"] = patch["name"];
                break;
            case rust::WorkflowOp::ProfilePatchDescriptionString:
                out[0] = patch["description_append"].isString();
                break;
            case rust::WorkflowOp::ProfilePatchDescription:
                groupSchema["description"] =
                    groupSchema["description"].toString() + "\n" + patch["description_append"].toString();
                break;
            case rust::WorkflowOp::ProfileReadPatchFields:
                removed = patch["remove"].toArray();
                extras = patch["fields"].toObject();
                break;
            case rust::WorkflowOp::ProfileClearFields:
                fields = {};
                break;
            case rust::WorkflowOp::ProfileFieldCount:
                out[0] = double(groupSchema["fields"].toArray().size());
                break;
            case rust::WorkflowOp::ProfileLoadField:
                field = groupSchema["fields"].toArray().at(int(a[0])).toObject();
                fieldKey = field["key"].toString();
                break;
            case rust::WorkflowOp::ProfileFieldRemoved:
                out[0] = removed.contains(fieldKey);
                break;
            case rust::WorkflowOp::ProfileExitKeyKind:
                out[0] =
                    QHash<QString, int>{{"Exit After Task", 1}, {"Exit with Error", 2}}.value(fieldKey, 0);
                break;
            case rust::WorkflowOp::ProfileExitTaskLabels:
                field["label"] = "完成后退出软件";
                field["description"] = "成功执行后退出本软件。双机模式不关闭游戏机上的游戏。";
                break;
            case rust::WorkflowOp::ProfileExitErrorLabel:
                field["description"] = "勾选“完成后退出软件”时，发生错误也退出本软件。";
                break;
            case rust::WorkflowOp::ProfileTakeFieldChange:
                change = extras.take(fieldKey).toObject();
                changeKeys = change.keys();
                break;
            case rust::WorkflowOp::ProfileChangeCount:
                out[0] = double(changeKeys.size());
                break;
            case rust::WorkflowOp::ProfileApplyFieldChange: {
                const auto key = changeKeys.at(int(a[0]));
                field[key] = change.value(key);
                break;
            }
            case rust::WorkflowOp::ProfileAppendField:
                fields.append(field);
                break;
            case rust::WorkflowOp::ProfileReadExtraKeys:
                extraKeys = extras.keys();
                break;
            case rust::WorkflowOp::ProfileExtraCount:
                out[0] = double(extraKeys.size());
                break;
            case rust::WorkflowOp::ProfileLoadExtraField: {
                const auto key = extraKeys.at(int(a[0]));
                field = extras.value(key).toObject();
                field["key"] = key;
                break;
            }
            case rust::WorkflowOp::ProfileStoreFields:
                groupSchema["fields"] = fields;
                break;
            case rust::WorkflowOp::ProfileAppendGroup:
                groups.append(groupSchema);
                break;
            case rust::WorkflowOp::ProfileStoreSection:
                schema_[section] = groups;
                break;
            case rust::WorkflowOp::ProfileMakeDirectory:
                QDir().mkpath(directory_);
                break;
            case rust::WorkflowOp::ProfileLoadDefaultConfig:
                currentId = groupSchema["id"].toString();
                config = defaults(currentId);
                break;
            case rust::WorkflowOp::ProfileSavedConfigExists:
                out[0] = QFile::exists(filename(currentId));
                break;
            case rust::WorkflowOp::ProfileReadSavedConfig:
                saved = readJson(filename(currentId));
                savedKeys = saved.keys();
                break;
            case rust::WorkflowOp::ProfileSavedKeyCount:
                out[0] = double(savedKeys.size());
                break;
            case rust::WorkflowOp::ProfileOverlaySavedKey: {
                const auto key = savedKeys.at(int(a[0]));
                config[key] = saved.value(key);
                break;
            }
            case rust::WorkflowOp::ProfileStoreInitialConfig:
                values_[currentId] = config;
                break;
            case rust::WorkflowOp::ProfileIdEmpty:
                out[0] = request.id.isEmpty();
                break;
            case rust::WorkflowOp::ProfileIdForwardSlash:
                out[0] = request.id.contains('/');
                break;
            case rust::WorkflowOp::ProfileIdBackwardSlash:
                out[0] = request.id.contains('\\');
                break;
            case rust::WorkflowOp::ProfileIdParent:
                out[0] = request.id == "..";
                break;
            case rust::WorkflowOp::ProfileBuildFilename:
                result.path = directory_ + '/' + request.id + ".json";
                break;
            case rust::WorkflowOp::ProfileGroupMatchesRequest:
                out[0] = groupSchema["id"] == request.id;
                break;
            case rust::WorkflowOp::ProfileCopyDefaultField:
                result.object[fieldKey] = field["value"];
                break;
            case rust::WorkflowOp::ProfileGroupHasTrigger:
                out[0] = groupSchema.contains("trigger");
                break;
            case rust::WorkflowOp::ProfileCopyDefaultEnabled:
                result.object["_enabled"] = groupSchema["enabled"];
                break;
            case rust::WorkflowOp::ProfileValueGroupExists:
                out[0] = values_.contains(request.id);
                break;
            case rust::WorkflowOp::ProfileReturnGroup:
                result.object = std::as_const(values_)[request.id];
                break;
            case rust::WorkflowOp::ProfileReadRequestedGroup:
                config = group(request.id);
                break;
            case rust::WorkflowOp::ProfileValueExists:
                out[0] = config.contains(request.key);
                break;
            case rust::WorkflowOp::ProfileReturnValue:
                result.value = std::as_const(config)[request.key];
                break;
            case rust::WorkflowOp::ProfileReturnFallback:
                result.value = request.fallback;
                break;
            case rust::WorkflowOp::ProfileFieldMatchesRequest:
                out[0] = field["key"] == request.key;
                break;
            case rust::WorkflowOp::ProfileStoreSpec:
                spec = field;
                break;
            case rust::WorkflowOp::ProfileIsEnabledKey:
                out[0] = request.key == "_enabled";
                break;
            case rust::WorkflowOp::ProfileInputBool:
                out[0] = request.input.isBool();
                break;
            case rust::WorkflowOp::ProfileSpecEmpty:
                out[0] = spec.isEmpty();
                break;
            case rust::WorkflowOp::ProfileSpecKind: {
                const QHash<QString, int> kinds{{"boolean", 1},   {"integer", 2}, {"number", 3},
                                                {"select", 4},    {"text", 5},    {"string", 5},
                                                {"multiline", 5}, {"list", 6},    {"multiselect", 6}};
                out[0] = kinds.value(spec["kind"].toString(), 0);
                break;
            }
            case rust::WorkflowOp::ProfileInputNumber:
                out[0] = request.input.isDouble();
                break;
            case rust::WorkflowOp::ProfileInputDouble:
                out[0] = request.input.toDouble();
                break;
            case rust::WorkflowOp::ProfileMinimumNumber:
                out[0] = spec["minimum"].isDouble();
                break;
            case rust::WorkflowOp::ProfileMinimum:
                out[0] = spec["minimum"].toDouble();
                break;
            case rust::WorkflowOp::ProfileMaximumNumber:
                out[0] = spec["maximum"].isDouble();
                break;
            case rust::WorkflowOp::ProfileMaximum:
                out[0] = spec["maximum"].toDouble();
                break;
            case rust::WorkflowOp::ProfileOptionContains:
                out[0] = spec["options"].toArray().contains(request.input);
                break;
            case rust::WorkflowOp::ProfileInputString:
                out[0] = request.input.isString();
                break;
            case rust::WorkflowOp::ProfileInputArray:
                out[0] = request.input.isArray();
                break;
            case rust::WorkflowOp::ProfileSetConfigValue:
                config[request.key] = request.input;
                break;
            case rust::WorkflowOp::ProfileWriteConfig:
                writeJson(filename(request.id), config);
                break;
            case rust::WorkflowOp::ProfileStoreConfig:
                values_[request.id] = config;
                break;
            case rust::WorkflowOp::ProfileReadInitialDefaults:
                initial = defaults(request.id);
                initialKeys = initial.keys();
                break;
            case rust::WorkflowOp::ProfileInitialKeyCount:
                out[0] = double(initialKeys.size());
                break;
            case rust::WorkflowOp::ProfileInitialKeyEnabled:
                out[0] = initialKeys.at(int(a[0])) == "_enabled";
                break;
            case rust::WorkflowOp::ProfileResetKey: {
                const auto key = initialKeys.at(int(a[0]));
                config[key] = initial.value(key);
                break;
            }
            case rust::WorkflowOp::ProfileCopySchema:
                result.object = schema_;
                break;
            case rust::WorkflowOp::ProfileReadCatalogConfig:
                config = group(groupSchema["id"].toString());
                break;
            case rust::WorkflowOp::ProfileReadVisibility:
                conditions = field["visible_when"].toObject();
                conditionKeys = conditions.keys();
                break;
            case rust::WorkflowOp::ProfileVisibilityCount:
                out[0] = double(conditionKeys.size());
                break;
            case rust::WorkflowOp::ProfileVisibilityMatches: {
                const auto key = conditionKeys.at(int(a[0]));
                out[0] = std::as_const(config)[key] == conditions.value(key);
                break;
            }
            case rust::WorkflowOp::ProfileFillCurrentValue:
                field["value"] = std::as_const(config)[fieldKey];
                break;
            case rust::WorkflowOp::ProfileFillCurrentEnabled:
                groupSchema["enabled"] = std::as_const(config)["_enabled"].toBool();
                break;
            case rust::WorkflowOp::ProfileStoreCatalogSection:
                result.object[section] = groups;
                break;
            case rust::WorkflowOp::ProfileFillPreferences:
                result.object["native_preferences"] = QJsonObject{
                    {"close_to_tray",
                     value("Basic Options", "Minimize Window to System Tray when Closing", false)},
                    {"hotkey", value("Basic Options", "Start/Stop", "F9")}};
                break;
            default:
                fail("Rust 配置存储宿主无效");
            }
        });
    return result;
}
Profile::Profile(QString directory, const QString &catalog)
    : directory_(std::move(directory)), schema_(readJson(catalog)) {
    RustRequest r;
    r.catalog = catalog;
    rustProfile(int(rust::WorkflowMethod::ProfileInitialize), std::move(r));
}
QString Profile::filename(const QString &id) const {
    RustRequest r;
    r.id = id;
    return const_cast<Profile *>(this)
        ->rustProfile(int(rust::WorkflowMethod::ProfileFilename), std::move(r))
        .path;
}
QJsonObject Profile::defaults(const QString &id) const {
    RustRequest r;
    r.id = id;
    return const_cast<Profile *>(this)
        ->rustProfile(int(rust::WorkflowMethod::ProfileDefaults), std::move(r))
        .object;
}
QJsonObject Profile::group(const QString &id) const {
    RustRequest r;
    r.id = id;
    return const_cast<Profile *>(this)
        ->rustProfile(int(rust::WorkflowMethod::ProfileGroup), std::move(r))
        .object;
}
QJsonValue Profile::value(const QString &id, const QString &key, QJsonValue fallback) const {
    RustRequest r;
    r.id = id;
    r.key = key;
    r.fallback = std::move(fallback);
    return const_cast<Profile *>(this)
        ->rustProfile(int(rust::WorkflowMethod::ProfileValue), std::move(r))
        .value;
}
void Profile::set(const QString &id, const QString &key, const QJsonValue &value) {
    RustRequest r;
    r.id = id;
    r.key = key;
    r.input = value;
    rustProfile(int(rust::WorkflowMethod::ProfileSet), std::move(r));
}
void Profile::reset(const QString &id) {
    RustRequest r;
    r.id = id;
    rustProfile(int(rust::WorkflowMethod::ProfileReset), std::move(r));
}
QJsonObject Profile::catalog() const {
    return const_cast<Profile *>(this)->rustProfile(int(rust::WorkflowMethod::ProfileCatalog), {}).object;
}
} // namespace okww::runtime
