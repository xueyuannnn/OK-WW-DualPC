#include "stamina.hpp"
#include "navigation.hpp"
#include <QLockFile>
namespace okww::runtime {
namespace {
struct Counter {
    TextLine line;
    int current, maximum;
};
QVector<Counter> counters(const QVector<TextLine> &lines) {
    const QRegularExpression regex("^(\\d+)\\s*/\\s*(\\d+)$");
    QVector<Counter> result;
    for (const auto &line : lines) {
        const auto match = regex.match(QString(line.text).replace("／", "/").trimmed());
        if (!match.hasMatch())
            continue;
        bool a = false, b = false;
        const int current = match.captured(1).toInt(&a), maximum = match.captured(2).toInt(&b);
        if (a && b && maximum >= 20 && maximum <= 9999)
            result.append({line, current, maximum});
    }
    return result;
}
int integerValue(const QJsonObject &data, const QString &key, int high) {
    const auto value = data[key];
    const double n = value.toDouble(-1);
    if (!value.isDouble() || !std::isfinite(n) || std::floor(n) != n || n < 0 || n > high)
        fail("日常体力魔方记录损坏，不执行材料操作");
    return int(n);
}
} // namespace
StaminaValues staminaValues(const QVector<TextLine> &lines) {
    const auto entries = counters(lines);
    if (entries.empty())
        return {};
    const auto selected = *std::max_element(
        entries.begin(), entries.end(), [](const auto &a, const auto &b) { return a.maximum < b.maximum; });
    const QRegularExpression reserve("^(?:MAX)?(\\d{1,4})(?:MAX)?$",
                                     QRegularExpression::CaseInsensitiveOption);
    int backup = 0, rightmost = INT_MIN;
    for (const auto &line : lines) {
        const auto match = reserve.match(QString(line.text).remove(' ').remove(','));
        if (match.hasMatch() && line.box.x() + line.box.width() <= selected.line.box.x() &&
            std::abs(line.center().y() - selected.line.center().y()) <=
                1.5 * std::max(line.box.height(), selected.line.box.height()) &&
            line.box.x() > rightmost) {
            backup = match.captured(1).toInt();
            rightmost = line.box.x();
        }
    }
    return {selected.current, backup, selected.current + backup};
}
StaminaValues readStamina(TaskContext &c) {
    QVector<TextLine> lines;
    c.wait(
        [&] {
            lines = c.text(.49, 0, .92, .1);
            return bool(matchingText(lines, "\\d"));
        },
        10);
    const auto values = staminaValues(lines);
    if (values.current >= 0 && c.info) {
        c.info("当前体力", QString::number(values.current));
        c.info("备用体力", QString::number(values.reserve));
    }
    return values;
}
StaminaSpend planStaminaSpend(StaminaValues before, int once, int remaining, bool exact) {
    if (once <= 0 || once > 240 || before.current < 0 || before.reserve < 0 ||
        before.total != before.current + before.reserve)
        fail("未确认体力数量，停止领奖");
    if (before.total < once)
        return {};
    const bool twice = exact ? remaining >= once * 2 && before.total >= once * 2
                             : before.current >= once * 2 || (remaining > once && before.total >= once * 2);
    const int used = once * (twice ? 2 : 1);
    const bool enough = before.total - used >= once;
    return {used,
            enough && (exact ? remaining - used > 0 : remaining - used > 0 || before.current - used >= once)};
}
StaminaSpend claimStamina(TaskContext &c, int once, int remaining, bool exact) {
    c.sleep(1);
    const auto before = readStamina(c);
    const auto plan = planStaminaSpend(before, once, remaining, exact);
    if (!plan.used)
        return plan;
    const auto button = c.findAny(
        plan.used == once
            ? QStringList{"cancel_button_hcenter_vcenter", "cancel_button_highlight_hcenter_vcenter"}
            : QStringList{"confirm_btn_hcenter_vcenter", "confirm_btn_highlight_hcenter_vcenter"});
    if (!button || !c.find("claim_stamina_sign"))
        throw NavigationFailure("未确认体力领奖界面");
    c.click(*button, 2);
    double stable = -1;
    const bool refill = c.wait(
        [&] {
            if (!c.find("gem_add_stamina", {.horizontal = .4, .vertical = .05})) {
                stable = -1;
                return false;
            }
            if (stable < 0)
                stable = c.execution.now();
            return c.execution.now() - stable >= .5;
        },
        2);
    if (refill) {
        if (before.current >= plan.used || before.reserve < plan.used - before.current)
            fail("补充体力界面与已读数量不符，停止自动确认");
        // Preserve the original reserve-waveplate conversion path. No cube use
        // here: cube quantity and daily journal belong to useOneDailyCube.
        const auto point =
            Templates::anchored({1344, 767, 1, 1}, {1920, 1080}, {c.frame.cols, c.frame.rows}, true)
                .topLeft();
        c.click(point, 1);
        c.click(point, 1);
        c.key("esc", 1);
        // Re-identify before the second reward click; a disappeared dialog
        // must not turn a stale position into a repeated material action.
        const auto retry = c.find(button->name);
        if (!retry || !c.find("claim_stamina_sign"))
            fail("补充备用体力后未回到领奖界面");
        c.click(*retry, 1);
    }
    if (!c.wait(
            [&] {
                return !c.find("claim_stamina_sign") &&
                       !c.find("gem_add_stamina", {.horizontal = .4, .vertical = .05});
            },
            8))
        fail("体力领奖结果未确认，不重复点击消耗按钮");
    return plan;
}
CubeBudget::CubeBudget(QString path, int limit, QDateTime now)
    : path_(std::move(path)), day_(now.addSecs(-4 * 3600).date().toString(Qt::ISODate)),
      limit_(std::clamp(limit, 0, 4)) {
    data_ = read(false);
}
QJsonObject CubeBudget::read(bool allowPending) const {
    const QJsonObject fresh{{"day", day_}, {"reserved", 0}, {"verified", 0}, {"pending", false}};
    if (!QFile::exists(path_))
        return fresh;
    const auto data = readJson(path_);
    integerValue(data, "reserved", 4);
    if (!data["pending"].isBool())
        fail("日常体力魔方状态无效");
    if (data["pending"].toBool() && !allowPending)
        fail("上次魔方消耗尚未核实，停止自动使用");
    // A pending operation remains blocked across the 04:00 reset as well.
    if (data["day"].toString() != day_ && !data["pending"].toBool())
        return fresh;
    const int verified = integerValue(data, "verified", 4);
    if (verified > data["reserved"].toInt())
        fail("日常体力魔方计数不一致");
    return data;
}
void CubeBudget::reserve() {
    QDir().mkpath(QFileInfo(path_).absolutePath());
    QLockFile lock(path_ + ".lock");
    if (!lock.tryLock(0))
        fail("另一任务正在处理体力魔方记录");
    data_ = read(false);
    if (pending() || !remaining() || owned_)
        fail("日常体力魔方次数已用尽或待核实");
    data_["reserved"] = used() + 1;
    data_["pending"] = true;
    writeJson(path_, data_);
    owned_ = true;
}
void CubeBudget::verify() {
    if (!owned_)
        fail("当前执行没有可核实的魔方操作");
    QLockFile lock(path_ + ".lock");
    if (!lock.tryLock(0))
        fail("体力魔方记录正在使用");
    const auto current = read(true);
    if (current != data_ || !pending())
        fail("魔方操作记录已变化，需要核对");
    data_["pending"] = false;
    data_["verified"] = data_["verified"].toInt() + 1;
    writeJson(path_, data_);
    owned_ = false;
}
bool useOneDailyCube(TaskContext &c, CubeBudget &budget, bool consume) {
    if (!budget.remaining() || budget.pending())
        return false;
    const auto before = readStamina(c);
    if (before.current < 0)
        return false;
    const auto dialog = [&] {
        return bool(
            matchingText(c.text(.18, .17, .4, .23), "补充结晶波片|補充結晶波片|replenish waveplates"));
    };
    const auto open = [&] {
        if (dialog())
            return true;
        const auto entries = counters(c.text(.49, 0, .92, .1));
        if (entries.empty())
            return false;
        const auto counter =
            *std::max_element(entries.begin(), entries.end(),
                              [](const auto &a, const auto &b) { return a.maximum < b.maximum; });
        c.click(QPointF(pixelRound(counter.line.box.x() + counter.line.box.width() + .027 * c.frame.cols),
                        pixelRound(.06 * c.frame.rows)),
                .4);
        c.refresh();
        return dialog();
    };
    const auto stock = [&]() -> std::optional<int> {
        c.clickRelative(.497, .444, .7);
        std::optional<int> value;
        c.wait(
            [&] {
                c.refresh();
                QString text;
                for (const auto &line : c.text(.37, .53, .63, .585))
                    text += QString(line.text).remove(' ');
                const QRegularExpression one(
                    "消耗1[结結]晶[溶熔][剂劑][补補]充60[结結]晶波片|consume1crystalsolvent.*60waveplates",
                    QRegularExpression::CaseInsensitiveOption);
                if (!one.match(text).hasMatch())
                    return false;
                QVector<int> values;
                for (const auto &line : c.textScaled(gameBox(c.frame, .50, .49, .535, .53)))
                    if (QRegularExpression("^\\d+$").match(line.text.trimmed()).hasMatch()) {
                        bool ok = false;
                        int n = line.text.trimmed().toInt(&ok);
                        if (ok)
                            values.append(n);
                    }
                if (values.size() != 1)
                    return false;
                value = values[0];
                return true;
            },
            3);
        return value;
    };
    const auto cancel = [&] {
        if (auto button = matchingText(c.text(.22, .68, .4, .76), "^(?:取消|cancel)$"))
            c.click(*button, .2);
    };
    if (!open())
        return false;
    const auto owned = stock();
    if (!owned || *owned <= 0) {
        cancel();
        return false;
    }
    const auto confirm = matchingText(c.text(.60, .68, .79, .76), "^(?:确认|確認|confirm)$");
    if (!confirm) {
        cancel();
        return false;
    }
    if (!consume) {
        if (c.info)
            c.info("体力魔方检查", QString("库存 %1；单次补充 60 体力；未消耗").arg(*owned));
        c.sleep(.8);
        cancel();
        return true;
    }
    budget.reserve();
    c.click(*confirm, .5);
    StaminaValues after;
    if (!c.wait(
            [&] {
                c.refresh();
                after = readStamina(c);
                return after.current >= before.current + 60 && after.reserve == before.reserve;
            },
            5))
        fail("魔方操作后体力变化未确认，已保留次数记录并停止继续消耗");
    if (!open())
        fail("魔方操作后无法核对库存，已保留次数记录");
    const auto remaining = stock();
    cancel();
    if (!remaining || *remaining != *owned - 1)
        fail("魔方库存变化未确认，已保留次数记录并停止继续消耗");
    budget.verify();
    if (c.info)
        c.info("今日体力魔方", QString("%1/%2").arg(budget.used()).arg(budget.limit()));
    return true;
}
} // namespace okww::runtime
