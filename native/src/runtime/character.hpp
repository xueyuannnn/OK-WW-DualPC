#pragma once
#include "combat_rules.hpp"

namespace okww::runtime {
class Character;
using SwitchAction = std::function<void(Character &, bool)>;
enum class Skill { Resonance, Echo, Liberation, ExtraAction };
enum class Forte { Mouse, Resonance, Bar };
class CombatEnded : public std::runtime_error {
  public:
    explicit CombatEnded(const QString &reason) : std::runtime_error(reason.toUtf8().constData()) {}
};
// An interrupted observation/rotation is not evidence that the enemy was
// defeated. Background combat retains its existing CombatEnded handling;
// one-shot farms can resume this segment without counting a completed round.
class CombatInterrupted final : public CombatEnded {
  public:
    using CombatEnded::CombatEnded;
};
// Native rotations depend on game observations and HID actions through this
// interface. Offline replays use the same rotations with recorded observations.
class CombatHost {
  public:
    FreezeTimeline freezes;
    bool inLiberation = false, skipCombatCheck = false, useLiberation = true, hasLevitator = false;
    double combatStart = 0;
    double macroRearmAt = -1, macroStartDelay = 0;
    bool macroBusy = false;
    virtual ~CombatHost() = default;
    virtual double now() const = 0;
    virtual void sleep(double seconds) = 0;
    virtual void nextFrame() = 0;
    virtual void checkCombat() = 0;
    virtual bool inTeam() = 0;
    virtual bool openWorldAutoCombat() const = 0;
    virtual bool hasCd(Skill skill, int index = -1) = 0;
    virtual double cooldown(Skill skill, int index = -1) = 0;
    virtual bool available(Skill skill, int index = -1, bool checkColor = true, bool checkCd = true) = 0;
    virtual double highlighted(Skill skill) = 0;
    virtual double white(const QString &box) = 0;
    virtual double levitatorWhite() = 0;
    virtual bool forte(Forte kind) = 0;
    virtual double concerto(Character &character) = 0;
    virtual void tap(QString key, double after = 0, double interval = -1, double down = .01) = 0;
    virtual QString hotkey(Skill skill) const = 0;
    virtual void click(QString button = "left", double interval = -1) = 0;
    virtual void mouseDown(QString button = "left") = 0;
    virtual void mouseUp(QString button = "left") = 0;
    virtual void keyDown(QString key) = 0;
    virtual void keyUp(QString key) = 0;
    virtual void switchNext(Character &current, bool freeIntro = false, bool targetLowCon = false,
                            const SwitchAction &post = {}) = 0;
    virtual QString toolKey() const = 0;
    virtual QString jumpKey() const = 0;
    virtual QVector<Character *> characters() = 0;
    virtual bool breakF(bool force = false) = 0;
    virtual bool checkBreak() = 0;
    virtual bool feature(const QString &name, double threshold = .8, const QString &box = {}) = 0;
    virtual QString bestInBox(const QString &box, const QStringList &names, double threshold = .6) = 0;
    virtual double whiteRatio(QSize reference, QRect area, cv::Scalar lower = {250, 246, 244},
                              bool hc = true) = 0;
    virtual QJsonValue characterSetting(const QString &name, QJsonValue fallback = {}) = 0;
    virtual int segmentedGauge(QSize reference, QRect area, ColorRange color, int segments, int minFrequency,
                               int maxFrequency, double minAmplitude) = 0;
    virtual QString targetName() const = 0;
    virtual int partyIndex() = 0;
    virtual QString taskId() const = 0;
    virtual cv::Mat imageRegion(QSize reference, QRect area, bool hc = true) = 0;
    virtual bool partialFeature(const QString &name, double top, double boxHeight, double threshold) = 0;
    virtual bool featureInRegion(const QString &name, QSize reference, QRect area, double threshold) = 0;
    virtual bool targetLocked() = 0;
    virtual void ensureElement(Character &character) = 0;
    virtual bool isAutoCombatTask() const = 0;
    virtual void log(const QString &message) = 0;
    virtual int partyCount() { return static_cast<int>(characters().size()); }
    virtual void refreshObservation() { nextFrame(); }
    virtual void preciseWait(double seconds) { sleep(seconds); }
    virtual void rawTap(const QString &key, double hold) { tap(key, 0, -1, hold); }
    virtual void rawClick(const QString &button, double) { click(button); }
    virtual cv::Mat namedImage(const QString &) { return {}; }
    virtual void clickRelative(double, double) { fail("当前战斗环境不支持菜单点击"); }
    virtual void scroll(int) { fail("当前战斗环境不支持滚轮"); }
    virtual void snapshot(const QString &) {}
    virtual QString dodgeKey() const { return "shift"; }
    virtual void clearBreak() {}
    virtual bool macroBreakPrompt() { return checkBreak(); }
    virtual bool visibleBreakPrompt(double threshold = .8) { return feature("f_break", threshold); }
    virtual uint64_t observationId() const { return 0; }
    virtual double concertoAreaRatio(Character &) { return 0; }
    virtual cv::Mat frameImage() { return {}; }
    virtual QJsonObject workshopCommand(QJsonObject) { fail("当前环境不支持工坊输入通道"); }
    virtual bool visibleTarget() { return targetLocked(); }
    virtual void workshopTelemetry(QJsonObject) {}
    virtual void workshopCheckpoint() { preciseWait(0); }
    virtual QJsonObject workshopTaskConfig() const { return {}; }
    virtual bool combatEngaged() const { return true; }
    bool wait(const std::function<bool()> &condition, double timeout, const std::function<void()> &post = {},
              double settle = 0, const std::function<void()> &pre = {});
};
struct ResonanceOptions {
    double postSleep = 0;
    bool animation = false, sendClick = true;
    double animationMinimum = 0, timeout = 0;
    bool clickF = true;
};
struct ResonanceResult {
    bool clicked = false;
    double duration = 0;
    bool animated = false;
};
struct LiberationOptions {
    double conLessThan = -1;
    bool sendClick = false;
    double waitIfReady = .1, animationMinimum = 0;
    bool clickF = true;
};
struct EchoOptions {
    double duration = 0, sleepTime = 0, timeout = 1;
};
class Character {
  protected:
    CombatHost &host_;
    virtual void doPerform();
    virtual void send(Skill skill, double after = 0, double interval = -1, double down = .01);
    void record(Skill skill);
    void alertSkillFailed();

  public:
    CharacterState state;
    double introMotion = .9;
    bool checkFOnSwitch = true;
    double cycleStartTime = 0;
    Character(CombatHost &host, CharacterState state);
    virtual ~Character() = default;
    void perform();
    virtual int switchPriority(const Character *current, bool hasIntro, bool targetLowCon = false);
    virtual bool skipCombatCheck() { return false; }
    virtual bool skipSwitchCombatCheck(bool) { return false; }
    virtual bool autoDodge(const std::function<bool()> &) { return false; }
    virtual bool waitSwitch() { return false; }
    virtual bool flying();
    bool down();
    virtual void waitDown(bool click = true);
    void waitIntro(double timeout = 1.2, bool click = true);
    void sleep(double seconds, bool checkCombat = true);
    double elapsed(double start, bool introMotion = false) const;
    bool available(Skill skill, bool checkColor = true, bool checkCd = true);
    virtual bool resonanceAvailable() { return available(Skill::Resonance, false); }
    virtual bool echoAvailable() { return available(Skill::Echo, false); }
    virtual bool liberationAvailable() { return available(Skill::Liberation); }
    virtual bool forteFull() { return host_.forte(Forte::Bar); }
    bool longAction(int variant = 1);
    QString outroSource() const;
    Character *hasCharacter(const QString &className) const;
    bool shoreKeeperDodge();
    void switchOther(bool allowAutoCombat = false);
    bool isConFull() { return state.currentCon == 1 || host_.concerto(*this) == 1; }
    double currentCon();
    bool hasBuff() const { return state.hasBuff(host_.freezes, host_.now()); }
    bool allBuffs();
    virtual ResonanceResult resonance(ResonanceOptions options = {});
    virtual bool liberation(LiberationOptions options = {});
    virtual bool echo(EchoOptions options = {});
    void normalAttack();
    void normalAttackUntilCanSwitch();
    virtual bool needFastPerform();
    virtual bool partnerReady() { return false; }
    virtual void setPartner(Character *) {}
    virtual void clearPartnerGauge() {}
    virtual bool smallForm() const { return true; }
    virtual void synchronizeForm(bool) {}
    virtual void synchronizeLiberationPhase(int) {}
    virtual bool inOutro() const { return false; }
    virtual double introDuration() const { return introMotion; }
    virtual double partnerGauge() const { return 0; }
    virtual double partnerTimeLeft() const { return 0; }
    virtual int partnerPhase() { return 0; }
    virtual int outroCount() const { return 0; }
    void cycleStart() { cycleStartTime = host_.now(); }
    void cycleSleep(double duration = .1);
    void heavyAttack(double duration = .6);
    bool heavyForte(Forte kind = Forte::Bar);
    bool heavyForte(const std::function<bool()> &condition);
    void normalAttacks(double duration, double interval = .1, double after = 0, bool resonanceIfReady = false,
                       bool untilFull = false);
    void repeatedKey(QString key, double duration, double interval = .1);
    void rightClicks(double duration, double interval = .1, QString direction = {});
    virtual void switchNext(bool freeIntro = false, bool targetLowCon = false, const SwitchAction &post = {});
    virtual void resetState();
    virtual void combatEnd();
    virtual void switchOut(bool fullCon = false) { state.switchOut(fullCon, host_.now()); }
};
// Only explicitly ported implementations are registered. Missing names throw;
// there is no silent fallback to a generic rotation for an unported character.
QStringList nativeCharacterClasses();
std::unique_ptr<Character> makeCharacter(const QString &className, CombatHost &host, CharacterState state);
} // namespace okww::runtime
