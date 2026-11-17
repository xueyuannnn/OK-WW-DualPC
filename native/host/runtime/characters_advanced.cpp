#include "character_registry.hpp"
namespace okww::runtime {
namespace {
class Augusta final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::AdvancedPerform, {0.}); }

  public:
    using Character::Character;
    bool liberationAvailable() override {
        return rustRun(rust::RotationMethod::AdvancedLifecycle, {0., 2., 0.})[0] != 0;
    }
    bool resonanceAvailable() override {
        return rustRun(rust::RotationMethod::AdvancedLifecycle, {0., 3., 0.})[0] != 0;
    }
    void combatEnd() override { rustRun(rust::RotationMethod::AdvancedLifecycle, {0., 5., 0.}); }
};
class Linnai final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::AdvancedPerform, {1.}); }

  public:
    using Character::Character;
    int switchPriority(const Character *current, bool intro, bool) override {
        return int(rustRun(rust::RotationMethod::AdvancedPriority,
                           {1., double(intro), double(current != nullptr),
                            double(current && current->state.name == "char_moning")})[0]);
    }
    void combatEnd() override { rustRun(rust::RotationMethod::AdvancedLifecycle, {1., 5., 0.}); }
};
class Jinhsi final : public Character {
    std::array<double, 3> local_{0, 0, 0};
    void doPerform() override {
        rustRun(rust::RotationMethod::AdvancedPerform, {2.}, {}, {}, {}, nullptr, local_);
    }

  public:
    using Character::Character;
    int switchPriority(const Character *current, bool intro, bool) override {
        return int(rustRun(rust::RotationMethod::AdvancedPriority,
                           {2., double(intro), double(current != nullptr), 0.}, {}, {}, {}, nullptr,
                           local_)[0]);
    }
    void resetState() override {
        rustRun(rust::RotationMethod::AdvancedLifecycle, {2., 0., 0.}, {}, {}, {}, nullptr, local_);
    }
    void switchNext(bool = false, bool = false, const SwitchAction & = {}) override {
        rustRun(rust::RotationMethod::AdvancedLifecycle, {2., 1., 0.}, {}, {}, {}, nullptr, local_);
    }
};
class Lucy final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::AdvancedPerform, {3.}); }

  public:
    using Character::Character;
    int switchPriority(const Character *current, bool intro, bool) override {
        return int(rustRun(rust::RotationMethod::AdvancedPriority,
                           {3., double(intro), double(current != nullptr),
                            double(current && current->state.name == "char_rebecca")})[0]);
    }
};
class Brant final : public Character {
    std::array<double, 1> local_{0};
    void doPerform() override {
        rustRun(rust::RotationMethod::AdvancedPerform, {4.}, {}, {}, {}, nullptr, local_);
    }

  public:
    Brant(CombatHost &host, CharacterState value) : Character(host, std::move(value)) {
        rustRun(rust::RotationMethod::AdvancedLifecycle, {4., 0., 0.});
    }
    int switchPriority(const Character *current, bool intro, bool) override {
        return int(rustRun(rust::RotationMethod::AdvancedPriority,
                           {4., double(intro), double(current != nullptr),
                            double(current && current->state.name == "char_lupa")},
                           {}, {}, {}, nullptr, local_)[0]);
    }
    void waitDown(bool click = true) override {
        rustRun(rust::RotationMethod::AdvancedLifecycle, {4., 6., double(click)}, {}, {}, {}, nullptr,
                local_);
    }
};
class Lucilla final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::AdvancedPerform, {5.}); }

  public:
    using Character::Character;
    int switchPriority(const Character *current, bool intro, bool) override {
        return int(rustRun(rust::RotationMethod::AdvancedPriority,
                           {5., double(intro), double(current != nullptr),
                            double(current && (current->state.name == "char_verina" ||
                                               current->state.name == "char_shorekeeper"))})[0]);
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
