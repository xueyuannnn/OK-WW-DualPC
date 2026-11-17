#pragma once
#include "reference_echo_operations.hpp"
#include "workflow_scene_replay.hpp"
#include <QCryptographicHash>
namespace okww::runtime {
struct EchoOpsContractAccess {
    template <class Operations> static QJsonObject exercise(Operations &ops, int operation, int scene) {
        QJsonObject result;
        try {
            switch (operation) {
            case 0: {
                const auto line =
                    ops.waitText(QRect(0, 0, 640, 360),
                                 "^主属性衍射伤害加成$|^凝夜白霜$|^主属性湮灭伤害加成$|^培养$", .15);
                result["text"] = line ? QJsonValue(line->json()) : QJsonValue();
                break;
            }
            case 1: {
                const auto line = ops.cultivate();
                result["text"] = line ? QJsonValue(line->json()) : QJsonValue();
                break;
            }
            case 2:
                result["value"] = ops.zeroLevel(scene & 1);
                break;
            case 3:
                ops.enterCultivate();
                break;
            case 4:
                ops.backToBag();
                break;
            case 5:
                ops.mark(scene & 1);
                break;
            case 6:
                ops.upgrade();
                break;
            case 7:
                ops.screenshot("test", 7, QStringLiteral("a<>:\"/\\|?*b_") + QString(110, QChar('x')));
                break;
            }
            result["error"] = "";
        } catch (const Cancelled &) {
            result["error"] = "cancelled";
        } catch (const std::exception &e) {
            result["error"] = errorText(e);
        }
        return result;
    }
};
namespace echo_operation_context_test {
template <class Operations> QJsonObject trace(int operation, int scene) {
    Scenes s({640, 360});
    s.profile.set("Monthly Card Config", "Check Monthly Card", false);
    const bool lock = scene & 1;
    s.frame("bag", {});
    s.frame("marked", {lock ? "echo_locked" : "echo_dropped"});
    s.frame("unmarked", {lock ? "echo_not_locked" : "echo_not_dropped"});
    s.frame("confirm", {"confirm_btn_hcenter_vcenter"});
    s.words["bag"] = {s.word("培養", .85, .89, .1)};
    int phase = scene % 5 == 0 ? 1 : 0, addClicks = 0, commits = 0;
    QJsonArray reads;
    s.textOverride = [&](QRect area, QVector<TextLine> words) {
        reads.append(QJsonArray{s.scene, area.x(), area.y(), area.width(), area.height()});
        if (operation < 3) {
            const QStringList terms{
                "培養",     "培养", "主屬性行射傷害加成", "凝夜自霜", "主属性灭伤害加成", "声骸技能",
                "聲骸技能", "other"};
            return QVector<TextLine>{s.word(terms[scene % terms.size()], .4, .4)};
        }
        if (operation == 6) {
            if (phase == 0)
                return QVector<TextLine>{s.word("阶段放入", .15, .7, .12)};
            if (phase == 1)
                return QVector<TextLine>{s.word("强化并调谐", .15, .91, .14)};
            if (phase == 2)
                return QVector<TextLine>{s.word(scene & 1 ? "不再提示" : "调谐成功", .4, .5, .2)};
            return QVector<TextLine>{};
        }
        return words;
    };
    s.action = [&](QJsonObject event) {
        if (operation == 4 && s.keyUp(event, Input::keyCode("esc")) && scene % 4 != 0)
            s.scene = "bag";
        if (operation == 5 && s.keyUp(event, Input::keyCode(lock ? "c" : "z")))
            s.scene = "marked";
        if (!s.leftUp(event))
            return;
        if (operation == 3)
            s.scene = "blank";
        if (operation == 6) {
            const auto commitPoint = gamePoint(s.size, .22, .925);
            if ((s.pointer - commitPoint).manhattanLength() < 8)
                ++commits;
            if (phase == 0) {
                if (++addClicks >= 1 + scene % 3)
                    phase = 1;
            } else if (phase == 1) {
                phase = 2;
                s.scene = (scene & 1) && scene % 3 != 0 ? "confirm" : "blank";
            } else if (phase == 2 && scene % 4 != 0) {
                phase = 3;
                s.scene = "blank";
            }
        }
    };
    s.show(operation == 3 && (scene & 1) ? "bag"
           : operation == 5              ? scene % 7 == 0   ? "blank"
                                           : scene % 3 == 0 ? "marked"
                                                            : "unmarked"
                                         : "blank");
    const auto screenshots = s.directory.path() + "/screenshots";
    if (operation == 7 && (scene & 1)) {
        QFile file(screenshots);
        if (!file.open(QIODevice::WriteOnly))
            fail("test setup failed");
        file.write("not a directory");
    }
    Operations ops(s.c, screenshots);
    auto out = EchoOpsContractAccess::exercise(ops, operation, scene);
    QJsonArray files;
    for (const auto &name : QDir(screenshots + "/test").entryList(QDir::Files, QDir::Name)) {
        QFile file(screenshots + "/test/" + name);
        if (!file.open(QIODevice::ReadOnly))
            fail("test artifact unreadable");
        files.append(QJsonObject{
            {"name", name.mid(19)},
            {"hash", QString::fromLatin1(
                         QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex())}});
    }
    QJsonArray events;
    for (auto event : s.events)
        events.append(event);
    out["events"] = events;
    out["reads"] = reads;
    out["files"] = files;
    out["time"] = s.clock.time;
    out["phase"] = phase;
    out["commits"] = commits;
    out["adds"] = addClicks;
    out["idle"] = s.input.idle();
    return out;
}
} // namespace echo_operation_context_test
} // namespace okww::runtime
