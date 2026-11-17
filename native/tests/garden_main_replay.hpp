#pragma once
#include "reference_garden_main.hpp"
#include "workflow_scene_replay.hpp"
namespace okww::runtime::garden_main_test {
template <class GardenType> QJsonObject trace(int scene) {
    Scenes s;
    s.profile.set("Monthly Card Config", "Check Monthly Card", false);
    s.profile.set("Task Recovery", "Recovery Retry Limit", 1);
    s.frame("day", {"garden_next_day"});
    s.frame("blessing", {});
    s.frame("finish", {"a_garden_back", "a_garden_restart"});
    s.frame("book", {"gray_book_quest"});
    s.words["book"] = {s.word("周度游历", .3, .12), s.word("本周游历值已满", .1, .8, .22)};
    int day = 1, speed = 1 + scene % 3, opened = scene & 4 ? 1 : 0, used = 0, claims = 0, advanced = 0;
    const int days = scene % 3 + 1;
    QJsonArray notes, reads;
    const auto update = [&] {
        const auto stage =
            s.word(scene & 1 ? QString("Stage 1/7 Day %1").arg(day) : QString("阶段1/7 第%1天").arg(day), .03,
                   .11, .18);
        s.words["day"] = {stage, s.word(speed < 3 ? QString("x%1").arg(speed) : "MAX", .64, .04, .04)};
        s.words["blessing"] = {
            stage, s.word(scene & 1 ? "use blessing" : "使用祝福", .07, .72, .10),
            s.word(scene & 1 ? QString("remaining uses: %1").arg(day == 1 && !(scene & 2) ? 1 : 0)
                             : QString("剩余次数：%1").arg(day == 1 && !(scene & 2) ? 1 : 0),
                   .07, .77, .10)};
    };
    s.action = [&](QJsonObject event) {
        if (s.keyUp(event, Input::keyCode("f2"))) {
            s.scene = "book";
            return;
        }
        if (!s.leftUp(event))
            return;
        const auto nearPoint = [&](double x, double y) {
            return (s.pointer - gamePoint(s.size, x, y)).manhattanLength() < 12;
        };
        if (s.scene == "day") {
            if (s.pointer.y() < .1 * s.size.height())
                ++speed;
            else if (nearPoint(.18, .785)) {
                ++opened;
                s.scene = "blessing";
            } else if (s.templates.box("garden_next_day", s.size).contains(s.pointer)) {
                if (speed != 3 || opened != day)
                    fail("fixture advanced before speed/skill check");
                ++advanced;
                if (++day > days)
                    s.scene = "finish";
            } else
                fail("fixture unexpected weekly click");
        } else if (s.scene == "blessing") {
            if (day == 1 && !(scene & 2) && s.pointer.y() < .77 * s.size.height()) {
                ++used;
                s.scene = "day";
            } else if (nearPoint(.5, .8))
                s.scene = "day";
            else
                fail("fixture unexpected blessing click");
        } else if (s.scene == "finish") {
            if (!s.templates.box("a_garden_back", s.size).contains(s.pointer))
                fail("fixture restarted a completed weekly round");
            s.scene = "world";
        } else if (s.scene == "book") {
            if (nearPoint(.246, .486))
                fail("fixture restarted after reaching weekly cap");
            if (nearPoint(.927, .893))
                ++claims;
        }
        update();
    };
    s.textOverride = [&](QRect area, QVector<TextLine> lines) {
        QJsonArray words;
        for (const auto &line : lines)
            words.append(line.json());
        reads.append(QJsonObject{{"scene", s.scene},
                                 {"area", QJsonArray{area.x(), area.y(), area.width(), area.height()}},
                                 {"words", words},
                                 {"time", s.clock.time}});
        return lines;
    };
    s.c.info = [&](QString key, QString value) {
        notes.append(QJsonObject{{"key", key}, {"value", value}, {"time", s.clock.time}});
    };
    s.c.diagnostic = [&](QString reason, QJsonObject detail) {
        notes.append(QJsonObject{{"reason", reason}, {"detail", detail}});
    };
    update();
    s.show(scene & 8 ? "world" : scene & 4 ? "blessing" : "day");
    QJsonObject result;
    try {
        GardenType(s.c, s.root).run();
        result["error"] = "";
    } catch (const std::exception &error) {
        result["error"] = QString::fromUtf8(error.what());
    }
    result["speed"] = speed;
    result["opened"] = opened;
    result["used"] = used;
    result["claims"] = claims;
    result["advanced"] = advanced;
    result["scene"] = s.scene;
    result["notes"] = notes;
    result["reads"] = reads;
    result["time"] = s.clock.time;
    QJsonArray events;
    for (const auto &event : s.events)
        events.append(event);
    result["events"] = events;
    result["idle"] = s.input.idle();
    return result;
}
} // namespace okww::runtime::garden_main_test
