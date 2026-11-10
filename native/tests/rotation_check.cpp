#include "rotation_replay.hpp"
#include <iostream>
using namespace okww;
using namespace okww::runtime;
using namespace okww::runtime::test;
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser p;
    p.addHelpOption();
    p.addOptions(
        {{"cases", "Saved observation scenarios", "path"}, {"report", "JSON action traces", "path"}});
    p.process(app);
    try {
        QJsonArray results;
        for (auto value : readJson(p.value("cases"))["cases"].toArray()) {
            auto input = value.toObject();
            RotationReplay host(input);
            CharacterState state;
            state.index = 0;
            state.name = input["class"].toString();
            state.current = true;
            state.hasIntro = input["intro"].toBool();
            state.hasSubDpsIntro = input["sub_intro"].toBool();
            state.ringIndex = input["element"].toInt();
            auto actor = makeCharacter(state.name, host, state);
            host.character = actor.get();
            QString error;
            try {
                auto steps = input["steps"].toArray();
                if (steps.isEmpty())
                    steps.append(input);
                for (auto value : steps) {
                    const auto step = value.toObject();
                    if (step["switch_out"].toBool())
                        actor->switchOut();
                    host.sleep(step["delay"].toDouble());
                    host.observe(step);
                    if (step.contains("intro"))
                        actor->state.hasIntro = step["intro"].toBool();
                    if (step.contains("sub_intro"))
                        actor->state.hasSubDpsIntro = step["sub_intro"].toBool();
                    if (step.contains("con"))
                        actor->state.currentCon = 0;
                    actor->state.current = true;
                    if (auto other = host.teammate())
                        other->state.current = false;
                    const auto action = step["action"].toString();
                    if (action == "heavy")
                        actor->heavyAttack(1);
                    else if (action == "liberation")
                        actor->liberation();
                    else if (action == "switch")
                        actor->switchNext(step["free_intro"].toBool());
                    else if (action == "dodge") {
                        const double end = host.now() + step["condition_duration"].toDouble(1);
                        host.event("dodge", actor->autoDodge([&] { return host.now() < end; }) ? "1" : "0");
                    } else if (action == "priority") {
                        host.event("priority", QString::number(actor->switchPriority(
                                                   host.teammate(), step["intro"].toBool(), false)));
                    } else if (action == "toolbox") {
                        host.event("toolbox",
                                   host.teammate() && host.teammate()->state.hasToolBox ? "1" : "0");
                    } else
                        actor->perform();
                }
            } catch (const CombatEnded &e) {
                error = errorText(e);
            }
            results.append(QJsonObject{{"events", host.events},
                                       {"duration", host.now() - 100},
                                       {"error", error},
                                       {"held", host.held},
                                       {"held_keys", QJsonArray::fromStringList(QStringList(
                                                         host.heldKeys.begin(), host.heldKeys.end()))},
                                       {"liberation", host.inLiberation}});
        }
        writeJson(p.value("report"), {{"results", results}, {"hardware_input_sent", false}});
        std::cout << results.size() << " native rotation scenarios evaluated\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
