#include "character_registry.hpp"
#include "combat_scopes.hpp"
namespace okww::runtime {
namespace {
class Zhezhi final : public Character {
    bool blue_ = false;
    Character *partner_ = nullptr;
    int forte_ = 0;
    bool blue() { return host_.whiteRatio({5120, 2880}, {2242, 2664, 58, 26}, {244, 244, 244}) > .3; }
    int gauge() {
        return forte_ = host_.segmentedGauge({5120, 2880}, {2164, 2675, 736, 10},
                                             {{235, 240, 185}, {255, 255, 215}}, 3, 12, 14, 100);
    }
    bool conLock() {
        return partner_->partnerReady() || currentCon() < .6 || (isConFull() && !partner_->partnerReady());
    }
    void untilNotBlue() {
        const double start = host_.now();
        while (!blue()) {
            if (host_.now() - start > .3)
                break;
            host_.checkCombat();
            host_.nextFrame();
        }
        double jump = host_.now() + .4;
        HeldCombatInput held(host_);
        while (resonanceAvailable() && blue()) {
            send(Skill::Resonance);
            if (needFastPerform() && host_.now() - start > 1.1)
                break;
            if (isConFull() && (!partner_ || conLock()))
                break;
            if (host_.now() - start > 4)
                break;
            if (host_.now() > jump) {
                host_.tap(host_.jumpKey(), .01);
                jump += .4;
            }
            host_.checkCombat();
            host_.nextFrame();
        }
        held.release();
    }
    void interlock() {
        if (state.hasIntro)
            normalAttacks(1.3);
        if (flying()) {
            host_.wait([&] { return !flying(); }, 1.2, [&] { host_.click("left", .1); });
            rightClicks(.05);
        }
        if (!blue() && gauge() < 3 && !(isConFull() && partner_->partnerReady())) {
            normalAttacks(1.4);
            if (!partner_->partnerReady()) {
                switchNext();
                return;
            }
        }
        if (!blue() && resonanceAvailable() && gauge() > 1 && !(isConFull() && partner_->partnerReady())) {
            if (conLock() && liberation())
                sleep(.2);
            resonance();
            normalAttacks(.8);
        }
        if (conLock() && blue() && resonanceAvailable()) {
            untilNotBlue();
            if (conLock() && liberation({.waitIfReady = .5}))
                sleep(.2);
            else if (echoAvailable())
                rightClicks(.05);
        }
        echo({.timeout = 2});
        switchNext();
    }
    void doPerform() override {
        if (partner_) {
            interlock();
            return;
        }
        if (state.hasIntro)
            normalAttacks(1.5);
        liberation();
        if ((blue_ || blue()) && resonanceAvailable()) {
            blue_ = false;
            untilNotBlue();
            switchNext();
            return;
        }
        if (resonanceAvailable() && !forteFull()) {
        } else if (resonanceAvailable() && forteFull()) {
            resonance();
            normalAttacks(.8);
            blue_ = true;
            switchNext();
            return;
        }
        if (!echo())
            normalAttacks(.1);
        switchNext();
    }

  public:
    using Character::Character;
    void resetState() override {
        Character::resetState();
        blue_ = false;
        partner_ = nullptr;
        forte_ = 0;
    }
    void setPartner(Character *partner) override { partner_ = partner; }
    void clearPartnerGauge() override { forte_ = 0; }
    bool resonanceAvailable() override {
        return (state.current && blue()) || Character::resonanceAvailable();
    }
    int switchPriority(const Character *current, bool intro, bool low) override {
        if (partner_ && partner_->partnerReady())
            return int(SwitchPriority::Must);
        return Character::switchPriority(current, intro, low);
    }
};
class Lupa final : public Character {
    bool wolf_ = false;
    int gauge() {
        return !forteFull() ? 0
                            : host_.segmentedGauge({3840, 2160}, {1633, 2004, 527, 12},
                                                   {{75, 75, 235}, {105, 105, 255}}, 2, 19, 21, 400);
    }
    bool stillLib() const { return elapsed(state.lastLiberation) < 12; }
    bool wolfRes(double timeout = 1) {
        const double start = host_.now();
        bool clicked = false;
        while (true) {
            if (!(wolf_ && host_.now() - start < .2) && !host_.feature("lupa_wolf_icon2", .85))
                break;
            send(Skill::Resonance);
            clicked = true;
            if (host_.now() - start > timeout)
                break;
            host_.checkCombat();
            host_.nextFrame();
        }
        if (clicked) {
            state.lastLiberation = -1;
            wolf_ = false;
            sleep(1.2);
        }
        return clicked;
    }
    void jumping(double duration) {
        const double start = host_.now();
        bool click = false;
        while (host_.now() - start <= duration) {
            if (click)
                host_.click();
            else
                host_.tap(host_.jumpKey(), .01);
            click = !click;
            if (gauge() == 2)
                return;
            host_.checkCombat();
            host_.nextFrame();
        }
    }
    void consume(bool outro) {
        if (flying())
            host_.wait([&] { return !forteFull(); }, 2, [&] { host_.click("left", .1); });
        else {
            heavyAttack();
            host_.wait([&] { return !forteFull(); }, 1.4, [&] { host_.click("left", .1); });
        }
        if (!forteFull())
            wolf_ = true;
        if (outro) {
            host_.wait([&] { return host_.feature("lupa_wolf_icon2", .85); }, .5);
            wolfRes();
        }
    }
    void doPerform() override {
        bool outro = false;
        if (state.hasIntro) {
            normalAttacks(1);
            outro = outroSource() == "chang_changli";
        }
        echo({.timeout = 0});
        if (wolfRes() && !outro) {
            switchNext();
            return;
        }
        if (gauge() == 2 && !host_.feature("lupa_wolf_icon2", .85)) {
            consume(outro);
            if (!outro) {
                switchNext();
                return;
            }
        }
        if (host_.highlighted(Skill::Resonance) > .05 && resonance().clicked) {
            state.lastLiberation = -1;
            if (liberationAvailable())
                waitDown();
            else {
                switchNext();
                return;
            }
        }
        host_.breakF();
        if ((outro || !needFastPerform()) && liberation()) {
            normalAttacks(.3);
            if (outro)
                normalAttacks(1);
            else {
                switchNext();
                return;
            }
        }
        if (stillLib()) {
            jumping(4);
            consume(outro);
            switchNext();
            return;
        }
        normalAttacks(.1);
        switchNext();
    }

  public:
    Lupa(CombatHost &host, CharacterState value) : Character(host, std::move(value)) {
        checkFOnSwitch = false;
    }
    void resetState() override {
        Character::resetState();
        wolf_ = false;
    }
    int switchPriority(const Character *current, bool intro, bool low) override {
        if (stillLib() || (intro && current && current->state.name == "chang_changli"))
            return int(SwitchPriority::Must);
        return Character::switchPriority(current, intro, low);
    }
};
class Ciaccona final : public Character {
    int attribute_ = 0;
    bool liberated_ = false, recital_ = false;
    Character *cartethyia_ = nullptr;
    double outroTime_ = -1;
    int gauge() {
        return host_.forte(Forte::Mouse)
                   ? 3
                   : host_.segmentedGauge({3840, 2160}, {1612, 1987, 576, 21},
                                          {{180, 240, 70}, {210, 255, 100}}, 3, 12, 14, 100);
    }
    void decide() {
        for (auto c : host_.characters())
            if (c) {
                if (c->state.className == "Cartethyia") {
                    cartethyia_ = c;
                    attribute_ = 3;
                    return;
                }
                if (c->state.className == "Phoebe" || c->state.className == "Zani") {
                    attribute_ = 2;
                    return;
                }
            }
        attribute_ = 1;
    }
    void jumping(double duration) {
        const double start = host_.now();
        bool click = true;
        while (host_.now() - start <= duration) {
            if (click)
                host_.click();
            else
                host_.tap(host_.jumpKey(), .01);
            click = !click;
            host_.checkCombat();
            host_.nextFrame();
        }
    }
    void send(Skill skill, double after = 0, double interval = -1, double down = .01) override {
        if (skill == Skill::Liberation)
            recital_ = true;
        Character::send(skill, after, interval, down);
    }
    void doPerform() override {
        liberated_ = false;
        bool wait = false, jump = true;
        if (!attribute_)
            decide();
        if (state.hasIntro) {
            normalAttacks(.8);
            if (!needFastPerform())
                normalAttacks(.7);
        }
        if (host_.highlighted(Skill::Echo) < .22)
            echo({.timeout = 0});
        if (!state.hasIntro && !needFastPerform() && !host_.forte(Forte::Mouse)) {
            jumping(.4);
            host_.wait([&] { return !flying(); }, 1.2, [&] { host_.click("left", .1); });
            normalAttacks(.2);
        }
        if (resonance().clicked) {
            jump = false;
            wait = true;
        }
        if (gauge() >= 3) {
            if (jump) {
                const double start = host_.now();
                while (!flying()) {
                    host_.tap(host_.jumpKey(), .01);
                    if (host_.now() - start > .3)
                        break;
                    host_.nextFrame();
                }
            }
            heavyForte(Forte::Mouse);
            wait = true;
        }
        if (liberationAvailable()) {
            if (wait)
                sleep(.4);
            if (liberation()) {
                liberated_ = true;
                if (attribute_ == 2) {
                    const double start = host_.now();
                    while (host_.now() - start < .6)
                        host_.tap("a");
                }
            }
        }
        if (!liberated_ && host_.highlighted(Skill::Echo) > .25)
            echo();
        switchNext();
    }

  public:
    Ciaccona(CombatHost &host, CharacterState value) : Character(host, std::move(value)) {
        introMotion = .73;
    }
    void resetState() override {
        Character::resetState();
        attribute_ = 0;
        cartethyia_ = nullptr;
        recital_ = false;
    }
    bool skipCombatCheck() override { return elapsed(state.lastLiberation) < 2; }
    bool needFastPerform() override {
        // Original checks a class attribute, while Cartethyia defines only an
        // instance attribute. Keep its actual false result during migration.
        return false;
    }
    bool inOutro() const override { return elapsed(outroTime_) < 30; }
    void switchNext(bool freeIntro = false, bool low = false, const SwitchAction &post = {}) override {
        if (isConFull())
            outroTime_ = host_.now();
        Character::switchNext(freeIntro, low, post);
    }
    void switchOut(bool full = false) override {
        recital_ = false;
        Character::switchOut(full);
    }
    void combatEnd() override {
        if (recital_) {
            recital_ = false;
            switchOther();
        }
    }
    int switchPriority(const Character *current, bool intro, bool low) override {
        if (attribute_ == 2 && liberated_ && elapsed(state.lastLiberation) < 20)
            return int(SwitchPriority::No);
        if (attribute_ == 3 && liberated_ && (elapsed(state.lastLiberation) < 8 || !cartethyia_->smallForm()))
            return int(SwitchPriority::No);
        return Character::switchPriority(current, intro, low);
    }
};
class Hiyuki final : public Character {
    int count_ = 0;
    double fieldTimeout() const { return state.hasIntro && outroSource() == "char_linnai" ? 18 : 16; }
    bool heavyReady() { return host_.feature("hiyuki_lib_forte", .7); }
    bool locked() {
        return host_.wait([&] { return longAction(2); }, 2, [&] { host_.click(); });
    }
    bool holdLib() {
        if (!host_.useLiberation)
            return false;
        const double start = host_.now();
        double last = 0;
        try {
            while (host_.inTeam() && (liberationAvailable() || host_.cooldown(Skill::Liberation) <= 1.5) &&
                   host_.now() - start < 8) {
                if (host_.now() - start > last) {
                    host_.keyDown(host_.hotkey(Skill::Liberation));
                    last += .2;
                }
                sleep(.05);
            }
            host_.inLiberation = true;
            host_.keyUp(host_.hotkey(Skill::Liberation));
            host_.wait([&] { return host_.inTeam(); }, 3);
            host_.inLiberation = false;
        } catch (...) {
            host_.inLiberation = false;
            try {
                host_.keyUp(host_.hotkey(Skill::Liberation));
            } catch (...) {
            }
            throw;
        }
        host_.freezes.add(host_.now(), start, host_.now() - start);
        return true;
    }
    void standard() {
        const double timeout = fieldTimeout();
        while (longAction() && elapsed(state.lastPerform) < timeout) {
            echo({.timeout = 0});
            resonance({.sendClick = false, .timeout = 0});
            if (liberationAvailable() && liberation())
                return;
            if (host_.forte(Forte::Mouse)) {
                host_.click("right");
                heavyForte(Forte::Mouse);
                host_.wait([&] { return liberationAvailable(); }, 16, [&] { host_.click("left", .1); });
                if (liberation())
                    return;
            } else
                host_.click("left", .1);
            sleep(.05);
        }
    }
    bool performLib() {
        const double start = host_.now(), timeout = fieldTimeout();
        while (longAction(2) && elapsed(start) < timeout) {
            host_.breakF();
            echo({.timeout = 0});
            heavyReady();
            const bool timed = elapsed(start) >= timeout - .5;
            if (liberationAvailable() && (count_ >= 4 || timed) && holdLib()) {
                count_ = 0;
                return true;
            }
            if (resonance({.sendClick = false, .timeout = 0}).clicked) {
                if (timed)
                    break;
                normalAttacks(.3);
            } else if (heavyReady()) {
                if (timed)
                    break;
                if (locked())
                    heavyForte([&] { return heavyReady(); });
                if (host_.wait([&] { return liberationAvailable(); }, .5, [&] { host_.click(); }) &&
                    locked() && holdLib()) {
                    count_ = 0;
                    return true;
                }
                ++count_;
                sleep(.1);
            } else if (host_.feature("hiyuki_left", .5)) {
                host_.wait([&] { return !host_.feature("hiyuki_left", .5); }, 3, [&] { host_.click(); });
                if (timed)
                    break;
                sleep(.1);
            } else if (host_.feature("hiyuki_right", .5)) {
                host_.click("right", 1);
                sleep(.1);
            } else
                host_.click();
            sleep(.05);
        }
        if (liberationAvailable() && holdLib()) {
            count_ = 0;
            return true;
        }
        return false;
    }
    void doPerform() override {
        if (state.hasIntro)
            normalAttacks(1);
        if (longAction() && host_.cooldown(Skill::Liberation) <= 3.5)
            standard();
        if (longAction(2) && (performLib() || (liberationAvailable() && holdLib())))
            sleep(.5);
        switchNext();
    }

  public:
    using Character::Character;
    void switchOut(bool full = false) override {
        Character::switchOut(full);
        count_ = 0;
    }
    int switchPriority(const Character *current, bool intro, bool low) override {
        if (intro && current && QStringList{"char_linnai", "char_lucilla"}.contains(current->state.name))
            return int(SwitchPriority::Must);
        return Character::switchPriority(current, intro, low);
    }
};
} // namespace
std::span<const CharacterRegistration> gaugeCharacters() {
    static const CharacterRegistration entries[] = {{"Zhezhi", createCharacter<Zhezhi>},
                                                    {"Lupa", createCharacter<Lupa>},
                                                    {"Ciaccona", createCharacter<Ciaccona>},
                                                    {"Hiyuki", createCharacter<Hiyuki>}};
    return entries;
}
} // namespace okww::runtime
