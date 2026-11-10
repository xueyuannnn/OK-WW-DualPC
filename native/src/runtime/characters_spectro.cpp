#include "character_registry.hpp"
#include "combat_scopes.hpp"
#include "gauge_analysis.hpp"
namespace okww::runtime {
namespace {
class Camellya final : public Character {
    bool waiting_ = false;
    double dropAt_ = 0, lastForte_ = 0, differenceSum_ = 0;
    double gauge(bool budding = false) {
        const auto image = host_.imageRegion({3840, 2160}, {1630, 2002, 546, 2});
        const ColorRange color = budding ? ColorRange{{168, 161, 220}, {225, 213, 255}}
                                         : ColorRange{{127, 46, 193}, {163, 93, 255}};
        return gaugeRoundHalfUp(stripedGaugeRatio(image, color), 3);
    }
    bool ephemeral() {
        return gaugeRingRatio(host_.imageRegion({2560, 1440}, {2110, 1236, 107, 107}),
                              {{168, 71, 234}, {179, 82, 245}}, .395, .496, true) > .1;
    }
    void castEphemeral() {
        host_.checkCombat();
        const double start = host_.now();
        while (ephemeral()) {
            if (host_.now() - start > 8)
                throw CombatInterrupted("Camellya ephemeral did not consume its indicator");
            send(Skill::Resonance);
            sleep(.1);
        }
        sleep(1.1);
    }
    void target(bool held = false) {
        if (!host_.targetLocked()) {
            if (held)
                host_.mouseUp();
            host_.checkCombat();
            host_.nextFrame();
            if (held)
                host_.mouseDown();
        }
    }
    int retry(bool budding = false) {
        const double current = gauge(budding);
        if (current < 0)
            return -1;
        const double diff = lastForte_ - current;
        if (!waiting_ && diff >= 0 && diff <= .01) {
            waiting_ = true;
            dropAt_ = host_.now();
            differenceSum_ = 0;
        }
        if (waiting_) {
            differenceSum_ += diff;
            if (differenceSum_ > .01)
                waiting_ = false;
        }
        if (waiting_ && elapsed(dropAt_) > .6) {
            waiting_ = false;
            host_.mouseUp();
            sleep(.1, false);
            host_.mouseDown();
            sleep(.1, false);
        }
        lastForte_ = current;
        return 0;
    }
    void heavy(double duration, bool untilFull = false) {
        lastForte_ = 0;
        bool frozen = false;
        double frozenAt = 0;
        HeldCombatInput held(host_);
        const double start = host_.now();
        while (host_.now() - start < duration) {
            if (untilFull && isConFull())
                break;
            const double forte = gauge();
            if (forte >= 0 && forte <= .01)
                break;
            target(true);
            host_.nextFrame();
            if (frozen && host_.now() - frozenAt >= .2)
                frozen = false;
            if (!frozen && retry() < 0) {
                frozen = true;
                frozenAt = host_.now();
            }
        }
        sleep(.1, false);
        held.release();
        waiting_ = false;
    }
    void doPerform() override {
        if (state.hasIntro) {
            normalAttacks(1.2);
            sleep(.1);
            heavy(4.6, true);
        }
        if (liberationAvailable())
            liberation({.conLessThan = .82});
        const double con = currentCon();
        double loop = con < .82 ? 1.1 : 4.6;
        if (con < .82 && resonanceAvailable())
            resonance();
        double start = host_.now(), frozenAt = 0;
        const double deadline = start + 30;
        bool budding = false, frozen = false;
        std::unique_ptr<HeldCombatInput> held;
        lastForte_ = 0;
        while (host_.now() - start < loop || host_.feature("camellya_budding", .7)) {
            if (host_.now() > deadline)
                throw CombatInterrupted("Camellya budding indicator did not end");
            if (!budding) {
                if (ephemeral() && isConFull()) {
                    castEphemeral();
                    budding = true;
                } else {
                    host_.click("left", .1);
                    if (currentCon() < .82) {
                        if (!isConFull()) {
                            echo();
                            switchNext();
                            return;
                        } else if (loop < 3.1)
                            loop += 1;
                    }
                }
                if (budding) {
                    target();
                    start = host_.now();
                    loop = 5.1;
                }
            }
            if (budding) {
                if (!held)
                    held = std::make_unique<HeldCombatInput>(host_);
                if (host_.now() - start < 1.5 && liberationAvailable() && liberation()) {
                    host_.mouseUp();
                    sleep(.2, false);
                    host_.mouseDown();
                }
            }
            target(bool(held));
            host_.nextFrame();
            if (frozen && host_.now() - frozenAt >= .2)
                frozen = false;
            if (!frozen && held && retry(budding) < 0) {
                frozen = true;
                frozenAt = host_.now();
            }
        }
        waiting_ = false;
        if (held) {
            held->release();
            sleep(.1);
        }
        if (budding) {
            resonance();
            sleep(.1);
        }
        echo();
        switchNext();
    }

  public:
    using Character::Character;
    void resetState() override {
        Character::resetState();
        waiting_ = false;
    }
    bool echo(EchoOptions = {}) override {
        if (!echoAvailable())
            return false;
        send(Skill::Echo);
        return true;
    }
    void combatEnd() override { switchOther(); }
    int switchPriority(const Character *current, bool intro, bool low) override {
        return intro ? int(SwitchPriority::Must) : Character::switchPriority(current, intro, low);
    }
};
class Phoebe final : public Character {
    enum class Prayer { Success, Unavailable, Timeout };
    int attribute_ = 0, entered_ = 0, starflash_ = 0, liberations_ = 0, outros_ = 0, priorityLib_ = 0;
    bool star_ = false, firstDone_ = false;
    Character *zani_ = nullptr;
    double starRatio(bool blue) {
        return gaugeColorRatio(host_.imageRegion({3840, 2160}, {1890, 2010, 25, 20}),
                               blue ? ColorRange{{240, 240, 240}, {255, 255, 255}}
                                    : ColorRange{{160, 220, 235}, {190, 250, 255}});
    }
    bool middle() {
        if (star_)
            return true;
        if (attribute_ > 0 && starRatio(attribute_ == 2) > .25)
            return star_ = true;
        return false;
    }
    bool confession() {
        return gaugeRingRatio(host_.imageRegion({2560, 1440}, {2110, 1236, 107, 107}, false),
                              {{250, 176, 124}, {255, 186, 134}}, .425, .490) > .15;
    }
    std::function<bool()> prayerCondition() {
        if (!middle())
            return [&] { return forteFull(); };
        if (confession())
            return [&] { return confession(); };
        return [] { return false; };
    }
    void land() {
        host_.wait([&] { return !flying(); }, 2,
                   [&] {
                       host_.click("left", .1);
                       host_.sleep(.1);
                   });
    }
    Prayer prayer() {
        host_.wait([&] { return host_.inTeam(); }, 3);
        const auto condition = prayerCondition();
        if (!condition())
            return Prayer::Unavailable;
        double start = host_.now();
        const double deadline = start + 10;
        while (condition()) {
            if (host_.now() - start > 2 || host_.now() > deadline)
                return Prayer::Timeout;
            HeldCombatInput held(host_, attribute_ == 2 ? host_.hotkey(Skill::Resonance) : "left",
                                 attribute_ != 2);
            const double down = host_.now();
            while (condition() || host_.now() - down < .4) {
                if (host_.now() - down > 1)
                    break;
                host_.nextFrame();
            }
            held.release();
            if (flying()) {
                land();
                start = host_.now();
            }
            host_.nextFrame();
        }
        rightClicks(.05);
        star_ = true;
        if (attribute_ == 2)
            entered_ = starflash_ = liberations_ = outros_ = priorityLib_ = 0;
        ++entered_;
        return Prayer::Success;
    }
    bool heavy() {
        if (prayer() != Prayer::Unavailable)
            return false;
        double start = host_.now();
        const double deadline = start + 10;
        while (forteFull()) {
            if (host_.now() - start > 2 || host_.now() > deadline)
                return false;
            bool flew = false;
            HeldCombatInput held(host_);
            const double down = host_.now();
            while (host_.now() - down < .5) {
                if (!forteFull()) {
                    held.release();
                    return true;
                }
                if ((flew = flying()))
                    break;
                host_.nextFrame();
            }
            held.release();
            if (flew) {
                land();
                start = host_.now();
            }
            host_.checkCombat();
            host_.nextFrame();
        }
        return true;
    }
    int gauge() {
        const auto image = host_.imageRegion({3840, 2160}, {1633, 2004, 527, 10});
        return attribute_ == 1 ? phoebeGaugeValue(image, {{165, 240, 240}, {195, 255, 255}}, 4, 9, 11, 25)
                               : phoebeGaugeValue(image, {{190, 225, 225}, {225, 255, 255}}, 2, 18, 20, 50);
    }
    void starflash() {
        const double start = host_.now();
        double check = start;
        const auto condition = prayerCondition();
        if (!condition() && !forteFull()) {
            while (!forteFull()) {
                if (flying())
                    shoreKeeperDodge();
                host_.click();
                if (host_.now() - start > 5)
                    return;
                if (host_.now() - check > 1) {
                    if (condition() || gauge() == 0)
                        return;
                } else
                    check = host_.now();
                host_.checkCombat();
                host_.nextFrame();
            }
            rightClicks(.05);
        }
        if (heavy())
            ++starflash_;
    }
    void remaining(bool liber = true) {
        if (attribute_ != 1 && attribute_ != 2)
            return;
        const int count = attribute_ == 1 ? 4 : 2;
        for (int i = 0; i < count; ++i) {
            if (liber && liberations_ < 1 && liberationAvailable() && liberation())
                ++liberations_;
            if (gauge() > 0) {
                starflash();
                host_.nextFrame();
            }
        }
    }
    bool mismatch() {
        const double light = starRatio(false), blue = starRatio(true);
        if (light <= .25 && blue <= .25) {
            star_ = false;
            return false;
        }
        const int attribute = light > blue ? 1 : 2;
        if (attribute != attribute_) {
            const int previous = attribute_;
            attribute_ = attribute;
            try {
                remaining(false);
            } catch (...) {
                attribute_ = previous;
                throw;
            }
            attribute_ = previous;
            return true;
        }
        return false;
    }
    bool libNow() {
        if (star_ && !flying() && liberationAvailable() && liberation({.sendClick = true})) {
            ++liberations_;
            priorityLib_ = 1;
            host_.checkCombat();
            return true;
        }
        return false;
    }
    int zaniPhase() { return attribute_ == 2 && zani_ ? zani_->partnerPhase() : -1; }
    bool linkage() {
        const int phase = zaniPhase();
        if (zani_->partnerGauge() >= .9) {
            if (!resonanceAvailable()) {
                if (phase == 0 || zani_->partnerTimeLeft() > 3)
                    normalAttacks(1, .15);
            } else if (firstDone_ && !confession())
                resonance({.sendClick = false});
            return true;
        }
        if (phase == 1) {
            remaining();
            return true;
        }
        return false;
    }
    void firstCon() {
        if (!firstDone_) {
            firstDone_ = true;
            if (!isConFull())
                normalAttacks(5, .1, 0, false, true);
        }
    }
    void onceRes() {
        const double start = host_.now();
        while (resonanceAvailable()) {
            host_.checkCombat();
            if (host_.now() - start > .5)
                return;
            send(Skill::Resonance);
            host_.nextFrame();
        }
    }
    void decide() {
        zani_ = hasCharacter("Zani");
        attribute_ = zani_ || (hasCharacter("Cartethyia") && hasCharacter("Rover")) ? 2 : 1;
    }
    void doPerform() override {
        state.lastOutro = -1;
        const double start = host_.now();
        if (!attribute_)
            decide();
        if (state.hasIntro)
            normalAttacks(1.5);
        else
            sleep(.01);
        libNow();
        if (attribute_ == 1)
            echo({.timeout = 0});
        if (flying()) {
            normalAttacks(.1);
            switchNext();
            return;
        }
        const bool wrong = mismatch();
        if (attribute_ == 2 && zani_) {
            if (!star_)
                prayer();
            if (linkage()) {
                switchNext();
                return;
            }
        }
        const double uiWait = .35 - (host_.now() - start);
        if (uiWait > 0 && star_ && gauge() == 0)
            normalAttacks(uiWait);
        const auto entered = prayer();
        host_.checkCombat();
        if ((!wrong || entered == Prayer::Success) && star_ && liberation({.sendClick = true})) {
            ++liberations_;
            priorityLib_ = 1;
            host_.checkCombat();
        }
        if (entered == Prayer::Success || gauge() > 0) {
            starflash();
            libNow();
            if (attribute_ == 2 && starflash_ < 2 && zaniPhase() != 1)
                starflash();
        }
        libNow();
        if (resonanceAvailable()) {
            if (attribute_ == 2) {
                if (!confession() && firstDone_)
                    onceRes();
            } else
                resonance();
            firstCon();
            switchNext();
            return;
        }
        normalAttacks(.1);
        firstCon();
        switchNext();
    }

  public:
    using Character::Character;
    void resetState() override {
        Character::resetState();
        attribute_ = 0;
        star_ = false;
        zani_ = nullptr;
        firstDone_ = false;
    }
    bool forteFull() override { return star_ ? host_.forte(Forte::Mouse) : Character::forteFull(); }
    int outroCount() const override { return outros_; }
    void switchNext(bool free = false, bool low = false, const SwitchAction &post = {}) override {
        if (attribute_ == 2 && !priorityLib_)
            libNow();
        if (attribute_ == 2 && isConFull()) {
            echo();
            ++outros_;
        }
        Character::switchNext(free, low, post);
    }
    int switchPriority(const Character *current, bool intro, bool low) override {
        if (!intro && state.lastOutro > 0 && elapsed(state.lastOutro, true) < 4.5)
            return int(SwitchPriority::No);
        return Character::switchPriority(current, intro, low);
    }
};
class Zani final : public Character {
    enum class Result { Done, ForteFull, Failed, Interrupted };
    enum class Wait { False, True, Interrupted };
    double libTime_ = 0, blazes_ = -1, threshold_ = -1, crisis_ = -1, nightfall_ = -1, chair_ = -1,
           lastLib2_ = -1, dodge_ = -1;
    bool liberated_ = false;
    int phase_ = 0;
    Character *phoebe_ = nullptr;
    double crisisLeft() const { return crisis_ <= 0 ? 0 : 1.6 - elapsed(crisis_, true); }
    double nightfallLeft() const { return nightfall_ <= 0 ? 0 : 2.2 - elapsed(nightfall_, true); }
    bool notLib() {
        return host_.featureInRegion("box_target_enemy_inner", {2560, 1440}, {1909, 1274, 48, 48}, .75);
    }
    bool checkLib() {
        if (!host_.inTeam())
            return liberated_;
        if (notLib())
            liberated_ = false;
        else if (host_.featureInRegion("box_target_enemy_inner", {2560, 1440}, {1779, 1273, 51, 49}, .75))
            liberated_ = true;
        return liberated_;
    }
    void blazes() {
        const auto ratio = gaugeColorRatio(host_.imageRegion({3840, 2160}, {1627, 2014, 549, 3}),
                                           {{171, 239, 231}, {201, 255, 257}});
        blazes_ = std::ceil(ratio * 100) / 100;
    }
    double gauge() {
        return gaugeRoundHalfUp(gaugeColorRatio(host_.imageRegion({3840, 2160}, {1628, 1997, 555, 6}),
                                                {{156, 222, 239}, {196, 255, 255}}),
                                3);
    }
    bool nightfallReady(double threshold = .15) {
        return gaugeRingRatio(host_.imageRegion({2560, 1440}, {1853, 1233, 111, 111}),
                              {{205, 245, 245}, {225, 255, 255}}, .425, .490) > threshold;
    }
    Wait wait(const std::function<bool()> &condition, double timeout, const std::function<void()> &post = {},
              const std::function<bool()> &interrupt = {}, double settle = 0) {
        if (timeout <= 0)
            return Wait::False;
        const double start = host_.now();
        double stable = -1;
        bool once = true;
        while (host_.now() - start < timeout) {
            if (condition()) {
                if (settle == 0)
                    return Wait::True;
                if (stable < 0)
                    stable = host_.now();
                else if (host_.now() - stable >= settle)
                    return Wait::True;
            } else
                stable = -1;
            if (interrupt && interrupt())
                return Wait::Interrupted;
            if (once) {
                host_.checkCombat();
                once = false;
            }
            if (post)
                post();
            host_.nextFrame();
        }
        return Wait::False;
    }
    Result waitForte(double timeout, bool click = false, bool check = false, double settle = 0) {
        if (timeout <= 0)
            return Result::Done;
        const double start = host_.now();
        double lastCheck = 0, lastValue = -1;
        const auto interrupted = [&] {
            if (flying())
                return true;
            const double now = host_.now();
            if (check && now - start > .8 && now - lastCheck >= .2) {
                const double value = gauge();
                if (lastValue > 0 && value - lastValue < .01 && !host_.forte(Forte::Resonance)) {
                    rightClicks(.05);
                    dodge_ = host_.now();
                    return true;
                }
                lastValue = value;
                lastCheck = now;
            }
            return false;
        };
        const auto result =
            wait([&] { return host_.forte(Forte::Resonance); }, timeout,
                 click ? std::function<void()>([&] { host_.click("left", .1); }) : std::function<void()>{},
                 interrupted, settle);
        return result == Wait::Interrupted ? Result::Interrupted
               : result == Wait::True      ? Result::ForteFull
                                           : Result::Done;
    }
    void waitRes(bool click = false, bool checkLibTime = false) {
        // The old helper discards the wait result; callers see None.
        wait([&] { return host_.highlighted(Skill::Resonance) != 0; }, 2.5,
             click ? std::function<void()>([&] { host_.click("left", .1); }) : std::function<void()>{},
             checkLibTime ? std::function<bool()>([&] { return partnerTimeLeft() < 1.7; })
                          : std::function<bool()>{},
             .1);
    }
    void waitCrisis() {
        if (crisisLeft() <= 0)
            return;
        if (state.lastResonance > 0 && elapsed(state.lastResonance) < 5)
            wait([&] { return crisisLeft() <= 0; }, 2);
        else
            waitRes();
    }
    Result defense() {
        if (host_.forte(Forte::Resonance))
            return Result::ForteFull;
        if (resonanceAvailable()) {
            resonance({.sendClick = false});
            sleep(.2);
            normalAttacks(.2);
            return Result::Done;
        }
        return Result::Failed;
    }
    Result breakthrough() {
        double chair = 1.2;
        if (chair_ == -1) {
            auto result = defense();
            if (result == Result::Failed) {
                if ((result = waitForte(.3 - (host_.now() - dodge_))) != Result::Done)
                    return result;
                HeldCombatInput held(host_);
                if ((result = waitForte(.6)) != Result::Done)
                    return result; // RAII fixes the old early-return hold leak.
                held.release();
                chair = 1.15;
                if ((result = waitForte(.85, true)) != Result::Done)
                    return result;
            } else if (result == Result::ForteFull)
                return result;
        } else {
            chair -= host_.now() - chair_;
            chair_ = -1;
        }
        const auto result = waitForte(chair);
        if (result != Result::Done)
            return result;
        normalAttacks(.2);
        return result;
    }
    bool crisisCombo() {
        host_.checkCombat();
        if (!host_.forte(Forte::Resonance)) {
            auto result = breakthrough();
            if (result == Result::Done) {
                result = waitForte(2.2, false, true);
                if (result == Result::Done) {
                    rightClicks(.05);
                    dodge_ = host_.now();
                }
            }
            if (result != Result::ForteFull && !host_.forte(Forte::Resonance))
                return false;
        }
        const double start = host_.now();
        wait([&] { return !host_.forte(Forte::Resonance); }, 1, [&] { send(Skill::Resonance); });
        if (host_.now() - start < .35)
            return false;
        crisis_ = host_.now();
        return true;
    }
    void lib2() {
        double start = host_.now();
        const double deadline = start + 12;
        bool sendKey = true;
        host_.inLiberation = true;
        try {
            while (!notLib()) {
                if (host_.now() > deadline)
                    throw CombatInterrupted("Zani liberation exit indicator timed out");
                if (host_.now() - start > 6) {
                    host_.inLiberation = false;
                    if (!checkLib())
                        blazes();
                    return;
                }
                if (host_.highlighted(Skill::Resonance) == 0)
                    start = host_.now();
                else if (host_.now() - start > 1.5)
                    sendKey = false;
                if (sendKey)
                    send(Skill::Liberation);
                host_.nextFrame();
            }
            host_.inLiberation = false;
            const double now = host_.now();
            if (now - start >= 2.25) {
                lastLib2_ = now;
                host_.freezes.add(now, now - 2.25, 2.25, 0);
            }
            liberated_ = false;
            blazes_ = -1;
            libTime_ = -1;
            phase_ = 0;
        } catch (...) {
            host_.inLiberation = false;
            throw;
        }
    }
    bool shouldEnd(bool timeOnly = false) {
        if (partnerTimeLeft() < 1.7)
            return true;
        if (timeOnly || nightfallReady())
            return false;
        waitRes(true, true);
        return !host_.forte(Forte::Mouse);
    }
    void nightfall(bool cancel = false) {
        double start = host_.now();
        if (!nightfallReady()) {
            while (!nightfallReady() || host_.now() - start < 1.6) {
                host_.click();
                if (host_.now() - start > 3.5 || !liberated_)
                    return;
                if (shouldEnd(true))
                    lib2(); // Original lib2 returns None; no early return here.
                host_.checkCombat();
                host_.nextFrame();
            }
        }
        normalAttacks(.5);
        if (cancel) {
            start = host_.now();
            while (nightfallReady(.035)) {
                if (host_.now() - start > 2.5)
                    break;
                host_.click();
                host_.nextFrame();
            }
            sleep(.25, false);
            rightClicks(.1);
        } else
            nightfall_ = host_.now();
    }
    bool tryLib(bool crisis = false) {
        if (crisis) {
            waitCrisis();
            blazes();
        }
        if (echoAvailable())
            echo({.timeout = 0});
        if (!liberation({.sendClick = true}))
            return false;
        crisis_ = -1;
        phase_ = 1;
        liberated_ = true;
        libTime_ = host_.now();
        checkLib();
        rightClicks(.05);
        normalAttacks(.15);
        nightfall(true);
        sleep(.1);
        if (host_.forte(Forte::Mouse))
            nightfall();
        return true;
    }
    void doPerform() override {
        if (threshold_ == -1) {
            phoebe_ = hasCharacter("Phoebe");
            threshold_ = phoebe_ ? .6 : .4;
        }
        waitDown();
        checkLib();
        if (liberated_) {
            phase_ = 1;
            if (shouldEnd())
                lib2();
            else
                nightfall();
            switchNext();
            return;
        }
        phase_ = 0;
        host_.breakF();
        crisis_ = -1;
        blazes();
        const bool full = host_.forte(Forte::Resonance);
        bool available = host_.highlighted(Skill::Resonance) > .05, lib = liberationAvailable();
        if (state.hasIntro && blazes_ >= 1 && !lib) {
            sleep(.2, false);
            lib = liberationAvailable();
            available = host_.highlighted(Skill::Resonance) > .05;
        }
        const double predicted = blazes_ + .1;
        if (blazes_ >= 1 && lib) {
            if (!tryLib()) {
                sleep(.1);
                tryLib();
            }
            switchNext();
            return;
        }
        if (full) {
            if (crisisCombo() && predicted >= threshold_ && liberationAvailable())
                tryLib(true);
            switchNext();
            return;
        }
        if (available) {
            if (crisisCombo() && blazes_ >= threshold_ && liberationAvailable())
                tryLib(true);
            switchNext();
            return;
        }
        normalAttackUntilCanSwitch();
        switchNext();
    }

  public:
    Zani(CombatHost &host, CharacterState value) : Character(host, std::move(value)) {
        introMotion = 1.42;
        checkFOnSwitch = false;
    }
    void resetState() override {
        phoebe_ = nullptr;
        threshold_ = -1;
        chair_ = -1;
        Character::resetState();
    }
    double partnerGauge() const override { return blazes_; }
    double partnerTimeLeft() const override {
        return !liberated_ || libTime_ <= 0 ? 0 : 20 - elapsed(libTime_);
    }
    int partnerPhase() override {
        if (phase_ == 1 && partnerTimeLeft() <= 0) {
            blazes_ = -1;
            phase_ = 0;
        }
        return phase_;
    }
    bool partnerReady() override {
        if (state.current)
            blazes();
        return blazes_ >= threshold_ || (phoebe_ && phoebe_->outroCount() >= 1 && blazes_ >= .4);
    }
    bool waitSwitch() override { return state.hasIntro && nightfallLeft() > 0 && partnerTimeLeft() >= 2; }
    int switchPriority(const Character *current, bool intro, bool low) override {
        if (liberated_)
            return int(SwitchPriority::Must);
        if (intro && crisisLeft() > 0)
            return int(SwitchPriority::No);
        return Character::switchPriority(current, intro, low);
    }
};
} // namespace
std::span<const CharacterRegistration> spectroCharacters() {
    static const CharacterRegistration entries[] = {{"Camellya", createCharacter<Camellya>},
                                                    {"Phoebe", createCharacter<Phoebe>},
                                                    {"Zani", createCharacter<Zani>}};
    return entries;
}
} // namespace okww::runtime
