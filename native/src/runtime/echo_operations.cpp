#include "echo_operations.hpp"
#include <opencv2/imgcodecs.hpp>
namespace okww::runtime {
namespace {
double number(QString value) {
    value.replace(QChar(0xff05), '%');
    bool ok = false;
    const auto result = value.section('%', 0, 0).trimmed().toDouble(&ok);
    return ok && std::isfinite(result) ? result : 0;
}
QString simple(QString value) {
    // Terms used on the Simplified/Traditional Chinese echo pages.
    const QString from = "聲強調諧階段擊傷禦鳴普輔鎖棄認數據構獲養當屬選擇點擊任處返還體效攻擊";
    const QString to = "声强调谐阶段击伤御鸣普辅锁弃认数据构获养当属选择点击任处返还体效攻击";
    for (int i = 0; i < from.size(); ++i)
        value.replace(from[i], to[i]);
    return value;
}
QString fixedMain(QString text) {
    return simple(text)
        .replace("凝夜自霜", "凝夜白霜")
        .replace("主属性灭伤害加成", "主属性湮灭伤害加成")
        .replace(QRegularExpression("(?<!湮)灭伤害加成"), "湮灭伤害加成")
        .replace("主属性行射伤害加成", "主属性衍射伤害加成");
}
} // namespace
QString normalizeEchoStat(QString p, const QString &value) {
    p = simple(p);
    if (p.contains("暴击伤害"))
        return "暴击伤害";
    if (p.contains("暴击"))
        return "暴击";
    for (const auto *stat : {"攻击", "生命", "防御"})
        if (p.contains(stat))
            return QString(stat) + (value.contains('%') || value.contains(QChar(0xff05)) ? "百分比" : "");
    if (p.contains("效率"))
        return "共鸣效率";
    if (p.contains("普攻"))
        return "普攻伤害加成";
    if (p.contains("重击"))
        return "重击伤害加成";
    if (p.contains("解放"))
        return "共鸣解放伤害加成";
    if (p.contains("技能"))
        return "共鸣技能伤害加成";
    return p;
}
QVector<EchoStat> pairEchoStats(const QVector<TextLine> &lines) {
    QVector<TextLine> properties, values;
    const QRegularExpression property("[\\x{4e00}-\\x{9fff}]{2,}"), numeric("^[\\d.%％ ]+$");
    for (auto line : lines) {
        line.text = simple(line.text);
        const auto match = property.match(line.text);
        if (match.hasMatch() && !line.text.contains("辅音")) {
            line.text = match.captured();
            properties.append(line);
        }
        if (numeric.match(line.text).hasMatch())
            values.append(line);
    }
    QVector<EchoStat> result;
    for (const auto &p : properties) {
        QString value = "0";
        if (!values.empty()) {
            const auto closest =
                std::min_element(values.begin(), values.end(), [&](const auto &a, const auto &b) {
                    return std::abs(a.box.y() - p.box.y()) < std::abs(b.box.y() - p.box.y());
                });
            value = closest->text;
            values.erase(closest);
        }
        result.append({p.text, value});
    }
    return result;
}
EchoDecision evaluateEcho(const QVector<EchoStat> &stats, const QJsonObject &config) {
    EchoDecision result;
    result.count = static_cast<int>(stats.size());
    const auto reject = [&](QString reason) {
        result.keep = false;
        result.reason = reason;
        return result;
    };
    const auto valid = config.value("有效词条").toArray();
    bool rate = false, damage = false, first = false, encountered = false;
    double rateValue = 0, damageValue = 0;
    for (const auto &stat : stats) {
        const auto p = normalizeEchoStat(stat.property, stat.value);
        const double v = number(stat.value);
        const bool crit = p == "暴击" || p == "暴击伤害";
        if (config["双爆出现之前必须全有效词条"].toBool() && valid.contains("暴击") &&
            valid.contains("暴击伤害") && !encountered) {
            if (!crit && !valid.contains(p))
                return reject("双爆前含无效_" + p);
            if (crit)
                encountered = true;
        }
        if (p == "暴击") {
            rate = true;
            rateValue += v;
        }
        if (p == "暴击伤害") {
            damage = true;
            damageValue += v;
        }
        if (crit && valid.contains(p) && !first) {
            first = true;
            if ((p == "暴击" ? v : v / 2) < config["首条双爆>="].toDouble())
                return reject(QString("首条%1不足_%2").arg(p == "暴击" ? "暴击" : "爆伤").arg(v));
        }
        if (!valid.contains(p))
            ++result.invalid;
    }
    const int remaining = 5 - result.count, missing = int(!rate) + int(!damage);
    if (config["必须有双爆"].toBool() && remaining < missing)
        return reject(QString("无法凑齐双爆_缺%1").arg(missing));
    if (rate && damage && rateValue + damageValue / 2 < config["双爆总计>="].toDouble())
        return reject(QString("双爆总计不足_%1").arg(rateValue + damageValue / 2, 0, 'f', 1));
    if (result.count == 1 && config["第一条必须为有效词条"].toBool() && result.invalid == 1)
        return reject("首条无效");
    if (5 - result.invalid < config["有效词条>="].toInt())
        return reject(QString("有效词条不足_上限%1").arg(5 - result.invalid));
    return result;
}
std::optional<TextLine> EchoOperations::waitText(QRect area, const QString &pattern, double timeout) {
    std::optional<TextLine> result;
    c_.wait(
        [&] {
            auto lines = c_.text(area);
            for (auto &line : lines)
                line.text = fixedMain(line.text);
            result = matchingText(lines, pattern);
            return bool(result);
        },
        timeout, .1);
    return result;
}
std::optional<TextLine> EchoOperations::cultivate() {
    auto lines = c_.text(.82, .86, .97, .96);
    for (auto &line : lines)
        line.text = simple(line.text);
    return matchingText(lines, "^培养$");
}
bool EchoOperations::zeroLevel(bool change) {
    auto lines = change ? c_.text(.66, .48, .77, .56) : c_.text(.65, .35, 1, .57);
    for (auto &line : lines)
        line.text = simple(line.text);
    return bool(matchingText(lines, "声骸技能"));
}
void EchoOperations::enterCultivate() {
    if (!cultivate())
        fail("必须在背包声骸界面过滤后开始");
    const auto end = c_.execution.now() + 5;
    do {
        const auto button = cultivate();
        if (!button)
            return;
        c_.click(*button, .5);
    } while (c_.execution.now() < end);
    fail("未能打开声骸培养界面");
}
void EchoOperations::backToBag() {
    if (!c_.wait(
            [&] {
                if (cultivate())
                    return true;
                c_.key("esc", 1);
                return false;
            },
            10))
        fail("声骸处理后未能返回背包");
}
void EchoOperations::mark(bool lock) {
    const QString yes = lock ? "echo_locked" : "echo_dropped",
                  no = lock ? "echo_not_locked" : "echo_not_dropped";
    const QSize size(c_.frame.cols, c_.frame.rows);
    auto box = c_.templates.box(yes, size).united(c_.templates.box(no, size));
    box.adjust(-pixelRound(box.width() * .025), -pixelRound(box.height() * .025),
               pixelRound(box.width() * .025), pixelRound(box.height() * .025));
    if (!c_.wait(
            [&] {
                const auto a = c_.find(yes, {.threshold = .7, .search = box}),
                           b = c_.find(no, {.threshold = .7, .search = box});
                if (!a && !b)
                    fail(lock ? "无法确认声骸上锁状态" : "无法确认声骸弃置状态");
                if (a && (!b || a->confidence >= b->confidence))
                    return true;
                c_.key(lock ? "c" : "z", 1);
                return false;
            },
            5))
        fail(lock ? "声骸上锁失败" : "声骸弃置失败");
}
void EchoOperations::screenshot(const QString &category, int count, QString reason) {
    reason.remove(QRegularExpression("[<>:\"/\\\\|?*]"));
    const auto dir = screenshots_ + "/" + category;
    if (!QDir().mkpath(dir))
        fail("无法创建声骸截图目录");
    const auto rect = gameBox(c_.frame, .09, .09, .37, .55);
    std::vector<uchar> buffer;
    if (!cv::imencode(".png", c_.frame(cv::Rect(rect.x(), rect.y(), rect.width(), rect.height())), buffer))
        fail("声骸截图编码失败");
    QSaveFile file(dir + "/" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") +
                   QString("_%1_").arg(count) + reason.left(100) + ".png");
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(reinterpret_cast<const char *>(buffer.data()), buffer.size()) !=
            static_cast<qint64>(buffer.size()) ||
        !file.commit())
        fail("声骸截图保存失败");
}
void EchoOperations::upgrade() {
    bool saw = false;
    const auto until = c_.execution.now() + 5;
    while (c_.execution.now() < until) {
        auto add = waitText(gameBox(c_.frame, .09, .6, .38, .86), "^阶段放入$", 1);
        if (add) {
            saw = true;
            c_.click(*add, .3);
        } else if (saw)
            break;
    }
    if (!saw)
        fail("强化设置需要开启阶段放入");
    const auto button = waitText(gameBox(c_.frame, .1, .88, .29, .96), "^强化并调谐$");
    if (!button)
        fail("找不到强化并调谐，请开启同步调谐并检查材料");
    c_.sleep(.1);
    c_.click(*button, 1.5); // One commit. Never repeat this click on an uncertain result.
    for (int i = 0; i < 12; ++i) {
        const auto handle = waitText(gameBox(c_.frame, .24, .18, .75, .98), "不再提示|^调谐成功$|点击任", 2);
        if (!handle)
            return;
        if (handle->text.contains("不再提示")) {
            if (!c_.skipConfirm())
                fail("强化确认界面未识别，停止继续消耗材料");
        } else
            c_.click(*handle, 1);
    }
    fail("强化结果未关闭，停止继续消耗材料");
}
int EchoOperations::enhance() {
    TaskScope scope(c_, "EnhanceEchoTask");
    if (c_.gameLanguage != "zh_CN" && c_.gameLanguage != "zh_TW")
        fail("强化声骸仅支持简体或繁体中文游戏");
    const auto config = c_.profile.group(c_.task);
    int success = 0, failed = 0;
    for (int item = 0; item < 3000; ++item) {
        c_.refresh();
        if (!cultivate())
            fail("必须在背包声骸界面过滤后开始");
        c_.clickRelative(.13, .21, .5);
        if (!zeroLevel()) {
            c_.clickRelative(.35, .917, .5);
            c_.clickRelative(.35, .917, .5);
            c_.clickRelative(.13, .21, .5);
            if (!zeroLevel())
                return success;
        }
        enterCultivate();
        for (int count = 1; count <= 5; ++count) {
            upgrade();
            QVector<EchoStat> stats;
            // A failed/partial OCR must not trigger another material-consuming upgrade.
            const bool verified = c_.wait(
                [&] {
                    stats = pairEchoStats(c_.text(.09, .3, .40, .53));
                    return stats.size() == count &&
                           std::all_of(stats.begin(), stats.end(),
                                       [](const auto &s) { return number(s.value) > 0; });
                },
                4, .2);
            if (!verified)
                fail("强化后词条数量或数值未确认，停止继续消耗材料");
            const auto result = evaluateEcho(stats, config);
            if (!result.keep) {
                mark(false);
                screenshot("failed", ++failed, result.reason);
                if (c_.info)
                    c_.info("失败声骸数量", QString::number(failed));
                backToBag();
                break;
            }
            if (count == 5) {
                mark(true);
                screenshot("success", ++success);
                if (c_.info)
                    c_.info("成功声骸数量", QString::number(success));
                if (config["Pause after Success"].toBool(true)) {
                    if (c_.notify)
                        c_.notify("符合条件的声骸", "已锁定并暂停任务，请查看强化结果", false);
                    return success;
                }
                backToBag();
            }
        }
    }
    fail("声骸处理达到单次批量上限");
}
int EchoOperations::change() {
    TaskScope scope(c_, "ChangeEchoTask");
    if (c_.gameLanguage != "zh_CN")
        fail("修改声骸主属性仅支持简体中文游戏");
    const auto target = c_.setting("目标属性", "攻击").toString();
    const QStringList allowed{"攻击",         "暴击伤害",     "暴击",         "生命",
                              "防御",         "共鸣效率",     "冷凝伤害加成", "热熔伤害加成",
                              "导电伤害加成", "气动伤害加成", "衍射伤害加成", "湮灭伤害加成"};
    if (!allowed.contains(target))
        fail("目标主属性无效");
    int success = 0;
    for (int i = 0; i < 3000; ++i) {
        c_.refresh();
        if (!cultivate())
            fail("必须在背包声骸界面过滤后开始");
        if (!zeroLevel(true))
            return success;
        enterCultivate();
        const QRect screen(0, 0, c_.frame.cols, c_.frame.rows);
        if (!waitText(screen, "^声骸强化$"))
            fail("声骸培养页面未确认");
        c_.sleep(.5);
        auto current = c_.text(.10, .23, .35, .275);
        if (current.empty())
            fail("找不到当前主属性");
        for (const auto &line : current)
            if (fixedMain(line.text).contains(target))
                fail("目标属性和当前属性相同，请修改声骸过滤条件");
        c_.clickRelative(.04, .41);
        if (!waitText(screen, "^主音属性$"))
            fail("主音属性页面未确认");
        c_.sleep(.8);
        c_.clickRelative(.52, .71);
        const auto option = waitText(screen, "^(?:主属性)?" + QRegularExpression::escape(target) + "$");
        if (!option)
            fail("没有找到目标主属性");
        c_.sleep(.1);
        c_.click(*option, .5);
        const auto confirm = waitText(screen, "^确认$");
        if (!confirm)
            fail("没有找到主属性选择确认按钮");
        c_.click(*confirm, 2);
        const auto reconstruct = waitText(gameBox(c_.frame, .37, .82, .64, .99), "^数据重构$");
        if (!reconstruct)
            fail("没有找到数据重构按钮");
        c_.click(*reconstruct, .5);
        if (!waitText(screen, "^获得声骸$", 10))
            fail("未能确认重构结果，停止继续消耗材料");
        backToBag();
        if (c_.info)
            c_.info("成功声骸数量", QString::number(++success));
        else
            ++success;
    }
    fail("主属性修改达到单次批量上限");
}
void EchoOperations::centered(double x, double y, double after) { c_.clickRelative(x, y, after, true); }
bool echoMergeResultConfirmed(const QVector<TextLine> &lines) {
    return bool(matchingText(lines, "^(?:获得声骸|獲得聲骸|数据融合成功|數據融合成功|Echoes? Obtained|Data "
                                    "(?:Merge|Fusion) Complete)[!！。:.：]*$"));
}
void awaitEchoMergeResult(TaskContext &c) {
    // The old game's 'do not ask again' checkbox can suppress confirmation on
    // subsequent batches. Observe the result itself before counting a batch.
    bool confirmed = false;
    if (!c.wait(
            [&] {
                if (echoMergeResultConfirmed(c.text(.12, .04, .88, .97, .7)))
                    return true;
                if (!confirmed)
                    confirmed = c.skipConfirm();
                return false;
            },
            12, .2))
        fail("合成结果未确认，停止重复合成");
}
int EchoOperations::merge() {
    TaskScope scope(c_, "MergeEchoTask");
    navigation_.ensureMain();
    c_.key(c_.hotkey("Bag Key", "b"));
    if (!c_.wait([&] { return !c_.inTeam(); }, 5))
        throw NavigationFailure("未能打开背包");
    c_.sleep(3);
    if (!c_.findAny({"cancel_button_hcenter_vcenter", "cancel_button_highlight_hcenter_vcenter"})) {
        if (c_.info)
            c_.info("声骸合成", "未达到 1000 个弃置声骸");
        navigation_.ensureMain();
        return 0;
    }
    navigation_.confirmDialog(false);
    c_.sleep(2);
    c_.clickRelative(.034, .293, 1);
    c_.clickRelative(.580, .911, 4);
    centered(.602, .124, .5);
    centered(.520, .904, 2);
    c_.clickRelative(.041, .918, 1);
    c_.clickRelative(.826, .840, .5);
    c_.clickRelative(.717, .204, .5);
    c_.clickRelative(.041, .918, .5);
    int batches = 0;
    for (; batches < 100; ++batches) {
        c_.clickRelative(.310, .915, .5);
        std::optional<Match> all;
        if (!c_.wait(
                [&] {
                    all = c_.find("echo_select_all", {.horizontal = .3});
                    return bool(all);
                },
                5))
            fail("未找到声骸全选按钮");
        c_.click(*all, 1);
        if (!matchingText(c_.text(.670, .660, .895, .958), "100\\s*/\\s*100"))
            break;
        c_.clickRelative(.782, .910);
        awaitEchoMergeResult(c_);
        c_.sleep(3);
        centered(.496, .972, 1);
    }
    navigation_.ensureMain();
    return batches;
}
} // namespace okww::runtime
