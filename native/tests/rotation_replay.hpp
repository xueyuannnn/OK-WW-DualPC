#pragma once
#include "runtime/character.hpp"
#include "runtime/combat_vision.hpp"
#include "runtime/gauge_analysis.hpp"
namespace okww::runtime::test {
// Deterministic fake game for migration checks. It has no Backend/Input handle.
class RotationReplay final : public CombatHost {
    class Teammate final : public Character {
        int priority_;

      public:
        Teammate(CombatHost &host, CharacterState state, int priority)
            : Character(host, std::move(state)), priority_(priority) {}
        int switchPriority(const Character *, bool, bool) override { return priority_; }
    };
    QJsonObject config_;
    std::array<bool, 3> ready_{};
    std::array<double, 3> readyAt_{1e9, 1e9, 1e9};
    double time_ = 100, liberationAt_ = -1, mouseAt_ = -1;
    QMap<QString, double> lastEvents_;
    std::unique_ptr<Character> teammate_;
    std::vector<std::unique_ptr<Character>> party_;
    QMap<QString, cv::Mat> images_;
    QString regionKey(QRect area) const {
        return QString("%1,%2,%3,%4").arg(area.x()).arg(area.y()).arg(area.width()).arg(area.height());
    }
    void refreshReady() {
        for (int i = 0; i < 3; ++i)
            if (time_ >= readyAt_[i]) {
                ready_[i] = true;
                readyAt_[i] = 1e9;
            }
    }
    void interval(QString id, double interval) {
        if (interval > 0 && lastEvents_.contains(id))
            sleep(std::max(0., interval - (time_ - lastEvents_[id])));
        lastEvents_[id] = time_;
    }

  public:
    QJsonArray events;
    Character *character = nullptr;
    bool held = false;
    QSet<QString> heldKeys;
    explicit RotationReplay(QJsonObject config) : config_(config) {
        for (int i = 0; i < 3; ++i)
            ready_[i] = config["ready"].toArray()[i].toBool();
        useLiberation = config["use_liberation"].toBool(true);
        combatStart = config["combat_start"].toDouble();
        hasLevitator = config["levitator"].toBool();
        if (config.contains("ready_at"))
            for (int i = 0; i < 3; ++i)
                readyAt_[i] = config["ready_at"].toArray()[i].toDouble(1e9);
        if (config.contains("teammate") || config["fast"].toBool()) {
            CharacterState other;
            other.index = 1;
            other.name = config["teammate"].toString("Other");
            other.className = other.name;
            teammate_ = std::make_unique<Teammate>(
                *this, other, int(config["fast"].toBool() ? SwitchPriority::Must : SwitchPriority::Normal));
        }
        for (auto entry : config["party"].toArray()) {
            const auto member = entry.toObject();
            CharacterState other;
            other.index = int(party_.size()) + 1;
            other.className = member["class"].toString();
            other.name = member["name"].toString(other.className);
            other.role = Role(member["role"].toInt());
            other.ringIndex = member["element"].toInt(-1);
            other.lastBuff = member["buff_at"].toDouble(-1);
            other.lastSwitch = member["switch_at"].toDouble(-1);
            party_.push_back(makeCharacter(other.className, *this, other));
        }
    }
    void observe(const QJsonObject &changes) {
        for (auto it = changes.begin(); it != changes.end(); ++it)
            config_[it.key()] = it.value();
        if (changes.contains("ready"))
            for (int i = 0; i < 3; ++i)
                ready_[i] = changes["ready"].toArray()[i].toBool();
        if (changes.contains("forte"))
            mouseAt_ = -1;
        useLiberation = config_["use_liberation"].toBool(true);
        hasLevitator = config_["levitator"].toBool();
        if (changes.contains("ready_at"))
            for (int i = 0; i < 3; ++i)
                readyAt_[i] = changes["ready_at"].toArray()[i].toDouble(1e9);
    }
    Character *teammate() { return party_.empty() ? teammate_.get() : party_[0].get(); }
    double now() const override { return time_; }
    void event(QString kind, QString value) {
        events.append(QJsonObject{{"kind", kind}, {"value", value}, {"time", time_}});
    }
    void sleep(double seconds) override {
        time_ += std::max(0., seconds);
        if (config_.contains("cancel_at") && time_ >= config_["cancel_at"].toDouble())
            throw Cancelled();
        if (!skipCombatCheck)
            checkCombat();
    }
    void nextFrame() override { sleep(.02); }
    void checkCombat() override {
        if (!skipCombatCheck && config_.contains("stop_at") && time_ >= config_["stop_at"].toDouble())
            throw CombatEnded("replay interrupted");
        if (time_ > config_["budget_until"].toDouble(300))
            throw CombatEnded("replay exceeded budget");
    }
    bool inTeam() override {
        const auto t = time_ - liberationAt_;
        return time_ >= config_["team_at"].toDouble(0) &&
               (!config_["animation"].toBool(true) || liberationAt_ < 0 || t < .03 || t >= .55);
    }
    bool openWorldAutoCombat() const override { return config_["open_world"].toBool(); }
    bool hasCd(Skill skill, int = -1) override {
        refreshReady();
        return skill != Skill::ExtraAction && !ready_[int(skill)];
    }
    double cooldown(Skill skill, int = -1) override {
        return available(skill) ? 0
               : config_["cd_values"].toArray().isEmpty()
                   ? 2
                   : config_["cd_values"].toArray()[int(skill)].toDouble();
    }
    bool available(Skill skill, int = -1, bool = true, bool = true) override {
        refreshReady();
        return skill != Skill::ExtraAction && ready_[int(skill)];
    }
    double highlighted(Skill skill) override {
        const auto white = config_["white"].toObject();
        const auto key = "box_" + QStringList{"resonance", "echo", "liberation", "extra_action"}[int(skill)];
        return white.contains(key) ? white[key].toDouble() : available(skill) ? .1 : 0;
    }
    double white(const QString &box) override { return config_["white"].toObject()[box].toDouble(); }
    double levitatorWhite() override {
        return config_["flying"].toBool() && time_ < config_["land_at"].toDouble(1e9) ? .05 : .5;
    }
    bool forte(Forte) override {
        return config_["forte"].toBool() && (mouseAt_ < 0 || time_ - mouseAt_ < .12);
    }
    double concerto(Character &) override {
        if (config_.contains("con_at") && time_ >= config_["con_at"].toDouble())
            return 1;
        return config_["con"].toDouble();
    }
    void tap(QString key, double after = 0, double between = -1, double down = .01) override {
        interval("key:" + key, between);
        event("key", key);
        for (int i = 0; i < 3; ++i)
            if (key == hotkey(Skill(i))) {
                if (i == 2 && ready_[i])
                    liberationAt_ = time_;
                ready_[i] = false;
            }
        sleep(down);
        if (after)
            sleep(after);
    }
    QString hotkey(Skill skill) const override { return QStringList{"e", "r", "q", "4"}[int(skill)]; }
    QString toolKey() const override { return "t"; }
    QString jumpKey() const override { return "space"; }
    void click(QString button = "left", double between = -1) override {
        interval("mouse:" + button, between);
        event("click", button);
        sleep(.02);
    }
    void mouseDown(QString button = "left") override {
        event("down", button);
        held = true;
        mouseAt_ = time_;
    }
    void mouseUp(QString button = "left") override {
        event("up", button);
        held = false;
    }
    void keyDown(QString key) override {
        event("key_down", key);
        heldKeys.insert(key);
    }
    void keyUp(QString key) override {
        event("key_up", key);
        heldKeys.remove(key);
    }
    void switchNext(Character &current, bool freeIntro = false, bool = false,
                    const SwitchAction &post = {}) override {
        event("switch", "");
        if (teammate() && config_["confirm_switch"].toBool()) {
            const bool intro = freeIntro || current.isConFull();
            current.switchOut(intro);
            teammate()->state.current = true;
            if (post)
                post(*teammate(), intro);
        }
    }
    QVector<Character *> characters() override {
        if (!party_.empty()) {
            QVector<Character *> out{character};
            for (auto &member : party_)
                out.append(member.get());
            return out;
        }
        return teammate_ ? QVector<Character *>{character, teammate_.get()} : QVector<Character *>{character};
    }
    bool breakF(bool = false) override { return false; }
    bool checkBreak() override { return false; }
    bool feature(const QString &name, double = .8, const QString &box = {}) override {
        auto features = config_["features"].toObject();
        auto value = features.value(box.isEmpty() ? name : name + "@" + box);
        if (value.isUndefined())
            value = features.value(name);
        if (value.isArray()) {
            const auto span = value.toArray();
            return time_ >= span[0].toDouble() && time_ < span[1].toDouble();
        }
        return value.toBool();
    }
    QString bestInBox(const QString &box, const QStringList &names, double threshold = .6) override {
        for (const auto &name : names)
            if (feature(name, threshold, box))
                return name;
        return {};
    }
    double whiteRatio(QSize reference, QRect area, cv::Scalar lower = {250, 246, 244},
                      bool hc = true) override {
        if (config_["regions"].toObject().contains(regionKey(area)))
            return gaugeColorRatio(imageRegion(reference, area, hc), {lower, {255, 255, 255}});
        return forte(Forte::Bar) ? config_["forte_white"].toDouble(.1) : 0;
    }
    QJsonValue characterSetting(const QString &key, QJsonValue fallback = {}) override {
        const auto values = config_["character_config"].toObject();
        return values.contains(key) ? values[key] : fallback;
    }
    QString targetName() const override { return "has_target"; }
    int segmentedGauge(QSize reference, QRect area, ColorRange color, int segments, int minFreq, int maxFreq,
                       double amp) override {
        if (config_["regions"].toObject().contains(regionKey(area)))
            return segmentedGaugeValue(imageRegion(reference, area), color, segments, minFreq, maxFreq, amp);
        return config_["gauge"].toInt();
    }
    int partyIndex() override { return config_["party_index"].toInt(0); }
    QString taskId() const override { return config_["task_id"].toString("AutoCombatTask"); }
    cv::Mat imageRegion(QSize, QRect area, bool = true) override {
        auto value = config_["regions"].toObject()[regionKey(area)];
        QString path;
        if (value.isString())
            path = value.toString();
        else if (value.isObject()) {
            const auto v = value.toObject();
            if (time_ >= v["from"].toDouble(0) && time_ < v["until"].toDouble(1e9))
                path = v["path"].toString();
        }
        if (!path.isEmpty()) {
            if (!images_.contains(path))
                images_[path] = readImage(path);
            return images_[path];
        }
        return cv::Mat::zeros(area.height(), area.width(), CV_8UC3);
    }
    bool partialFeature(const QString &name, double, double, double threshold) override {
        return feature(name + "#partial", threshold);
    }
    bool featureInRegion(const QString &name, QSize, QRect area, double threshold) override {
        return feature(name, threshold, regionKey(area));
    }
    bool targetLocked() override { return config_["target"].toBool(true); }
    void ensureElement(Character &actor) override {
        if (actor.state.ringIndex < 0)
            actor.state.ringIndex = config_["element"].toInt(0);
    }
    bool isAutoCombatTask() const override { return config_["auto_task"].toBool(true); }
    void log(const QString &) override {}
};
} // namespace okww::runtime::test
