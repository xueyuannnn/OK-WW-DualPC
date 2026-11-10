#include "instance_flow.hpp"
namespace okww::runtime {
namespace {
QString clean(QString text) { return text.remove(QRegularExpression("\\s+")); }
bool matches(const QString &pattern, const QString &text) {
    return QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption).match(text).hasMatch();
}
QString combine(const QVector<TextLine> &lines) {
    QString value;
    for (const auto &line : lines)
        value += clean(line.text);
    return value;
}
std::optional<TextLine> exact(const QVector<TextLine> &lines, const QString &pattern) {
    for (const auto &line : lines)
        if (matches(pattern, clean(line.text)))
            return line;
    return {};
}
} // namespace
RestartDialog restartDialog(const QVector<TextLine> &lines) {
    const auto all = combine(lines);
    const auto cancel = exact(lines, "^(?:取消|cancel)$");
    if (matches("结晶波片|結晶波片|体力|體力|领取奖励|領取獎勵|领取次数|領取次數|waveplate|claimreward|"
                "claimlimit",
                all) &&
        (matches("是否|确定|確定|确认|確認|sure|confirm", all) || cancel))
        return {RestartKind::Resource, cancel};
    if (auto button =
            exact(lines,
                  "^(?:重新挑战|再次挑战|重新挑戰|再次挑戰|challengeagain|retrychallenge|restartchallenge)$"))
        return {RestartKind::Restart, button};
    if (matches("重新挑战|再次挑战|重新挑戰|再次挑戰|challengeagain|restartchallenge", all) &&
        matches("是否|确定|確定|确认|確認|sure|confirm", all))
        if (auto button = exact(lines, "^(?:确认|确定|確認|確定|confirm)$"))
            return {RestartKind::Confirm, button};
    return {};
}
bool isNestExitText(QString text) {
    return matches("^(?:F[:：]?)?(?:(?:离开|離開|退出)(?:当前|當前)?(?:副本|秘境|挑战|挑戰)|(?:leave|exit)(?:"
                   "domain|instance|challenge))$",
                   clean(text));
}
bool isNestCompleteText(QString text) {
    return matches("(?:梦魔|夢魔|梦魇|夢魘).*(?:已被除|已破除|已净化|已淨化)|(?:nightmare|nest).*(?:purified|"
                   "cleared|defeated)",
                   clean(text));
}
bool nestExitContext(const QVector<TextLine> &lines) {
    const auto all = combine(lines);
    if (!matches("(?:离开|離開|退出).{0,20}(?:副本|秘境|挑战|挑戰)|(?:leave|exit).{0,20}(?:domain|instance|"
                 "challenge)",
                 all) ||
        !matches("是否|确定|確定|确认|確認|进度|進度|sure|progress", all) ||
        matches("消耗|结晶波片|結晶波片|贝币|貝幣|星声|星聲|重构|重構|复苏|復甦|购买|購買|purchase|waveplate|"
                "revive",
                all))
        return false;
    return true;
}
std::optional<TextLine> nestExitConfirmation(const QVector<TextLine> &lines, int width) {
    if (!nestExitContext(lines))
        return {};
    std::optional<TextLine> selected;
    for (const auto &line : lines)
        if (matches("^(?:确认|確認|确定|確定|离开|離開|退出|confirm|leave|exit)$", clean(line.text)) &&
            line.center().x() > width * .5 && (!selected || line.box.x() > selected->box.x()))
            selected = line;
    return selected;
}
RestartAction RestartTracker::step(RestartKind kind, bool hud, bool realm) {
    if (kind != RestartKind::None) {
        if (requested_ && kind == RestartKind::Restart && pending_ < 4) {
            ++pending_;
            return RestartAction::Wait;
        }
        if (++actions_[int(kind) - 1] > 3)
            return RestartAction::Fail;
        if (kind == RestartKind::Resource) {
            requested_ = false;
            loading_ = false;
        } else {
            requested_ = true;
            pending_ = 0;
        }
        return RestartAction::Click;
    }
    if (requested_) {
        if (!hud)
            loading_ = true;
        else if (loading_ && realm)
            return RestartAction::Complete;
    } else if (hud) {
        if (++actions_[3] > 3)
            return RestartAction::Fail;
        return RestartAction::Menu;
    }
    return RestartAction::Observe;
}
void restartBossDomain(TaskContext &c) {
    Navigation navigation(c);
    RestartTracker tracker;
    for (int i = 0; i < 120; ++i) {
        c.refresh();
        const auto dialog = restartDialog(c.text(.16, .2, .86, .82, .7));
        const bool hud = c.inTeam();
        const auto action = tracker.step(dialog.kind, hud, hud && navigation.inRealm());
        if (action == RestartAction::Complete) {
            if (c.info)
                c.info("再次挑战", "下一轮副本已确认加载完成");
            return;
        }
        if (action == RestartAction::Fail)
            break;
        if (action == RestartAction::Click) {
            if (dialog.button)
                c.click(*dialog.button, dialog.kind == RestartKind::Resource ? .3 : 0);
            else
                c.key("esc", .3);
            if (dialog.kind != RestartKind::Resource)
                continue;
        } else if (action == RestartAction::Menu)
            c.key("esc", .5);
        c.sleep(.5);
    }
    throw NavigationFailure("再次挑战未完成：尚未确认下一轮副本加载");
}
bool completedNest(TaskContext &c) {
    for (const auto &line : c.text(.015, .15, .38, .38, .7))
        if (isNestCompleteText(line.text))
            return true;
    return false;
}
void leaveNest(TaskContext &c, bool sendEscape) {
    Navigation navigation(c);
    const auto outside = [&] { return navigation.inWorld() && !completedNest(c); };
    if (outside())
        return;
    bool escape = !sendEscape;
    for (int i = 0; i < 4; ++i) {
        c.refresh();
        if (outside())
            return;
        const auto dialogText = c.text(.2, .25, .82, .78, .7);
        auto button = nestExitConfirmation(dialogText, c.frame.cols);
        if (!button && nestExitContext(dialogText))
            if (auto feature =
                    c.findAny({"confirm_btn_hcenter_vcenter", "confirm_btn_highlight_hcenter_vcenter"},
                              {.threshold = .6}))
                button = TextLine{feature->name, feature->box, feature->confidence};
        if (button)
            c.click(*button, 1);
        else {
            bool prompt = false;
            if (c.find("pick_up_f_hcenter_vcenter", {.threshold = .7, .search = fSearchBox(c)}))
                for (const auto &line : c.text(.59, .34, .94, .67, .7))
                    prompt |= isNestExitText(line.text);
            if (prompt)
                c.key("f", 1);
            else {
                std::optional<TextLine> exit;
                for (const auto &line : c.text(.04, .18, .96, .92, .7))
                    if (isNestExitText(line.text)) {
                        exit = line;
                        break;
                    }
                if (exit)
                    c.click(*exit, 1);
                else {
                    if (c.inTeam() && !escape) {
                        c.key("esc", 1);
                        escape = true;
                    } else
                        c.sleep(1);
                    continue;
                }
            }
        }
        if (c.wait(outside, 4))
            return;
    }
    if (!c.wait(outside, 45))
        throw NavigationFailure("巢穴退出未完成：尚未确认回到大世界");
}
} // namespace okww::runtime
