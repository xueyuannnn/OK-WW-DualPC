#include "character_registry.hpp"
namespace okww::runtime {
namespace {
class Verina final : public Character {
    double lastHeavy_ = -1;
    void doPerform() override {
        const double start = host_.now();
        normalAttacks(.6);
        auto stop = [&] { return isConFull() || elapsed(start) >= 6.5; };
        if (!stop()) {
            if (liberationAvailable())
                liberation();
            if (!stop()) {
                if (resonanceAvailable())
                    resonance({.sendClick = true, .timeout = 0});
                if (!stop()) {
                    if (echoAvailable())
                        echo({.timeout = 0});
                    if (!stop()) {
                        host_.wait([&] { return host_.inTeam(); }, 2);
                        sleep(.8);
                        if (host_.forte(Forte::Mouse) && elapsed(lastHeavy_) >= 8) {
                            heavyAttack(.7);
                            lastHeavy_ = host_.now();
                        }
                        host_.tap(host_.jumpKey(), .01);
                        normalAttacks(.5);
                    }
                }
            }
        }
        switchNext();
    }

  public:
    using Character::Character;
    int switchPriority(const Character *current, bool intro, bool low) override {
        if (intro && current && current->state.name == "char_hiyuki")
            return int(SwitchPriority::Must);
        return Character::switchPriority(current, intro, low);
    }
};
class Mortefi final : public Character {
    void doPerform() override {
        waitDown();
        const bool liberated = liberation();
        resonance();
        echo();
        if (!liberated)
            liberation({.waitIfReady = 1});
        switchNext();
    }

  public:
    using Character::Character;
};
class Sanhua final : public Character {
    void doPerform() override {
        bool liberated = false;
        double sleepTime = .85;
        sleep(.02);
        double start = host_.now();
        host_.mouseDown();
        try {
            waitDown(false);
            if (liberation({.sendClick = false})) {
                liberated = true;
                sleepTime += .1;
                sleep(.15, false);
            } else if (resonanceAvailable()) {
                host_.mouseUp();
                resonance({.sendClick = false});
                start = host_.now();
                host_.mouseDown();
                sleep(.1, false);
            }
            sleepTime -= elapsed(start);
            if (sleepTime > 0)
                sleep(sleepTime, false);
            host_.mouseUp();
        } catch (...) {
            try {
                host_.mouseUp();
            } catch (...) {
            }
            throw;
        }
        sleep(.8);
        if (liberated) {
            resonance({.sendClick = false});
            sleep(.3);
        }
        if (isConFull())
            echo();
        switchNext();
    }

  public:
    using Character::Character;
};
class Baizhi final : public Character {
    void doPerform() override {
        if (state.hasIntro)
            normalAttacks(1.2, .1, 0, true);
        liberation({.conLessThan = 1});
        resonance();
        echo();
        switchNext();
    }

  public:
    using Character::Character;
};
class Yuanwu final : public Character {
    void doPerform() override {
        if (liberation({.conLessThan = 1})) {
            resonance();
            switchNext();
            return;
        }
        if (state.hasIntro) {
            normalAttacks(1.2);
            switchNext();
            return;
        }
        resonance();
        echo();
        switchNext();
    }

  public:
    using Character::Character;
};
class Taoqi final : public Character {
    void doPerform() override {
        if (state.hasIntro) {
            waitDown();
            normalAttacks(2.5);
        } else {
            liberation();
            resonance();
            echo({.sleepTime = .1});
        }
        switchNext();
    }

  public:
    using Character::Character;
};
class Calcharo final : public Character {
    void doPerform() override {
        if (state.hasIntro) {
            sleep(1);
            host_.wait([&] { return host_.inTeam(); }, 3);
            host_.checkCombat();
        }
        Character::doPerform();
    }

  public:
    using Character::Character;
};
class Jianxin final : public Character {
    void doPerform() override {
        if (state.hasIntro)
            normalAttacks(1);
        liberation();
        if (resonanceAvailable())
            resonance();
        if (echoAvailable())
            echo();
        switchNext();
    }

  public:
    using Character::Character;
};
class Yinlin final : public Character {
    void doPerform() override {
        if (state.hasIntro)
            sleep(.4);
        const bool liberated = liberation();
        if (host_.forte(Forte::Mouse)) {
            if (!state.hasIntro && !liberated)
                normalAttack();
            heavyAttack();
            sleep(.4);
        } else if (resonance({.sendClick = false}).clicked)
            sleep(.1);
        else if (echoAvailable())
            echo();
        else
            heavyAttack();
        switchNext();
    }

  public:
    using Character::Character;
};
class Danjin final : public Character {
    void doPerform() override {
        if (liberation()) {
            sleep(1.2);
            echo({.timeout = 2});
            switchNext();
            return;
        }
        if (host_.forte(Forte::Bar) && state.hasIntro) {
            heavyAttack(.8);
            sleep(.2);
            normalAttack();
            sleep(.1);
            switchNext();
            return;
        }
        if (state.hasIntro)
            normalAttacks(1.1);
        else {
            waitDown();
            normalAttacks(.4, .1);
        }
        repeatedKey(host_.hotkey(Skill::Resonance), 1.1, .2);
        switchNext();
    }

  public:
    using Character::Character;
};
} // namespace
std::span<const CharacterRegistration> basicCharacters() {
    static const CharacterRegistration entries[] = {
        {"BaseChar", createCharacter<Character>}, {"Verina", createCharacter<Verina>},
        {"Mortefi", createCharacter<Mortefi>},    {"Sanhua", createCharacter<Sanhua>},
        {"Baizhi", createCharacter<Baizhi>},      {"Yuanwu", createCharacter<Yuanwu>},
        {"Taoqi", createCharacter<Taoqi>},        {"Calcharo", createCharacter<Calcharo>},
        {"Jianxin", createCharacter<Jianxin>},    {"Yinlin", createCharacter<Yinlin>},
        {"Danjin", createCharacter<Danjin>},      {"Chixia", createCharacter<Character>},
        {"Youhu", createCharacter<Character>}};
    return entries;
}
} // namespace okww::runtime
