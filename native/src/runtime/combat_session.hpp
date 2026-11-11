#pragma once
#include "combat_vision.hpp"
#include "team_store.hpp"
namespace okww::runtime {
class CharacterDead final : public CombatEnded {
  public:
    CharacterDead() : CombatEnded("复苏弹窗中断战斗，本轮尚未完成") {}
};
class CharacterRevived final : public CombatEnded {
  public:
    CharacterRevived() : CombatEnded("死亡后已免费恢复，需要重新进入并执行本轮") {}
};
struct CombatOptions {
    double waitSeconds = 200;
    bool required = true, target = false, realmEntryAtHealPoint = false, useLiberation = true;
    double maximumSeconds = 1800;
    // A task may restore its own boss location after free healing. It must
    // throw on failure; returning still reports CharacterRevived, never a win.
    std::function<void()> recover;
};
// The GUI task registry remains gated until character/team and recovery parity
// is complete. This connects native rotations to real native vision and HID.
class CombatSession final : public CombatHost {
    TaskContext &context_;
    CombatVision vision_;
    std::vector<std::unique_ptr<Character>> party_;
    std::shared_ptr<TeamStore> teams_;
    QString selectedTeam_;
    std::unique_ptr<TeamMacro> macro_;
    QVector<CharacterDefinition> partyDefinitions_;
    Concerto concerto_;
    QString calibrationPath_;
    bool engaged_ = false, checking_ = false, checkingDialog_ = false, canBreak_ = false;
    bool healthCache_ = false, openWorld_ = false;
    bool combatScope_ = false;
    double combatDeadline_ = 0;
    bool physicalFWasDown_ = false, pendingMacroReset_ = false;
    uint64_t enabledRevision_ = 0;
    int combatCount_ = 0;
    int targetEscCount_ = 0;
    uint64_t checkedVersion_ = UINT64_MAX;
    double lastInterval_ = 0, lastDeathCheck_ = 0, lastBreakCheck_ = 0, lastHealthCheck_ = -1;
    double lastWorkshopObservation_ = -1;
    uint64_t partyScanVersion_ = UINT64_MAX;
    QJsonObject bridgeInfo_, lastPartyReport_;
    QByteArray lastPartyKey_;
    double lastPartyReportAt_ = -1;
    void publishParty(const QString &codeState = "ready");
    bool interval(double seconds);
    void watchdog();
    bool healthBar();
    bool targetEnemy();
    bool checkBreak() override;
    void updatePortraits();
    void switchHealer();
    Character *current();
    bool combatActive();
    void resetCombat();
    void performCurrent();

  public:
    CombatSession(TaskContext &context, const QString &resources, const QString &profileDirectory,
                  std::shared_ptr<TeamStore> teams = {});
    bool loadParty();
    void pollControls();
    bool runAutoCombat();
    bool active();
    bool combatOnce(const CombatOptions &options = {});
    void recoverStartingPopup(bool realmEntryAtHealPoint = false);
    std::function<bool()> onCombatCheck, combatEndCondition;
    double targetEnemyTimeout = 3;
    bool hasHealthBar() { return healthBar(); }
    bool closeRevivalDialog();
    double now() const override { return context_.execution.now(); }
    void sleep(double seconds) override;
    void nextFrame() override;
    int partyCount() override { return vision_.party().size; }
    void refreshObservation() override {
        context_.refresh();
        watchdog();
    }
    void preciseWait(double seconds) override { context_.execution.wait(seconds); }
    void workshopCheckpoint() override {
        context_.execution.checkpoint();
        if (lastWorkshopObservation_ < 0 || now() - lastWorkshopObservation_ >= .1) {
            lastWorkshopObservation_ = now();
            refreshObservation();
        }
    }
    QJsonObject workshopTaskConfig() const override {
        auto config = context_.profile.group(context_.task);
        for (auto it = context_.taskOverrides.begin(); it != context_.taskOverrides.end(); ++it)
            config[it.key()] = it.value();
        return config;
    }
    bool combatEngaged() const override { return engaged_; }
    void rawTap(const QString &key, double hold) override { context_.input.key(key, hold); }
    void rawClick(const QString &button, double hold) override { context_.input.click({}, hold, button); }
    cv::Mat namedImage(const QString &name) override;
    void clickRelative(double x, double y) override {
        context_.input.click(gamePoint({context_.frame.cols, context_.frame.rows}, x, y), .2);
    }
    void scroll(int steps) override {
        context_.input.scroll(steps, QPointF(context_.frame.cols * .5, context_.frame.rows * .5));
    }
    void snapshot(const QString &name) override;
    QString dodgeKey() const override { return context_.hotkey("Dodge Key", "shift"); }
    void clearBreak() override { canBreak_ = false; }
    bool macroBreakPrompt() override;
    bool visibleBreakPrompt(double threshold = .8) override;
    uint64_t observationId() const override { return context_.frameVersion; }
    double concertoAreaRatio(Character &character) override;
    cv::Mat frameImage() override { return context_.frame; }
    QJsonObject workshopCommand(QJsonObject command) override {
        return context_.input.workshopCommand(std::move(command));
    }
    void workshopTelemetry(QJsonObject info) override {
        bridgeInfo_ = std::move(info);
        publishParty();
    }
    bool visibleTarget() override;
    void checkCombat() override;
    bool inTeam() override { return vision_.party().visible; }
    bool openWorldAutoCombat() const override { return openWorld_; }
    bool hasCd(Skill skill, int index = -1) override;
    double cooldown(Skill skill, int index = -1) override;
    bool available(Skill skill, int index = -1, bool checkColor = true, bool checkCd = true) override;
    double highlighted(Skill skill) override;
    double white(const QString &box) override { return vision_.white(box); }
    double levitatorWhite() override { return vision_.white("edge_levitator"); }
    bool forte(Forte kind) override { return vision_.forte(kind); }
    double concerto(Character &character) override;
    void tap(QString key, double after = 0, double between = -1, double down = .01) override;
    QString hotkey(Skill skill) const override;
    QString toolKey() const override { return context_.hotkey("Tool Key", "t"); }
    QString jumpKey() const override { return context_.hotkey("Jump Key", "space"); }
    void click(QString button = "left", double between = -1) override;
    void mouseDown(QString button = "left") override { context_.input.buttonDown(button); }
    void mouseUp(QString button = "left") override { context_.input.buttonUp(button); }
    void keyDown(QString key) override { context_.input.keyDown(key); }
    void keyUp(QString key) override { context_.input.keyUp(key); }
    void switchNext(Character &current, bool freeIntro = false, bool targetLowCon = false,
                    const SwitchAction &post = {}) override;
    QVector<Character *> characters() override;
    bool breakF(bool force = false) override;
    bool feature(const QString &name, double threshold = .8, const QString &box = {}) override;
    QString bestInBox(const QString &box, const QStringList &names, double threshold = .6) override;
    double whiteRatio(QSize reference, QRect area, cv::Scalar lower = {250, 246, 244},
                      bool hc = true) override;
    QJsonValue characterSetting(const QString &name, QJsonValue fallback = {}) override {
        return context_.profile.value("Character Config", name, fallback);
    }
    QString targetName() const override {
        return context_.frame.cols == 1600 ? "has_target_169" : "has_target";
    }
    int partyIndex() override { return vision_.party().current; }
    QString taskId() const override { return context_.task; }
    cv::Mat imageRegion(QSize reference, QRect area, bool hc = true) override;
    bool partialFeature(const QString &name, double top, double boxHeight, double threshold) override {
        return bool(
            context_.find(name, {.threshold = threshold, .templateTop = top, .searchHeight = boxHeight}));
    }
    bool featureInRegion(const QString &name, QSize reference, QRect area, double threshold) override;
    bool targetLocked() override;
    void ensureElement(Character &character) override {
        if (character.state.ringIndex < 0)
            concerto(character);
    }
    int segmentedGauge(QSize reference, QRect area, ColorRange color, int segments, int minFrequency,
                       int maxFrequency, double minAmplitude) override;
    bool isAutoCombatTask() const override { return context_.task == "AutoCombatTask"; }
    void log(const QString &message) override {
        if (context_.info)
            context_.info("战斗", message);
    }
};
} // namespace okww::runtime
