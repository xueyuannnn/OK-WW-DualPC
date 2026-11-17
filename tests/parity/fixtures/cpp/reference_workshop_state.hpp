#pragma once
#include "runtime/diagnostics.hpp"
#include "runtime/workshop.hpp"
#include <QProcessEnvironment>
#include <QThread>
namespace okww::runtime::workshop_state_reference {
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
double finiteNumber(QJsonValue value) {
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        fail("工坊状态包含无效数值");
    return value.toDouble();
}
bool stateBoolean(QJsonValue value) {
    if (!value.isBool())
        fail("工坊状态包含无效开关值");
    return value.toBool();
}
double percentile(QVector<double> values, double fraction) {
    if (values.isEmpty())
        return 0;
    std::sort(values.begin(), values.end());
    return values[std::min(values.size() - 1, qsizetype(std::ceil(values.size() * fraction)) - 1)];
}
} // namespace
QJsonObject workshopCombatState(CombatHost &host) {
    const auto now = host.now();
    QJsonArray chars, freezes;
    for (auto c : host.characters()) {
        const auto &s = c->state;
        QJsonObject value{{"index", s.index},
                          {"current", s.current},
                          {"intro", s.hasIntro},
                          {"sub_intro", s.hasSubDpsIntro},
                          {"current_con", s.currentCon},
                          {"check_f", c->checkFOnSwitch},
                          {"buff_time", s.buffTime},
                          {"role", s.role == Role::Healer   ? "Healer"
                                   : s.role == Role::SubDps ? "SubDps"
                                                            : "MainDps"}};
        for (const auto &[key, member] : commonTimes)
            value[key] = s.*member < 0 ? -1 : now - s.*member;
        chars.append(value);
    }
    for (const auto &f : host.freezes.entries())
        freezes.append(
            QJsonObject{{"age", now - f.start}, {"duration", f.duration}, {"threshold", f.threshold}});
    return {{"chars", chars},
            {"freezes", freezes},
            {"in_liberation", host.inLiberation},
            {"combat_age", host.combatStart < 0 ? -1 : now - host.combatStart}};
}
void applyWorkshopCombatState(CombatHost &host, const QJsonObject &state) {
    const auto party = host.characters();
    const auto values = state["chars"].toArray();
    if (values.size() != party.size())
        fail("工坊队伍状态与当前队伍不一致");
    const auto now = host.now();
    QSet<int> seen;
    QVector<QPair<Character *, CharacterState>> prepared;
    QVector<bool> checks;
    for (const auto &v : values) {
        const auto value = v.toObject();
        const auto index = finiteNumber(value["index"]);
        auto it = std::find_if(party.begin(), party.end(), [&](auto c) { return c->state.index == index; });
        if (it == party.end() || seen.contains(int(index)))
            fail("工坊返回了不一致的角色位置");
        seen.insert(int(index));
        auto s = (*it)->state;
        const auto role = value["role"].toString();
        if (!QStringList{"MainDps", "SubDps", "Healer"}.contains(role))
            fail("工坊角色定位无效");
        s.role = role == "Healer" ? Role::Healer : role == "SubDps" ? Role::SubDps : Role::MainDps;
        s.current = stateBoolean(value["current"]);
        s.hasIntro = stateBoolean(value["intro"]);
        s.hasSubDpsIntro = stateBoolean(value["sub_intro"]);
        s.currentCon = finiteNumber(value["current_con"]);
        if (s.currentCon < 0 || s.currentCon > 1)
            fail("工坊协奏值无效");
        s.buffTime = finiteNumber(value["buff_time"]);
        if (s.buffTime < 0 || s.buffTime > 86400)
            fail("工坊角色增益时间无效");
        for (const auto &[key, member] : commonTimes) {
            const auto age = finiteNumber(value[key]);
            s.*member = age < 0 ? -1 : now - age;
        }
        prepared.append({*it, s});
        checks.append(stateBoolean(value["check_f"]));
    }
    FreezeTimeline freezes;
    const auto timeline = state["freezes"].toArray();
    if (timeline.size() > 2048)
        fail("工坊冻结时间线过大");
    for (const auto &v : timeline) {
        const auto f = v.toObject();
        const auto age = finiteNumber(f["age"]), duration = finiteNumber(f["duration"]),
                   threshold = finiteNumber(f["threshold"]);
        if (duration < 0 || duration > 600)
            fail("工坊冻结时间无效");
        freezes.add(now, now - age, duration, threshold);
    }
    const auto combatAge = finiteNumber(state["combat_age"]);
    const auto inLiberation = stateBoolean(state["in_liberation"]);
    for (int i = 0; i < prepared.size(); ++i) {
        prepared[i].first->state = prepared[i].second;
        prepared[i].first->checkFOnSwitch = checks[i];
    }
    host.freezes = std::move(freezes);
    host.combatStart = combatAge < 0 ? -1 : now - combatAge;
    host.inLiberation = inLiberation;
}
void validateWorkshopHandshake(const QJsonObject &hello) {
    if (hello["event"] != "ready" || hello["protocol"].toInt() != workshopProtocolVersion ||
        hello["api"].toInt() != workshopApiVersion)
        fail("工坊桥接版本不一致，请使用同一完整包中的主程序和兼容引擎");
    const auto capabilities = hello["capabilities"].toArray();
    for (const auto &capability : {"request-identity", "combat-state-ages", "immutable-frame-cache",
                                   "bridge-metrics", "character-policy", "task-config-context"})
        if (!capabilities.contains(capability))
            fail("工坊兼容引擎缺少必要接口能力，请更新完整包");
}

} // namespace okww::runtime::workshop_state_reference
