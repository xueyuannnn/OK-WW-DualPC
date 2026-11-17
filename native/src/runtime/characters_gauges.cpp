#include "character_registry.hpp"
namespace okww::runtime {
namespace {
class Zhezhi final : public Character {
    std::array<double, 2> local_{0, 0};
    Character *partner_ = nullptr;
    void doPerform() override {
        rustRun(rust::RotationMethod::GaugePerform, {0.}, {}, {}, {}, nullptr, local_, &partner_);
    }

  public:
    using Character::Character;
    void resetState() override {
        rustRun(rust::RotationMethod::GaugeLifecycle, {0., 0., 0., 0.}, {}, {}, {}, nullptr, local_,
                &partner_);
    }
    void setPartner(Character *partner) override { partner_ = partner; }
    void clearPartnerGauge() override {
        rustRun(rust::RotationMethod::GaugeLifecycle, {0., 1., 0., 0.}, {}, {}, {}, nullptr, local_,
                &partner_);
    }
    bool resonanceAvailable() override {
        return rustRun(rust::RotationMethod::GaugeLifecycle, {0., 2., 0., 0.}, {}, {}, {}, nullptr, local_,
                       &partner_)[0] != 0;
    }
    int switchPriority(const Character *current, bool intro, bool) override {
        return int(rustRun(rust::RotationMethod::GaugePriority,
                           {0., double(intro), double(current != nullptr), 0.}, {}, {}, {}, nullptr, local_,
                           &partner_)[0]);
    }
};
class Lupa final : public Character {
    std::array<double, 1> local_{0};
    void doPerform() override {
        rustRun(rust::RotationMethod::GaugePerform, {1.}, {}, {}, {}, nullptr, local_);
    }

  public:
    Lupa(CombatHost &host, CharacterState value) : Character(host, std::move(value)) {
        rustRun(rust::RotationMethod::GaugeLifecycle, {1., 4., 0., 0.});
    }
    void resetState() override {
        rustRun(rust::RotationMethod::GaugeLifecycle, {1., 0., 0., 0.}, {}, {}, {}, nullptr, local_);
    }
    int switchPriority(const Character *current, bool intro, bool) override {
        return int(rustRun(rust::RotationMethod::GaugePriority,
                           {1., double(intro), double(current != nullptr),
                            double(current && current->state.name == "chang_changli")},
                           {}, {}, {}, nullptr, local_)[0]);
    }
};
class Ciaccona final : public Character {
    std::array<double, 4> local_{0, 0, 0, -1};
    Character *cartethyia_ = nullptr;
    void send(Skill skill, double after = 0, double interval = -1, double down = .01) override {
        rustRun(rust::RotationMethod::GaugeSend, {double(skill), after, interval, down}, {}, {}, {}, nullptr,
                local_, &cartethyia_);
    }
    void doPerform() override {
        rustRun(rust::RotationMethod::GaugePerform, {2.}, "a", {}, {}, nullptr, local_, &cartethyia_);
    }

  public:
    Ciaccona(CombatHost &host, CharacterState value) : Character(host, std::move(value)) {
        rustRun(rust::RotationMethod::GaugeLifecycle, {2., 4., 0., 0.});
    }
    void resetState() override {
        rustRun(rust::RotationMethod::GaugeLifecycle, {2., 0., 0., 0.}, {}, {}, {}, nullptr, local_,
                &cartethyia_);
    }
    bool skipCombatCheck() override {
        return rustRun(rust::RotationMethod::GaugeLifecycle, {2., 2., 0., 0.}, {}, {}, {}, nullptr, local_,
                       &cartethyia_)[0] != 0;
    }
    bool needFastPerform() override {
        return rustRun(rust::RotationMethod::GaugeLifecycle, {2., 3., 0., 0.})[0] != 0;
    }
    bool inOutro() const override {
        auto *self = const_cast<Ciaccona *>(this);
        return self->rustRun(rust::RotationMethod::GaugeLifecycle, {2., 7., 0., 0.}, {}, {}, {}, nullptr,
                             self->local_, &self->cartethyia_)[0] != 0;
    }
    void switchNext(bool intro = false, bool low = false, const SwitchAction &post = {}) override {
        rustRun(rust::RotationMethod::GaugeLifecycle, {2., 6., double(intro), double(low)}, {}, {}, post,
                nullptr, local_, &cartethyia_);
    }
    void switchOut(bool full = false) override {
        rustRun(rust::RotationMethod::GaugeLifecycle, {2., 1., double(full), 0.}, {}, {}, {}, nullptr, local_,
                &cartethyia_);
    }
    void combatEnd() override {
        rustRun(rust::RotationMethod::GaugeLifecycle, {2., 5., 0., 0.}, {}, {}, {}, nullptr, local_,
                &cartethyia_);
    }
    int switchPriority(const Character *current, bool intro, bool) override {
        return int(rustRun(rust::RotationMethod::GaugePriority,
                           {2., double(intro), double(current != nullptr), 0.}, {}, {}, {}, nullptr, local_,
                           &cartethyia_)[0]);
    }
};
class Hiyuki final : public Character {
    std::array<double, 1> local_{0};
    void doPerform() override {
        rustRun(rust::RotationMethod::GaugePerform, {3.}, {}, {}, {}, nullptr, local_);
    }

  public:
    using Character::Character;
    void switchOut(bool full = false) override {
        rustRun(rust::RotationMethod::GaugeLifecycle, {3., 1., double(full), 0.}, {}, {}, {}, nullptr,
                local_);
    }
    int switchPriority(const Character *current, bool intro, bool) override {
        return int(rustRun(rust::RotationMethod::GaugePriority,
                           {3., double(intro), double(current != nullptr),
                            double(current && (current->state.name == "char_linnai" ||
                                               current->state.name == "char_lucilla"))},
                           {}, {}, {}, nullptr, local_)[0]);
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
