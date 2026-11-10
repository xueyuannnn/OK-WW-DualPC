#include "character_registry.hpp"
#include "combat_scopes.hpp"
#include <opencv2/imgproc.hpp>
namespace okww::runtime {
namespace {
class Changli final : public Character {
    bool enhanced_ = false;
    int gauge() {
        return host_.forte(Forte::Mouse)
                   ? 4
                   : host_.segmentedGauge({3840, 2160}, {1633, 2004, 527, 12},
                                          {{95, 85, 240}, {115, 105, 255}}, 4, 9, 11, 400);
    }
    bool flick(double timeout = .2, bool click = true) {
        if (click && resonanceAvailable())
            host_.wait([&] { return host_.highlighted(Skill::Resonance) > 0; }, .2,
                       [&] { host_.click("left", .1); });
        if (host_.highlighted(Skill::Resonance) > 0 && resonanceAvailable()) {
            host_.wait([&] { return !resonanceAvailable(); }, timeout, [&] { send(Skill::Resonance); });
            return true;
        }
        return false;
    }
    bool libHeavy() {
        double start = host_.now(), last = 0;
        bool clicked = false, held = false;
        try {
            while (liberationAvailable() && host_.inTeam()) {
                const double now = host_.now();
                if (now - last > .1) {
                    send(Skill::Liberation);
                    if (!clicked) {
                        clicked = true;
                        record(Skill::Liberation);
                    }
                    last = now;
                }
                if (host_.now() - start > 5)
                    throw CombatInterrupted("Changli liberation input timed out");
                host_.nextFrame();
            }
            if (clicked) {
                if (host_.wait([&] { return !host_.inTeam(); }, .4))
                    host_.inLiberation = true;
                else {
                    host_.inLiberation = false;
                    return false;
                }
            }
            start = host_.now();
            while (!host_.inTeam()) {
                host_.inLiberation = true;
                if (!clicked) {
                    clicked = true;
                    record(Skill::Liberation);
                }
                if (host_.now() - start > 1.5 && !held) {
                    host_.mouseDown();
                    held = true;
                }
                if (host_.now() - start > 7)
                    throw CombatInterrupted("Changli liberation animation timed out");
                host_.nextFrame();
            }
            host_.freezes.add(host_.now(), start, host_.now() - start);
            host_.inLiberation = false;
            if (clicked) {
                host_.wait([&] { return host_.inTeam() && host_.forte(Forte::Mouse); }, .6);
                host_.wait([&] { return host_.inTeam() && !host_.forte(Forte::Mouse); }, .6);
            }
            host_.mouseUp();
            held = false;
            host_.checkCombat();
            return clicked;
        } catch (...) {
            host_.inLiberation = false;
            if (held) {
                try {
                    host_.mouseUp();
                } catch (...) {
                }
            }
            throw;
        }
    }
    void outro(int forte) {
        if (forte == 3) {
            const double start = host_.now();
            double full = start;
            bool res = true;
            while (host_.now() - start < 5) {
                if (res && flick(.2, false)) {
                    res = false;
                    continue;
                }
                host_.click("left", .1);
                if (host_.forte(Forte::Mouse)) {
                    if (host_.now() - full > .2)
                        break;
                } else
                    full = host_.now();
                host_.checkCombat();
                host_.nextFrame();
            }
            if (host_.forte(Forte::Mouse)) {
                heavyForte(Forte::Mouse);
                sleep(1);
            }
        } else if (forte >= 4 || host_.forte(Forte::Mouse)) {
            if (flying())
                heavyAttack();
            heavyForte(Forte::Mouse);
            forte = 0;
            sleep(1);
        }
        if (liberationAvailable() && libHeavy()) {
            sleep(.6);
            forte = 0;
        }
        if (forte < 3 && flick(.2, false)) {
            enhanced_ = true;
            return;
        }
        echo();
    }
    void doPerform() override {
        checkFOnSwitch = true;
        if (state.hasIntro) {
            normalAttacks(.3);
            enhanced_ = true;
        }
        int forte = gauge();
        if (enhanced_) {
            normalAttacks(.2);
            sleep(.2);
            if (outroSource() == "char_brant") {
                sleep(.2);
                outro(gauge());
                switchNext();
                return;
            }
            if (forte == 3) {
                sleep(.2);
                forte = gauge();
            }
        }
        enhanced_ = false;
        if (forte == 4 || host_.forte(Forte::Mouse)) {
            if (flying())
                heavyAttack();
            heavyForte(Forte::Mouse);
            checkFOnSwitch = false;
            host_.checkCombat();
            switchNext();
            return;
        }
        if (!(forte >= 3 && resonanceAvailable()) && liberationAvailable() && libHeavy()) {
            checkFOnSwitch = false;
            switchNext();
            return;
        }
        if (flick(.2, false)) {
            enhanced_ = true;
            switchNext();
            return;
        }
        if (echo()) {
            switchNext();
            return;
        }
        normalAttacks(.1);
        switchNext();
    }

  public:
    using Character::Character;
    void resetState() override {
        Character::resetState();
        enhanced_ = false;
    }
    int switchPriority(const Character *current, bool intro, bool low) override {
        if (intro && current && current->state.name == "char_brant")
            return int(SwitchPriority::Must);
        return Character::switchPriority(current, intro, low);
    }
};
class Rover final : public Character {
    int lastForm_ = -1;
    Role configuredRole_;
    bool skyfall_ = false;
    void updateRole() {
        const int form = state.ringIndex >= 0 ? state.ringIndex : lastForm_;
        state.role = form == -1 || form == 1 ? Role::SubDps : configuredRole_;
    }
    bool electricReady() { return forteFull() && host_.forte(Forte::Resonance); }
    bool electricShort() {
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (!electricReady())
                return false;
            const double start = host_.now();
            send(Skill::Resonance, 0, -1, .05);
            if (host_.wait(
                    [&] { return elapsed(start) >= 1.5 && !forteFull() && !host_.forte(Forte::Resonance); },
                    2.6, {}, .2, [&] { host_.checkCombat(); })) {
                record(Skill::Resonance);
                return true;
            }
        }
        return false;
    }
    void electric() {
        waitDown(false);
        echo();
        liberation({.clickF = false});
        if (!electricReady() && resonanceAvailable() && resonance({.sendClick = false, .timeout = 1}).clicked)
            sleep(.1);
        const double start = host_.now(), duration = state.hasIntro ? 8 : 12;
        while (!electricReady() && elapsed(start) < duration) {
            host_.click();
            sleep(.1);
        }
        if (!electricReady() || !electricShort())
            return;
        if (!isConFull())
            liberation({.clickF = false});
        if (!isConFull())
            normalAttacks(2, .1, 0, false, true);
    }
    bool windFlying() { return host_.hasLevitator ? flying() : host_.highlighted(Skill::Resonance) > .15; }
    bool windClick(double duration) {
        const double start = host_.now();
        while (host_.now() - start < duration) {
            if (!windFlying())
                return false;
            host_.click("left", .1);
            sleep(.1);
        }
        return true;
    }
    void windDown(bool checkForte = true) {
        if (windFlying()) {
            if (host_.hasLevitator)
                waitDown();
            else
                host_.wait([&] { return host_.highlighted(Skill::Resonance) < .15; }, 2.5,
                           [&] {
                               host_.click("left", .1);
                               host_.sleep(.01);
                           });
        }
        if (checkForte) {
            sleep(.03);
            if (forteFull())
                send(Skill::Resonance);
        } else
            sleep(.01);
    }
    void wind() {
        if (!(state.hasIntro && windClick(2))) {
            windDown(false);
            if (resonanceAvailable() && !forteFull()) {
                echo({.timeout = 0});
                const double start = host_.now();
                bool flew = false;
                while (host_.now() - start < 1) {
                    send(Skill::Resonance, 0, .1);
                    host_.nextFrame();
                    host_.click("left", .1);
                    if ((flew = windFlying()))
                        break;
                }
                if (flew)
                    windClick(skyfall_ ? 1.6 : 1.74);
                if (skyfall_ && resonance({.sendClick = false}).clicked)
                    windClick(1);
            }
        }
        liberation({.sendClick = true});
        windDown();
    }
    void spectro() {
        if (state.hasIntro)
            normalAttacks(1);
        waitDown();
        heavyAttack();
        sleep(.4);
        normalAttacks(.7);
        echo({.timeout = 0});
        if (forteFull()) {
            host_.checkCombat();
            if (resonanceAvailable() && resonance().clicked) {
                normalAttacks(1.4);
                sleep(.1);
            }
        }
        host_.checkCombat();
        if (!liberation({.sendClick = true}))
            resonance();
    }
    void havoc() {
        waitDown();
        heavyForte(Forte::Mouse);
        liberation({.sendClick = true});
        if (resonance({.sendClick = true}).clicked)
            return;
        if (!echo())
            host_.click();
        normalAttacks(1.1 - elapsed(state.lastSwitch));
    }
    void basic() {
        if (state.hasIntro)
            normalAttacks(introMotion + .2);
        waitDown();
        echo();
        const bool lib = liberation({.sendClick = true});
        const bool res = resonance({.sendClick = true}).clicked;
        if (!lib && !res)
            normalAttacks(1);
    }
    void doPerform() override {
        host_.ensureElement(*this);
        updateRole();
        if (state.ringIndex == 4)
            skyfall_ = hasCharacter("Cartethyia") && hasCharacter("Phoebe");
        if (!state.hasIntro)
            sleep(.01);
        switch (state.ringIndex) {
        case 5:
            introMotion = .64;
            havoc();
            break;
        case 0:
            introMotion = .92;
            spectro();
            break;
        case 4:
            introMotion = .52;
            wind();
            break;
        case 1:
            electric();
            break;
        default:
            basic();
            break;
        }
        switchNext();
    }

  public:
    Rover(CombatHost &host, CharacterState value)
        : Character(host, std::move(value)), configuredRole_(state.role) {
        updateRole();
    }
    void resetState() override {
        if (state.ringIndex >= 0)
            lastForm_ = state.ringIndex;
        state.ringIndex = -1;
        Character::resetState();
        updateRole();
    }
};
class Cartethyia final : public Character {
    bool small_ = true, midOnce_ = false, transformed_ = false;
    std::array<bool, 3> buffs_{};
    double resTime_ = -1, n4Time_ = -1;
    bool small() { return small_ = host_.partialFeature("forte_cartethyia_sword3", .5, .6, .5); }
    void buffs() {
        for (int i = 0; i < 3; ++i)
            buffs_[i] = host_.feature(QString("forte_cartethyia_sword%1").arg(i + 1), .9);
    }
    bool firstEngage() const {
        const double gap = state.lastPerform - host_.combatStart;
        return gap >= 0 && gap < .4;
    }
    double n4Duration() {
        double duration;
        if (!transformed_ && state.hasIntro)
            duration = 3.9 - (host_.now() - state.lastPerform);
        else if (transformed_ || firstEngage() || elapsed(n4Time_, true) < 1.5)
            duration = 3.25;
        else if (const double backswing = elapsed(resTime_, true); backswing < 2.5)
            duration = 2 + std::max(0., 1.6 - backswing);
        else
            duration = 1.9 - (host_.now() - state.lastPerform);
        n4Time_ = resTime_ = -1;
        return duration;
    }
    bool bigLib() {
        if (!host_.feature("lib_cartethyia_big"))
            return false;
        state.liberationReady = true;
        if (liberation()) {
            small_ = true;
            resonance();
            return true;
        }
        return false;
    }
    bool bigRes() {
        if (host_.hasCd(Skill::Resonance))
            return false;
        double last = 0, clickedAt = 0;
        const double start = host_.now();
        bool clicked = false;
        while (true) {
            if (clickedAt != 0 && host_.now() - clickedAt > 8) {
                host_.inLiberation = false;
                break;
            }
            if (host_.now() - start > 15)
                throw CombatInterrupted("Cartethyia resonance observation timed out");
            host_.checkCombat();
            const double now = host_.now(), white = host_.highlighted(Skill::Resonance);
            if (!resonanceAvailable())
                break;
            if (now - last > .1) {
                if (white > 0 && resonanceAvailable()) {
                    if (white < .17 && host_.now() - clickedAt < 2.5) {
                        host_.click();
                        continue;
                    }
                    if (clickedAt == 0) {
                        clicked = true;
                        clickedAt = now;
                    }
                    send(Skill::Resonance);
                }
                last = now;
            }
            if (bigLib())
                break;
            host_.nextFrame();
        }
        if (clicked) {
            record(Skill::Resonance);
            resTime_ = host_.now();
        }
        return clicked;
    }
    bool midAvailable() {
        if (!small_ || host_.whiteRatio({3840, 2160}, {2298, 1997, 63, 25}) <= .15)
            return false;
        const auto image = host_.imageRegion({3840, 2160}, {2298, 1997, 63, 25});
        if (image.empty())
            return false;
        cv::Mat gray;
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
        cv::Scalar mean, deviation;
        cv::meanStdDev(gray, mean, deviation);
        return mean[0] > 190 && deviation[0] < 45;
    }
    void midAction() {
        host_.tap(host_.jumpKey(), .1);
        if (echoAvailable())
            echo({.timeout = 0});
        host_.click();
        host_.sleep(.1);
    }
    void midAir() {
        buffs();
        if (!liberationAvailable() && !(buffs_[0] && buffs_[1] && buffs_[2]) && !midOnce_)
            return;
        const double start = host_.now();
        if (midAvailable()) {
            while (true) {
                midAction();
                if (!midAvailable()) {
                    sleep(.4);
                    break;
                }
                if (host_.now() - start > 2)
                    break;
                sleep(.1);
            }
        } else if (midOnce_)
            while (host_.now() - start < .8)
                midAction();
        midOnce_ = false;
    }
    bool acquire() {
        buffs();
        if (buffs_[0] && buffs_[1] && buffs_[2])
            return false;
        const bool action = !(buffs_[1] && buffs_[2]);
        if (!buffs_[1]) {
            const bool once = host_.partialFeature("forte_cartethyia_sword2", .5, .6, .85);
            double timeout = once ? (firstEngage() ? 2.5 : 2) : 3.5, start = host_.now();
            bool interrupted = false;
            while (host_.now() - start < timeout) {
                if (!once && host_.partialFeature("forte_cartethyia_sword2", .5, .6, .85))
                    break;
                if (!interrupted && flying()) {
                    if (timeout == 2)
                        timeout = 2.5;
                    interrupted = true;
                    host_.wait([&] { return !flying(); }, 3);
                    start = host_.now();
                }
                host_.click("left", .1);
                host_.sleep(.01);
                host_.checkCombat();
                host_.nextFrame();
            }
        }
        bool res = false;
        if (!buffs_[2]) {
            res = resonance().clicked;
            host_.checkCombat();
        }
        if (liberationAvailable()) {
            if (res)
                sleep(.2);
        } else if (action)
            return true;
        if (!buffs_[0]) {
            HeldCombatInput held(host_);
            const double start = host_.now();
            while (host_.now() - start < 1.5) {
                if (host_.feature("forte_cartethyia_sword1", .9))
                    break;
                host_.nextFrame();
            }
            held.release();
            host_.checkCombat();
        }
        if (!buffs_[0] && !buffs_[1] && !buffs_[2])
            midOnce_ = true;
        return !liberationAvailable();
    }
    void doPerform() override {
        transformed_ = false;
        if (state.hasIntro)
            normalAttacks(1.2);
        else
            echo({.timeout = 0});
        if (small()) {
            // Base waitDown reads the virtual form-specific intro duration.
            introMotion = introDuration();
            waitDown();
            if (acquire()) {
                switchNext();
                return;
            }
            host_.checkCombat();
            midAir();
            host_.checkCombat();
            if (liberation()) {
                small_ = false;
                state.lastResonance = -1;
                transformed_ = true;
            } else if (!small())
                transformed_ = true;
        }
        if (!bigRes()) {
            const double timeout = small() ? 1.1 : n4Duration(), start = host_.now();
            while (host_.now() - start < timeout) {
                if (bigLib()) {
                    switchNext();
                    return;
                }
                host_.click("left", .1);
                host_.checkCombat();
                host_.nextFrame();
            }
            n4Time_ = host_.now();
        }
        bigLib();
        switchNext();
    }

  public:
    using Character::Character;
    bool smallForm() const override { return small_; }
    void synchronizeForm(bool value) override { small_ = value; }
    double introDuration() const override { return small_ ? .6 : .78; }
    int switchPriority(const Character *current, bool intro, bool low) override {
        return small_ ? Character::switchPriority(current, intro, low) : int(SwitchPriority::Must);
    }
    void combatEnd() override {
        if (small_)
            return;
        const auto party = host_.characters();
        if (party.size() < 2)
            return;
        const int next = (state.index + 1) % party.size();
        const double start = host_.now();
        while (host_.now() - start < 6) {
            const int current = host_.partyIndex();
            if (current >= 0 && current != state.index)
                break;
            host_.tap(QString::number(next + 1));
            sleep(.2, false);
        }
    }
};
class Carlotta final : public Character {
    double switchLock_ = -1;
    int pressW_ = -1, forte_ = 0;
    Character *zhezhi_ = nullptr;
    bool continueLib_ = false, libReady_ = false;
    void decide() {
        pressW_ = 0;
        if (host_.taskId() == "FarmEchoTask")
            pressW_ = 1;
        else if ((zhezhi_ = hasCharacter("Zhezhi")))
            zhezhi_->setPartner(this);
    }
    int gauge() {
        if (host_.forte(Forte::Mouse))
            return 4;
        return forte_ = host_.segmentedGauge({5120, 2880}, {2164, 2670, 736, 10},
                                             {{235, 195, 70}, {255, 225, 100}}, 4, 9, 11, 100);
    }
    bool libOne() {
        if (pressW_ == 1) {
            HeldCombatInput held(host_, "w", false);
            const bool result = liberation();
            held.release();
            return result;
        }
        return liberation();
    }
    bool libLoop(bool pressW) {
        double dodge = -1;
        const double start = host_.now();
        bool cast = false;
        while (liberationAvailable()) {
            if (host_.now() - start > 20)
                throw CombatInterrupted("Carlotta liberation chain timed out");
            if (dodge != -1 && host_.now() - dodge > .5 && flying())
                shoreKeeperDodge();
            if (pressW ? libOne() : liberation()) {
                dodge = host_.now();
                cast = true;
                if (!pressW) {
                    continueLib_ = false;
                    libReady_ = false;
                }
            }
            host_.checkCombat();
        }
        return cast;
    }
    void outro() {
        zhezhi_->clearPartnerGauge();
        gauge();
        const double deadline = host_.now() + 40;
        if (!libReady_)
            while (!host_.forte(Forte::Mouse)) {
                if (resonanceAvailable())
                    resonance();
                else
                    host_.click("left", .1);
                if (elapsed(state.lastPerform) > 6)
                    break;
                if (host_.now() > deadline)
                    throw CombatInterrupted("Carlotta outro forte timed out");
                host_.checkCombat();
            }
        if (heavyForte(Forte::Mouse)) {
            libReady_ = true;
            forte_ = 0;
        }
        host_.checkCombat();
        // The original's two `== False` expressions are comparisons, not assignments.
        bool clicked = false, liberated = false;
        if (libReady_)
            while (elapsed(state.lastPerform) < 14) {
                if (host_.now() > deadline)
                    throw CombatInterrupted("Carlotta outro timed out");
                if (liberationAvailable() && !liberated) {
                    if (libLoop(false)) {
                        liberated = true;
                        forte_ = 0;
                        sleep(.2);
                    }
                }
                if (resonance().clicked) {
                    normalAttacks(.8);
                    ++forte_;
                    clicked = true;
                }
                host_.click("left", .1);
                if (!liberationAvailable() && !resonanceAvailable() && clicked)
                    break;
                host_.checkCombat();
            }
        if (echo({.timeout = 2}))
            switchLock_ = host_.now();
        continueLib_ = !liberated;
    }
    void interlock() {
        if (state.hasIntro) {
            normalAttacks(1.3);
            if (outroSource() == "char_zhezhi") {
                outro();
                switchNext();
                return;
            }
        }
        if (gauge() < 4 && resonanceAvailable() && !libReady_) {
            if (!state.hasIntro)
                heavyAttack();
            if (resonance().clicked) {
                forte_ += 2;
                switchLock_ = host_.now();
                switchNext();
                return;
            }
        }
        if (partnerReady())
            continueLib_ = false;
        if (heavyForte(Forte::Mouse)) {
            libReady_ = true;
            switchNext();
            return;
        }
        if (liberationAvailable() && continueLib_)
            libLoop(false);
        if (echoAvailable())
            echo();
        normalAttacks(.31);
        switchNext();
    }
    void doPerform() override {
        if (pressW_ == -1)
            decide();
        if (zhezhi_) {
            interlock();
            return;
        }
        if (state.hasIntro)
            normalAttacks(1.3);
        if (heavyForte(Forte::Mouse)) {
            switchNext();
            return;
        }
        if (liberationAvailable() && !needFastPerform()) {
            libLoop(true);
            echo();
            switchLock_ = host_.now();
            switchNext();
            return;
        }
        if (resonanceAvailable()) {
            if (!state.hasIntro)
                heavyAttack();
            if (resonance().clicked) {
                switchNext();
                return;
            }
        }
        if (echo()) {
            switchNext();
            return;
        }
        normalAttacks(.31);
        switchNext();
    }

  public:
    using Character::Character;
    void resetState() override {
        Character::resetState();
        switchLock_ = -1;
        pressW_ = -1;
        zhezhi_ = nullptr;
        forte_ = 0;
        continueLib_ = libReady_ = false;
    }
    bool partnerReady() override { return forte_ > 2 || (resonanceAvailable() && forte_ > 0) || libReady_; }
    bool echoAvailable() override { return !host_.hasCd(Skill::Echo, state.current ? -1 : state.index); }
    bool waitSwitch() override { return state.hasIntro && elapsed(switchLock_, true) < 2.5; }
    int switchPriority(const Character *current, bool intro, bool low) override {
        if (pressW_ == -1)
            decide();
        if (intro && current && current->state.name == "char_zhezhi")
            return int(SwitchPriority::Must);
        return Character::switchPriority(current, intro, low);
    }
    bool liberation(LiberationOptions o = {}) override {
        double start = host_.now(), last = 0;
        bool clicked = false;
        try {
            while (liberationAvailable()) {
                const double now = host_.now();
                if (now - last > .1) {
                    send(Skill::Liberation);
                    clicked = true;
                    last = now;
                }
                if (host_.now() - start > .4)
                    return false;
                host_.nextFrame();
            }
            if (!clicked)
                return false;
            if (!host_.wait([&] { return !host_.inTeam(); }, .4, [&] { host_.click("left", .1); })) {
                host_.inLiberation = false;
                return false;
            }
            host_.inLiberation = true;
            start = host_.now();
            double lastF = 0;
            while (!host_.inTeam()) {
                const double now = host_.now();
                if (o.clickF && now - start >= o.animationMinimum && now - lastF >= .1) {
                    host_.tap("f");
                    lastF = now;
                }
                if (now - start > 7)
                    throw CombatInterrupted("Carlotta liberation animation timed out");
                host_.nextFrame();
            }
            host_.freezes.add(host_.now(), start, host_.now() - start);
            record(Skill::Liberation);
            host_.inLiberation = false;
            state.liberationReady = false;
            return clicked;
        } catch (...) {
            host_.inLiberation = false;
            throw;
        }
    }
    ResonanceResult resonance(ResonanceOptions o = {}) override {
        double last = 0, clickedAt = 0;
        const double start = host_.now();
        bool clicked = false, lastRes = false;
        while (true) {
            if (host_.now() - start > (o.timeout == 0 ? 10 : o.timeout)) {
                host_.inLiberation = false;
                alertSkillFailed();
                break;
            }
            host_.checkCombat();
            const double now = host_.now(), white = host_.highlighted(Skill::Resonance);
            if (host_.hasCd(Skill::Resonance))
                break;
            if (now - last > .1) {
                if (o.sendClick && (white == 0 || lastRes)) {
                    host_.click();
                    lastRes = false;
                    continue;
                }
                if (white > 0 && resonanceAvailable()) {
                    if (clickedAt == 0) {
                        clicked = true;
                        clickedAt = now;
                        record(Skill::Resonance);
                    }
                    lastRes = true;
                    send(Skill::Resonance);
                    if (o.animation)
                        sleep(.2, false);
                }
                last = now;
            }
            host_.nextFrame();
        }
        host_.inLiberation = false;
        if (clicked)
            sleep(o.postSleep);
        return {clicked, clickedAt != 0 ? host_.now() - clickedAt : 0, false};
    }
};
} // namespace
std::span<const CharacterRegistration> formCharacters() {
    static const CharacterRegistration entries[] = {{"Changli", createCharacter<Changli>},
                                                    {"Rover", createCharacter<Rover>},
                                                    {"Cartethyia", createCharacter<Cartethyia>},
                                                    {"Carlotta", createCharacter<Carlotta>}};
    return entries;
}
} // namespace okww::runtime
