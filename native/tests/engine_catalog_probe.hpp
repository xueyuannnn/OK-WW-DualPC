#pragma once
#include "runtime/engine_abi.hpp"
#include <QJsonDocument>

namespace okww::runtime::test {
// Frozen catalogue/status behavior from the pre-migration engine.cpp.
// JSON values deliberately include missing and non-string fields: converting
// an id for lookup must not change the value exposed to the interface.
struct CatalogProbe {
    using O = rust::EngineOp;
    QJsonObject profile, result, entry, script;
    QJsonArray scripts, templates, snapshot, teams, input, tasks, states;
    QStringList native;
    QString current;
    int selectedNative = 0, pausedReads = 0;
    bool busy = false, warming = false, changingPause = false, initialPause = false;

    explicit CatalogProbe(int scene) {
        busy = (scene & 1) != 0;
        warming = (scene & 2) != 0;
        initialPause = (scene & 4) != 0;
        changingPause = (scene & 8) != 0;
        current = scene & 16 ? QString::fromUtf8("每日任务") : QString();
        native = {"DailyTask2", "AutoCombatTask", ""};
        QJsonArray definitions;
        for (int i = 0; i < (scene / 4) % 7; ++i) {
            QJsonObject value{{"id", i % 2 ? "MissingTask" : "DailyTask2"},
                              {"name", "任务 " + QString::number(i)},
                              {"description", QString::fromUtf8("说明")}, {"enabled", i % 2 == 0}};
            if ((scene & 32) && i == 0) value.remove("id");
            if ((scene & 64) && i == 1) value["enabled"] = 17;
            if ((scene & 128) && i == 2) { value["id"] = 123; value["name"] = false; }
            definitions.append(value);
        }
        for (int i = 0; i < (scene / 3) % 5; ++i) {
            QJsonObject value{{"id", "script-" + QString::number(i)}, {"source", "private code"},
                              {"enabled", i % 2 == 0}, {"native_supported", false}};
            if ((scene & 32) && i == 0) value.remove("source");
            if ((scene & 64) && i == 1) value.remove("enabled");
            scripts.append(value);
        }
        profile = {{"tasks", definitions}, {"other", QJsonArray{1, "unchanged"}}};
        templates = {QJsonObject{{"id", "template"}}};
        snapshot = {QJsonObject{{"name", "snapshot"}}};
        teams = {QJsonObject{{"name", "live"}}};
    }
    bool paused() { return initialPause ^ (changingPause && (++pausedReads % 2 == 0)); }
    QJsonObject originalCatalog() {
        auto output = profile;
        QJsonArray rows;
        for (auto v : output["tasks"].toArray()) {
            auto t = v.toObject();
            const auto id = t["id"].toString();
            const bool supported = std::any_of(native.begin(), native.end(), [&](const auto &item) { return item == id; });
            t["native_supported"] = supported;
            if (!supported) t["description"] = t["description"].toString() + "\n此版本不支持该任务。";
            if (!supported) t["name"] = t["name"].toString() + "（暂不支持）";
            rows.append(t);
        }
        for (auto value : scripts) { auto row = value.toObject(); row.remove("source"); rows.append(row); }
        output["tasks"] = rows;
        output["scripts"] = scripts;
        output["script_templates"] = templates;
        output["teams"] = busy ? snapshot : teams;
        return output;
    }
    QJsonObject originalStatus() {
        QJsonArray rows;
        for (auto v : profile["tasks"].toArray()) {
            auto t = v.toObject();
            rows.append(QJsonObject{{"id", t["id"]}, {"enabled", t["enabled"]}, {"paused", paused()}});
        }
        for (auto value : scripts) {
            const auto row = value.toObject();
            rows.append(QJsonObject{{"id", row["id"]}, {"enabled", row["enabled"]}, {"paused", paused()}});
        }
        return {{"paused", paused()}, {"warming", warming}, {"current", current}, {"tasks", rows}};
    }
    void invoke(O op, const double *a, double *out) {
        switch (op) {
        case O::CatalogProfileLoad: result = profile; input = result["tasks"].toArray(); break;
        case O::ProfileCatalogLoad: input = profile["tasks"].toArray(); break;
        case O::CatalogCount: out[0] = double(input.size()); break;
        case O::SelectCatalog: entry = input.at(int(a[0])).toObject(); break;
        case O::NativeCount: out[0] = double(native.size()); break;
        case O::NativeSelect: selectedNative = int(a[0]); break;
        case O::NativeMatches: out[0] = native.at(selectedNative) == entry["id"].toString(); break;
        case O::CatalogSetSupported: entry["native_supported"] = a[0] != 0; break;
        case O::CatalogUnsupportedDescription: entry["description"] = entry["description"].toString() + "\n此版本不支持该任务。"; break;
        case O::CatalogUnsupportedName: entry["name"] = entry["name"].toString() + "（暂不支持）"; break;
        case O::CatalogAppendTask: tasks.append(entry); break;
        case O::ScriptsCount: out[0] = double(scripts.size()); break;
        case O::SelectScriptEntry: script = scripts.at(int(a[0])).toObject(); break;
        case O::ScriptRemoveSource: script.remove("source"); break;
        case O::CatalogAppendScript: tasks.append(script); break;
        case O::CatalogStoreTasks: result["tasks"] = tasks; break;
        case O::CatalogStoreScripts: result["scripts"] = scripts; break;
        case O::CatalogStoreTemplates: result["script_templates"] = templates; break;
        case O::TeamBusy: out[0] = busy; break;
        case O::CatalogSnapshotTeams: result["teams"] = snapshot; break;
        case O::CatalogLiveTeams: result["teams"] = teams; break;
        case O::AppendCatalogState: states.append(QJsonObject{{"id", entry["id"]}, {"enabled", entry["enabled"]}, {"paused", paused()}}); break;
        case O::AppendScriptState: states.append(QJsonObject{{"id", std::as_const(script)["id"]}, {"enabled", std::as_const(script)["enabled"]}, {"paused", paused()}}); break;
        case O::BuildStatusResult: result = {{"paused", paused()}, {"warming", warming}, {"current", current}, {"tasks", states}}; break;
        default: throw std::runtime_error("unexpected catalogue operation");
        }
    }
};
inline QJsonObject catalogTrace(int scene, int method, bool migrated) {
    CatalogProbe probe(scene);
    if (!migrated) return method == 13 ? probe.originalCatalog() : probe.originalStatus();
    rust::engineRun(rust::EngineMethod(method), [&](rust::EngineOp op, const double *a, double *out, const std::exception_ptr &) { probe.invoke(op, a, out); });
    return probe.result;
}
}
