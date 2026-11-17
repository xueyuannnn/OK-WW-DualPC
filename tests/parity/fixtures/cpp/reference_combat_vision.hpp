#pragma once
// Frozen pre-migration implementation; test-only parity oracle.
#include "runtime/combat_vision.hpp"
#include <opencv2/imgproc.hpp>
namespace okww::runtime::reference_vision {

struct CharacterDefinition {
    QString className, canonical;
    QStringList templates;
    Role role = Role::MainDps;
    int element = -1;
    double buffTime = 14;
    bool shortTarget = false;
};
struct CharacterIdentity {
    CharacterDefinition definition;
    double confidence = 0;
};
struct Cooldowns {
    double timestamp = 0;
    double fallbackAt = -1;
    std::array<double, 3> seconds{};
    std::array<double, 3> observedAt{};
};
struct TargetObservation {
    bool matched = false, locked = false, extended = false;
};
bool frozen_systemRunDialogConfirmed(const QVector<TextLine> &lines);
QVector<Match> frozen_shiftedBarCandidates(const cv::Mat &frame);
int frozen_segmentedGaugeValue(const cv::Mat &image, ColorRange color, int segments, int minFrequency,
                        int maxFrequency, double minAmplitude);
QVector<Match> frozen_colorRectangles(const cv::Mat &frame, ColorRange color, double minWidth, double minHeight,
                               double maxHeight = -1, std::optional<QRect> area = {},
                               double minimumFill = .95);
class CombatVision {
    TaskContext &context_;
    QVector<CharacterDefinition> definitions_;
    uint64_t partyVersion_ = UINT64_MAX, cdVersion_ = UINT64_MAX;
    PartyHud party_;
    QMap<int, Cooldowns> cooldowns_;
    std::optional<Match> best(QRect region, QStringList names, double threshold);

  public:
    CombatVision(TaskContext &context, const QString &catalog);
    PartyHud party();
    void reset() {
        cooldowns_.clear();
        cdVersion_ = partyVersion_ = UINT64_MAX;
    }
    QRect box(const QString &name);
    std::optional<CharacterIdentity> character(int index, const CharacterState *previous = nullptr);
    void refreshCooldowns(int currentIndex);
    double cooldown(Skill skill, int index, const FreezeTimeline &time, double now) const;
    const QMap<int, Cooldowns> &cooldowns() const { return cooldowns_; }
    bool hasTarget(bool allowShort = false, bool extended = false);
    TargetObservation targetObservation(bool allowShort = false, bool extended = false);
    bool healthBar(bool inCombat);
    bool shiftedHealthBar();
    bool deathDialog();
    std::optional<bool> challengeResult(); // true: success; false: failure
    bool forte(Forte kind);
    double white(const QString &name);
    static double cdX(int width, int height, double fraction);
};

bool frozen_systemRunDialogConfirmed(const QVector<TextLine> &lines) {
    return bool(matchingText(lines, "Windows.*(名称|名稱|name)|Type.*name.*program")) &&
           bool(matchingText(lines, "^(打开|開啟|Open)\\s*[(（:]")) &&
           bool(matchingText(lines, "浏览|瀏覽|Browse"));
}
static bool frozen_enemyLevelConfirmed(const QVector<TextLine> &lines) {
    if (matchingText(lines,"(?<![A-Za-z])Lv[\\s.·]*\\d{1,3}(?!\\d)")) return true;
    // OCR may detect "Lv." and its number as two adjacent boxes. Require
    // both on the same row instead of treating either fragment as evidence.
    for (const auto &prefix : lines) {
        if (!QRegularExpression("^Lv[\\s.·]*$",QRegularExpression::CaseInsensitiveOption).match(prefix.text.trimmed()).hasMatch()) continue;
        for (const auto &number : lines) {
            if (!QRegularExpression("^\\d{1,3}$").match(number.text.trimmed()).hasMatch()) continue;
            const auto height=std::max(prefix.box.height(),number.box.height());
            const auto gap=number.box.left()-prefix.box.right();
            if (std::abs(number.center().y()-prefix.center().y())<=height*.6 && gap>=-height*.5 && gap<=height*2.) return true;
        }
    }
    return false;
}
int frozen_segmentedGaugeValue(const cv::Mat &image, ColorRange color, int segments, int minFrequency,
                        int maxFrequency, double minAmplitude) {
    if (image.empty() || segments < 1)
        return 0;
    cv::Mat mask;
    cv::inRange(image, color.lower, color.upper, mask);
    const int step = mask.cols / segments;
    if (step < 64)
        return 0;
    for (int count = segments; count > 0; --count) {
        auto part = mask(cv::Rect(step * (count - 1), 0, step, mask.rows));
        const int white = cv::countNonZero(part);
        if (!white || white == part.rows * part.cols)
            continue;
        cv::Mat profile(1, step, CV_32F);
        for (int x = 0; x < step; ++x)
            profile.at<float>(0, x) = float(cv::countNonZero(part.col(x)));
        float sum = 0;
        for (int x = 0; x < step; ++x)
            sum += profile.at<float>(0, x);
        profile -= sum / float(step);
        cv::Mat values, spectrum;
        profile.convertTo(values, CV_64F);
        cv::dft(values, spectrum, cv::DFT_COMPLEX_OUTPUT);
        double amplitude = 0;
        int frequency = 0;
        for (int x = 1; x < step; ++x) {
            const auto value = spectrum.at<cv::Vec2d>(0, x);
            const double magnitude = std::hypot(value[0], value[1]);
            if (magnitude > amplitude) {
                amplitude = magnitude;
                frequency = x;
            }
        }
        if ((frequency >= minFrequency && frequency <= maxFrequency) || amplitude >= minAmplitude)
            return count;
    }
    return 0;
}
QVector<Match> frozen_colorRectangles(const cv::Mat &frame, ColorRange color, double minWidth, double minHeight,
                               double maxHeight, std::optional<QRect> area, double minimumFill) {
    auto r = area.value_or(QRect(0, 0, frame.cols, frame.rows)).intersected({0, 0, frame.cols, frame.rows});
    if (r.isEmpty())
        return {};
    cv::Mat mask;
    cv::inRange(frame(cv::Rect(r.x(), r.y(), r.width(), r.height())), color.lower, color.upper, mask);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    QVector<Match> results;
    for (const auto &contour : contours) {
        auto b = cv::boundingRect(contour);
        if (b.width < minWidth || b.height < minHeight || (maxHeight >= 0 && b.height > maxHeight))
            continue;
        const double fraction = double(cv::countNonZero(mask(b))) / b.area();
        if (fraction >= minimumFill)
            results.append({"health", {r.x() + b.x, r.y() + b.y, b.width, b.height}, fraction});
    }
    return results;
}
QVector<Match> frozen_shiftedBarCandidates(const cv::Mat &frame) {
    const int h = frame.rows, w = frame.cols;
    const double scale = h / 1080.;
    cv::Mat hsv;
    cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);
    const cv::Mat kernel = cv::Mat::ones(1, std::max(3, pixelRound(3. * w / 1920)), CV_8U);
    QVector<Match> boxes;
    for (int value : {75, 110, 150, 190, 230}) {
        cv::Mat mask, upper, joined;
        cv::inRange(hsv, cv::Scalar(0, 55, value), cv::Scalar(26, 255, 255), mask);
        cv::inRange(hsv, cv::Scalar(170, 55, value), cv::Scalar(179, 255, 255), upper);
        mask |= upper;
        cv::morphologyEx(mask, joined, cv::MORPH_CLOSE, kernel);
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(joined, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        cv::Mat horizontal;
        cv::morphologyEx(mask,horizontal,cv::MORPH_OPEN,
                        cv::Mat::ones(1,std::max(3,pixelRound(48.*w/1920)),CV_8U));
        std::vector<std::vector<cv::Point>> isolated;
        cv::findContours(horizontal,isolated,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
        contours.insert(contours.end(),isolated.begin(),isolated.end());
        for (const auto &contour : contours) {
            auto b = cv::boundingRect(contour);
            if (b.width < pixelRound(50. * w / 1920) || b.width > pixelRound(345. * w / 1920) ||
                b.height < std::max(2, pixelRound(4 * scale)) ||
                b.height > std::max(3, pixelRound(22 * scale)) || b.width < b.height * 6 || b.y <= h * .08 ||
                b.y >= h * .85)
                continue;
            const double filled = double(cv::countNonZero(mask(b))) / b.area();
            auto core = b.height > 2 ? cv::Rect(b.x + 1, b.y + 1, b.width - 2, b.height - 2) : b;
            // White highlights and compression break the red fill into holes.
            // Candidate acceptance still requires an adjacent OCR level label.
            if (filled >= .75 && double(cv::countNonZero(mask(core))) / core.area() >= .8)
                boxes.append({"shifted_health", {b.x, b.y, b.width, b.height}, filled});
        }
    }
    std::stable_sort(boxes.begin(), boxes.end(),
                     [](const auto &a, const auto &b) { return a.confidence > b.confidence; });
    QVector<Match> unique;
    for (const auto &box : boxes) {
        bool overlap = false;
        for (const auto &other : unique) {
            auto intersection = box.box.intersected(other.box);
            if (double(intersection.width()) * intersection.height() /
                    std::min(box.box.width() * box.box.height(), other.box.width() * other.box.height()) >
                .8) {
                overlap = true;
                break;
            }
        }
        if (!overlap)
            unique.append(box);
        if (unique.size() == 6)
            break;
    }
    return unique;
}
CombatVision::CombatVision(TaskContext &context, const QString &catalog) : context_(context) {
    for (auto value : readJson(catalog)["characters"].toArray()) {
        auto o = value.toObject();
        CharacterDefinition d;
        d.className = o["class"].toString();
        d.canonical = o["canonical"].toString();
        d.role = Role(o["role"].toInt());
        d.element = o["element"].toInt(-1);
        d.buffTime = o["buff_time"].toDouble();
        d.shortTarget = o["target_box_short_combat_check"].toBool();
        for (auto name : o["templates"].toArray())
            d.templates.append(name.toString());
        if (d.templates.isEmpty() || d.className.isEmpty())
            fail("角色模板目录无效");
        definitions_.append(std::move(d));
    }
}
PartyHud CombatVision::party() {
    if (partyVersion_ != context_.frameVersion) {
        party_ = context_.party();
        partyVersion_ = context_.frameVersion;
    }
    return party_;
}
QRect CombatVision::box(const QString &name) {
    return context_.templates.box(name, {context_.frame.cols, context_.frame.rows});
}
std::optional<Match> CombatVision::best(QRect area, QStringList names, double threshold) {
    std::optional<Match> result;
    for (const auto &name : names)
        if (auto match = context_.find(name, {.threshold = threshold, .search = area}))
            if (!result || match->confidence > result->confidence)
                result = match;
    return result;
}
std::optional<CharacterIdentity> CombatVision::character(int index, const CharacterState *previous) {
    if (index < 0 || index > 2)
        fail("角色位置无效");
    const auto area = box(QString("box_char_%1").arg(index + 1));
    if (previous && previous->confidence > .92)
        for (const auto &d : definitions_)
            if (d.templates.contains(previous->name))
                if (auto match = best(area, d.templates, .6))
                    return CharacterIdentity{d, match->confidence};
    std::optional<CharacterIdentity> result;
    for (const auto &d : definitions_)
        if (auto match = best(area, d.templates, .6))
            if (!result || match->confidence > result->confidence)
                result = CharacterIdentity{d, match->confidence};
    if (!result || result->confidence < .75) {
        std::optional<CharacterIdentity> softened;
        double second = 0;
        for (const auto &d : definitions_) {
            double score = 0;
            for (const auto &name : d.templates)
                if (auto match=context_.find(name,{.threshold=.75,.search=area,.grayscale=true,.blurSigma=.9}))
                    score=std::max(score,match->confidence);
            if (softened && score<=softened->confidence) second=std::max(second,score);
            else if (score>0) {
                if (softened) second=std::max(second,softened->confidence);
                softened=CharacterIdentity{d,score};
            }
        }
        if (softened && softened->confidence>=.82 && softened->confidence-second>=.04 &&
            (!result || softened->confidence>result->confidence)) result=softened;
    }
    return result;
}
double CombatVision::cdX(int width, int height, double x) {
    return 1 - (1 - x) * std::min(double(width), height * 16. / 9) / width;
}
void CombatVision::refreshCooldowns(int index) {
    if (cdVersion_ == context_.frameVersion || index < 0)
        return;
    const auto hud=party();
    if (!hud.visible || hud.current!=index) return;
    const auto now = context_.execution.now();
    if (cooldowns_.contains(index) && now-cooldowns_[index].timestamp < .1)
        return;
    // A missed digit is unknown, not proof that a skill has become ready.
    Cooldowns cds = cooldowns_.value(index);
    cds.timestamp = context_.execution.now();
    const int w = context_.frame.cols, h = context_.frame.rows;
    const auto area = relativeBox(context_.frame, cdX(w, h, .82), .86, cdX(w, h, .97), .93);
    const QRegularExpression regex("^\\s*(\\d{1,2})(?:[.,·]\\s*(\\d))?\\s*$");
    int recognized = 0;
    int seen = 0;
    const auto collect = [&](const QVector<TextLine> &lines) {
        for (const auto &line : lines) {
            if (line.center().y()<h*.885) continue; // Upper badges are skill charges, not cooldowns.
            const auto match = regex.match(line.text);
            if (!match.hasMatch()) continue;
            const double seconds = match.captured(1).toInt() + match.captured(2).toInt() / 10.;
            if (seconds < 0 || seconds > 60) continue;
            const int skill = line.center().x() < w * cdX(w,h,.86) ? 0
                            : line.center().x() > w * cdX(w,h,.91) ? 2 : 1;
            cds.seconds[skill] = seen & (1<<skill) ? std::max(cds.seconds[skill],seconds) : seconds;
            cds.observedAt[skill] = cds.timestamp;
            seen |= 1<<skill;
            ++recognized;
        }
    };
    collect(context_.text(area, .7, Filter::InvertWhite));
    // Remote video can turn pure white numerals grey. Read the enlarged
    // original crop when the binary path sees no complete counter.
    if (recognized < 3 && (cds.fallbackAt < 0 || now-cds.fallbackAt >= .3)) {
        cds.fallbackAt = now;
        collect(context_.textScaled(area,.65,2));
    }
    cooldowns_[index] = cds;
    cdVersion_ = context_.frameVersion;
}
double CombatVision::cooldown(Skill skill, int index, const FreezeTimeline &time, double now) const {
    if (skill == Skill::ExtraAction || !cooldowns_.contains(index))
        return 0;
    const auto &cds = cooldowns_[index];
    return cds.seconds[int(skill)] - time.elapsed(now, cds.observedAt[int(skill)]);
}
static QRect scaled(QRect r, double x, double y) {
    const int w = pixelRound(r.width() * x), h = pixelRound(r.height() * y);
    return {std::max(0, pixelRound(r.x() + r.width() / 2. - w / 2.)),
            std::max(0, pixelRound(r.y() + r.height() / 2. - h / 2.)), w, h};
}
bool CombatVision::hasTarget(bool allowShort, bool extended) {
    return targetObservation(allowShort, extended).locked;
}
TargetObservation CombatVision::targetObservation(bool allowShort, bool extended) {
    QString has = "has_target", no = "no_target";
    if (context_.frame.cols == 1600) {
        has += "_169";
        no += "_169";
    }
    const QStringList names{has, no};
    auto reference=context_.templates.pattern(has,{context_.frame.cols,context_.frame.rows});
    cv::Mat referenceHsv,ring;
    cv::cvtColor(reference,referenceHsv,cv::COLOR_BGR2HSV);
    cv::inRange(referenceHsv,cv::Scalar(12,65,55),cv::Scalar(45,255,255),ring);
    const auto observe = [&](QRect area) {
        auto raw=best(area,names,.6);
        if (!raw || raw->confidence<.82) {
            auto yes=context_.find(has,{.threshold=.8,.search=area,.grayscale=true,.blurSigma=.9});
            auto absent=context_.find(no,{.threshold=.8,.search=area,.grayscale=true,.blurSigma=.9});
            if (yes && (!absent || yes->confidence>absent->confidence)) raw=yes;
            else if (absent) raw=absent;
        }
        // Both icons share the skull shape. Remote resampling can make the
        // grey template score higher even when the gold target ring is there.
        // Confirm its colour only after locating the icon's shape.
        if (raw && raw->confidence>=.75 && cv::countNonZero(ring)>0 && raw->box.size()==box(has).size()) {
            const auto b=raw->box;
            cv::Mat hsv,gold;
            cv::cvtColor(context_.frame(cv::Rect(b.x(),b.y(),b.width(),b.height())),hsv,cv::COLOR_BGR2HSV);
            cv::inRange(hsv,cv::Scalar(10,35,55),cv::Scalar(48,255,255),gold);
            cv::dilate(gold,gold,cv::Mat::ones(3,3,CV_8U));
            const double ratio=double(cv::countNonZero(gold & ring))/cv::countNonZero(ring);
            if (ratio>=.35) raw->name=has;
            else if (ratio<=.1) raw->name=no;
        }
        return raw;
    };
    auto match = observe(scaled(box(has), 1.1, 1.1));
    if (!match)
        match = observe(box("box_target_enemy_long"));
    if (!match)
        match = observe(box("target_box_long2"));
    if (!match && allowShort)
        match = observe(box("target_box_short"));
    bool expanded = false;
    if (!match && extended) {
        match = observe(scaled(box(has), 1.1, 2));
        expanded = bool(match);
    }
    return {bool(match), match && match->name == has, expanded};
}
bool CombatVision::healthBar(bool inCombat) {
    const auto &image = context_.frame;
    const int minHeight = int(image.rows * 9. / 2160);
    const int minWidth = int(image.cols * (inCombat ? 12. : 100) / 3840);
    if (!frozen_colorRectangles(image, {{55, 55, 174}, {76, 85, 225}}, minWidth, minHeight, minHeight * 3).isEmpty())
        return true;
    auto bossBox =
        Templates::anchored({1269, 58, 1264, 142}, {3840, 2160}, {image.cols, image.rows}, true, true);
    if (frozen_colorRectangles(image, {{4, 30, 245}, {75, 185, 255}}, minWidth, minHeight * 1.3, -1, bossBox)
            .size() == 1)
        return true;
    return false;
}
bool CombatVision::shiftedHealthBar() {
    const auto &image = context_.frame;
    const int w = image.cols, h = image.rows;
    const double scale = h / 1080.;
    const auto levelAt = [&](QRect area) {
        return frozen_enemyLevelConfirmed(context_.text(area,.65)) ||
               frozen_enemyLevelConfirmed(context_.textScaled(area,.6,2));
    };
    auto top = relativeBox(image, .28, .025, .72, .09);
    cv::Mat hsv, mask, upper;
    cv::cvtColor(image(cv::Rect(top.x(), top.y(), top.width(), top.height())), hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(0, 90, 120), cv::Scalar(35, 255, 255), mask);
    cv::inRange(hsv, cv::Scalar(170, 90, 120), cv::Scalar(179, 255, 255), upper);
    mask |= upper;
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    for (const auto &contour : contours) {
        auto b = cv::boundingRect(contour);
        if (b.width >= w * .15 && b.height >= std::max(2., 4 * scale) && b.height <= 16 * scale &&
            double(b.width) / b.height >= 20 && cv::contourArea(contour) / b.area() >= .7)
            if (levelAt(relativeBox(image,.3,0,.7,.052)))
                return true;
    }
    for (const auto &bar : frozen_shiftedBarCandidates(image)) {
        const auto b = bar.box;
        const int x = std::max(0, b.x() - pixelRound(12 * scale)),
                  y = std::max(0, b.y() - pixelRound(48 * scale));
        const int right = std::min(w, b.x() + b.width() + pixelRound(55 * scale)),
                  bottom = std::max(y + 1, b.y() - pixelRound(3 * scale));
        if (levelAt(QRect(x,y,right-x,bottom-y)))
            return true;
    }
    return false;
}
bool CombatVision::deathDialog() {
    const QString title =
        "^(?:选择复苏物品|選擇復甦物品|失去意识|失去意識|Select\\s+Revival\\s+Item|Unconscious)$";
    if (!matchingText(context_.text(.15, .14, .85, .40), title))
        return false;
    return bool(matchingText(context_.text(.35, .65, .82, .93), "^(?:复苏|復甦|确认|確認|Revive|Confirm)$"));
}
std::optional<bool> CombatVision::challengeResult() {
    // Both anchors are required. A missing party HUD alone can be an ultimate
    // animation, and must never interrupt an author's rotation.
    const auto titles = context_.text(.2, .12, .8, .55);
    const bool success = bool(matchingText(titles,
        "^(?:挑战成功|挑戰成功|Challenge\\s*(?:Success|Complete|Completed))$"));
    const bool failure = bool(matchingText(titles,
        "^(?:挑战失败|挑戰失敗|Challenge\\s*Failed)$"));
    if (!success && !failure) return {};
    if (!matchingText(context_.text(.2, .6, .8, .95),
        "^(?:确认成绩|確認成績|再次挑战|再次挑戰|重新挑战|重新挑戰|返回深塔|"
        "Confirm\\s*(?:Score|Results?)|Challenge\\s*Again|Retry\\s*Challenge|Return\\s*to\\s*Tower)$"))
        return {};
    return success;
}
bool CombatVision::forte(Forte kind) {
    if (kind == Forte::Mouse)
        return bool(context_.find(
            "mouse_forte",
            {.threshold = .6, .horizontal = .025, .vertical = .015, .filter = Filter::Binary244}));
    if (kind == Forte::Resonance)
        return bool(
            context_.find("e_forte", {.threshold = .6, .horizontal = .025, .filter = Filter::Binary220}));
    auto r = Templates::anchored({2251, 1993, 60, 23}, {3840, 2160},
                                 {context_.frame.cols, context_.frame.rows}, true);
    return colorPercentage(context_.frame, r, {250, 246, 244}, {255, 255, 255}) > .08;
}
double CombatVision::white(const QString &name) {
    const auto area=box(name);
    const auto strict=colorPercentage(context_.frame,area,{244,244,244},{255,255,255});
    if (strict>0) return strict;
    const auto tolerant=colorPercentage(context_.frame,area,{222,222,222},{255,255,255});
    return tolerant*area.width()*area.height()>=3 ? tolerant : 0;
}

}
