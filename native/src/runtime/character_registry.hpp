#pragma once
#include "character.hpp"

namespace okww::runtime {
struct CharacterRegistration {
    const char *name;
    std::unique_ptr<Character> (*create)(CombatHost &, CharacterState);
};
template <typename T> std::unique_ptr<Character> createCharacter(CombatHost &host, CharacterState state) {
    return std::make_unique<T>(host, std::move(state));
}
std::span<const CharacterRegistration> basicCharacters();
std::span<const CharacterRegistration> extendedCharacters();
std::span<const CharacterRegistration> statefulCharacters();
std::span<const CharacterRegistration> advancedCharacters();
std::span<const CharacterRegistration> gaugeCharacters();
std::span<const CharacterRegistration> formCharacters();
std::span<const CharacterRegistration> spectroCharacters();
std::span<const CharacterRegistration> additionalCharacters();
} // namespace okww::runtime
