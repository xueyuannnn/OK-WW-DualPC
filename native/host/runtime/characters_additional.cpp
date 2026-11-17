#include "character_registry.hpp"
// Sequences ported from zzc-tongji/ok-ww-enhanced at 726b10e65cbeb1303214774664d0277a71b7e18a.
// Recognition assets and upstream attribution remain in character-updates.json.
namespace okww::runtime {
namespace {
class Aalto final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::AdditionalPerform, {0.}); }

  public:
    using Character::Character;
};
class Lingyang final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::AdditionalPerform, {1.}); }

  public:
    using Character::Character;
};
class Lumi final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::AdditionalPerform, {2.}); }

  public:
    using Character::Character;
};
class Yangyang final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::AdditionalPerform, {3.}); }

  public:
    using Character::Character;
};
class Suoming final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::AdditionalPerform, {4.}); }

  public:
    Suoming(CombatHost &host, CharacterState value) : Character(host, std::move(value)) {
        rustRun(rust::RotationMethod::SuomingUpdateRole);
    }
    void resetState() override {
        Character::resetState();
        rustRun(rust::RotationMethod::SuomingUpdateRole);
    }
    int switchPriority(const Character *current, bool intro, bool lowCon = false) override {
        rustRun(rust::RotationMethod::SuomingUpdateRole);
        return Character::switchPriority(current, intro, lowCon);
    }
};
} // namespace
std::span<const CharacterRegistration> additionalCharacters() {
    static const CharacterRegistration entries[] = {{"Aalto", createCharacter<Aalto>},
                                                    {"Lingyang", createCharacter<Lingyang>},
                                                    {"Lumi", createCharacter<Lumi>},
                                                    {"Yangyang", createCharacter<Yangyang>},
                                                    {"Suoming", createCharacter<Suoming>}};
    return entries;
}
} // namespace okww::runtime
