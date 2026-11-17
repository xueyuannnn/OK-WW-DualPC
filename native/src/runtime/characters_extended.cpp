#include "character_registry.hpp"
namespace okww::runtime {
namespace {
// Native object/virtual-call adapters; state transitions are implemented in Rust.
class ShoreKeeper final : public Character {
    std::array<double, 3> local_{-1, 0, 0};
    void doPerform() override {
        rustRun(rust::RotationMethod::ExtendedPerform, {0.}, {}, {}, {}, nullptr, local_);
    }

  public:
    using Character::Character;
    int switchPriority(const Character *current, bool intro, bool) override {
        return int(rustRun(
            rust::RotationMethod::ExtendedPriority,
            {0., double(intro), double(current != nullptr),
             double(current && (current->state.name == "Augusta" || current->state.name == "char_augusta"))},
            {}, {}, {}, nullptr, local_)[0]);
    }
    bool skipCombatCheck() override { return rustRun(rust::RotationMethod::KeeperSkipCheck)[0] != 0; }
    bool skipSwitchCombatCheck(bool intro) override { return intro; }
    void switchNext(bool intro = false, bool low = false, const SwitchAction &post = {}) override {
        rustRun(rust::RotationMethod::ExtendedSwitchNext, {0., double(intro), double(low)}, {}, {}, post,
                nullptr, local_);
    }
    bool autoDodge(const std::function<bool()> &condition) override {
        return rustRun(rust::RotationMethod::KeeperDodge, {}, {}, condition, {}, nullptr, local_)[0] != 0;
    }
};
class Encore final : public Character {
    std::array<double, 3> local_{0, 0, 0};
    void doPerform() override {
        rustRun(rust::RotationMethod::ExtendedPerform, {1.}, {}, {}, {}, nullptr, local_);
    }

  public:
    using Character::Character;
    void switchOut(bool full = false) override {
        rustRun(rust::RotationMethod::EncoreSwitchOut, {double(full)}, {}, {}, {}, nullptr, local_);
    }
    int switchPriority(const Character *current, bool intro, bool) override {
        return int(rustRun(rust::RotationMethod::ExtendedPriority,
                           {1., double(intro), double(current != nullptr), 0.}, {}, {}, {}, nullptr,
                           local_)[0]);
    }
};
class Roccia final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::ExtendedPerform, {2.}, "w"); }

  public:
    using Character::Character;
    void switchNext(bool intro = false, bool low = false, const SwitchAction & = {}) override {
        rustRun(rust::RotationMethod::ExtendedSwitchNext, {2., double(intro), double(low)});
    }
};
class Jiyan final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::ExtendedPerform, {3.}); }

  public:
    using Character::Character;
};
class Xiangliyao final : public Character {
    std::array<double, 1> local_{0};
    void doPerform() override {
        rustRun(rust::RotationMethod::ExtendedPerform, {4.}, {}, {}, {}, nullptr, local_);
    }

  public:
    using Character::Character;
};
class Qiuyuan final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::ExtendedPerform, {5.}); }

  public:
    using Character::Character;
};
class JingRan final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::ExtendedPerform, {6.}); }

  public:
    using Character::Character;
};
class YangYangSp final : public Character {
    void doPerform() override { rustRun(rust::RotationMethod::ExtendedPerform, {7.}); }

  public:
    using Character::Character;
};
} // namespace
bool isShoreKeeper(const Character *character) {
    return dynamic_cast<const ShoreKeeper *>(character) != nullptr;
}
std::span<const CharacterRegistration> extendedCharacters() {
    static const CharacterRegistration entries[] = {
        {"ShoreKeeper", createCharacter<ShoreKeeper>}, {"Encore", createCharacter<Encore>},
        {"Roccia", createCharacter<Roccia>},           {"Jiyan", createCharacter<Jiyan>},
        {"Xiangliyao", createCharacter<Xiangliyao>},   {"Qiuyuan", createCharacter<Qiuyuan>},
        {"JingRan", createCharacter<JingRan>},         {"YangYangSp", createCharacter<YangYangSp>}};
    return entries;
}
} // namespace okww::runtime
