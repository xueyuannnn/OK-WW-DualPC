#include "combat_rules.hpp"
#include <opencv2/imgproc.hpp>
#include <tuple>

namespace okww::runtime {
void FreezeTimeline::add(double now, double start, double duration, double threshold) {
    if (duration < 0)
        duration = now - start;
    if (start > 0 && duration > threshold) {
        freezes_.removeIf([&](const Freeze &f) { return f.start <= now - 60; });
        freezes_.append({start, duration, threshold});
    }
}
double FreezeTimeline::elapsed(double now, double start, bool introMotion) const {
    if (start < 0)
        return 10000;
    double frozen = 0;
    for (const auto &f : freezes_) {
        if (start >= f.start)
            continue;
        if (f.threshold == -100 && !introMotion)
            continue;
        frozen += f.duration - (f.threshold == -100 ? 0 : f.threshold);
    }
    return now - start - frozen;
}
bool CharacterState::hasBuff(const FreezeTimeline &time, double now) const {
    return buffTime > 0 && lastBuff > 0 && time.elapsed(now, lastBuff) < buffTime;
}
double CharacterState::buffRemaining(const FreezeTimeline &time, double now) const {
    return hasBuff(time, now) ? std::max(0., buffTime - time.elapsed(now, lastBuff)) : 0;
}
bool CharacterState::healerLocked(const FreezeTimeline &time, double now) const {
    return role == Role::Healer && lastFullConSwitch >= 0 && time.elapsed(now, lastFullConSwitch) < 16;
}
bool CharacterState::switchCoolingDown(const FreezeTimeline &time, double now) const {
    return time.elapsed(now, lastSwitch) <= 1;
}
void CharacterState::switchOut(bool fullCon, double now) {
    lastSwitch = now;
    current = hasIntro = hasSubDpsIntro = false;
    if (fullCon || currentCon == 1) {
        if (buffTime > 0)
            lastBuff = now;
        if (role == Role::Healer)
            lastFullConSwitch = now;
        currentCon = 0;
    }
}
void applyIntro(const CharacterState &previous, CharacterState &next, bool hasIntro) {
    next.hasIntro = hasIntro;
    next.hasSubDpsIntro = hasIntro && previous.role == Role::SubDps;
}
SwitchDecision chooseSwitch(const CharacterState &current, std::span<const SwitchCandidate> input,
                            bool hasIntro, const FreezeTimeline &time, double now) {
    using Chars = QVector<const CharacterState *>;
    Chars candidates;
    int highest = 0;
    for (const auto &candidate : input) {
        auto c = candidate.character;
        if (!c || c->index == current.index || c->healerLocked(time, now) || candidate.priority <= 0)
            continue;
        if (candidate.priority > highest) {
            candidates.clear();
            highest = candidate.priority;
        }
        if (candidate.priority == highest)
            candidates.append(c);
    }
    if (candidates.isEmpty())
        return {current.index, "no_candidate_above_no_priority"};
    auto oldest = [](const Chars &chars) -> const CharacterState * {
        if (chars.isEmpty())
            return nullptr;
        return *std::min_element(chars.begin(), chars.end(), [](auto a, auto b) {
            return std::tie(a->lastSwitchIn, a->index) < std::tie(b->lastSwitchIn, b->index);
        });
    };
    auto filtered = [&](const auto &predicate) {
        Chars result;
        for (auto c : candidates)
            if (predicate(*c))
                result.append(c);
        return result;
    };
    auto support = [&]() -> const CharacterState * {
        for (Role role : {Role::Healer, Role::SubDps}) {
            if (auto result = oldest(filtered([&](const auto &c) {
                    return c.role == role && c.buffTime > 0 && !c.hasBuff(time, now);
                })))
                return result;
        }
        return nullptr;
    };
    const CharacterState *target = nullptr;
    if (hasIntro) {
        if (highest >= int(SwitchPriority::Must))
            return {oldest(candidates)->index, QString("priority_%1").arg(highest)};
        target = support();
        if (!target)
            for (Role role : {Role::MainDps, Role::SubDps, Role::Healer}) {
                target = oldest(filtered([&](const auto &c) { return c.role == role; }));
                if (target)
                    break;
            }
    } else {
        auto available = filtered([&](const auto &c) { return !c.switchCoolingDown(time, now); });
        if (!available.isEmpty())
            candidates = std::move(available);
        if (current.role == Role::MainDps) {
            auto buffers = filtered([](const auto &c) { return c.role != Role::MainDps && c.buffTime > 0; });
            if (!buffers.isEmpty())
                target = *std::min_element(buffers.begin(), buffers.end(), [&](auto a, auto b) {
                    return std::tuple(a->buffRemaining(time, now), a->lastSwitchIn, a->index) <
                           std::tuple(b->buffRemaining(time, now), b->lastSwitchIn, b->index);
                });
        }
        if (!target && current.role != Role::MainDps && current.buffTime > 0)
            target = oldest(filtered([&](const auto &c) {
                return c.role != Role::MainDps && c.buffTime > 0 && !c.hasBuff(time, now);
            }));
        if (!target && current.role != Role::MainDps)
            target = oldest(filtered([](const auto &c) { return c.role == Role::MainDps; }));
        if (!target)
            target = support();
        if (!target)
            target = oldest(filtered([](const auto &c) { return c.role == Role::MainDps; }));
    }
    if (!target)
        target = oldest(candidates);
    const QString role = target->role == Role::Healer ? "healer" : "subdps";
    QString reason;
    if (target->index != current.index && target->role != Role::MainDps && !target->hasBuff(time, now))
        reason = (hasIntro ? "intro_unbuffed_" : "unbuffed_") + role;
    else if (hasIntro)
        reason = "intro_role_order_main_sub_healer";
    else if (current.role == Role::MainDps && target->role != Role::MainDps)
        reason = "lowest_support_buff_remaining";
    else if (current.role != Role::MainDps && target->role == Role::MainDps)
        reason = "support_buffs_active_return_to_main_dps";
    else
        reason = "fallback_role_order";
    return {target->index, reason};
}
const std::array<ColorRange, 6> &concertoColors() {
    static const std::array<ColorRange, 6> colors{{{{90, 190, 205}, {130, 222, 235}},
                                                   {{210, 95, 150}, {249, 140, 190}},
                                                   {{75, 100, 200}, {105, 130, 230}},
                                                   {{210, 150, 60}, {245, 180, 95}},
                                                   {{155, 215, 70}, {190, 250, 110}},
                                                   {{145, 65, 190}, {175, 105, 220}}}};
    return colors;
}
RingResult countRing(const cv::Mat &bgr, ColorRange color, double minBoundingArea) {
    if (bgr.empty() || bgr.type() != CV_8UC3 || bgr.rows < 4 || bgr.cols < 4)
        fail("协奏环图像无效");
    const cv::Point center(bgr.cols / 2, bgr.rows / 2);
    cv::Mat mask = cv::Mat::zeros(bgr.size(), CV_8UC1), masked, raw, closed, labels, stats, centroids;
    cv::circle(mask, center, int(std::ceil(bgr.rows * .42261)), 255, -1);
    cv::circle(mask, center, int(std::floor(bgr.rows * .35119)), 0, -1);
    cv::bitwise_and(bgr, bgr, masked, mask);
    cv::inRange(masked, color.lower, color.upper, raw);
    cv::morphologyEx(raw, closed, cv::MORPH_CLOSE, cv::getStructuringElement(cv::MORPH_RECT, {3, 3}));
    const cv::Rect stripe(center.x + 1, center.y - 1, bgr.cols - center.x - 1, 3);
    raw(stripe).copyTo(closed(stripe));
    const int count = cv::connectedComponentsWithStats(closed, labels, stats, centroids, 8);
    RingResult result;
    for (int label = 1; label < count; ++label) {
        if (double(stats.at<int>(label, cv::CC_STAT_WIDTH)) * stats.at<int>(label, cv::CC_STAT_HEIGHT) <
            minBoundingArea)
            continue;
        cv::Mat component = labels == label;
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(component, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        if (contours.size() == 1) {
            std::vector<cv::Point> polygon;
            cv::approxPolyDP(contours[0], polygon, .05 * cv::arcLength(contours[0], true), true);
            if (polygon.size() >= 4 && cv::isContourConvex(polygon))
                result.full = true;
        }
        result.area = stats.at<int>(label, cv::CC_STAT_AREA);
        ++result.components;
    }
    if (result.components > 1) {
        result.full = false;
        result.area = 0;
    }
    return result;
}
QRect Concerto::region(QSize screen) {
    return Templates::anchored({1431, 1942, 126, 126}, {3840, 2160}, screen, true, false);
}
double Concerto::detect(const cv::Mat &frame, CharacterState &character) {
    QRect box = region({frame.cols, frame.rows});
    if (!QRect(0, 0, frame.cols, frame.rows).contains(box))
        fail("协奏环区域超出画面");
    if (character.ringIndex < 0) {
        double best = 0;
        character.ringIndex = 0;
        for (int i = 0; i < 6; ++i) {
            auto color = concertoColors()[i];
            const double p = colorPercentage(frame, box, color.lower, color.upper);
            if (p > best) {
                best = p;
                character.ringIndex = i;
            }
        }
    }
    if (character.ringIndex >= 6)
        fail("角色属性索引无效");
    const auto ring =
        countRing(frame(cv::Rect(box.x(), box.y(), box.width(), box.height())),
                  concertoColors()[character.ringIndex], 1500. / 3840 / 2160 * frame.cols * frame.rows);
    double &full = fullAreas_[character.ringIndex];
    if (ring.full)
        full = ring.area;
    const double percent = ring.full ? 1 : full > 0 ? std::min(.99, ring.area / full) : 0;
    character.currentCon = percent;
    return percent;
}
} // namespace okww::runtime
