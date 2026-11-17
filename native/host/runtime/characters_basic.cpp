#include "character_registry.hpp"
namespace okww::runtime {
namespace {
// Qt/native object adapters only; sequences are in rust/src/rotation/basic.rs.
class Verina final : public Character {
    double lastHeavy_ = -1;
    void doPerform() override {
        rustRun(rust::RotationMethod::BasicPerform, {0.}, {}, {}, {}, nullptr, {&lastHeavy_, 1});
    }

  public:
    using Character::Character;
    int switchPriority(const Character *current, bool intro, bool) override {
        return int(rustRun(rust::RotationMethod::VerinaPriority,
                           {double(intro), double(current != nullptr),
                            double(current && current->state.name == "char_hiyuki")})[0]);
    }
};
class Mortefi final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::BasicPerform, {1.}); }

  public:
    using Character::Character;
};
class Sanhua final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::BasicPerform, {2.}); }

  public:
    using Character::Character;
};
class Baizhi final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::BasicPerform, {3.}); }

  public:
    using Character::Character;
};
class Yuanwu final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::BasicPerform, {4.}); }

  public:
    using Character::Character;
};
class Taoqi final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::BasicPerform, {5.}); }

  public:
    using Character::Character;
};
class Calcharo final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::BasicPerform, {6.}); }

  public:
    using Character::Character;
};
class Jianxin final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::BasicPerform, {7.}); }

  public:
    using Character::Character;
};
class Yinlin final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::BasicPerform, {8.}); }

  public:
    using Character::Character;
};
class Danjin final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::BasicPerform, {9.}); }

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
