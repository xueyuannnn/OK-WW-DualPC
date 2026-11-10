#include "runtime/combat_rules.hpp"
#include "runtime/combat_vision.hpp"
#include "runtime/gauge_analysis.hpp"
#include <iostream>
using namespace okww;
using namespace okww::runtime;
static CharacterState character(QJsonObject o) {
    CharacterState c;
    c.index = o["index"].toInt();
    c.role = Role(o["role"].toInt());
    c.buffTime = o["buff_time"].toDouble();
    c.lastBuff = o["last_buff_time"].toDouble();
    c.lastSwitch = o["last_switch_time"].toDouble();
    c.lastSwitchIn = o["last_switch_in_time"].toDouble();
    c.lastFullConSwitch = o["last_full_con_switch_time"].toDouble();
    c.currentCon = o["current_con"].toDouble();
    return c;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser p;
    p.addHelpOption();
    p.addOptions({{"cases", "Differential inputs", "path"}, {"report", "Native results", "path"}});
    p.process(app);
    try {
        QJsonArray output;
        for (auto value : readJson(p.value("cases"))["cases"].toArray()) {
            auto o = value.toObject();
            const double now = o["now"].toDouble();
            FreezeTimeline timeline;
            for (auto entry : o["freezes"].toArray()) {
                auto f = entry.toArray();
                timeline.add(f[0].toDouble(), f[1].toDouble(), f[2].toDouble(), f[3].toDouble());
            }
            const auto kind = o["kind"].toString();
            QJsonObject result;
            if (kind == "switch") {
                QVector<CharacterState> chars;
                for (auto ch : o["chars"].toArray())
                    chars.append(character(ch.toObject()));
                std::vector<SwitchCandidate> candidates;
                const auto priorities = o["priorities"].toArray();
                for (int i = 0; i < chars.size(); ++i)
                    candidates.push_back({&chars[i], priorities[i].toInt()});
                auto chosen =
                    chooseSwitch(chars[o["current"].toInt()], candidates, o["intro"].toBool(), timeline, now);
                result["index"] = chosen.index;
                result["reason"] = chosen.reason;
            } else if (kind == "freeze") {
                result["elapsed"] = timeline.elapsed(now, o["start"].toDouble(), o["intro"].toBool());
                result["count"] = timeline.entries().size();
            } else if (kind == "switch_out") {
                auto c = character(o["char"].toObject());
                c.switchOut(o["full"].toBool(), now);
                result = {{"last_switch_time", c.lastSwitch},
                          {"last_buff_time", c.lastBuff},
                          {"last_full_con_switch_time", c.lastFullConSwitch},
                          {"current_con", c.currentCon}};
            } else if (kind == "ring") {
                auto image = readImage(o["image"].toString());
                auto r = o["box"].toArray();
                if (!r.isEmpty())
                    image = image(cv::Rect(r[0].toInt(), r[1].toInt(), r[2].toInt(), r[3].toInt()));
                auto ring =
                    countRing(image, concertoColors().at(o["element"].toInt()), o["min_area"].toDouble());
                result = {{"area", ring.area}, {"full", ring.full}};
            } else if (kind == "gauge") {
                const auto image = readImage(o["image"].toString());
                const auto lower = o["lower"].toArray(), upper = o["upper"].toArray();
                const ColorRange color{{lower[0].toDouble(), lower[1].toDouble(), lower[2].toDouble()},
                                       {upper[0].toDouble(), upper[1].toDouble(), upper[2].toDouble()}};
                const auto algorithm = o["algorithm"].toString();
                if (algorithm == "ring")
                    result["value"] = gaugeRingRatio(image, color, o["inner"].toDouble(),
                                                     o["outer"].toDouble(), o["nonzero"].toBool());
                else if (algorithm == "stripe") {
                    result["value"] = stripedGaugeRatio(image, color);
                } else if (algorithm == "phoebe")
                    result["value"] =
                        phoebeGaugeValue(image, color, o["segments"].toInt(), o["min_freq"].toInt(),
                                         o["max_freq"].toInt(), o["min_amp"].toDouble());
                else if (algorithm == "segment")
                    result["value"] =
                        segmentedGaugeValue(image, color, o["segments"].toInt(), o["min_freq"].toInt(),
                                            o["max_freq"].toInt(), o["min_amp"].toDouble());
                else
                    fail("Unknown gauge algorithm");
            } else
                fail("Unknown differential case");
            output.append(result);
        }
        writeJson(p.value("report"), {{"results", output}, {"hardware_input_sent", false}});
        std::cout << output.size() << " native rule cases evaluated\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
