#pragma once
#include "character.hpp"
namespace okww::runtime {
class MacroBroken : public CombatEnded {
  public:
    explicit MacroBroken(const QString &reason) : CombatEnded(reason) {}
};
struct MacroSwitch {
    bool click = false;
    std::optional<bool> clickDuring;
    double settle = 0, timeout = 2.5;
    bool readCon = false, assumeCon = false;
    double clickInterval = .12;
    int clickFirst = 0;
};
struct MacroKeySpam {
    double interval = .1;
    std::optional<bool> poke;
    int presses = -1;
    double clickInterval = .12;
    std::function<bool()> stop;
};
// Native team macros use explicit HID operations and a cancellable monotonic
// timer. The game PC is never controlled using local SendInput/PostMessage.
class TeamMacro {
  protected:
    CombatHost &h_;
    Character *cur_ = nullptr;
    bool holding_ = false, poking_ = false;
    bool armed_ = true;
    double lastOpener_ = -1, macroStart_ = 0, switchPress_ = 0, libPress_ = 0;
    int loopCount_ = 0;
    QStringList marks_;
    double attackInterval_ = .12, keyInterval_ = .1, switchKeyInterval_ = 0, switchKeyDown_ = .02;
    int untrustedConfirm_ = 3;
    double animationTimeout_ = 8, animationTail_ = .15, noAnimationGrace_ = .35;
    double conTimeout_ = 2, conInterval_ = .05, resendStep_ = .06, skillSettle_ = .4;
    void wait(double seconds);
    void click();
    void key(QString key, double hold = .01);
    void send(Character &character, Skill skill, double hold = .01);
    void record(Character &character, Skill skill);
    void hold();
    void release();
    void start();
    void mark(const QString &name);
    void need(bool ok, const QString &reason);
    int spam(double window, double interval = -1, int maxClicks = -1,
             const std::function<bool()> &sample = {}, int sampleEvery = 2);
    int spamKey(const std::function<void()> &send, double window, const MacroKeySpam &options = {});
    int gap(double seconds);
    bool quickSwitch(Character *target, const MacroSwitch &options = {});
    void handoff(Character &previous, Character &next, bool intro);
    Character *teammate(const QString &name);
    Character *onField(double timeout = 1.5);
    bool waitHud(bool poke = false, double timeout = -1, double tail = 0);
    bool waitHudUp(double cap = 1);
    bool conAtLeast(Character &character, double level, double cap = -1, double interval = -1,
                    bool poke = true);
    bool pressUntilCd(const std::function<void()> &send, const std::function<bool()> &available, double cap,
                      double grace = .15, bool poke = false);
    bool pressRes(Character &character, double cap, double grace = -1, bool poke = false);
    bool pressEcho(Character &character, double cap, double grace = .15);
    void echoNow(Character &character, int presses = 2, double window = 0);
    bool openerPending(double gap = 60) const;
    bool execute(Character &entry, const std::function<void()> &body);

  public:
    explicit TeamMacro(CombatHost &host) : h_(host) {}
    virtual ~TeamMacro();
    virtual bool perform(Character &entry) = 0;
    virtual void configure() {}
    virtual std::optional<int> priority(const Character &, bool) { return {}; }
    virtual void reset() { armed_ = true; }
    virtual void combatEnd() {
        release();
        poking_ = false;
    }
};
} // namespace okww::runtime
