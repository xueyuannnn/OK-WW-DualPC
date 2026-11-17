#pragma once
#include "reference_daily.hpp"
#include "workflow_scene_replay.hpp"
namespace okww::runtime {
struct DailyContractAccess {
    template <class DailyType> static QJsonObject exercise(DailyType &daily, int operation, int scene) {
        daily.config_ = daily.c_.profile.group("DailyTask2");
        daily.claimedAtPoints_ = (scene % 7 - 1) * 20;
        if (operation == 5)
            daily.noProgress_ = (scene % 5 == 0);
        QJsonObject result;
        const auto store = [&](const auto &p) {
            result["points"] = p.points;
            result["stamina"] = p.stamina ? QJsonValue(*p.stamina) : QJsonValue();
            result["echo"] = p.echoNeeded ? QJsonValue(*p.echoNeeded) : QJsonValue();
        };
        try {
            switch (operation) {
            case 0:
                store(daily.open(scene & 1));
                break;
            case 1:
                store(daily.claim());
                break;
            case 2:
                daily.mail();
                break;
            case 3:
                daily.battlePass();
                break;
            case 4:
                daily.validate();
                break;
            case 5:
                daily.completeStamina();
                break;
            case 6:
                daily.farmCount(DomainKind(scene % 3));
                break;
            case 7:
                daily.run(scene & 1);
                break;
            }
            result["error"] = "";
        } catch (const Cancelled &) {
            result["error"] = "cancelled";
        } catch (const std::exception &e) {
            result["error"] = errorText(e);
        }
        result["observation"] = daily.dailyObservation_;
        result["no_progress"] = daily.noProgress_;
        result["claimed"] = daily.claimedAtPoints_;
        result["spent"] = QJsonObject::fromVariantMap([&] {
            QVariantMap map;
            for (auto i = daily.dailySpent_.cbegin(); i != daily.dailySpent_.cend(); ++i)
                map[i.key()] = i.value();
            return map;
        }());
        result["extras_done"] = daily.extrasDone_;
        result["extras_total"] = daily.extrasTotal_;
        return result;
    }
};
namespace daily_actions_test {
inline EchoDetector &detector() {
    static EchoDetector value(QString(OKWW_SOURCE_ROOT) + "/assets/echo_model/echo.onnx");
    return value;
}
template <class DailyType> QJsonObject trace(int operation, int scene) {
    Scenes s({640, 360});
    s.profile.set("Monthly Card Config", "Check Monthly Card", false);
    s.profile.set("Task Recovery", "Recovery Retry Limit", 1);
    s.profile.set("DailyTask2", "Use Stamina Cubes for Daily", false);
    s.profile.set("DailyTask2", "Farm Nightmare Nest for Daily Echo", bool(scene & 2));
    s.profile.set("DailyTask2", "Additional Tasks to Run After Daily Task", QJsonArray{});
    for (auto key : {"Tacet Suppression Count", "Forgery Challenge Count", "Simulation Challenge Count"})
        s.profile.set("DailyTask2", key, 0);
    if (operation == 4) {
        QJsonArray selected;
        if (scene & 1)
            selected.append("Teleport and Farm 4C Echo");
        if (scene & 2)
            selected.append("4C Echo and Advanced Skill Material");
        if (scene & 4)
            selected.append("Auto Farm all Nightmare Nest");
        s.profile.set("DailyTask2", "Additional Tasks to Run After Daily Task", selected);
        if (scene & 8)
            s.profile.set("FarmEchoTask", "Teleport to Boss", "Boss Challenge");
        if (scene & 16)
            s.profile.set("FarmEchoTask2", "Teleport to Boss", "Boss Challenge");
        s.profile.set("NightmareNestTask", "Which to Farm",
                      scene & 32 ? QJsonArray{"Tacet Discord Nest"} : QJsonArray{});
    }
    s.frame("book", {"gray_book_quest"});
    s.frame("pass", {});
    s.frame("menu", {});
    s.frame("mail", {});
    int points = operation == 7 || operation == 5 ? 100 : (scene % 6) * 20;
    int claimed = 0, readings = 0, scroll = 0, rewardPopups = 0, claims = 0;
    const auto update = [&] {
        QVector<TextLine> words{s.word("活跃度", .15, .12), s.word("活跃度", .195, .91),
                                s.word(QString::number(points), .2, .85, .035)};
        if (scene & 4 || operation == 7 || operation == 5)
            words.append(s.word("180/180", .2, .4));
        if (scene & 8)
            words.append(s.word("获得1个声骸0/1", .2, .5, .25));
        if (operation == 0 && scene & 16 && !claimed)
            words.append(s.word("领取", .84, .4, .08));
        if (operation == 0 && scene & 32)
            words.removeAt(2);
        if (operation == 0 && scene % 13 == 0)
            words[1].text = "游历值";
        s.words["book"] = words;
        s.words["pass"] = {s.word("50", .23, .16, .03)};
    };
    update();
    QJsonArray notes, diagnostics, reads, flows;
    s.c.info = [&](QString k, QString v) { notes.append(QJsonArray{k, v}); };
    s.c.notify = [&](QString k, QString v, bool error) { notes.append(QJsonArray{k, v, error}); };
    s.c.diagnostic = [&](QString reason, QJsonObject detail) {
        diagnostics.append(QJsonArray{reason, detail});
    };
    s.c.progress = [&](QJsonObject patch) {
        patch.remove("flow_observed_at");
        patch.remove("observed_at");
        flows.append(patch);
    };
    s.textOverride = [&](QRect area, QVector<TextLine> words) {
        reads.append(QJsonArray{s.scene, area.x(), area.y(), area.width(), area.height()});
        if (operation == 0 && scene % 11 == 0 && s.scene == "book" && area.y() > 280 && ++readings < 3)
            words.erase(std::remove_if(words.begin(), words.end(),
                                       [](auto line) { return line.text == "0" || line.text == "100"; }),
                        words.end());
        if (operation == 3 && scene % 5 == 0 && s.scene == "pass")
            return QVector<TextLine>{};
        return words;
    };
    s.action = [&](QJsonObject event) {
        if (s.keyUp(event, Input::keyCode("esc")))
            s.scene = s.scene == "world" ? "menu" : "world";
        if (s.keyUp(event, Input::keyCode("f2")))
            s.scene = "book";
        if (event["op"] == "scroll") {
            scroll += event["steps"].toInt();
            update();
        }
        if (!s.leftUp(event))
            return;
        const auto pointNear = [&](double x, double y) {
            return (s.pointer - gamePoint(s.size, x, y)).manhattanLength() < 8;
        };
        if (s.scene == "world" && pointNear(.86, .05)) {
            if (operation == 3 && scene % 7 == 0)
                throw std::runtime_error("input failed");
            s.scene = "pass";
        } else if (s.scene == "menu" && pointNear(.64, .95))
            s.scene = "mail";
        else if (s.scene == "book") {
            if (operation == 0 && s.pointer.x() > .8 * s.size.width() &&
                s.pointer.y() < .79 * s.size.height()) {
                ++claims;
                if (!(scene & 64)) {
                    ++claimed;
                    points = std::min(100, points + 20);
                    update();
                }
            }
            if (operation == 1 && pointNear(.5, .175)) {
                rewardPopups = 0;
            } else if (operation == 1 && s.pointer.y() > .86 * s.size.height())
                ++rewardPopups;
        }
    };
    s.show(operation == 2 || operation == 3 || operation == 7 ? "world" : "book");
    s.c.task = "DailyTask2";
    CombatSession combat(s.c, s.root, s.directory.path());
    DailyType daily(s.c, combat, detector(), s.root, s.directory.path());
    auto out = DailyContractAccess::exercise(daily, operation, scene);
    // Timing history uses wall clocks. Compare the logical flow fields only.
    QJsonArray events;
    for (auto e : s.events)
        events.append(e);
    out["events"] = events;
    out["flows"] = flows;
    out["reads"] = reads;
    out["notes"] = notes;
    out["diagnostics"] = diagnostics;
    out["time"] = s.clock.time;
    out["scene"] = s.scene;
    out["idle"] = s.input.idle();
    out["claims"] = claims;
    out["scroll"] = scroll;
    return out;
}
} // namespace daily_actions_test
} // namespace okww::runtime
