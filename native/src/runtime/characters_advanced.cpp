#include "character_registry.hpp"
#include "combat_scopes.hpp"
namespace okww::runtime {
namespace {
class Augusta final : public Character {
    bool majestyReady() {
        return host_.highlighted(Skill::Liberation) > 0 && host_.feature("Augusta_lib2", .5);
    }
    bool prowessReady() { return host_.feature("target_enemy_long_inner", .8); }
    bool prowess() {
        if (!heavyForte([&] { return prowessReady(); }))
            return false;
        normalAttacks(.3);
        return true;
    }
    bool majesty() {
        HeldCombatInput held(host_, host_.hotkey(Skill::Liberation), false);
        host_.inLiberation = true;
        try {
            host_.wait([&] { return !host_.inTeam(); }, .6);
            const double start = host_.now();
            held.release();
            if (host_.inTeam()) {
                host_.inLiberation = false;
                return false;
            }
            host_.wait([&] { return host_.inTeam(); }, 10, [&] { host_.click(); });
            host_.freezes.add(host_.now(), start, host_.now() - start);
            return true;
        } catch (...) {
            host_.inLiberation = false;
            throw;
        }
    }
    void doPerform() override {
        double timeout = 3;
        if (state.hasIntro) {
            normalAttacks(1.13);
            if (state.hasSubDpsIntro && outroSource() == "char_iuno")
                timeout = 14;
        }
        if (flying())
            waitDown();
        const double start = host_.now();
        while (host_.now() - start < timeout + 3) {
            cycleStart();
            if (majestyReady() && majesty()) {
                send(Skill::Echo);
                switchNext();
                return;
            }
            if (flying())
                shoreKeeperDodge();
            if (prowessReady() && prowess() && host_.now() - start > timeout) {
                switchNext();
                return;
            }
            if (resonanceAvailable()) {
                const double before = host_.now();
                resonance();
                if (host_.now() - before < 1.4) {
                    if (flying())
                        continue;
                    if (host_.wait([&] { return prowessReady(); }, 1) && prowess() &&
                        host_.now() - start > timeout && !flying()) {
                        switchNext();
                        return;
                    }
                } else if (majestyReady()) {
                    waitDown();
                    if (majesty())
                        send(Skill::Echo);
                    switchNext();
                    return;
                }
            }
            if (liberationAvailable() &&
                host_.wait([&] { return !liberationAvailable(); }, 2, [&] { send(Skill::Liberation); })) {
                record(Skill::Liberation);
                if (timeout < 14) {
                    switchNext();
                    return;
                }
            }
            host_.click();
            cycleSleep();
        }
        send(Skill::Echo);
        switchNext();
    }

  public:
    using Character::Character;
    bool liberationAvailable() override {
        return host_.highlighted(Skill::Liberation) > 0 && host_.feature("Augusta_lib1", .5);
    }
    bool resonanceAvailable() override { return !host_.hasCd(Skill::Resonance); }
    void combatEnd() override {
        const auto size = host_.characters().size();
        if (size)
            host_.tap(QString::number((state.index + 1) % size + 1));
    }
};
class Linnai final : public Character {
    bool checkRes() {
        if (!host_.inTeam())
            return false;
        const auto names =
            QStringList{host_.targetName(),
                        host_.targetName().startsWith("has_target_169") ? "no_target_169" : "no_target"};
        return !host_.bestInBox("target_box_long2", names).isEmpty() ||
               !host_.bestInBox("box_target_enemy_long", names).isEmpty() ||
               host_.feature("target_box_short", .6);
    }
    void waitKick() {
        sleep(.3);
        waitDown();
    }
    void underIntro() {
        if (!checkRes() && !host_.wait([&] { return checkRes(); },
                                       state.hasIntro && outroSource() == "char_aemeath" ? 1.6 : 1,
                                       [&] { host_.click("left", .1); }))
            return;
        host_.wait([&] { return host_.whiteRatio({5120, 2880}, {2846, 2602, 43, 88}) > .06 || isConFull(); },
                   1, [&] { host_.click(); });
        if (host_.wait([&] { return !forteFull(); }, 3, [&] { host_.tap(host_.jumpKey(), .01); })) {
            if (host_.wait([&] { return resonance().clicked; }, 2, [&] { host_.click(); })) {
                waitKick();
                bool second = false;
                host_.wait(
                    [&] {
                        if (isConFull())
                            return true;
                        return second = resonance().clicked;
                    },
                    3, [&] { host_.click(); });
                if (second)
                    waitKick();
            }
        }
        if (!isConFull() && liberation())
            host_.wait([&] { return isConFull(); }, 1.2, [&] { host_.click("left", .1); });
    }
    void charge() {
        normalAttacks(1);
        echo({.timeout = 0});
        if (!isConFull())
            liberation();
        if (!host_.forte(Forte::Mouse))
            resonance();
        host_.wait([&] { return host_.forte(Forte::Mouse); }, 2, [&] { host_.click(); });
        HeldCombatInput held(host_);
        if (host_.wait([&] { return !host_.forte(Forte::Mouse); }, 5)) {
            held.release();
            sleep(.4);
            underIntro();
        } else
            held.release();
    }
    void doPerform() override {
        if (state.hasIntro && checkRes())
            normalAttacks(1.33);
        else
            charge();
        if (liberationAvailable())
            liberation();
        switchNext();
    }

  public:
    using Character::Character;
    void combatEnd() override { switchOther(); }
    int switchPriority(const Character *current, bool intro, bool low) override {
        if (current && current->state.name == "char_moning")
            return int(SwitchPriority::Must);
        return Character::switchPriority(current, intro, low);
    }
};
class Jinhsi final : public Character {
    bool incarnation_ = false, incarnationCd_ = false, freeIntro_ = false;
    void handleIncarnation() {
        incarnation_ = false;
        const double start = host_.now();
        double animation = 0;
        bool lastResonance = true;
        host_.inLiberation = true;
        try {
            while (true) {
                if (host_.now() - start > 6)
                    break;
                if (host_.inTeam()) {
                    if (lastResonance) {
                        host_.click("left", .1);
                        lastResonance = false;
                    } else {
                        send(Skill::Resonance);
                        lastResonance = true;
                    }
                    if (animation != 0)
                        break;
                } else {
                    if (animation == 0)
                        animation = host_.now();
                    host_.inLiberation = true;
                }
                host_.checkCombat();
                host_.nextFrame();
            }
        } catch (...) {
            host_.inLiberation = false;
            throw;
        }
        host_.inLiberation = false;
        if (!echo())
            host_.click();
        host_.freezes.add(host_.now(), animation);
    }
    void handleIntro() {
        const double start = host_.now();
        while (true) {
            const double since = host_.now() - start;
            if (since > 15)
                throw CombatInterrupted("今汐共鸣阶段等待超时");
            if (host_.hasCd(Skill::Resonance)) {
                if (since > .3 && since < 1.5) {
                    incarnationCd_ = true;
                    if (!echo())
                        host_.click();
                    return;
                }
                if (since > 1.5)
                    break;
            } else
                send(Skill::Resonance, 0, .1);
            host_.nextFrame();
            host_.checkCombat();
        }
        if (liberation({.sendClick = true}))
            normalAttacks(.3);
        else
            normalAttacks(1.4);
        incarnation_ = true;
        incarnationCd_ = false;
    }
    void doPerform() override {
        if (incarnation_)
            handleIncarnation();
        else if (state.hasIntro || incarnationCd_)
            handleIntro();
        else
            echo();
        switchNext();
    }

  public:
    using Character::Character;
    void resetState() override {
        Character::resetState();
        incarnation_ = incarnationCd_ = freeIntro_ = false;
    }
    int switchPriority(const Character *, bool intro, bool) override {
        return int(intro || incarnation_ || incarnationCd_ ? SwitchPriority::Must : SwitchPriority::No);
    }
    void switchNext(bool = false, bool = false, const SwitchAction & = {}) override {
        Character::switchNext(freeIntro_, true);
        freeIntro_ = false;
    }
};
class Lucy final : public Character {
    void standard() {
        if (forteFull())
            return;
        const double start = host_.now();
        while (!forteFull()) {
            if (host_.now() - start > 8.5)
                break;
            if (resonanceAvailable())
                resonance();
            heavyAttack(.4);
            if (resonanceAvailable())
                resonance();
        }
        normalAttacks(.1);
    }
    void libSequence() {
        if (resonanceAvailable())
            resonance();
        host_.breakF();
        if (host_.forte(Forte::Mouse)) {
            heavyAttack(.4);
            sleep(.2, false);
            heavyAttack(.4);
        }
        if (!host_.forte(Forte::Mouse)) {
            const double start = host_.now();
            while (!host_.forte(Forte::Mouse) && host_.now() - start < 6) {
                host_.click();
                sleep(.1);
                host_.nextFrame();
            }
        }
        if (host_.forte(Forte::Mouse)) {
            HeldCombatInput held(host_);
            const double start = host_.now();
            while (host_.now() - start < 2.5) {
                sleep(.05);
                host_.nextFrame();
            }
            held.release();
            sleep(.1);
            const double wait = host_.now();
            bool available = liberationAvailable();
            while (host_.now() - wait < 2 && !available) {
                sleep(.1);
                available = liberationAvailable();
                host_.nextFrame();
            }
            if (echoAvailable())
                echo({.timeout = 0});
        } else if (echoAvailable())
            echo({.timeout = 0});
        if (liberationAvailable()) {
            liberation({.sendClick = true, .waitIfReady = 1.5});
            record(Skill::Liberation);
            for (int i = 0; i < 11; ++i) {
                host_.click();
                sleep(.1, false);
            }
        }
    }
    void doPerform() override {
        if (host_.checkBreak())
            host_.breakF();
        if (!forteFull())
            standard();
        libSequence();
        switchNext();
    }

  public:
    using Character::Character;
    int switchPriority(const Character *current, bool intro, bool low) override {
        if (intro && current && current->state.name == "char_rebecca")
            return int(SwitchPriority::Must);
        return Character::switchPriority(current, intro, low);
    }
};
class Brant final : public Character {
    double anchor_ = 0;
    bool stillLib() const { return elapsed(state.lastLiberation) < 12; }
    void waitLanding(double timeout = 1.2, bool click = true) {
        host_.wait([&] { return !flying(); }, timeout,
                   click ? std::function<void()>([&] { host_.click("left", .1); }) : std::function<void()>{});
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
    void forteResonance() {
        const double start = host_.now();
        while (resonanceAvailable() && forteFull()) {
            send(Skill::Resonance);
            if (host_.now() - start > 1)
                break;
            host_.checkCombat();
            host_.nextFrame();
        }
    }
    void jumping(double duration) {
        bool click = false;
        if (host_.hasLevitator && !flying()) {
            flick();
            sleep(.2);
            click = true;
        }
        const double start = host_.now();
        while (host_.now() - start <= duration) {
            if (click)
                host_.click();
            else
                host_.tap(host_.jumpKey(), .01);
            click = !click;
            host_.nextFrame();
        }
    }
    void underOutro() {
        double start = host_.now(), timeout = stillLib() ? 10 : 1.5;
        bool liberated = stillLib();
        const double deadline = start + 40;
        while (true) {
            if (host_.now() >= deadline)
                throw CombatInterrupted("布兰特延奏阶段超时");
            if (forteFull() && resonanceAvailable()) {
                forteResonance();
                state.lastLiberation = -1;
                waitLanding(4);
                if (liberated)
                    break;
            }
            if (liberationAvailable() && liberation()) {
                start = host_.now();
                timeout = 10;
                liberated = true;
            }
            if (host_.now() - start > timeout)
                break;
            if (host_.hasLevitator && !flying()) {
                flick();
                sleep(.2);
            } else if (!host_.hasLevitator)
                host_.tap(host_.jumpKey(), .01);
            host_.click();
            host_.checkCombat();
            host_.nextFrame();
        }
        echo();
    }
    void doPerform() override {
        if (state.hasIntro) {
            normalAttacks(1.3);
            if (state.hasSubDpsIntro && outroSource() == "char_lupa") {
                underOutro();
                switchNext();
                return;
            }
        }
        host_.breakF();
        if (forteFull() && resonanceAvailable()) {
            forteResonance();
            state.lastLiberation = -1;
            anchor_ = host_.now();
            if (echoAvailable())
                echo();
        }
        if (!needFastPerform() && !forteFull() && liberation())
            normalAttacks(.8);
        jumping(1.3);
        if (forteFull() && resonanceAvailable()) {
            forteResonance();
            anchor_ = host_.now();
            switchNext();
            return;
        }
        if (!stillLib() && echoAvailable())
            echo();
        switchNext();
    }

  public:
    Brant(CombatHost &host, CharacterState value) : Character(host, std::move(value)) {
        checkFOnSwitch = false;
    }
    void waitDown(bool click = true) override { waitLanding(1.2, click); }
    int switchPriority(const Character *current, bool intro, bool low) override {
        if (elapsed(anchor_, true) < 4)
            return int(SwitchPriority::No);
        if (stillLib() || (intro && current && current->state.name == "char_lupa"))
            return int(SwitchPriority::Must);
        return Character::switchPriority(current, intro, low);
    }
};
class Lucilla final : public Character {
    void pulse(double duration) {
        const double end = host_.now() + duration;
        bool seen = false;
        while (host_.now() < end) {
            HeldCombatInput held(host_);
            sleep(std::min(.6, end - host_.now()), false);
            held.release();
            const double con = host_.concerto(*this);
            if (con > .1)
                seen = true;
            else if (seen && con < .05)
                break;
            sleep(.02, false);
        }
    }
    bool tryLib() {
        if (!liberationAvailable())
            return false;
        if (echoAvailable())
            echo({.timeout = 0});
        if (host_.useLiberation) {
            const double start = host_.now();
            while (liberationAvailable() && host_.now() - start < 1.5) {
                send(Skill::Liberation);
                sleep(.1, false);
            }
            record(Skill::Liberation);
            sleep(3, false);
            pulse(15);
        }
        switchNext();
        return true;
    }
    void doPerform() override {
        const double start = host_.now();
        host_.wait([&] { return host_.inTeam(); }, .8);
        sleep(.5, false);
        host_.nextFrame();
        if (tryLib())
            return;
        while (host_.now() - start < 7.2) {
            if (available(Skill::Liberation, true, false) && !liberationAvailable())
                break;
            if (!liberationAvailable() && host_.cooldown(Skill::Liberation) > 1.5)
                break;
            if (tryLib())
                return;
            if (resonanceAvailable()) {
                HeldCombatInput held(host_, host_.hotkey(Skill::Resonance), false);
                sleep(1.4, false);
                held.release();
                record(Skill::Resonance);
            } else
                heavyAttack(1.4);
            host_.nextFrame();
        }
        switchNext();
    }

  public:
    using Character::Character;
    int switchPriority(const Character *current, bool intro, bool low) override {
        if (intro && current && QStringList{"char_verina", "char_shorekeeper"}.contains(current->state.name))
            return int(SwitchPriority::Must);
        return Character::switchPriority(current, intro, low);
    }
};
} // namespace
std::span<const CharacterRegistration> advancedCharacters() {
    static const CharacterRegistration entries[] = {
        {"Augusta", createCharacter<Augusta>}, {"Linnai", createCharacter<Linnai>},
        {"Jinhsi", createCharacter<Jinhsi>},   {"Lucy", createCharacter<Lucy>},
        {"Brant", createCharacter<Brant>},     {"Lucilla", createCharacter<Lucilla>}};
    return entries;
}
} // namespace okww::runtime
