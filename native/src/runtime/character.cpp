#include "character.hpp"
#include "character_registry.hpp"

namespace okww::runtime {
namespace {
class FlagScope {
    bool &flag_;
    bool previous_;

  public:
    FlagScope(bool &flag, bool value) : flag_(flag), previous_(flag) { flag_ = value; }
    ~FlagScope() { flag_ = previous_; }
};
class HeldMouse {
    CombatHost &host_;
    bool held_ = true;

  public:
    explicit HeldMouse(CombatHost &host) : host_(host) { host_.mouseDown(); }
    void release() {
        if (held_) {
            held_ = false;
            host_.mouseUp();
        }
    }
    ~HeldMouse() {
        if (held_)
            try {
                host_.mouseUp();
            } catch (...) {
            }
    }
};
} // namespace
bool CombatHost::wait(const std::function<bool()> &condition, double timeout,
                      const std::function<void()> &post, double settle, const std::function<void()> &pre) {
    if (timeout < 0 || !std::isfinite(timeout) || !std::isfinite(settle) || settle < 0)
        fail("战斗等待时间无效");
    const double deadline = now() + (timeout == 0 ? 10 : timeout);
    double settled = -1;
    do {
        if (pre)
            pre();
        nextFrame();
        if (condition()) {
            if (settle == 0)
                return true;
            if (settled >= 0 && now() - settled > settle)
                return true;
            if (settled < 0)
                settled = now();
            continue;
        }
        settled = -1;
        if (post)
            post();
        if (now() > deadline)
            return false;
    } while (true);
}
Character::Character(CombatHost &host, CharacterState value) : host_(host), state(std::move(value)) {
    checkFOnSwitch = state.role != Role::Healer;
}
void Character::perform() {
    state.lastPerform = host_.now();
    doPerform();
}
void Character::doPerform() {
    waitIntro(1.2);
    echo({.timeout = 0});
    liberation();
    if (!resonance().clicked)
        heavyForte(Forte::Mouse);
    switchNext();
}
void Character::sleep(double seconds, bool checkCombat) {
    FlagScope skip(host_.skipCombatCheck, host_.skipCombatCheck || !checkCombat);
    host_.sleep(std::max(0., seconds));
}
double Character::elapsed(double start, bool introMotionFreeze) const {
    return host_.freezes.elapsed(host_.now(), start, introMotionFreeze);
}
bool Character::available(Skill skill, bool checkColor, bool checkCd) {
    return state.current ? host_.available(skill, -1, checkColor, checkCd) : !host_.hasCd(skill, state.index);
}
double Character::currentCon() {
    if (state.currentCon == 1)
        return 1;
    return state.currentCon = host_.concerto(*this);
}
int Character::switchPriority(const Character *, bool, bool) {
    return int(state.healerLocked(host_.freezes, host_.now()) ? SwitchPriority::No : SwitchPriority::Normal);
}
bool Character::allBuffs() {
    if (!state.hasIntro)
        return false;
    int count = 0;
    for (auto c : host_.characters())
        if (c && c != this) {
            ++count;
            if (c->state.buffTime <= 0 || !c->hasBuff())
                return false;
        }
    return count == 2;
}
bool Character::longAction(int variant) {
    const QString box = variant == 1   ? "box_target_enemy_long"
                        : variant == 2 ? "target_box_long2"
                                       : "target_box_short";
    return host_.feature(host_.targetName(), .6, box);
}
QString Character::outroSource() const {
    if (!state.hasIntro)
        return "null";
    double latest = 0;
    QString name = "null";
    for (auto c : host_.characters())
        if (c && c != this && c->state.lastSwitch > latest) {
            latest = c->state.lastSwitch;
            name = c->state.name;
        }
    return name;
}
Character *Character::hasCharacter(const QString &name) const {
    for (auto c : host_.characters())
        if (c && c->state.className == name)
            return c;
    return nullptr;
}
bool Character::shoreKeeperDodge() {
    auto c = hasCharacter("ShoreKeeper");
    return c && c->autoDodge([&] { return flying(); });
}
void Character::switchOther(bool allowAutoCombat) {
    const auto party = host_.characters();
    if (party.isEmpty() || (host_.isAutoCombatTask() && !allowAutoCombat))
        return;
    int target = (state.index + 1) % party.size();
    for (auto c : party)
        if (c && c != this && c->state.role == Role::Healer) {
            target = c->state.index;
            break;
        }
    const double start = host_.now();
    while (host_.now() - start < 6) {
        const int index = host_.partyIndex();
        if (host_.inTeam() && index != state.index) {
            for (auto c : party)
                if (c)
                    c->state.current = c->state.index == index;
            break;
        }
        host_.tap(QString::number(target + 1));
        sleep(.2, false);
    }
}
bool Character::flying() { return host_.hasLevitator && host_.levitatorWhite() < .1; }
bool Character::down() {
    return (host_.highlighted(Skill::Resonance) > 0 && !host_.hasCd(Skill::Resonance)) ||
           (host_.highlighted(Skill::Liberation) > 0 && !host_.hasCd(Skill::Liberation));
}
void Character::waitDown(bool click) {
    if (!host_.hasLevitator && state.hasIntro) {
        if (click)
            normalAttacks(introMotion);
        else
            sleep(introMotion);
    } else {
        const auto start = host_.now();
        while (flying() && host_.now() - start < 2.5) {
            if (click)
                host_.click("left", .2);
            else
                sleep(.2);
            host_.nextFrame();
        }
    }
}
void Character::waitIntro(double timeout, bool click) {
    if (state.hasIntro)
        host_.wait([&] { return down(); }, timeout,
                   click ? std::function<void()>([&] { host_.click("left", .1); }) : std::function<void()>{});
}
void Character::send(Skill skill, double after, double interval, double down) {
    if (skill == Skill::Resonance)
        state.resonanceReady = false;
    else if (skill == Skill::Liberation)
        state.liberationReady = false;
    else if (skill == Skill::Echo)
        state.echoReady = false;
    host_.tap(host_.hotkey(skill), after, interval, down);
}
void Character::record(Skill skill) {
    if (skill == Skill::Resonance)
        state.lastResonance = host_.now();
    else if (skill == Skill::Liberation)
        state.lastLiberation = host_.now();
    else if (skill == Skill::Echo)
        state.lastEcho = host_.now();
}
void Character::alertSkillFailed() { host_.log("技能释放超时，请核对游戏与软件中的键位设置"); }
ResonanceResult Character::resonance(ResonanceOptions o) {
    ResonanceResult result;
    double lastClick = 0, clickTime = 0, animationStart = 0, lastF = 0;
    bool lastWasSkill = false;
    const double start = host_.now();
    double timeout = o.timeout == 0 ? 15 : o.timeout;
    try {
        while (true) {
            if (host_.now() - start > timeout || (host_.inLiberation && host_.now() - start > 6))
                break;
            if (o.animation) {
                if (!host_.inTeam()) {
                    host_.inLiberation = true;
                    const double now = host_.now();
                    if (animationStart == 0)
                        animationStart = now;
                    timeout = 15;
                    if (now - clickTime > 6)
                        host_.inLiberation = false;
                    if (o.clickF && now - animationStart >= o.animationMinimum && now - lastF >= .1) {
                        host_.tap("f");
                        lastF = now;
                    }
                    host_.nextFrame();
                    host_.checkCombat();
                    continue;
                } else if (host_.inLiberation)
                    break;
            }
            host_.checkCombat();
            const double now = host_.now();
            if (!resonanceAvailable() && (!o.animation || now - start > o.animationMinimum))
                break;
            if (now - lastClick > .1) {
                if (o.sendClick && lastWasSkill) {
                    host_.click();
                    lastWasSkill = false;
                    continue;
                }
                if (resonanceAvailable()) {
                    if (clickTime == 0) {
                        result.clicked = true;
                        clickTime = now;
                        record(Skill::Resonance);
                    }
                    lastWasSkill = true;
                    send(Skill::Resonance);
                    if (o.animation)
                        sleep(.2, false);
                }
                lastClick = now;
            }
            host_.nextFrame();
        }
        host_.inLiberation = false;
        if (result.clicked)
            sleep(o.postSleep);
        result.duration = clickTime != 0 ? host_.now() - clickTime : 0;
        result.animated = animationStart > 0;
        if (result.animated)
            host_.freezes.add(host_.now(), clickTime, host_.now() - animationStart);
        return result;
    } catch (...) {
        host_.inLiberation = false;
        throw;
    }
}
bool Character::echo(EchoOptions o) {
    if (o.timeout == 0 && echoAvailable()) {
        send(Skill::Echo);
        record(Skill::Echo);
        return true;
    }
    if (host_.openWorldAutoCombat() && state.ringIndex == 2)
        return false;
    if (host_.hasCd(Skill::Echo))
        return false;
    bool clicked = false;
    const auto start = host_.now();
    double lastClick = 0;
    while (true) {
        const auto now = host_.now();
        if (now - start > o.timeout + o.duration)
            return false;
        if (!echoAvailable() && (o.duration == 0 || !clicked))
            break;
        if (o.duration > 0 && start != 0 && now - start > o.duration)
            break;
        if (now - lastClick > .1) {
            if (!clicked) {
                record(Skill::Echo);
                clicked = true;
            }
            send(Skill::Echo);
            lastClick = now;
        }
        if (now - start > 15) {
            alertSkillFailed();
            break;
        }
        host_.nextFrame();
    }
    return clicked;
}
bool Character::liberation(LiberationOptions o) {
    if (!host_.useLiberation || (o.conLessThan > 0 && currentCon() > o.conLessThan))
        return false;
    double start = host_.now(), lastClick = 0;
    bool clicked = false;
    try {
        if (!host_.inLiberation) {
            while (liberationAvailable()) {
                if (o.sendClick)
                    host_.click("left", .1);
                const auto now = host_.now();
                if (now - lastClick > .1) {
                    send(Skill::Liberation);
                    clicked = true;
                    lastClick = now;
                }
                if (host_.now() - start > 15) {
                    alertSkillFailed();
                    throw CombatInterrupted("解放技能持续点击超时");
                }
                host_.nextFrame();
            }
            if (clicked) {
                host_.inLiberation =
                    host_.wait([&] { return !host_.inTeam(); }, .4,
                               o.sendClick ? std::function<void()>([&] { host_.click("left", .1); })
                                           : std::function<void()>{});
                if (!host_.inLiberation)
                    return false;
            } else {
                start = host_.now();
                while (!host_.hasCd(Skill::Liberation) && host_.now() - start < o.waitIfReady) {
                    send(Skill::Liberation, .05);
                    if (host_.wait([&] { return !host_.inTeam(); }, .1))
                        host_.inLiberation = true;
                }
                if (!host_.inLiberation)
                    return false;
            }
        }
        start = host_.now();
        double lastF = 0;
        while (!host_.inTeam()) {
            host_.inLiberation = true;
            clicked = true;
            if (o.sendClick)
                host_.click("left", .1);
            const double now = host_.now();
            if (o.clickF && now - start >= o.animationMinimum && now - lastF >= .1) {
                host_.tap("f");
                lastF = now;
            }
            if (now - start > 7)
                throw CombatInterrupted("解放动画超时或战斗已经结束");
            host_.nextFrame();
        }
        host_.freezes.add(host_.now(), start, host_.now() - start);
        record(Skill::Liberation);
        host_.inLiberation = false;
        state.liberationReady = false;
        return clicked;
    } catch (...) {
        host_.inLiberation = false;
        throw;
    }
}
void Character::normalAttack() {
    host_.checkCombat();
    host_.click();
}
void Character::normalAttackUntilCanSwitch() {
    host_.click();
    while (elapsed(state.lastPerform) < 1.1)
        host_.click("left", .1);
}
void Character::cycleSleep(double duration) {
    const double remaining = duration - (host_.now() - cycleStartTime);
    host_.checkBreak();
    sleep(remaining);
}
bool Character::needFastPerform() {
    const auto party = host_.characters();
    Character *current = nullptr;
    for (auto c : party)
        if (c && c->state.current) {
            current = c;
            break;
        }
    for (auto c : party)
        if (c && c != current && c->switchPriority(current, false, false) >= int(SwitchPriority::Must))
            return true;
    return false;
}
void Character::heavyAttack(double duration) {
    host_.checkCombat();
    HeldMouse held(host_);
    sleep(duration);
    held.release();
    sleep(.01);
}
bool Character::heavyForte(Forte kind) {
    return heavyForte([&] { return kind == Forte::Bar ? forteFull() : host_.forte(kind); });
}
bool Character::heavyForte(const std::function<bool()> &condition) {
    if (!condition())
        return false;
    HeldMouse held(host_);
    const bool success = host_.wait([&] { return !condition(); }, 2);
    held.release();
    sleep(.05);
    return success;
}
void Character::normalAttacks(double duration, double interval, double after, bool resonanceIfReady,
                              bool untilFull) {
    const double start = host_.now();
    while (host_.now() - start < duration) {
        if (resonanceIfReady && resonanceAvailable()) {
            resonance();
            return;
        }
        if (untilFull && isConFull())
            return;
        host_.click();
        sleep(interval);
    }
    sleep(after);
}
void Character::repeatedKey(QString key, double duration, double interval) {
    const auto start = host_.now();
    while (host_.now() - start < duration)
        host_.tap(key, 0, interval);
}
void Character::rightClicks(double duration, double interval, QString direction) {
    try {
        if (!direction.isEmpty()) {
            host_.keyDown(direction);
            host_.nextFrame();
        }
        const auto start = host_.now();
        while (host_.now() - start < duration)
            host_.click("right", interval);
        if (!direction.isEmpty())
            host_.keyUp(direction);
    } catch (...) {
        if (!direction.isEmpty())
            try {
                host_.keyUp(direction);
            } catch (...) {
            }
        throw;
    }
}
void Character::switchNext(bool freeIntro, bool targetLowCon, const SwitchAction &post) {
    forteFull();
    state.hasIntro = false;
    state.hasSubDpsIntro = false;
    state.liberationReady = liberationAvailable();
    if (state.hasToolBox) {
        host_.tap(host_.toolKey());
        state.hasToolBox = false;
    }
    host_.switchNext(*this, freeIntro, targetLowCon, post);
}
void Character::resetState() {
    state.hasIntro = state.hasSubDpsIntro = false;
    state.currentCon = 0;
    state.lastFullConSwitch = -1;
    state.hasToolBox = state.liberationReady = state.echoReady = state.resonanceReady = false;
}
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
    fail("角色尚未完成 C++ 迁移：" + name);
}
} // namespace okww::runtime
