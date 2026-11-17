#include "character_registry.hpp"
namespace okww::runtime {
namespace {
class Camellya final : public Character {
    std::array<double, 4> local_{};
    double query(int event) {
        return rustRun(rust::RotationMethod::SpectroLifecycle, {0., double(event), 0., 0.}, {}, {}, {},
                       nullptr, local_)[0];
    }
    void doPerform() override {
        rustRun(rust::RotationMethod::SpectroPerform, {0.}, {}, {}, {}, nullptr, local_);
    }

  public:
    using Character::Character;
    void resetState() override { query(0); }
    bool echo(EchoOptions = {}) override { return query(1) != 0; }
    void combatEnd() override { query(2); }
    int switchPriority(const Character *current, bool intro, bool) override {
        return int(rustRun(rust::RotationMethod::SpectroPriority,
                           {0., double(intro), double(current != nullptr), 0.}, {}, {}, {}, nullptr,
                           local_)[0]);
    }
};
class Phoebe final : public Character {
    std::array<double, 8> local_{};
    Character *zani_ = nullptr;
    double query(int event) const {
        auto *self = const_cast<Phoebe *>(this);
        return self->rustRun(rust::RotationMethod::SpectroLifecycle, {1., double(event), 0., 0.}, {}, {}, {},
                             nullptr, self->local_, &self->zani_)[0];
    }
    void doPerform() override {
        rustRun(rust::RotationMethod::SpectroPerform, {1.}, {}, {}, {}, nullptr, local_, &zani_);
    }

  public:
    using Character::Character;
    void resetState() override { query(0); }
    bool forteFull() override { return query(1) != 0; }
    int outroCount() const override { return int(query(2)); }
    void switchNext(bool free = false, bool low = false, const SwitchAction &post = {}) override {
        rustRun(rust::RotationMethod::SpectroLifecycle, {1., 3., double(free), double(low)}, {}, {}, post,
                nullptr, local_, &zani_);
    }
    int switchPriority(const Character *current, bool intro, bool) override {
        return int(rustRun(rust::RotationMethod::SpectroPriority,
                           {1., double(intro), double(current != nullptr), 0.}, {}, {}, {}, nullptr, local_,
                           &zani_)[0]);
    }
};
class Zani final : public Character {
    std::array<double, 10> local_{0, -1, -1, -1, -1, -1, -1, -1, 0, 0};
    Character *phoebe_ = nullptr;
    double query(int event) const {
        auto *self = const_cast<Zani *>(this);
        return self->rustRun(rust::RotationMethod::SpectroLifecycle, {2., double(event), 0., 0.}, {}, {}, {},
                             nullptr, self->local_, &self->phoebe_)[0];
    }
    void doPerform() override {
        rustRun(rust::RotationMethod::SpectroPerform, {2.}, {}, {}, {}, nullptr, local_, &phoebe_);
    }

  public:
    Zani(CombatHost &host, CharacterState value) : Character(host, std::move(value)) { query(6); }
    void resetState() override { query(0); }
    double partnerGauge() const override { return query(1); }
    double partnerTimeLeft() const override { return query(2); }
    int partnerPhase() override { return int(query(3)); }
    bool partnerReady() override { return query(4) != 0; }
    bool waitSwitch() override { return query(5) != 0; }
    int switchPriority(const Character *current, bool intro, bool) override {
        return int(rustRun(rust::RotationMethod::SpectroPriority,
                           {2., double(intro), double(current != nullptr), 0.}, {}, {}, {}, nullptr, local_,
                           &phoebe_)[0]);
    }
};
} // namespace
std::span<const CharacterRegistration> spectroCharacters() {
    static const CharacterRegistration entries[] = {{"Camellya", createCharacter<Camellya>},
                                                    {"Phoebe", createCharacter<Phoebe>},
                                                    {"Zani", createCharacter<Zani>}};
    return entries;
}
} // namespace okww::runtime
