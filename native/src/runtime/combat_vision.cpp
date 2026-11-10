#include "combat_vision.hpp"
#include <opencv2/imgproc.hpp>
namespace okww::runtime {
int segmentedGaugeValue(const cv::Mat &image, ColorRange color, int segments, int minFrequency,
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
QVector<Match> colorRectangles(const cv::Mat &frame, ColorRange color, double minWidth, double minHeight,
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
QVector<Match> shiftedBarCandidates(const cv::Mat &frame) {
    const int h = frame.rows, w = frame.cols;
    const double scale = h / 1080.;
    cv::Mat hsv;
    cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);
    const cv::Mat kernel = cv::Mat::ones(1, std::max(3, pixelRound(3. * w / 1920)), CV_8U);
    QVector<Match> boxes;
    for (int value : {75, 110, 150, 190, 230}) {
        cv::Mat mask, upper, joined;
        cv::inRange(hsv, cv::Scalar(0, 70, value), cv::Scalar(18, 255, 255), mask);
        cv::inRange(hsv, cv::Scalar(170, 70, value), cv::Scalar(179, 255, 255), upper);
        mask |= upper;
        cv::morphologyEx(mask, joined, cv::MORPH_CLOSE, kernel);
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(joined, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        for (const auto &contour : contours) {
            auto b = cv::boundingRect(contour);
            if (b.width < pixelRound(50. * w / 1920) || b.width > pixelRound(345. * w / 1920) ||
                b.height < std::max(2, pixelRound(4 * scale)) ||
                b.height > std::max(3, pixelRound(14 * scale)) || b.width < b.height * 6 || b.y <= h * .08 ||
                b.y >= h * .85)
                continue;
            const double filled = double(cv::countNonZero(mask(b))) / b.area();
            auto core = b.height > 2 ? cv::Rect(b.x + 1, b.y + 1, b.width - 2, b.height - 2) : b;
            if (filled >= .8 && double(cv::countNonZero(mask(core))) / core.area() >= .95)
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
    return result;
}
double CombatVision::cdX(int width, int height, double x) {
    return 1 - (1 - x) * std::min(double(width), height * 16. / 9) / width;
}
void CombatVision::refreshCooldowns(int index) {
    if (cdVersion_ == context_.frameVersion || index < 0)
        return;
    Cooldowns cds;
    cds.timestamp = context_.execution.now();
    const int w = context_.frame.cols, h = context_.frame.rows;
    const auto area = relativeBox(context_.frame, cdX(w, h, .82), .86, cdX(w, h, .97), .93);
    const QRegularExpression regex("\\d{1,2}\\.\\d");
    for (const auto &line : context_.text(area, .8, Filter::InvertWhite)) {
        auto match = regex.match(line.text);
        if (!match.hasMatch())
            continue;
        bool valid = false;
        double seconds = line.text.toDouble(&valid);
        if (!valid)
            seconds = match.captured().toDouble();
        const int skill = line.box.x() < int(w * cdX(w, h, .86))   ? 0
                          : line.box.x() > int(w * cdX(w, h, .91)) ? 2
                                                                   : 1;
        cds.seconds[skill] = seconds;
    }
    cooldowns_[index] = cds;
    cdVersion_ = context_.frameVersion;
}
double CombatVision::cooldown(Skill skill, int index, const FreezeTimeline &time, double now) const {
    if (skill == Skill::ExtraAction || !cooldowns_.contains(index))
        return 0;
    const auto &cds = cooldowns_[index];
    return cds.seconds[int(skill)] - time.elapsed(now, cds.timestamp);
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
    auto match = best(scaled(box(has), 1.1, 1.1), names, .6);
    if (!match)
        match = best(box("box_target_enemy_long"), names, .6);
    if (!match)
        match = best(box("target_box_long2"), names, .6);
    if (!match && allowShort)
        match = best(box("target_box_short"), names, .6);
    bool expanded = false;
    if (!match && extended) {
        match = best(scaled(box(has), 1.1, 2), names, .6);
        expanded = bool(match);
    }
    return {bool(match), match && match->name == has, expanded};
}
bool CombatVision::healthBar(bool inCombat) {
    const auto &image = context_.frame;
    const int minHeight = int(image.rows * 9. / 2160);
    const int minWidth = int(image.cols * (inCombat ? 12. : 100) / 3840);
    if (!colorRectangles(image, {{55, 55, 174}, {76, 85, 225}}, minWidth, minHeight, minHeight * 3).isEmpty())
        return true;
    auto bossBox =
        Templates::anchored({1269, 58, 1264, 142}, {3840, 2160}, {image.cols, image.rows}, true, true);
    if (colorRectangles(image, {{4, 30, 245}, {75, 185, 255}}, minWidth, minHeight * 1.3, -1, bossBox)
            .size() == 1)
        return true;
    return false;
}
bool CombatVision::shiftedHealthBar() {
    const auto &image = context_.frame;
    const int w = image.cols, h = image.rows;
    const double scale = h / 1080.;
    const QString level = "(?<![A-Za-z])Lv[\\s.·]*\\d{1,3}(?!\\d)";
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
            if (matchingText(context_.text(.3, 0, .7, .052, .65), level))
                return true;
    }
    for (const auto &bar : shiftedBarCandidates(image)) {
        const auto b = bar.box;
        const int x = std::max(0, b.x() - pixelRound(12 * scale)),
                  y = std::max(0, b.y() - pixelRound(48 * scale));
        const int right = std::min(w, b.x() + b.width() + pixelRound(55 * scale)),
                  bottom = std::max(y + 1, b.y() - pixelRound(3 * scale));
        if (matchingText(context_.text(QRect(x, y, right - x, bottom - y), .65), level))
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
    return colorPercentage(context_.frame, box(name), {244, 244, 244}, {255, 255, 255});
}
} // namespace okww::runtime
