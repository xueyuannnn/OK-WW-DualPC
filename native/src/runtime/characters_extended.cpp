#include "character_registry.hpp"

namespace okww::runtime {
namespace {
class ShoreKeeper final : public Character {
    double outroTime_ = -1;
    int dodgeCount_ = 0, attribute_ = 0;
    void doPerform() override {
        if (state.hasIntro) {
            host_.skipCombatCheck = true;
            try {
                // The original uses time.sleep here. Keep the delay cancellable.
                sleep(.1, false);
                if (!host_.inTeam())
                    host_.wait([&] { return host_.inTeam(); }, 4);
                else
                    normalAttacks(1.2);
            } catch (...) {
                host_.skipCombatCheck = false;
                throw;
            }
            host_.skipCombatCheck = false;
        }
        echo({.timeout = 0});
        liberation();
        // Source tests the whole result tuple, which is always truthy, rather
        // than its first element. Preserve its actual behavior: no heavy attack.
        resonance();
        switchNext();
    }

  public:
    using Character::Character;
    int switchPriority(const Character *current, bool intro, bool low) override {
        if (attribute_ == 0) {
            attribute_ = 1;
            for (auto c : host_.characters())
                if (c && (c->state.name == "Augusta" || c->state.name == "char_augusta"))
                    attribute_ = 2;
        }
        if (attribute_ == 2 && intro && current &&
            (current->state.name == "Augusta" || current->state.name == "char_augusta"))
            return int(SwitchPriority::Must);
        return Character::switchPriority(current, intro, low);
    }
    bool skipCombatCheck() override { return state.hasIntro || flying(); }
    bool skipSwitchCombatCheck(bool intro) override { return intro; }
    void switchNext(bool freeIntro = false, bool low = false, const SwitchAction &post = {}) override {
        if (isConFull()) {
            outroTime_ = host_.now();
            dodgeCount_ = 5;
        }
        Character::switchNext(freeIntro, low, post);
    }
    bool autoDodge(const std::function<bool()> &condition) override {
        bool clicked = false;
        if (elapsed(outroTime_) < 30 && dodgeCount_ > 0) {
            const double start = host_.now();
            while (host_.now() - start < 1.5) {
                if (!condition())
                    break;
                rightClicks(.05);
                sleep(.05);
                clicked = true;
                host_.nextFrame();
            }
        }
        if (clicked)
            --dodgeCount_;
        return clicked;
    }
};
class Encore final : public Character {
    double lastHeavy_ = 0, liberationTime_ = 0, lastResonance_ = 0;
    bool stillInLiberation() const { return elapsed(liberationTime_) < 9.5; }
    bool resonanceStep2(double delay = 2) const { return elapsed(lastResonance_, true) < delay; }
    void n4() {
        const double duration = resonance().clicked ? 2.7 : 2.4;
        if (elapsed(liberationTime_) < 6)
            normalAttacks(duration);
        else if (host_.forte(Forte::Mouse)) {
            heavyAttack();
            lastHeavy_ = host_.now();
        } else
            resonance();
    }
    void doPerform() override {
        if (state.hasIntro) {
            const double sinceLib = elapsed(liberationTime_);
            if (sinceLib > 6 && sinceLib < 10 && host_.forte(Forte::Mouse)) {
                heavyAttack(1.2);
                sleep(.1);
                lastHeavy_ = host_.now();
                switchNext();
                return;
            }
            waitDown();
        }
        if (stillInLiberation()) {
            n4();
            switchNext();
            return;
        }
        if (resonance().clicked && !resonanceStep2(4)) {
            lastResonance_ = host_.now();
            switchNext();
            return;
        }
        if (!needFastPerform() && !host_.openWorldAutoCombat() && liberation({.waitIfReady = .4})) {
            liberationTime_ = host_.now();
            n4();
            switchNext();
            return;
        }
        if (echoAvailable())
            echo();
        switchNext();
    }

  public:
    using Character::Character;
    void switchOut(bool fullCon = false) override {
        Character::switchOut(fullCon);
        lastResonance_ = 0;
    }
    int switchPriority(const Character *current, bool intro, bool low) override {
        if (elapsed(lastHeavy_, true) < 4.6)
            return int(SwitchPriority::No);
        if (stillInLiberation() || resonanceStep2())
            return int(SwitchPriority::Must);
        return Character::switchPriority(current, intro, low);
    }
};
class Roccia final : public Character {
    void plunge() {
        if (needFastPerform()) {
            normalAttackUntilCanSwitch();
            return;
        }
        double start = host_.now();
        const double deadline = start + 20;
        host_.keyDown("w");
        try {
            while (host_.forte(Forte::Mouse) && host_.now() - start < 6) {
                // The source can reset start after every liberation. A separate
                // deadline prevents bad observations from holding W forever.
                if (host_.now() >= deadline)
                    throw CombatInterrupted("洛可可下落攻击等待超时");
                if (host_.now() - start > 2 && !host_.hasCd(Skill::Resonance) &&
                    !host_.hasCd(Skill::Liberation) && liberation()) {
                    resonance();
                    start = host_.now();
                    continue;
                }
                host_.click("left", .1);
            }
        } catch (...) {
            try {
                host_.keyUp("w");
            } catch (...) {
            }
            throw;
        }
        host_.keyUp("w");
    }
    void doPerform() override {
        if (state.hasIntro) {
            heavyAttack(1.6);
            sleep(.1);
            plunge();
            if (!liberationAvailable() && !resonanceAvailable()) {
                switchNext();
                return;
            }
        }
        const bool liberated = liberation();
        if (resonance().clicked || !liberated) {
            plunge();
            switchNext();
            return;
        }
        echo();
        switchNext();
    }

  public:
    using Character::Character;
    void switchNext(bool freeIntro = false, bool low = false, const SwitchAction & = {}) override {
        Character::switchNext(freeIntro, low, [](Character &next, bool intro) {
            if (intro)
                next.state.hasToolBox = true;
        });
    }
};
class Jiyan final : public Character {
    void doPerform() override {
        if (state.hasIntro)
            normalAttacks(2);
        if (liberation()) {
            const double start = host_.now();
            while (host_.now() - start < 12) {
                if (resonance().clicked)
                    host_.click("middle");
                normalAttack();
            }
            switchNext();
            return;
        }
        int i = 0;
        const double start = host_.now();
        while (!host_.forte(Forte::Bar) && !isConFull()) {
            if (host_.now() - start >= 15)
                throw CombatInterrupted("忌炎等待回路或协奏超时");
            if (i % 4 == 0) {
                heavyAttack();
                if (resonanceAvailable() || echoAvailable()) {
                    host_.click("middle");
                    break;
                }
                i = 0;
            }
            normalAttack();
            ++i;
        }
        if (!host_.forte(Forte::Bar) && resonanceAvailable())
            resonance({.postSleep = 1});
        if (echoAvailable())
            echo();
        switchNext();
    }

  public:
    using Character::Character;
};
class Xiangliyao final : public Character {
    double liberationTime_ = 0;
    void doPerform() override {
        waitDown();
        if (liberation())
            liberationTime_ = host_.now();
        if (elapsed(liberationTime_) < 25) {
            const double deadline = host_.now() + 30;
            while (!resonance({.sendClick = true}).clicked) {
                if (host_.now() >= deadline)
                    throw CombatInterrupted("相里要解放期间等待共鸣技能超时");
                normalAttacks(1);
            }
        } else if (echoAvailable())
            echo();
        else
            resonance({.sendClick = true});
        switchNext();
    }

  public:
    using Character::Character;
};
class Qiuyuan final : public Character {
    void doPerform() override {
        if (state.hasIntro)
            normalAttacks(1.17);
        if (flying())
            waitDown();
        double start = host_.now();
        const double duration = state.hasSubDpsIntro ? 4 : 1.2;
        const double deadline = start + 20;
        while (host_.now() - start < duration) {
            if (host_.now() >= deadline)
                throw CombatInterrupted("秋远连招等待超时");
            echo({.timeout = 0});
            if (host_.now() - start < .5 && liberation())
                start = host_.now();
            if (heavyForte(Forte::Mouse)) {
                switchNext();
                return;
            }
            if (flying() && !host_.forte(Forte::Mouse))
                for (auto c : host_.characters())
                    if (auto keeper = dynamic_cast<ShoreKeeper *>(c)) {
                        keeper->autoDodge([&] { return flying(); });
                        break;
                    }
            host_.click();
            host_.checkCombat();
            host_.nextFrame();
        }
        resonance();
        switchNext();
    }

  public:
    using Character::Character;
};
class JingRan final : public Character {
    void doPerform() override {
        waitIntro();
        echo({.timeout = 0});
        const double start = host_.now();
        std::optional<double> liberationStart;
        while (elapsed(start) < 23) {
            if ((!liberationStart && elapsed(start) >= 8) ||
                (liberationStart && elapsed(*liberationStart) >= 15))
                break;
            cycleStart();
            if (host_.forte(Forte::Bar))
                heavyForte();
            else if (!liberationStart && liberation({.waitIfReady = 0}))
                liberationStart = host_.now();
            else if (resonance({.sendClick = false, .timeout = 1}).clicked)
                normalAttacks(.8);
            else
                host_.click("left", .1);
            cycleSleep();
        }
        switchNext();
    }

  public:
    using Character::Character;
};
class YangYangSp final : public Character {
    void doPerform() override {
        double duration = state.hasIntro ? 8 : 3.2;
        const double start = host_.now();
        double resonanceSince = 0;
        bool echoUsed = false;
        host_.mouseDown();
        try {
            while (elapsed(start) < duration) {
                if (host_.now() - start > 60)
                    throw CombatInterrupted("玄翎秧秧长按连招超时");
                if (liberationAvailable()) {
                    if (liberation({.sendClick = false, .waitIfReady = 0}))
                        duration += 2;
                } else if (resonanceAvailable()) {
                    if (resonanceSince == 0)
                        resonanceSince = host_.now();
                    if (resonanceSince != 0 && host_.now() - resonanceSince > .2)
                        resonance({.sendClick = false, .timeout = 1});
                } else if (!echoUsed) {
                    resonanceSince = 0;
                    echoUsed = echo({.timeout = 0});
                } else
                    resonanceSince = 0;
                sleep(.05);
            }
        } catch (...) {
            try {
                host_.mouseUp();
            } catch (...) {
            }
            throw;
        }
        host_.mouseUp();
        sleep(.1, false);
        switchNext();
    }

  public:
    using Character::Character;
};
} // namespace
std::span<const CharacterRegistration> extendedCharacters() {
    static const CharacterRegistration entries[] = {
        {"ShoreKeeper", createCharacter<ShoreKeeper>}, {"Encore", createCharacter<Encore>},
        {"Roccia", createCharacter<Roccia>},           {"Jiyan", createCharacter<Jiyan>},
        {"Xiangliyao", createCharacter<Xiangliyao>},   {"Qiuyuan", createCharacter<Qiuyuan>},
        {"JingRan", createCharacter<JingRan>},         {"YangYangSp", createCharacter<YangYangSp>}};
    return entries;
}
} // namespace okww::runtime
