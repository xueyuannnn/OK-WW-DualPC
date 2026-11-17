#include "character_registry.hpp"
namespace okww::runtime {
namespace {
class Changli final : public Character {
    std::array<double, 1> local_{0};
    void doPerform() override {
        rustRun(rust::RotationMethod::FormsPerform, {0.}, {}, {}, {}, nullptr, local_);
    }

  public:
    using Character::Character;
    void resetState() override {
        rustRun(rust::RotationMethod::FormsLifecycle, {0., 0., 0., 0.}, {}, {}, {}, nullptr, local_);
    }
    int switchPriority(const Character *current, bool intro, bool) override {
        return int(rustRun(rust::RotationMethod::FormsPriority,
                           {0., double(intro), double(current != nullptr),
                            double(current && current->state.name == "char_brant")},
                           {}, {}, {}, nullptr, local_)[0]);
    }
};
class Rover final : public Character {
    std::array<double, 3> local_{-1, 0, 0};
    void doPerform() override {
        rustRun(rust::RotationMethod::FormsPerform, {1.}, {}, {}, {}, nullptr, local_);
    }

  public:
    Rover(CombatHost &host, CharacterState value) : Character(host, std::move(value)) {
        rustRun(rust::RotationMethod::FormsLifecycle, {1., 1., 0., 0.}, {}, {}, {}, nullptr, local_);
    }
    void resetState() override {
        rustRun(rust::RotationMethod::FormsLifecycle, {1., 0., 0., 0.}, {}, {}, {}, nullptr, local_);
    }
};
class Cartethyia final : public Character {
    std::array<double, 8> local_{1, 0, 0, 0, 0, 0, -1, -1};
    std::array<double, 3> query(int event, double value = 0) const {
        auto *self = const_cast<Cartethyia *>(this);
        return self->rustRun(rust::RotationMethod::FormsLifecycle, {2., double(event), value, 0.}, {}, {}, {},
                             nullptr, self->local_);
    }
    void doPerform() override {
        rustRun(rust::RotationMethod::FormsPerform, {2.}, {}, {}, {}, nullptr, local_);
    }

  public:
    using Character::Character;
    bool smallForm() const override { return query(1)[0] != 0; }
    void synchronizeForm(bool value) override { query(2, double(value)); }
    double introDuration() const override { return query(3)[0]; }
    int switchPriority(const Character *current, bool intro, bool) override {
        return int(rustRun(rust::RotationMethod::FormsPriority,
                           {2., double(intro), double(current != nullptr), 0.}, {}, {}, {}, nullptr,
                           local_)[0]);
    }
    void combatEnd() override { query(4); }
};
class Carlotta final : public Character {
    std::array<double, 5> local_{-1, -1, 0, 0, 0};
    Character *zhezhi_ = nullptr;
    double query(int event) {
        return rustRun(rust::RotationMethod::FormsLifecycle, {3., double(event), 0., 0.}, {}, {}, {}, nullptr,
                       local_, &zhezhi_)[0];
    }
    void doPerform() override {
        rustRun(rust::RotationMethod::FormsPerform, {3.}, "w", {}, {}, nullptr, local_, &zhezhi_);
    }

  public:
    using Character::Character;
    void resetState() override { query(0); }
    bool partnerReady() override { return query(1) != 0; }
    bool echoAvailable() override { return query(2) != 0; }
    bool waitSwitch() override { return query(3) != 0; }
    int switchPriority(const Character *current, bool intro, bool) override {
        return int(rustRun(rust::RotationMethod::FormsPriority,
                           {3., double(intro), double(current != nullptr),
                            double(current && current->state.name == "char_zhezhi")},
                           {}, {}, {}, nullptr, local_, &zhezhi_)[0]);
    }
    bool liberation(LiberationOptions o = {}) override {
        return rustRun(
                   rust::RotationMethod::FormsLiberation,
                   {o.conLessThan, double(o.sendClick), o.waitIfReady, o.animationMinimum, double(o.clickF)},
                   {}, {}, {}, nullptr, local_, &zhezhi_)[0] != 0;
    }
    ResonanceResult resonance(ResonanceOptions o = {}) override {
        const auto result = rustRun(rust::RotationMethod::FormsResonance,
                                    {o.postSleep, double(o.animation), double(o.sendClick),
                                     o.animationMinimum, o.timeout, double(o.clickF)},
                                    {}, {}, {}, nullptr, local_, &zhezhi_);
        return {result[0] != 0, result[1], result[2] != 0};
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
