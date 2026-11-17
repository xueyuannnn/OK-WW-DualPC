#pragma once
#include "reference_team_macro.hpp"
#include "team_replay.hpp"
namespace okww::runtime::macro_test {
struct Host final : test::TeamReplay {
    int scene;
    mutable int ordinal = 0;
    mutable QJsonArray calls;
    bool injecting = false;
    explicit Host(int scene)
        : TeamReplay({{"entry", scene % 3},
                      {"con", scene % 2 ? .9 : 1.},
                      {"partial_hud", scene % 5 == 0},
                      {"switch_fail", scene % 7 == 0},
                      {"animation", scene % 3 != 0},
                      {"ready", scene % 2 == 0},
                      {"cancel_at", scene % 17 == 0 ? 100.15 : 10000.}}),
          scene(scene) {}
    void point(QString op) const {
        calls.append(QJsonObject{{"op", op}, {"at", TeamReplay::now()}});
        if (injecting && scene % 11 == 0 && ++ordinal == scene % 23 + 1)
            throw std::runtime_error("macro-sdk-failure");
    }
    double now() const override {
        point("now");
        return TeamReplay::now();
    }
    void preciseWait(double seconds) override {
        point("wait");
        TeamReplay::preciseWait(seconds);
    }
    void nextFrame() override {
        point("next");
        TeamReplay::nextFrame();
    }
    void refreshObservation() override {
        point("refresh");
        TeamReplay::refreshObservation();
    }
    bool inTeam() override {
        point("hud");
        return TeamReplay::inTeam();
    }
    int partyIndex() override {
        point("party-index");
        return TeamReplay::partyIndex();
    }
    int partyCount() override {
        point("party-count");
        return scene % 29 == 0 ? -1 : TeamReplay::partyCount();
    }
    QVector<Character *> characters() override {
        point("characters");
        auto actors = TeamReplay::characters();
        if (scene % 31 == 0)
            actors[2] = nullptr;
        return actors;
    }
    double concerto(Character &actor) override {
        point("concerto");
        return TeamReplay::concerto(actor);
    }
    void rawTap(const QString &key, double hold) override {
        point("key");
        TeamReplay::rawTap(key, hold);
    }
    void rawClick(const QString &button, double hold) override {
        point("click");
        TeamReplay::rawClick(button, hold);
    }
    void mouseDown(QString key = "left") override {
        point("down");
        TeamReplay::mouseDown(key);
    }
    void mouseUp(QString key = "left") override {
        point("up");
        if (injecting && scene % 37 == 0)
            throw std::runtime_error("macro-release-failure");
        TeamReplay::mouseUp(key);
    }
    void log(const QString &text) override {
        point("log");
        calls.append(QJsonObject{{"text", text}});
    }
};
template <class T> struct Macro final : T {
    explicit Macro(CombatHost &host) : T(host) {}
    bool perform(Character &) override { return false; }
    QJsonObject state() const {
        return {{"current", this->cur_ ? this->cur_->state.index : -1},
                {"holding", this->holding_},
                {"poking", this->poking_},
                {"armed", this->armed_},
                {"opener", this->lastOpener_},
                {"start", this->macroStart_},
                {"switch", this->switchPress_},
                {"marks", QJsonArray::fromStringList(this->marks_)}};
    }
    void prepare(int scene, Character &entry) {
        this->cur_ = scene % 3 ? &entry : nullptr;
        this->poking_ = scene % 2;
        this->armed_ = scene % 5 != 0;
        this->lastOpener_ = scene % 7 ? 95 : -1;
        this->switchKeyInterval_ = scene % 2 ? .05 : 0;
        this->untrustedConfirm_ = scene % 4;
        this->h_.macroRearmAt = scene % 3 ? 98 : -1;
        this->h_.macroStartDelay = scene % 4;
    }
    double run(int method, int scene, Character &entry, Character &next) {
        const double window = scene % 13 == 0 ? -1 : .1 + (scene % 4) * .07;
        int sends = 0, samples = 0;
        switch (method) {
        case 0:
            this->wait(window);
            return 0;
        case 1:
            this->record(entry, Skill(scene % 5));
            return 0;
        case 2:
            this->hold();
            this->release();
            return 0;
        case 3:
            this->start();
            this->mark("开场");
            this->wait(.025);
            this->mark("技能");
            return 0;
        case 4:
            this->need(scene % 3 != 0, "test-broken");
            return 0;
        case 5:
            return this->spam(
                window,
                scene % 7 == 0 ? std::numeric_limits<double>::quiet_NaN()
                : scene % 3    ? -1
                               : .03,
                scene % 5 - 2,
                [&] {
                    ++samples;
                    return samples % 2;
                },
                scene % 6);
        case 6: {
            MacroKeySpam options;
            options.interval = scene % 5 == 0 ? -1 : .02;
            options.clickInterval = .03;
            options.presses = scene % 7 - 2;
            if (scene % 3)
                options.poke = scene % 2 != 0;
            if (scene % 11 == 0)
                options.stop = [&] { return sends >= 2; };
            return this->spamKey(
                [&] {
                    ++sends;
                    this->key("e");
                },
                window, options);
        }
        case 7:
            return this->gap(window);
        case 8: {
            auto found = this->teammate(QStringList{"lucy", "REBECCA", "ShoreKeeper", "missing"}[scene % 4]);
            return found ? found->state.index : -1;
        }
        case 9:
            this->handoff(entry, next, scene % 2);
            return 0;
        case 10: {
            MacroSwitch options;
            options.click = scene % 2;
            options.clickDuring = scene % 3 ? std::optional<bool>(scene % 2) : std::nullopt;
            options.settle = .02;
            options.timeout = scene % 5 == 0 ? -.1 : .2;
            options.readCon = scene % 3 == 0;
            options.assumeCon = scene % 7 == 0;
            options.clickInterval = .03;
            options.clickFirst = scene % 4 - 1;
            return this->quickSwitch(scene % 13 == 0 ? nullptr : scene % 11 == 0 ? &entry : &next, options);
        }
        case 11: {
            auto found = this->onField(window);
            return found ? found->state.index : -1;
        }
        case 12:
            return this->waitHud(scene % 2, window, scene % 3 ? .03 : 0);
        case 13:
            return this->waitHudUp(window);
        case 14:
            return this->conAtLeast(entry, scene % 3 == 0 ? .5 : 1., window, .02, scene % 2);
        case 15:
            return this->pressUntilCd(
                [&] {
                    ++sends;
                    this->send(entry, Skill::Resonance);
                },
                [&] { return entry.resonanceAvailable(); }, window, scene % 3 ? .03 : -1, scene % 2);
        case 16:
            return this->pressRes(entry, window, scene % 2 ? -.1 : .03, scene % 2);
        case 17:
            return this->pressEcho(entry, window, .03);
        case 18:
            this->echoNow(entry, scene % 4 - 1, window);
            return 0;
        case 19:
            return this->openerPending(scene % 3 ? .1 : 60);
        case 20:
            return this->execute(entry, [&] {
                this->hold();
                this->mark("body");
                this->click();
                if (scene % 3 == 0)
                    throw MacroBroken("body-broken");
                if (scene % 3 == 1)
                    throw std::runtime_error("body-failure");
                this->release();
            });
        case 21:
            this->reset();
            return 0;
        case 22:
            this->hold();
            this->combatEnd();
            return 0;
        }
        return -1;
    }
};
template <class T> QJsonObject replay(int method, int scene) {
    Host host(scene);
    const auto party = host.test::TeamReplay::characters();
    auto &entry = *party[scene % 3], &next = *party[(scene + 1) % 3];
    entry.state.role = scene % 2 ? Role::SubDps : Role::MainDps;
    QJsonObject result;
    {
        Macro<T> macro(host);
        macro.prepare(scene, entry);
        host.injecting = true;
        try {
            result["result"] = macro.run(method, scene, entry, next);
        } catch (const std::exception &e) {
            result["error"] = errorText(e);
            result["type"] = typeid(e).name();
        } catch (...) {
            result["error"] = "unknown";
        }
        result["macro"] = macro.state();
    }
    QJsonArray actors;
    for (auto *c : party) {
        const auto &s = c->state;
        actors.append(QJsonObject{{"index", s.index},
                                  {"current", s.current},
                                  {"intro", s.hasIntro},
                                  {"sub_intro", s.hasSubDpsIntro},
                                  {"switch_in", s.lastSwitchIn},
                                  {"outro", s.lastOutro},
                                  {"resonance", s.lastResonance},
                                  {"echo", s.lastEcho},
                                  {"liberation", s.lastLiberation},
                                  {"switch", s.lastSwitch},
                                  {"full_switch", s.lastFullConSwitch},
                                  {"check_f", c->checkFOnSwitch}});
    }
    QJsonArray freezes;
    for (const auto &f : host.freezes.entries())
        freezes.append(QJsonArray{f.start, f.duration, f.threshold});
    result["actors"] = actors;
    result["calls"] = host.calls;
    result["events"] = host.events;
    result["freezes"] = freezes;
    result["held"] = QJsonArray::fromStringList(QStringList(host.held.begin(), host.held.end()));
    result["time"] = host.test::TeamReplay::now();
    result["skip"] = host.skipCombatCheck;
    result["busy"] = host.macroBusy;
    return result;
}
} // namespace okww::runtime::macro_test
