#include "character_registry.hpp"
#include "combat_scopes.hpp"

namespace okww::runtime {
namespace {
class Denia final : public Character {
    double lib2_ = -1;
    bool lib1Casted_ = false;
    void doPerform() override {
        if (state.hasIntro)
            normalAttacks(2);
        else if (lib1Casted_)
            normalAttacks(1.3);
        const double duration = lib1Casted_ ? 4.4 : 1.2, start = host_.now();
        while (elapsed(start) < duration) {
            cycleStart();
            if (elapsed(lib2_) < 10 && isConFull()) {
                switchNext();
                return;
            }
            if (resonance().clicked) {
            } else if (liberationAvailable()) {
                const bool second = host_.feature("denia_end_lib");
                if (liberation({.waitIfReady = 0})) {
                    if (second) {
                        lib2_ = host_.now();
                        lib1Casted_ = false;
                        echo({.timeout = 0});
                    } else {
                        lib1Casted_ = true;
                        normalAttacks(1.9);
                    }
                    switchNext();
                    return;
                }
            } else
                host_.click();
            cycleSleep();
        }
        switchNext();
    }

  public:
    using Character::Character;
    void synchronizeLiberationPhase(int phase) override {
        lib1Casted_ = phase == 1;
        if (phase == 2)
            lib2_ = host_.now();
    }
    void resetState() override {
        Character::resetState();
        lib2_ = -1;
        lib1Casted_ = false;
    }
    int switchPriority(const Character *current, bool intro, bool low) override {
        if (intro) {
            if (current &&
                (current->state.className == "Aemeath" || current->state.className == "Qingxiao") &&
                !hasBuff())
                return int(SwitchPriority::Normal);
            return 1;
        }
        return hasBuff() ? int(SwitchPriority::Low) : Character::switchPriority(current, intro, low);
    }
};
class Douling final : public Character {
    int segment_ = 1;
    void normalCycle(double duration) {
        const double start = host_.now();
        while (host_.now() - start < duration) {
            cycleStart();
            if (flying()) {
                waitDown();
                break;
            }
            host_.click();
            cycleSleep();
        }
    }
    void heldHeavy(double duration) {
        for (int retry = 0; retry < 3; ++retry) {
            host_.checkCombat();
            HeldCombatInput held(host_);
            const double start = host_.now();
            bool interrupted = false;
            while (host_.now() - start < duration) {
                if (flying()) {
                    interrupted = true;
                    break;
                }
                sleep(.1);
            }
            held.release();
            sleep(.01);
            if (!interrupted)
                return;
            waitDown();
        }
    }
    void doPerform() override {
        if (segment_ == 1) {
            normalCycle(1.2);
            if (flying())
                waitDown();
            host_.checkCombat();
            if (!resonance({.sendClick = true, .timeout = .5}).clicked) {
                echo({.timeout = 0});
                liberation();
                segment_ = 1;
                switchNext();
                return;
            }
            sleep(.2);
            if (flying())
                waitDown();
            host_.checkCombat();
            normalCycle(1);
            segment_ = 2;
            switchNext();
        } else {
            host_.checkCombat();
            host_.tap(host_.jumpKey(), .01);
            sleep(.05);
            host_.checkCombat();
            if (flying()) {
                host_.click();
                sleep(.05);
            } else
                waitDown();
            host_.checkCombat();
            heldHeavy(2.5);
            echo({.timeout = 0});
            liberation();
            segment_ = 1;
            switchNext();
        }
    }

  public:
    using Character::Character;
    int switchPriority(const Character *, bool, bool) override {
        return int(elapsed(state.lastPerform) < 8 ? SwitchPriority::No : SwitchPriority::Normal);
    }
};
class Chisa final : public Character {
    bool dps() { return host_.characterSetting("Chisa DPS").toBool(); }
    bool performForte() {
        if (flying())
            waitDown();
        host_.tap(host_.hotkey(Skill::Resonance), 0, -1, 1.2);
        if (forteFull())
            return false;
        heavyAttack(3.5);
        return true;
    }
    void support() {
        const bool longActions = state.hasIntro && !hasBuff();
        if (state.hasIntro)
            normalAttacks(2);
        if (flying() && !liberationAvailable() && !resonanceAvailable())
            waitDown();
        echo({.timeout = 0});
        const double start = host_.now(), duration = longActions ? 10 : 1.2;
        while (elapsed(start) < duration) {
            cycleStart();
            if (isConFull()) {
                switchNext();
                return;
            }
            if (liberationAvailable())
                liberation({.waitIfReady = 0});
            else if (forteFull()) {
                if (performForte())
                    break;
            } else if (resonance({.timeout = 0}).clicked) {
            } else
                host_.click();
            cycleSleep();
        }
        switchNext();
    }
    void doPerform() override {
        if (!dps()) {
            support();
            return;
        }
        double timeout = 2.5;
        checkFOnSwitch = true;
        if (state.hasIntro) {
            normalAttacks(.8);
            timeout = 2.3;
        }
        if (flying() && !liberationAvailable() && !resonanceAvailable())
            waitDown();
        echo();
        double start = host_.now();
        const double deadline = start + 30;
        bool underLib = false;
        while (host_.now() - start < timeout) {
            if (host_.now() >= deadline)
                throw CombatInterrupted("千咲循环等待超时");
            if (host_.now() - start < .5 && liberation()) {
                start = host_.now();
                underLib = true;
                timeout = 10;
                sleep(.2);
            }
            if (host_.now() - start < .5 && !forteFull() && resonance().clicked) {
                start = host_.now();
                if (timeout != 10)
                    timeout = 1.7;
            }
            if ((underLib || dps()) && forteFull() && performForte()) {
                checkFOnSwitch = false;
                switchNext();
                return;
            }
            host_.click();
            host_.checkCombat();
            host_.nextFrame();
        }
        switchNext();
    }

  public:
    Chisa(CombatHost &host, CharacterState value) : Character(host, std::move(value)) {
        if (dps()) {
            state.role = Role::MainDps;
            state.buffTime = 14;
            checkFOnSwitch = true;
        }
    }
};
class Iuno final : public Character {
    double lastHeavy_ = 0;
    bool c6() { return host_.characterSetting("Iuno C6").toBool(); }
    bool icon(const QString &name) { return host_.feature(name, .6, "box_extra_action"); }
    void everything() {
        double timeout = state.hasIntro ? 5.5 : 1.5, start = host_.now();
        const double deadline = start + 30;
        QString lastAction = "click";
        echo();
        bool c6Performed = false, jumped = false;
        while (elapsed(start) < timeout) {
            if (host_.now() >= deadline)
                throw CombatInterrupted("尤诺循环等待超时");
            const double cycle = host_.now();
            bool heavy = false;
            while (elapsed(lastHeavy_) > 20 && icon("iuno_heavy")) {
                if (host_.now() >= deadline)
                    throw CombatInterrupted("尤诺重击图标未消失");
                sleep(.05);
                heavyAttack();
                sleep(.05);
                heavy = true;
            }
            if (heavy) {
                lastHeavy_ = host_.now();
                if (!c6Performed && c6()) {
                    c6Performed = true;
                    start = host_.now();
                    timeout = 5;
                } else
                    return;
            }
            if (!jumped && icon("iuno_jump")) {
                while (icon("iuno_jump")) {
                    if (host_.now() >= deadline)
                        throw CombatInterrupted("尤诺跳跃图标未消失");
                    host_.tap(host_.jumpKey(), .1);
                }
                timeout += 3;
                jumped = true;
                if (state.hasIntro)
                    continue;
                return;
            }
            if (elapsed(state.lastLiberation) > 20 && liberation({.waitIfReady = 0})) {
                start = host_.now();
                timeout = 3;
            }
            if (lastAction == "click") {
                lastAction = "resonance";
                send(Skill::Resonance);
            } else {
                lastAction = "click";
                host_.click();
            }
            sleep(.1 - (host_.now() - cycle));
        }
    }
    void doPerform() override {
        waitDown();
        everything();
        switchNext();
    }

  public:
    Iuno(CombatHost &host, CharacterState value) : Character(host, std::move(value)) {
        if (c6()) {
            state.role = Role::MainDps;
            state.buffTime = 14;
            checkFOnSwitch = true;
        }
    }
    void combatEnd() override { switchOther(); }
};
class Hsin final : public Character {
    bool lib2_ = false;
    bool lib() {
        const bool second = host_.feature("hsin_lib2", .7);
        if (!(second || host_.feature("hsin_lib1", .7)) || !liberationAvailable())
            return false;
        const bool cast = liberation({.waitIfReady = 0});
        if (cast && second)
            lib2_ = true;
        return cast;
    }
    bool heavyAvailable() { return host_.feature("hsin_h1", .7) || host_.feature("hsin_h2", .7); }
    bool heavy(double start, double duration) {
        if (!heavyAvailable())
            return false;
        const double remaining = duration - elapsed(start);
        if (remaining <= 0)
            return false;
        host_.checkCombat();
        HeldCombatInput held(host_);
        host_.wait([&] { return !heavyAvailable(); }, std::min(1.2, remaining));
        held.release();
        sleep(.01);
        return true;
    }
    void doPerform() override {
        lib2_ = false;
        const double start = host_.now(), duration = state.hasIntro ? 12 : 6;
        while (elapsed(start) < duration) {
            cycleStart();
            if (resonanceAvailable() &&
                resonance({.animation = false, .sendClick = true, .animationMinimum = .5, .timeout = 0})
                    .clicked) {
            } else if (lib()) {
                if (lib2_)
                    break;
            } else if (heavy(start, duration)) {
            } else if (!echo({.timeout = 0}))
                host_.click();
            cycleSleep();
        }
        switchNext();
    }

  public:
    using Character::Character;
};
class Suisui final : public Character {
    double lastForteSwitch_ = -1;
    bool shouldHeavy_ = false;
    bool third() { return host_.feature("suisui_forte3", .75); }
    bool second() { return host_.feature("suisui_forte2", .75); }
    bool tryE() {
        if (!host_.feature("suisui_e1"))
            return false;
        const double start = host_.now();
        while (host_.feature("suisui_e1") && elapsed(start) < 2) {
            send(Skill::Resonance);
            sleep(.05);
        }
        return true;
    }
    void rotation() {
        const double start = host_.now();
        if (shouldHeavy_) {
            if (state.hasIntro)
                heavyAttack(1.5);
            resonance();
            shouldHeavy_ = false;
        }
        bool ready = false;
        while (elapsed(start) < 26) {
            if (third()) {
                ready = true;
                break;
            }
            if (second() || third()) {
                for (int i = 0; i < 7; ++i) {
                    if (third())
                        break;
                    host_.click();
                    sleep(.1);
                }
                ready = true;
                break;
            }
            cycleStart();
            if (!state.hasIntro && tryE()) {
                switchNext();
                return;
            }
            host_.click();
            cycleSleep();
        }
        if (!ready)
            return;
        bool liberated = false;
        const double fill = host_.now();
        while (elapsed(fill) < 12) {
            if (!liberated) {
                liberated = liberation({.waitIfReady = 0});
                if (liberated) {
                    host_.nextFrame();
                    continue;
                }
            }
            if (isConFull() && third())
                return;
            host_.click();
            sleep(.1);
        }
    }
    void doPerform() override {
        if (!shouldHeavy_)
            shouldHeavy_ = state.hasIntro;
        rotation();
        switchNext();
    }

  public:
    using Character::Character;
    void resetState() override {
        Character::resetState();
        lastForteSwitch_ = -1;
        shouldHeavy_ = false;
    }
    void switchOut(bool full = false) override {
        Character::switchOut(full);
        if (full)
            lastForteSwitch_ = host_.now();
    }
    int switchPriority(const Character *current, bool intro, bool low) override {
        const double since = elapsed(lastForteSwitch_);
        if (since > 40)
            return int(SwitchPriority::Must);
        if (since < 16)
            return int(SwitchPriority::No);
        bool main = false;
        for (auto c : host_.characters())
            if (c && c != this && c->state.role == Role::MainDps)
                main = true;
        if (main && current && current->state.role == Role::MainDps && intro)
            return int(SwitchPriority::Must);
        return Character::switchPriority(current, intro, low);
    }
};
class Rebecca final : public Character {
    double lastLib_ = -999;
    bool reenter() const { return host_.now() - lastLib_ < 17; }
    void hmg() {
        const double entry = host_.now();
        while (host_.now() - entry < .8) {
            send(Skill::Liberation);
            sleep(.1, false);
        }
        record(Skill::Liberation);
        const double start = host_.now();
        double last = start;
        while (host_.now() - start < 5.2) {
            host_.click("left", .08);
            const double now = host_.now();
            if (now - last > .9) {
                send(Skill::Liberation);
                last = now;
            }
            sleep(.01, false);
        }
        lastLib_ = host_.now();
    }
    void sequence() {
        normalAttacks(state.hasIntro ? 1.3 : 1.8);
        host_.wait([&] { return resonanceAvailable(); }, 2);
        resonance({.postSleep = 1.5});
        resonance({.postSleep = 1.5});
        if (!state.hasIntro)
            resonance({.postSleep = 1});
        send(Skill::Resonance);
        const double start = host_.now();
        while (!host_.forte(Forte::Mouse) && host_.now() - start < 4) {
            host_.click();
            sleep(.1);
            host_.nextFrame();
        }
        if (host_.forte(Forte::Mouse))
            heavyAttack(1.5);
        echo();
        hmg();
    }
    void doPerform() override {
        if (reenter()) {
            resonance();
            normalAttacks(.5);
        } else
            sequence();
        if (reenter()) {
            const double start = host_.now();
            while (!isConFull() && host_.now() - start < 2.2) {
                normalAttacks(1);
                sleep(.1);
            }
        }
        switchNext();
    }

  public:
    Rebecca(CombatHost &host, CharacterState value) : Character(host, std::move(value)) {
        checkFOnSwitch = false;
    }
};
class Qingxiao final : public Character {
    bool enhanced() {
        return host_.feature("qingxiao_e", .7) &&
               resonance({.animation = true, .animationMinimum = .5, .timeout = 1.5}).clicked;
    }
    QString heavyAvailable() {
        if (host_.feature("qingxiao_h1", .7))
            return "qingxiao_h1";
        if (host_.feature("qingxiao_h2", .7))
            return "qingxiao_h2";
        return {};
    }
    QString heavy() {
        const QString name = heavyAvailable();
        if (name.isEmpty())
            return {};
        const double start = host_.now();
        bool confirmed = false;
        HeldCombatInput held(host_);
        while (elapsed(start) < 2) {
            if (!heavyAvailable().isEmpty())
                host_.nextFrame();
            else {
                sleep(.25);
                host_.nextFrame();
                if (heavyAvailable().isEmpty()) {
                    confirmed = true;
                    break;
                }
            }
        }
        held.release();
        sleep(.01);
        return confirmed ? name : QString{};
    }
    void doPerform() override {
        if (!(allBuffs() && state.hasIntro)) {
            enhanced();
            switchNext();
            return;
        }
        const double start = host_.now();
        bool broke = false;
        while (elapsed(start) < 18) {
            cycleStart();
            auto name = heavy();
            if (!name.isEmpty()) {
                host_.breakF();
                if (name == "qingxiao_h2") {
                    if (!liberationAvailable())
                        host_.wait([&] { return liberationAvailable(); }, 1.5);
                    liberation();
                    broke = true;
                    break;
                }
            } else if (!enhanced())
                host_.click();
            cycleSleep();
        }
        if (!broke)
            heavy();
        if (broke)
            host_.wait([&] { return host_.inTeam(); }, 1);
        switchNext();
    }

  public:
    using Character::Character;
};
class Aemeath final : public Character {
    bool enhanced_ = false, lib2_ = false, mustLib2_ = false;
    bool heavy() {
        if (!longAction())
            return false;
        HeldCombatInput held(host_);
        const bool result = host_.wait([&] { return !longAction(); }, 1.2);
        held.release();
        sleep(.01);
        return result;
    }
    bool lib() {
        const bool second = host_.feature("aemeath_lib2", .7);
        const bool cast = liberation({.waitIfReady = 0});
        if (cast && second)
            lib2_ = true;
        return cast;
    }
    bool pending() const { return (state.hasIntro && !enhanced_) || (mustLib2_ && !lib2_); }
    void doPerform() override {
        enhanced_ = lib2_ = false;
        mustLib2_ = allBuffs() && state.hasIntro;
        const double deadline = host_.now() + 60;
        if (!mustLib2_) {
            while (longAction()) {
                if (host_.now() >= deadline)
                    throw CombatInterrupted("爱弥斯长动作条未结束");
                if (heavy())
                    sleep(.3);
            }
            switchNext();
            return;
        }
        if (state.hasIntro)
            normalAttacks(2.1);
        double start = host_.now();
        while (elapsed(start) < 12) {
            if (host_.now() >= deadline)
                throw CombatInterrupted("爱弥斯连招等待超时");
            cycleStart();
            if (heavy()) {
                start = host_.now();
                if (!host_.breakF())
                    sleep(.1);
                host_.checkCombat();
                continue;
            }
            bool acted = false;
            if (lib()) {
                if (lib2_)
                    break;
                acted = true;
            } else if (host_.feature("aemeath_e1", .7) || host_.feature("aemeath_e2", .7)) {
                if (resonance({.animation = true, .sendClick = true, .animationMinimum = .5, .timeout = 1.5})
                        .clicked) {
                    enhanced_ = true;
                    echo({.timeout = 0});
                    host_.nextFrame();
                }
                lib();
                if (lib2_)
                    break;
                acted = true;
            } else
                host_.click();
            if (acted) {
                if (longAction())
                    start = host_.now();
                else if (!pending())
                    break;
            }
            cycleSleep();
        }
        switchNext();
    }

  public:
    using Character::Character;
    void combatEnd() override { switchOther(); }
};
class Galbrena final : public Character {
    bool checkRes() {
        return host_.inTeam() &&
               !host_.bestInBox("box_target_enemy_long", {"has_target", "no_target"}).isEmpty();
    }
    void doPerform() override {
        if (state.hasIntro) {
            HeldCombatInput held(host_);
            sleep(1);
            if (needFastPerform()) {
                held.release();
                switchNext();
                return;
            }
            sleep(.44);
            held.release();
            rightClicks(.6);
        } else if (flying())
            waitDown();
        echo({.timeout = 0});
        if (forteFull() && !needFastPerform()) {
            resonance();
            if (liberation())
                normalAttacks(1);
        }
        if (checkRes() && !needFastPerform()) {
            liberation();
            const double start = host_.now();
            while (checkRes() && host_.now() - start < 10) {
                if (flying())
                    shoreKeeperDodge();
                host_.click();
                sleep(.1);
                host_.checkCombat();
            }
            switchNext();
            return;
        }
        normalAttacks(1);
        resonance();
        switchNext();
    }

  public:
    using Character::Character;
};
class Cantarella final : public Character {
    double lastHeavy_ = -1;
    void doPerform() override {
        bool underOutro = false;
        if (state.hasIntro) {
            normalAttacks(1.2);
            underOutro =
                state.hasSubDpsIntro && QStringList{"char_roccia", "char_sanhua"}.contains(outroSource());
        }
        liberation();
        if (host_.forte(Forte::Mouse) || !forteFull()) {
            resonance();
            if (underOutro && flying())
                waitDown();
            if (!flying() && host_.forte(Forte::Mouse)) {
                if (heavyForte(Forte::Mouse))
                    lastHeavy_ = host_.now();
            } else if (echo()) {
                switchNext();
                return;
            } else {
                normalAttacks(.1);
                switchNext();
                return;
            }
        }
        double forteDelay = host_.now(), count = -.1;
        try {
            while (elapsed(lastHeavy_) < 8 && !host_.forte(Forte::Mouse)) {
                const double now = host_.now();
                if (resonanceAvailable()) {
                    resonance({.sendClick = false}); // Original tests the nonempty result tuple.
                    if (!underOutro) {
                        host_.mouseUp();
                        switchNext();
                        return;
                    }
                }
                if (!underOutro && needFastPerform() && elapsed(state.lastPerform) > 1.1)
                    break;
                if (now - forteDelay > count) {
                    host_.mouseUp();
                    sleep(.2);
                    host_.mouseDown();
                    count += 1;
                }
                if (forteFull())
                    forteDelay = now;
                else if (now - forteDelay > .5)
                    break;
                host_.checkCombat();
                host_.nextFrame();
            }
        } catch (...) {
            try {
                host_.mouseUp();
            } catch (...) {
            }
            throw;
        }
        host_.mouseUp();
        echo();
        switchNext();
    }

  public:
    using Character::Character;
    int switchPriority(const Character *current, bool intro, bool low) override {
        if (intro && current && QStringList{"char_roccia", "char_sanhua"}.contains(current->state.name))
            return int(SwitchPriority::Must);
        return Character::switchPriority(current, intro, low);
    }
    bool resonanceAvailable() override {
        if (!host_.forte(Forte::Mouse) && forteFull())
            return !host_.hasCd(Skill::Resonance);
        return Character::resonanceAvailable();
    }
    bool forteFull() override { return host_.whiteRatio({5120, 2880}, {3034, 2640, 56, 60}) > .06; }
    void combatEnd() override {
        const auto size = host_.characters().size();
        if (size)
            host_.tap(QString::number((state.index + 1) % size + 1));
    }
};
class Phrolova final : public Character {
    bool sp_ = false, resReady_ = false;
    bool heavyAndLib() {
        if (!heavyForte(Forte::Mouse))
            return false;
        host_.wait([&] { return liberation({.waitIfReady = 0}); }, 3);
        return true;
    }
    void doPerform() override {
        state.lastLiberation = -1;
        bool underOutro = false;
        sp_ = false;
        if (state.hasIntro) {
            resReady_ = false;
            underOutro = outroSource() == "char_cantarella";
            normalAttacks(1.7);
            rightClicks(.1);
        }
        if (flying())
            waitDown();
        if ((liberationAvailable() && liberation({.waitIfReady = 0})) || heavyAndLib()) {
            switchNext();
            return;
        }
        if (resonanceAvailable() || resReady_) {
            normalAttacks(.1);
            resonance();
            normalAttacks(.1);
            host_.wait([&] { return !resonanceAvailable(); }, .3, [&] { host_.click(); });
            if (!echo())
                rightClicks(.1);
        }
        resReady_ = false;
        const double start = host_.now();
        if (underOutro)
            sp_ = true;
        while (underOutro ? elapsed(state.lastPerform) < 16 : host_.now() - start < 4) {
            if (liberationAvailable() && liberation({.waitIfReady = 0})) {
                switchNext();
                return;
            }
            if (flying())
                shoreKeeperDodge();
            if (heavyAndLib()) {
                switchNext();
                return;
            }
            if (resonanceAvailable() && host_.now() - start > 1) {
                if (underOutro) {
                    normalAttacks(.3);
                    if (resonance().clicked) {
                        normalAttacks(.1);
                        host_.wait([&] { return !resonanceAvailable(); }, .3, [&] { host_.click(); });
                        if (!echo())
                            rightClicks(.1);
                    }
                } else {
                    resReady_ = true;
                    break;
                }
            }
            host_.click();
            host_.checkCombat();
            host_.nextFrame();
        }
        switchNext();
    }

  public:
    using Character::Character;
    bool skipCombatCheck() override { return elapsed(state.lastLiberation) < 2; }
    bool resonanceAvailable() override {
        return sp_ ? !(flying() || host_.hasCd(Skill::Resonance)) : Character::resonanceAvailable();
    }
    int switchPriority(const Character *current, bool intro, bool low) override {
        if (elapsed(state.lastLiberation) > 14 && intro && current &&
            current->state.name == "char_cantarella")
            return int(SwitchPriority::Must);
        if (elapsed(state.lastLiberation) < 24)
            return int(SwitchPriority::No);
        return Character::switchPriority(current, intro, low);
    }
};
class Mornye final : public Character {
    double lastHeavy_ = 0;
    bool air() { return longAction(2); }
    bool elbow(bool ready) { return ready && !available(Skill::Echo); }
    void airborne() {
        const bool detect = echoAvailable();
        const double start = host_.now();
        while (host_.now() - start < 10 && air()) {
            if (elbow(detect))
                host_.wait([&] { return !elbow(detect); }, 1.5, [&] { rightClicks(.05); });
            liberation();
            if (air() && host_.forte(Forte::Mouse) &&
                heavyForte([&] { return host_.forte(Forte::Mouse) && !elbow(detect); })) {
                if (elbow(detect))
                    continue;
                if (!host_.wait([&] { return isConFull(); }, 1.5)) {
                    const double fill = host_.now();
                    while (!isConFull() && host_.now() - fill < 2 && air()) {
                        normalAttacks(.5);
                        if (elbow(detect))
                            break;
                    }
                    if (!isConFull())
                        echo({.duration = .2});
                }
                lastHeavy_ = host_.now();
                checkFOnSwitch = false;
                break;
            }
            resonance();
            host_.click();
            sleep(.01);
        }
    }
    void ground() {
        const double start = host_.now();
        bool dodge = true;
        while (host_.now() - start < 10 && !air()) {
            host_.click();
            resonance();
            host_.checkCombat();
            sleep(.1);
            if (host_.forte(Forte::Mouse)) {
                if (dodge) {
                    host_.click("right");
                    dodge = false;
                }
                heavyAttack();
                sleep(.3);
            }
        }
    }
    void doPerform() override {
        if (state.hasIntro)
            normalAttacks(1.33);
        checkFOnSwitch = true;
        if (!air()) {
            if (elapsed(lastHeavy_) < 23) {
                if (echo()) {
                    switchNext();
                    return;
                }
                if (resonance().clicked) {
                    switchNext();
                    return;
                }
                normalAttacks(.1);
                switchNext();
                return;
            }
            ground();
        }
        if (air())
            airborne();
        switchNext();
    }

  public:
    using Character::Character;
    int switchPriority(const Character *current, bool intro, bool low) override {
        if (intro && current && QStringList{"char_aemeath", "char_qingxiao"}.contains(current->state.name))
            return int(SwitchPriority::Must);
        if (intro && current && hasCharacter("Linnai") && current->state.name != "char_linnai")
            return int(SwitchPriority::Must);
        return Character::switchPriority(current, intro, low);
    }
    void combatEnd() override { switchOther(); }
};
class Xigelika final : public Character {
    bool holdHeavy() {
        const bool mouse = longAction();
        if (!mouse && host_.hasCd(Skill::Resonance)) {
            host_.click("left", .1);
            sleep(.05);
            return false;
        }
        HeldCombatInput held(host_, mouse ? "left" : host_.hotkey(Skill::Resonance), mouse);
        const bool result = host_.wait([&] { return mouse ? !longAction() : !forteFull(); }, 1.2);
        held.release();
        sleep(.01);
        return result;
    }
    bool heavy() {
        bool handled = false;
        const double start = host_.now();
        while (forteFull() && elapsed(start) < 3) {
            holdHeavy();
            handled = true;
        }
        return handled;
    }
    void doPerform() override {
        if (state.hasIntro)
            normalAttacks(.77);
        else
            waitDown();
        double start = host_.now(), timeout = state.hasSubDpsIntro ? 15 : .5;
        const double deadline = start + 45;
        while (elapsed(start) < timeout) {
            if (host_.now() >= deadline)
                throw CombatInterrupted("西格莉卡循环等待超时");
            echo({.timeout = 0});
            if (flying())
                shoreKeeperDodge();
            if (heavy())
                break;
            if (!state.hasIntro && liberation({.waitIfReady = 0})) {
                host_.breakF();
                timeout = 15;
                start = host_.now();
            } else if (!resonance({.sendClick = false, .timeout = 2}).clicked)
                host_.click("left", .1);
            sleep(.05);
        }
        switchNext();
    }

  public:
    using Character::Character;
    bool forteFull() override { return host_.whiteRatio({5120, 2880}, {3032, 2654, 44, 46}) > .1; }
};
class Luhesi final : public Character {
    double lastIntro_ = -1;
    bool elbow(bool ready) { return ready && !available(Skill::Echo); }
    bool checkRes() {
        return host_.inTeam() && !host_.bestInBox("target_box_long2", {"has_target", "no_target"}).isEmpty();
    }
    bool lib() {
        if (host_.white("box_luhesi_lib") > 0 && !host_.hasCd(Skill::Liberation) &&
            liberation({.waitIfReady = 0})) {
            host_.breakF();
            return true;
        }
        return false;
    }
    bool heavy(int resCount) {
        const double start = host_.now();
        bool kicked = false;
        while (host_.feature("luhesi_kick", .7) && host_.now() - start < 3) {
            kicked = true;
            if (resCount < 3)
                host_.breakF();
            host_.click();
            sleep(.1);
        }
        return kicked;
    }
    void everything() {
        normalAttacks(1.1);
        if (!state.hasSubDpsIntro) {
            send(Skill::Resonance, .1);
            return;
        }
        lastIntro_ = host_.now();
        if (!flying())
            host_.wait([&] { return flying(); }, .2, [&] { host_.tap(host_.jumpKey(), .01); });
        const bool detect = echoAvailable();
        const double start = host_.now();
        int resCount = 0;
        bool tryJump = false;
        while (elapsed(start) < 12) {
            if (elbow(detect))
                host_.wait([&] { return !elbow(detect); }, 1.5, [&] { rightClicks(.05); });
            else if (heavy(resCount) && resCount > 2) {
                lib();
                return;
            } else if (tryJump && !checkRes()) {
                host_.wait([&] { return host_.feature("luhesi_kick", .7) || elbow(detect); }, 1,
                           [&] { send(Skill::Resonance); });
                if (elbow(detect))
                    continue;
                tryJump = false;
                if (host_.feature("luhesi_kick", .7))
                    ++resCount;
                else {
                    if (resCount == 2)
                        waitDown();
                    lib();
                    return;
                }
            } else if (checkRes()) {
                host_.tap(host_.jumpKey(), .01);
                tryJump = true;
            } else
                host_.click();
            sleep(.01);
        }
    }
    void doPerform() override {
        everything();
        switchNext();
    }

  public:
    using Character::Character;
    int switchPriority(const Character *current, bool intro, bool low) override {
        if (host_.now() - lastIntro_ > 24 && intro)
            return int(SwitchPriority::Must);
        return Character::switchPriority(current, intro, low);
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
