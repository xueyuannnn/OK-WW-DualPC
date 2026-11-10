#include "character_registry.hpp"
#include "combat_scopes.hpp"

// Selective ports from zzc-tongji/ok-ww-enhanced at 726b10e65cbeb1303214774664d0277a71b7e18a.
// Source and genuine recognition asset hashes are recorded in character-updates.json.
namespace okww::runtime {
namespace {
class Aalto final : public Character {
    void doPerform() override {
        if (state.hasIntro)
            normalAttacks(.4);
        waitDown();
        const auto start = host_.now();
        bool skillUsed = resonance({.sendClick = true, .timeout = 1}).clicked;
        echo({.timeout = 0});
        bool liberated = liberation({.sendClick = true, .waitIfReady = 0});
        while (elapsed(start) < 4.5) {
            if (isConFull())
                break;
            if (!skillUsed && resonanceAvailable()) {
                skillUsed = resonance({.sendClick = true, .timeout = 1}).clicked;
                continue;
            }
            if (!liberated && liberationAvailable()) {
                liberated = liberation({.sendClick = true, .waitIfReady = 0});
                continue;
            }
            host_.click("left", .1);
            sleep(.05);
        }
        switchNext();
    }

  public:
    using Character::Character;
};

class Lingyang final : public Character {
    void doPerform() override {
        if (state.hasIntro)
            normalAttacks(.8);
        waitDown();
        echo({.timeout = 0});
        if (liberationAvailable() && liberation({.sendClick = true}))
            normalAttacks(1);
        const auto start = host_.now();
        while (elapsed(start) < 8) {
            if (isConFull() && elapsed(start) > 2)
                break;
            if (host_.forte(Forte::Mouse)) {
                if (heavyForte(Forte::Mouse))
                    normalAttacks(.4);
                continue;
            }
            if (resonanceAvailable() && resonance({.sendClick = true, .timeout = 1}).clicked) {
                normalAttacks(.35);
                continue;
            }
            if (liberationAvailable()) {
                liberation({.sendClick = true, .waitIfReady = 0});
                continue;
            }
            host_.click("left", .1);
            sleep(.05);
        }
        switchNext();
    }

  public:
    using Character::Character;
};

class Lumi final : public Character {
    void doPerform() override {
        if (state.hasIntro)
            normalAttacks(.5);
        waitDown();
        const auto start = host_.now();
        echo({.timeout = 0});
        liberation({.sendClick = true, .waitIfReady = 0});
        while (elapsed(start) < 5) {
            if (isConFull() && elapsed(start) > 1.5)
                break;
            if (resonanceAvailable() && resonance({.sendClick = true, .timeout = 1}).clicked) {
                normalAttacks(.4);
                continue;
            }
            if (host_.forte(Forte::Mouse)) {
                heavyForte(Forte::Mouse);
                continue;
            }
            if (liberationAvailable()) {
                liberation({.sendClick = true, .waitIfReady = 0});
                continue;
            }
            host_.click("left", .1);
            sleep(.05);
        }
        switchNext();
    }

  public:
    using Character::Character;
};

class Yangyang final : public Character {
    void doPerform() override {
        if (state.hasIntro)
            normalAttacks(.6);
        waitDown();
        resonance({.sendClick = true, .timeout = 1});
        if (host_.forte(Forte::Mouse))
            heavyForte(Forte::Mouse);
        echo({.timeout = 0});
        if (liberation({.sendClick = true, .waitIfReady = 0}))
            normalAttacks(.5);
        else
            normalAttacks(.8, .1, 0, true);
        switchNext();
    }

  public:
    using Character::Character;
};

class Suoming final : public Character {
    bool dpsConfigured() { return host_.characterSetting("Suoming DPS", false).toBool(); }
    void updateRole() {
        const bool dps = dpsConfigured();
        state.role = dps ? Role::MainDps : Role::SubDps;
        state.buffTime = dps ? 14 : 8;
    }
    bool forteAvailable() { return host_.feature("suoming_forte_e"); }
    void holdForteResonance() {
        const auto key = host_.hotkey(Skill::Resonance);
        const auto start = host_.now();
        std::unique_ptr<HeldCombatInput> held;
        host_.checkCombat();
        while (elapsed(start) < 6 && forteAvailable()) {
            if (held) {
                held->release();
                held.reset();
                sleep(.1);
            }
            held = std::make_unique<HeldCombatInput>(host_, key, false);
            sleep(2);
        }
        sleep(3);
        host_.wait([&] { return resonanceAvailable(); }, 8, {}, 0, [&] { host_.checkCombat(); });
        if (held)
            held->release();
        echo();
        sleep(.01);
    }
    void performForte() {
        host_.wait([&] { return forteAvailable(); }, 15,
                   [&] { host_.click("left", .1); }, 0, [&] { host_.checkCombat(); });
        holdForteResonance();
    }
    void doPerform() override {
        updateRole();
        if (state.hasIntro)
            normalAttacks(1.6);
        const bool liberated = liberation({.waitIfReady = 1});
        const bool dps = dpsConfigured();
        if (liberated && !dps) {
            echo({.timeout = 0});
            switchNext(true);
            return;
        }
        if (liberated || state.hasIntro) {
            if (!liberated)
                echo({.timeout = 0});
            if (dps)
                resonance({.sendClick = false, .timeout = 1});
            performForte();
        } else
            normalAttacks(1.1);
        switchNext();
    }

  public:
    Suoming(CombatHost &host, CharacterState value) : Character(host, std::move(value)) { updateRole(); }
    void resetState() override {
        Character::resetState();
        updateRole();
    }
    int switchPriority(const Character *current, bool intro, bool lowCon = false) override {
        updateRole();
        return Character::switchPriority(current, intro, lowCon);
    }
};
} // namespace

std::span<const CharacterRegistration> additionalCharacters() {
    static const CharacterRegistration entries[] = {
        {"Aalto", createCharacter<Aalto>}, {"Lingyang", createCharacter<Lingyang>},
        {"Lumi", createCharacter<Lumi>}, {"Yangyang", createCharacter<Yangyang>},
        {"Suoming", createCharacter<Suoming>}};
    return entries;
}
} // namespace okww::runtime
