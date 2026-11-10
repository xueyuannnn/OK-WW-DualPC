#include "team_macro.hpp"
namespace okww::runtime {
TeamMacro::~TeamMacro() {
    try {
        release();
    } catch (...) {
    }
}
void TeamMacro::wait(double seconds) { h_.preciseWait(std::max(0., seconds)); }
void TeamMacro::click() { h_.rawClick("left", .01); }
void TeamMacro::key(QString key, double hold) { h_.rawTap(key, hold); }
void TeamMacro::send(Character &, Skill skill, double hold) { key(h_.hotkey(skill), hold); }
void TeamMacro::record(Character &c, Skill skill) {
    if (skill == Skill::Resonance)
        c.state.lastResonance = h_.now();
    else if (skill == Skill::Echo)
        c.state.lastEcho = h_.now();
    else if (skill == Skill::Liberation)
        c.state.lastLiberation = h_.now();
}
void TeamMacro::hold() {
    h_.mouseDown();
    holding_ = true;
}
void TeamMacro::release() {
    if (holding_) {
        holding_ = false;
        h_.mouseUp();
    }
}
void TeamMacro::start() {
    macroStart_ = h_.now();
    marks_.clear();
}
void TeamMacro::mark(const QString &name) {
    marks_.append(QString("%1@%2").arg(name).arg(h_.now() - macroStart_, 0, 'f', 2));
}
void TeamMacro::need(bool ok, const QString &reason) {
    if (!ok)
        throw MacroBroken(reason);
}
int TeamMacro::spam(double window, double interval, int maxClicks, const std::function<bool()> &sample,
                    int sampleEvery) {
    interval = interval < 0 ? attackInterval_ : interval;
    if (!std::isfinite(window) || window < 0 || window > 120 || interval < 0 || sampleEvery < 1)
        fail("宏普攻时间参数无效");
    const int cap = int(window / std::max(interval, .01)) + 2;
    maxClicks = maxClicks < 0 ? cap : std::min(cap, maxClicks);
    const double start = h_.now();
    int n = 0;
    while (h_.now() - start < window && n < maxClicks) {
        click();
        ++n;
        if (sample && n % sampleEvery == 0)
            sample();
        const double remaining = window - (h_.now() - start);
        if (remaining <= 0)
            break;
        wait(std::min(interval, remaining));
    }
    return n;
}
int TeamMacro::spamKey(const std::function<void()> &send, double window, const MacroKeySpam &o) {
    if (!std::isfinite(window) || window < 0 || window > 120 || o.interval < 0 || o.clickInterval <= 0 ||
        o.presses > 12000)
        fail("宏技能时间参数无效");
    const auto start = h_.now();
    const double interval = o.presses > 0 ? std::max(o.interval, window / o.presses) : o.interval;
    int n = 0;
    double nextClick = 0;
    while (true) {
        send();
        ++n;
        if (o.stop && o.stop())
            break;
        double left = window - (h_.now() - start);
        if (o.presses >= 0) {
            if (n >= o.presses)
                break;
            left = interval;
        } else if (left <= 0)
            break;
        if (n > 12000)
            throw MacroBroken("宏技能重复次数异常");
        const double end = h_.now() + std::min(interval, left);
        while (h_.now() < end) {
            const auto now = h_.now();
            if (o.poke.value_or(poking_) && now >= nextClick) {
                click();
                nextClick = now + o.clickInterval;
            }
            wait(std::min(.02, end - h_.now()));
        }
    }
    return n;
}
int TeamMacro::gap(double seconds) {
    if (seconds <= 0)
        return 0;
    if (poking_)
        return spam(seconds);
    wait(seconds);
    return 0;
}
Character *TeamMacro::teammate(const QString &name) {
    for (auto *c : h_.characters())
        if (c && (c->state.className.compare(name, Qt::CaseInsensitive) == 0 ||
                  c->state.name.compare(name, Qt::CaseInsensitive) == 0))
            return c;
    return nullptr;
}
void TeamMacro::handoff(Character &from, Character &to, bool con) {
    h_.inLiberation = false;
    from.switchOut(con);
    to.state.current = true;
    to.state.hasIntro = con;
    to.state.hasSubDpsIntro = con && from.state.role == Role::SubDps;
    to.state.lastSwitchIn = h_.now();
    if (con) {
        h_.freezes.add(h_.now(), h_.now(), to.introDuration(), -100);
        from.state.lastOutro = h_.now();
    }
    cur_ = &to;
}
bool TeamMacro::quickSwitch(Character *target, const MacroSwitch &o) {
    if (!target)
        return false;
    if (target == cur_)
        return true;
    if (!cur_)
        cur_ = onField();
    if (!cur_)
        return false;
    auto *source = cur_;
    switchPress_ = h_.now();
    for (int i = 0; i < o.clickFirst; ++i) {
        click();
        if (i + 1 < o.clickFirst)
            wait(o.clickInterval);
    }
    key(QString::number(target->state.index + 1), switchKeyDown_);
    const bool full = o.assumeCon || (o.readCon && source->isConFull());
    const auto start = h_.now();
    double last = start, nextClick = 0;
    int untrusted = 0;
    const int expected = static_cast<int>(h_.characters().size());
    while (h_.now() - start < o.timeout) {
        const double now = h_.now();
        if (now - last >= switchKeyInterval_) {
            key(QString::number(target->state.index + 1), switchKeyDown_);
            last = now;
        }
        if (o.clickDuring.value_or(poking_) && now >= nextClick) {
            click();
            nextClick = now + o.clickInterval;
        }
        h_.nextFrame();
        const bool visible = h_.inTeam();
        const bool hit = visible && h_.partyIndex() == target->state.index,
                   trusted = visible && h_.partyCount() >= expected;
        untrusted = hit && !trusted ? untrusted + 1 : 0;
        if (hit && (trusted || untrusted >= untrustedConfirm_)) {
            handoff(*source, *target, full);
            if (o.click)
                click();
            gap(o.settle);
            return true;
        }
    }
    return false;
}
Character *TeamMacro::onField(double timeout) {
    const double end = h_.now() + timeout;
    const auto chars = h_.characters();
    do {
        h_.nextFrame();
        if (h_.inTeam() && h_.partyCount() >= chars.size())
            for (auto *c : chars)
                if (c && c->state.index == h_.partyIndex())
                    return c;
        if (h_.now() >= end)
            break;
        wait(.03);
    } while (true);
    return nullptr;
}
bool TeamMacro::waitHud(bool poke, double timeout, double tail) {
    if (timeout < 0)
        timeout = animationTimeout_;
    const double start = h_.now();
    bool gone = false, back = false;
    double nextClick = 0;
    while (h_.now() - start < timeout) {
        const double now = h_.now();
        if (poke && now >= nextClick) {
            click();
            nextClick = now + attackInterval_;
        }
        h_.nextFrame();
        if (!h_.inTeam())
            gone = true;
        else if (gone || h_.now() - start >= noAnimationGrace_) {
            back = true;
            break;
        }
        wait(.01);
    }
    h_.freezes.add(h_.now(), start, h_.now() - start);
    if (tail > 0) {
        if (poke)
            spam(tail);
        else
            wait(tail);
    }
    return back;
}
bool TeamMacro::waitHudUp(double cap) {
    const auto start = h_.now();
    while (h_.now() - start < cap) {
        h_.nextFrame();
        if (h_.inTeam())
            return true;
        wait(.02);
    }
    return false;
}
bool TeamMacro::conAtLeast(Character &c, double level, double cap, double interval, bool poke) {
    if (cap < 0)
        cap = conTimeout_;
    if (interval < 0)
        interval = conInterval_;
    const auto start = h_.now();
    double nextClick = 0;
    while (c.elapsed(start) < cap && h_.now() - start < cap + 30) {
        h_.refreshObservation();
        const auto con = c.currentCon(); // Cancellation/vision errors propagate, never become a zero reading.
        if (con >= level)
            return true;
        const auto now = h_.now();
        if (poke && now >= nextClick) {
            click();
            nextClick = now + attackInterval_;
        }
        wait(interval);
    }
    return false;
}
bool TeamMacro::pressUntilCd(const std::function<void()> &send, const std::function<bool()> &available,
                             double cap, double grace, bool poke) {
    const double start = h_.now();
    bool seen = false;
    int live = 0;
    double nextClick = 0;
    while (h_.now() - start < cap) {
        h_.nextFrame();
        const bool hud = h_.inTeam();
        if (hud) {
            if (available())
                seen = true;
            else if (seen && live > 0)
                return true;
            else if (!seen && h_.now() - start >= grace)
                return false;
        }
        send();
        if (hud)
            ++live;
        const auto end = h_.now() + resendStep_;
        while (h_.now() < end) {
            const auto now = h_.now();
            if (poke && now >= nextClick) {
                click();
                nextClick = now + attackInterval_;
            }
            wait(std::min(.02, end - now));
        }
    }
    return false;
}
bool TeamMacro::pressRes(Character &c, double cap, double grace, bool poke) {
    return pressUntilCd([&] { send(c, Skill::Resonance); }, [&] { return c.resonanceAvailable(); }, cap,
                        grace < 0 ? skillSettle_ : grace, poke);
}
bool TeamMacro::pressEcho(Character &c, double cap, double grace) {
    return pressUntilCd([&] { send(c, Skill::Echo); }, [&] { return c.echoAvailable(); }, cap, grace);
}
void TeamMacro::echoNow(Character &c, int presses, double window) {
    spamKey([&] { send(c, Skill::Echo); }, window, {.interval = 0, .poke = false, .presses = presses});
    record(c, Skill::Echo);
}
bool TeamMacro::openerPending(double gap) const {
    if (!armed_)
        return false;
    if (h_.macroRearmAt > lastOpener_)
        return h_.now() - h_.macroRearmAt >= h_.macroStartDelay;
    if (lastOpener_ >= 0 && h_.now() - lastOpener_ < gap)
        return false;
    return h_.useLiberation;
}
bool TeamMacro::execute(Character &entry, const std::function<void()> &body) {
    const bool previous = h_.skipCombatCheck;
    h_.skipCombatCheck = true;
    h_.macroBusy = true;
    cur_ = &entry;
    poking_ = false;
    for (auto *c : h_.characters())
        if (c)
            c->checkFOnSwitch = false;
    start();
    const auto cleanup = [&] {
        poking_ = false;
        h_.skipCombatCheck = previous;
        h_.macroBusy = false;
        release();
    };
    try {
        try {
            body();
        } catch (const MacroBroken &e) {
            h_.log("队伍连招中断，重新确认场上角色后继续：" + errorText(e));
        }
    } catch (...) {
        cleanup();
        throw;
    }
    cleanup();
    h_.log(marks_.join(' '));
    return true;
}
} // namespace okww::runtime
