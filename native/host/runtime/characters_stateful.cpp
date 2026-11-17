#include "character_registry.hpp"
#include "combat_scopes.hpp"

namespace okww::runtime {
namespace {
class Denia final : public Character {
    std::array<double, 2> local_{-1, 0};
    void doPerform() override {
        rustRun(rust::RotationMethod::StatefulPerform, {0.}, {}, {}, {}, nullptr, local_);
    }

  public:
    using Character::Character;
    void synchronizeLiberationPhase(int phase) override {
        rustRun(rust::RotationMethod::DeniaPhase, {double(phase)}, {}, {}, {}, nullptr, local_);
    }
    void resetState() override { rustRun(rust::RotationMethod::DeniaReset, {}, {}, {}, {}, nullptr, local_); }
    int switchPriority(const Character *current, bool intro, bool) override {
        return int(rustRun(rust::RotationMethod::StatefulPriority,
                           {0., double(intro), double(current != nullptr),
                            double(current && (current->state.className == "Aemeath" ||
                                               current->state.className == "Qingxiao"))},
                           {}, {}, {}, nullptr, local_)[0]);
    }
};
class Douling final : public Character {
    std::array<double, 1> local_{1};
    void doPerform() override {
        rustRun(rust::RotationMethod::StatefulPerform, {1.}, {}, {}, {}, nullptr, local_);
    }

  public:
    using Character::Character;
    int switchPriority(const Character *, bool, bool) override {
        return int(rustRun(rust::RotationMethod::StatefulPriority, {1., 0., 0., 0.})[0]);
    }
};
class Chisa final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::StatefulPerform, {2.}); }

  public:
    Chisa(CombatHost &host, CharacterState value) : Character(host, std::move(value)) {
        rustRun(rust::RotationMethod::StatefulInitialize, {2.});
    }
};
class Iuno final : public Character {
    std::array<double, 1> local_{0};
    void doPerform() override {
        rustRun(rust::RotationMethod::StatefulPerform, {3.}, {}, {}, {}, nullptr, local_);
    }

  public:
    Iuno(CombatHost &host, CharacterState value) : Character(host, std::move(value)) {
        rustRun(rust::RotationMethod::StatefulInitialize, {3.});
    }
    void combatEnd() override { switchOther(); }
};
class Hsin final : public Character {
    std::array<double, 1> local_{0};
    void doPerform() override {
        rustRun(rust::RotationMethod::StatefulPerform, {4.}, {}, {}, {}, nullptr, local_);
    }

  public:
    using Character::Character;
};
class Suisui final : public Character {
    std::array<double, 2> local_{-1, 0};
    void doPerform() override {
        rustRun(rust::RotationMethod::StatefulPerform, {5.}, {}, {}, {}, nullptr, local_);
    }

  public:
    using Character::Character;
    void resetState() override {
        rustRun(rust::RotationMethod::StatefulLifecycle, {5., 0., 0.}, {}, {}, {}, nullptr, local_);
    }
    void switchOut(bool full = false) override {
        rustRun(rust::RotationMethod::StatefulLifecycle, {5., 1., double(full)}, {}, {}, {}, nullptr, local_);
    }
    int switchPriority(const Character *current, bool intro, bool) override {
        return int(rustRun(rust::RotationMethod::StatefulPriority,
                           {5., double(intro), double(current != nullptr),
                            double(current && current->state.role == Role::MainDps)},
                           {}, {}, {}, nullptr, local_)[0]);
    }
};
class Rebecca final : public Character {
    std::array<double, 1> local_{-999};
    void doPerform() override {
        rustRun(rust::RotationMethod::StatefulPerform, {6.}, {}, {}, {}, nullptr, local_);
    }

  public:
    Rebecca(CombatHost &host, CharacterState value) : Character(host, std::move(value)) {
        rustRun(rust::RotationMethod::StatefulInitialize, {6.});
    }
};
class Qingxiao final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::StatefulPerform, {7.}); }

  public:
    using Character::Character;
};
class Aemeath final : public Character {
    std::array<double, 3> local_{0, 0, 0};
    void doPerform() override {
        rustRun(rust::RotationMethod::StatefulPerform, {8.}, {}, {}, {}, nullptr, local_);
    }

  public:
    using Character::Character;
    void combatEnd() override {
        rustRun(rust::RotationMethod::StatefulLifecycle, {8., 5., 0.}, {}, {}, {}, nullptr, local_);
    }
};
class Galbrena final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::StatefulPerform, {9.}); }

  public:
    using Character::Character;
};
class Cantarella final : public Character {
    std::array<double, 1> local_{-1};
    void doPerform() override {
        rustRun(rust::RotationMethod::StatefulPerform, {10.}, {}, {}, {}, nullptr, local_);
    }

  public:
    using Character::Character;
    int switchPriority(const Character *current, bool intro, bool) override {
        return int(rustRun(rust::RotationMethod::StatefulPriority,
                           {10., double(intro), double(current != nullptr),
                            double(current && (current->state.name == "char_roccia" ||
                                               current->state.name == "char_sanhua"))},
                           {}, {}, {}, nullptr, local_)[0]);
    }
    bool resonanceAvailable() override {
        return rustRun(rust::RotationMethod::StatefulLifecycle, {10., 2., 0.}, {}, {}, {}, nullptr,
                       local_)[0] != 0;
    }
    bool forteFull() override {
        return rustRun(rust::RotationMethod::StatefulLifecycle, {10., 3., 0.}, {}, {}, {}, nullptr,
                       local_)[0] != 0;
    }
    void combatEnd() override {
        rustRun(rust::RotationMethod::StatefulLifecycle, {10., 5., 0.}, {}, {}, {}, nullptr, local_);
    }
};
class Phrolova final : public Character {
    std::array<double, 2> local_{0, 0};
    void doPerform() override {
        rustRun(rust::RotationMethod::StatefulPerform, {11.}, {}, {}, {}, nullptr, local_);
    }

  public:
    using Character::Character;
    int switchPriority(const Character *current, bool intro, bool) override {
        return int(rustRun(rust::RotationMethod::StatefulPriority,
                           {11., double(intro), double(current != nullptr),
                            double(current && current->state.name == "char_cantarella")},
                           {}, {}, {}, nullptr, local_)[0]);
    }
    bool resonanceAvailable() override {
        return rustRun(rust::RotationMethod::StatefulLifecycle, {11., 2., 0.}, {}, {}, {}, nullptr,
                       local_)[0] != 0;
    }
    bool skipCombatCheck() override {
        return rustRun(rust::RotationMethod::StatefulLifecycle, {11., 4., 0.}, {}, {}, {}, nullptr,
                       local_)[0] != 0;
    }
};
class Mornye final : public Character {
    std::array<double, 1> local_{0};
    void doPerform() override {
        rustRun(rust::RotationMethod::StatefulPerform, {12.}, {}, {}, {}, nullptr, local_);
    }

  public:
    using Character::Character;
    int switchPriority(const Character *current, bool intro, bool) override {
        return int(rustRun(rust::RotationMethod::StatefulPriority,
                           {12., double(intro), double(current != nullptr),
                            double(int(current && (current->state.name == "char_aemeath" ||
                                                   current->state.name == "char_qingxiao")) |
                                   (int(current && current->state.name == "char_linnai") << 1))},
                           {}, {}, {}, nullptr, local_)[0]);
    }
    void combatEnd() override {
        rustRun(rust::RotationMethod::StatefulLifecycle, {12., 5., 0.}, {}, {}, {}, nullptr, local_);
    }
};
class Xigelika final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::StatefulPerform, {13.}); }

  public:
    using Character::Character;
    bool forteFull() override {
        return rustRun(rust::RotationMethod::StatefulLifecycle, {13., 3., 0.})[0] != 0;
    }
};
class Luhesi final : public Character {
    std::array<double, 1> local_{-1};
    void doPerform() override {
        rustRun(rust::RotationMethod::StatefulPerform, {14.}, {}, {}, {}, nullptr, local_);
    }

  public:
    using Character::Character;
    int switchPriority(const Character *current, bool intro, bool) override {
        return int(rustRun(rust::RotationMethod::StatefulPriority,
                           {14., double(intro), double(current != nullptr), 0.}, {}, {}, {}, nullptr,
                           local_)[0]);
    }
};
} // namespace
std::span<const CharacterRegistration> statefulCharacters() {
    static const CharacterRegistration entries[] = {{"Denia", createCharacter<Denia>},
                                                    {"Douling", createCharacter<Douling>},
                                                    {"Chisa", createCharacter<Chisa>},
                                                    {"Iuno", createCharacter<Iuno>},
                                                    {"Hsin", createCharacter<Hsin>},
                                                    {"Suisui", createCharacter<Suisui>},
                                                    {"Rebecca", createCharacter<Rebecca>},
                                                    {"Qingxiao", createCharacter<Qingxiao>},
                                                    {"Aemeath", createCharacter<Aemeath>},
                                                    {"Galbrena", createCharacter<Galbrena>},
                                                    {"Cantarella", createCharacter<Cantarella>},
                                                    {"Phrolova", createCharacter<Phrolova>},
                                                    {"Mornye", createCharacter<Mornye>},
                                                    {"Xigelika", createCharacter<Xigelika>},
                                                    {"Luhesi", createCharacter<Luhesi>}};
    return entries;
}
} // namespace okww::runtime
