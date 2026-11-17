#include "runtime/combat_rules.hpp"
#include "runtime/daily.hpp"
#include "runtime/garden.hpp"
#include "runtime/instance_flow.hpp"
#include <QCommandLineParser>
#include <iostream>
using namespace okww;
using namespace okww::runtime;

// Generate the same observations on both sides of the language migration.
// This executable has no Backend or device handle and sends no input.
static QJsonObject contract() {
    QRandomGenerator random(0x4f4b5757);
    auto number = [&](int bound) { return int(random.bounded(quint32(bound))); };
    QJsonArray rules, stamina, cards, shops, restarts, rounding, keys;
    for (int sample = 0; sample < 12000; ++sample) {
        const double now = 100 + number(10000) / 100.;
        FreezeTimeline time;
        for (int i = 0, count = number(12); i < count; ++i) {
            const auto start = now - number(9000) / 100.;
            time.add(now, start, number(300) / 100., number(6) == 0 ? -100 : .1);
        }
        std::array<CharacterState, 3> party;
        std::array<SwitchCandidate, 3> candidates;
        for (int i = 0; i < 3; ++i) {
            auto &c = party[i];
            c.index = i; c.role = Role(number(4));
            c.buffTime = number(4) == 0 ? 0 : 10 + number(20);
            c.lastBuff = number(5) == 0 ? -1 : now - number(4000) / 100.;
            c.lastSwitch = number(5) == 0 ? -1 : now - number(1000) / 100.;
            c.lastSwitchIn = now - number(3000) / 100.;
            c.lastFullConSwitch = number(4) == 0 ? -1 : now - number(3000) / 100.;
            c.currentCon = number(3) / 2.;
            c.current = c.hasIntro = c.hasSubDpsIntro = true;
            candidates[i] = {&c, number(6) * 100};
        }
        const auto current = number(3);
        const bool intro = number(2);
        const auto choice = chooseSwitch(party[current], candidates, intro, time, now);
        auto state = party[current];
        const bool buff = state.hasBuff(time, now), locked = state.healerLocked(time, now),
                   cooling = state.switchCoolingDown(time, now);
        const auto remaining = state.buffRemaining(time, now);
        state.switchOut(number(2), now);
        applyIntro(party[current], state, intro);
        rules.append(QJsonArray{choice.index, choice.reason, time.elapsed(now, now - 10, intro),
            time.entries().size(), buff, remaining, locked, cooling,
            state.lastSwitch, state.lastBuff, state.lastFullConSwitch, state.currentCon,
            state.current, state.hasIntro, state.hasSubDpsIntro});

        const int cost = 20 + number(11) * 20, currentStamina = number(241), reserve = number(241),
                  quota = number(600), progress = number(181);
        const bool exact = number(2), twice = number(2);
        auto spend = planStaminaSpend({currentStamina, reserve, currentStamina + reserve},cost,quota,exact,twice);
        stamina.append(QJsonArray{spend.used, spend.repeat,
            dailyStaminaBatch(progress,cost,currentStamina + reserve,twice)});

        QVector<GardenCard> options;
        QVector<GardenItem> items;
        for (int i = 0, count = number(9); i < count; ++i) {
            const bool recommended = number(2);
            options.append({{"card", {number(1600),0,30,40}, 1},number(4),recommended,bool(number(3)==0)});
            items.append({number(20), number(4)==0 ? std::nullopt : std::optional<int>(number(40)), recommended,{}});
        }
        cards.append(selectGardenCard(options));
        QJsonArray ordered;
        for (const auto &item : affordableGardenItems(items,number(40)))
            ordered.append(QJsonArray{item.index,item.price ? QJsonValue(*item.price) : QJsonValue(),item.recommended});
        shops.append(ordered);

        RestartTracker tracker;
        QJsonArray actions;
        for (int i=0;i<20;++i)
            actions.append(int(tracker.step(RestartKind(number(4)),bool(number(2)),bool(number(2)))));
        restarts.append(actions);
        const double pixel = (number(200000)-100000)*.5;
        rounding.append(pixelRound(pixel));
    }
    for (const auto &key : {"A","z","1","9","0","-","=","[","]","\\",";","'","`",",",".","/",
         "enter","return","esc","escape","backspace","tab","space","capslock","insert","home",
         "pageup","page_up","delete","end","pagedown","page_down","right","left","down","up",
         "ctrl","lctrl","control","lcontrol","ctrl_l","rctrl","rcontrol","ctrl_r","shift","lshift",
         "shift_l","rshift","shift_r","alt","lalt","alt_l","ralt","alt_r","win","cmd","f1","F12"})
        keys.append(Input::keyCode(key));
    return {{"rules",rules},{"stamina",stamina},{"cards",cards},{"shops",shops},{"restarts",restarts},
            {"rounding",rounding},{"keys",keys},{"hardware_input_sent",false}};
}
static bool equivalent(const QJsonValue &a,const QJsonValue &b) {
    if(a.type()!=b.type()) return false;
    if(a.isDouble()) return std::abs(a.toDouble()-b.toDouble())<=1e-9;
    if(a.isArray()) {
        auto x=a.toArray(),y=b.toArray();
        if(x.size()!=y.size()) return false;
        for(int i=0;i<x.size();++i) if(!equivalent(x[i],y[i])) return false;
        return true;
    }
    if(a.isObject()) {
        auto x=a.toObject(),y=b.toObject();
        if(x.keys()!=y.keys())return false;
        for(auto it=x.begin();it!=x.end();++it) if(!equivalent(it.value(),y[it.key()]))return false;
        return true;
    }
    return a==b;
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    QCommandLineParser parser;
    parser.addOptions({{"report","Write observed results","path"},{"reference","Compare original C++ results","path"}});
    parser.process(app);
    try {
        const auto results=contract();
        if(parser.isSet("report")) writeJson(parser.value("report"),results);
        if(parser.isSet("reference")) {
            const auto reference=readJson(parser.value("reference"));
            for(auto it=results.begin();it!=results.end();++it)
                if(!equivalent(it.value(),reference[it.key()]))fail("Behavior differs: "+it.key());
        }
        std::cout << "12000 deterministic scenarios per rule group; no device input\n";
        return 0;
    } catch(const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
