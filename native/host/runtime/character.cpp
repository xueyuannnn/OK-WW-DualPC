#include "character.hpp"
#include "character_registry.hpp"

namespace okww::runtime {
Character::Character(CombatHost &host, CharacterState value) : host_(host), state(std::move(value)) {
    checkFOnSwitch = rustRun(rust::RotationMethod::Initialize)[0] != 0;
}
void Character::perform() { rustRun(rust::RotationMethod::Perform); }
void Character::doPerform() { rustRun(rust::RotationMethod::DefaultPerform); }
void Character::sleep(double seconds, bool checkCombat) {
    rustRun(rust::RotationMethod::Sleep, {seconds, double(checkCombat)});
}
double Character::elapsed(double start, bool introMotionFreeze) const {
    return host_.freezes.elapsed(host_.now(), start, introMotionFreeze);
}
bool Character::available(Skill skill, bool checkColor, bool checkCd) {
    return rustRun(rust::RotationMethod::Available,
                   {double(skill), double(checkColor), double(checkCd)})[0] != 0;
}
double Character::currentCon() { return rustRun(rust::RotationMethod::CurrentCon)[0]; }
int Character::switchPriority(const Character *, bool, bool) {
    return int(rustRun(rust::RotationMethod::SwitchPriority)[0]);
}
bool Character::allBuffs() { return rustRun(rust::RotationMethod::AllBuffs)[0] != 0; }
bool Character::longAction(int variant) {
    return rustRun(rust::RotationMethod::LongAction, {double(variant)})[0] != 0;
}
QString Character::outroSource() const {
    Character *source = nullptr;
    const_cast<Character *>(this)->rustRun(rust::RotationMethod::OutroSource, {}, {}, {}, {}, &source);
    return source ? source->state.name : QString("null");
}
Character *Character::hasCharacter(const QString &name) const {
    Character *result = nullptr;
    const_cast<Character *>(this)->rustRun(rust::RotationMethod::FindCharacter, {}, name, {}, {}, &result);
    return result;
}
bool Character::shoreKeeperDodge() {
    return rustRun(rust::RotationMethod::ShoreKeeperDodge, {}, "ShoreKeeper")[0] != 0;
}
void Character::switchOther(bool allowAutoCombat) {
    rustRun(rust::RotationMethod::SwitchOther, {double(allowAutoCombat)});
}
bool Character::flying() { return rustRun(rust::RotationMethod::Flying)[0] != 0; }
bool Character::down() { return rustRun(rust::RotationMethod::Down)[0] != 0; }
void Character::waitDown(bool click) { rustRun(rust::RotationMethod::WaitDown, {double(click)}); }
void Character::waitIntro(double timeout, bool click) {
    rustRun(rust::RotationMethod::WaitIntro, {timeout, double(click)});
}
void Character::send(Skill skill, double after, double interval, double down) {
    rustRun(rust::RotationMethod::Send, {double(skill), after, interval, down});
}
void Character::record(Skill skill) { rustRun(rust::RotationMethod::Record, {double(skill)}); }
void Character::alertSkillFailed() { host_.log("技能释放超时，请核对游戏与软件中的键位设置"); }
ResonanceResult Character::resonance(ResonanceOptions o) {
    const auto result =
        rustRun(rust::RotationMethod::Resonance, {o.postSleep, double(o.animation), double(o.sendClick),
                                                  o.animationMinimum, o.timeout, double(o.clickF)});
    return {result[0] != 0, result[1], result[2] != 0};
}
bool Character::echo(EchoOptions o) {
    return rustRun(rust::RotationMethod::Echo, {o.duration, o.timeout})[0] != 0;
}
bool Character::liberation(LiberationOptions o) {
    return rustRun(rust::RotationMethod::Liberation, {o.conLessThan, double(o.sendClick), o.waitIfReady,
                                                      o.animationMinimum, double(o.clickF)})[0] != 0;
}
void Character::normalAttack() { rustRun(rust::RotationMethod::NormalAttack); }
void Character::normalAttackUntilCanSwitch() { rustRun(rust::RotationMethod::NormalUntilSwitch); }
void Character::cycleSleep(double duration) { rustRun(rust::RotationMethod::CycleSleep, {duration}); }
bool Character::needFastPerform() { return rustRun(rust::RotationMethod::NeedFastPerform)[0] != 0; }
void Character::heavyAttack(double duration) { rustRun(rust::RotationMethod::HeavyAttack, {duration}); }
bool Character::heavyForte(Forte kind) {
    return rustRun(rust::RotationMethod::HeavyForteKind, {double(kind)})[0] != 0;
}
bool Character::heavyForte(const std::function<bool()> &condition) {
    return rustRun(rust::RotationMethod::HeavyForte, {}, {}, condition)[0] != 0;
}
void Character::normalAttacks(double duration, double interval, double after, bool resonanceIfReady,
                              bool untilFull) {
    rustRun(rust::RotationMethod::NormalAttacks,
            {duration, interval, after, double(resonanceIfReady), double(untilFull)});
}
void Character::repeatedKey(QString key, double duration, double interval) {
    rustRun(rust::RotationMethod::RepeatedKey, {duration, interval}, key);
}
void Character::rightClicks(double duration, double interval, QString direction) {
    rustRun(rust::RotationMethod::RightClicks, {duration, interval, double(!direction.isEmpty())}, direction);
}
void Character::switchNext(bool freeIntro, bool targetLowCon, const SwitchAction &post) {
    rustRun(rust::RotationMethod::SwitchNext, {double(freeIntro), double(targetLowCon)}, {}, {}, post);
}
void Character::resetState() { rustRun(rust::RotationMethod::ResetState); }
void Character::combatEnd() {}
QStringList nativeCharacterClasses() {
    QStringList names;
    for (auto group : {basicCharacters(), extendedCharacters(), statefulCharacters(), advancedCharacters(),
                       gaugeCharacters(), formCharacters(), spectroCharacters(), additionalCharacters()})
        for (const auto &entry : group)
            names.append(QString::fromLatin1(entry.name));
    return names;
}
std::unique_ptr<Character> makeCharacter(const QString &name, CombatHost &host, CharacterState state) {
    state.className = name;
    for (auto group : {basicCharacters(), extendedCharacters(), statefulCharacters(), advancedCharacters(),
                       gaugeCharacters(), formCharacters(), spectroCharacters(), additionalCharacters()})
        for (const auto &entry : group)
            if (name == QLatin1StringView(entry.name))
                return entry.create(host, std::move(state));
    fail("内置角色暂不支持：" + name);
}
} // namespace okww::runtime
