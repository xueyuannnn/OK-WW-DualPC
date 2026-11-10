#include "team_replay.hpp"
#include "../personal/teams.hpp"
#include "../personal/hsin.hpp"
using namespace okww;
using namespace okww::runtime;
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 3)
        return 2;
    try {
        QJsonArray results;
        for (auto value : readJson(app.arguments()[1])["cases"].toArray()) {
            auto config = value.toObject();
            test::TeamReplay host(config);
            if (config["vision_probe"].toBool()) {
                personal::HsinTeam probe(host);
                results.append(probe.visionProbe());
                continue;
            }
            auto macro = config["team"] == "hsin"      ? personal::hsinTeam(host)
                         : config["team"] == "aemeath" ? personal::aemeathTeam(host)
                         : config["team"] == "cart"    ? personal::cartethyiaTeam(host)
                         : config["team"] == "denia"   ? personal::deniaTeam(host)
                                                       : personal::lucyTeam(host);
            QString error;
            try {
                for (int i = 0; i < config["rounds"].toInt(1); ++i) {
                    if (i == 1 && config["reset_after_first"].toBool())
                        macro->reset();
                    macro->perform(host.entry());
                }
            } catch (const Cancelled &) {
                error = "cancelled";
            } catch (const std::exception &e) {
                error = errorText(e);
            }
            macro.reset();
            results.append(QJsonObject{{"events", host.events},
                                       {"duration", host.now() - 100},
                                       {"error", error},
                                       {"held", !host.held.empty()},
                                       {"busy", host.macroBusy},
                                       {"skipping", host.skipCombatCheck},
                                       {"index", host.partyIndex()}});
        }
        writeJson(app.arguments()[2], {{"results", results}, {"hardware_input_sent", false}});
        return 0;
    } catch (...) {
        return 1;
    }
}
