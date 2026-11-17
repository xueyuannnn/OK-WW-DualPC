#include "combat_rules.hpp"
#include "workflow_abi.hpp"
#include <opencv2/imgproc.hpp>
#include <tuple>

namespace okww::runtime {

FreezeTimeline::FreezeTimeline() : state_(nullptr) {
    rust::verifyAbi();
    state_ = rust::okww_rs_freeze_new();
    if (!state_)
        throw std::bad_alloc();
}
FreezeTimeline::~FreezeTimeline() { rust::okww_rs_freeze_free(state_); }
FreezeTimeline::FreezeTimeline(const FreezeTimeline &other)
    : state_(rust::okww_rs_freeze_clone(other.state_)) {
    if (!state_)
        throw std::bad_alloc();
}
FreezeTimeline &FreezeTimeline::operator=(const FreezeTimeline &other) {
    if (this != &other) {
        auto next = rust::okww_rs_freeze_clone(other.state_);
        if (!next)
            throw std::bad_alloc();
        rust::okww_rs_freeze_free(state_);
        state_ = next;
        dirty_ = true;
    }
    return *this;
}
FreezeTimeline::FreezeTimeline(FreezeTimeline &&other) noexcept
    : state_(std::exchange(other.state_, nullptr)), cache_(std::move(other.cache_)), dirty_(other.dirty_) {
    other.dirty_ = true;
}
FreezeTimeline &FreezeTimeline::operator=(FreezeTimeline &&other) noexcept {
    if (this != &other) {
        rust::okww_rs_freeze_free(state_);
        state_ = std::exchange(other.state_, nullptr);
        cache_ = std::move(other.cache_);
        dirty_ = other.dirty_;
        other.dirty_ = true;
    }
    return *this;
}
void FreezeTimeline::add(double now, double start, double duration, double threshold) {
    if (!state_) {
        state_ = rust::okww_rs_freeze_new();
        if (!state_)
            throw std::bad_alloc();
    }
    const auto status = rust::okww_rs_freeze_add(state_, now, start, duration, threshold);
    if (status == 2)
        throw std::bad_alloc();
    if (status != 1)
        fail("Rust 冻结计时接口异常");
    dirty_ = true;
}
double FreezeTimeline::elapsed(double now, double start, bool intro) const {
    return rust::okww_rs_freeze_elapsed(state_, now, start, intro);
}
void FreezeTimeline::clear() {
    rust::okww_rs_freeze_clear(state_);
    cache_.clear();
    dirty_ = false;
}
const QVector<Freeze> &FreezeTimeline::entries() const {
    if (dirty_) {
        cache_.resize(qsizetype(rust::okww_rs_freeze_count(state_)));
        if (!rust::okww_rs_freeze_entries(state_, cache_.data(), size_t(cache_.size())))
            fail("Rust 冻结计时同步失败");
        dirty_ = false;
    }
    return cache_;
}
namespace {
rust::Character marshal(const CharacterState &c) {
    return {c.index,
            int(c.role),
            c.buffTime,
            c.lastBuff,
            c.lastSwitch,
            c.lastSwitchIn,
            c.lastFullConSwitch,
            c.currentCon,
            uint32_t((c.current ? 1 : 0) | (c.hasIntro ? 2 : 0) | (c.hasSubDpsIntro ? 4 : 0)),
            0};
}
void apply(CharacterState &c, const rust::Character &s) {
    c.lastSwitch = s.lastSwitch;
    c.lastBuff = s.lastBuff;
    c.lastFullConSwitch = s.lastFullConSwitch;
    c.currentCon = s.currentCon;
    c.current = s.flags & 1;
    c.hasIntro = s.flags & 2;
    c.hasSubDpsIntro = s.flags & 4;
}
double status(const CharacterState &c, const FreezeTimeline &time, double now, int kind) {
    const auto state = marshal(c);
    return rust::okww_rs_character_status(time.nativeHandle(), &state, now, kind);
}
} // namespace
bool CharacterState::hasBuff(const FreezeTimeline &time, double now) const {
    return status(*this, time, now, 0) != 0;
}
double CharacterState::buffRemaining(const FreezeTimeline &time, double now) const {
    return status(*this, time, now, 1);
}
bool CharacterState::healerLocked(const FreezeTimeline &time, double now) const {
    return status(*this, time, now, 2) != 0;
}
bool CharacterState::switchCoolingDown(const FreezeTimeline &time, double now) const {
    return status(*this, time, now, 3) != 0;
}
void CharacterState::switchOut(bool fullCon, double now) {
    auto s = marshal(*this);
    rust::okww_rs_switch_out(&s, fullCon, now);
    apply(*this, s);
}
void applyIntro(const CharacterState &previous, CharacterState &next, bool intro) {
    auto s = marshal(next);
    rust::okww_rs_apply_intro(int(previous.role), &s, intro);
    apply(next, s);
}
SwitchDecision chooseSwitch(const CharacterState &current, std::span<const SwitchCandidate> candidates,
                            bool intro, const FreezeTimeline &time, double now) {
    std::vector<rust::Candidate> input;
    input.reserve(candidates.size());
    for (const auto &candidate : candidates)
        input.push_back({candidate.character ? marshal(*candidate.character) : rust::Character{},
                         candidate.priority, bool(candidate.character)});
    const auto c = marshal(current);
    rust::Decision out{};
    if (!rust::okww_rs_choose_switch(&c, input.data(), input.size(), intro, time.nativeHandle(), now, &out))
        fail("Rust 切人规则接口异常");
    static const QStringList reasons{"no_candidate_above_no_priority",
                                     "",
                                     "intro_unbuffed_healer",
                                     "intro_unbuffed_subdps",
                                     "unbuffed_healer",
                                     "unbuffed_subdps",
                                     "intro_role_order_main_sub_healer",
                                     "lowest_support_buff_remaining",
                                     "support_buffs_active_return_to_main_dps",
                                     "fallback_role_order"};
    if (out.reason < 0 || out.reason >= reasons.size())
        fail("Rust 切人结果无效");
    return {out.index, out.reason == 1 ? QString("priority_%1").arg(out.priority) : reasons[out.reason]};
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
    cv::Mat mask, masked, raw, closed, labels, stats, centroids, component;
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Point> polygon;
    const std::array<double, 1> args{minBoundingArea};
    const auto result = rust::workflowRun(
        rust::WorkflowMethod::CountConcertoRing, args,
        [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::RingImageEmpty:
                out[0] = bgr.empty();
                break;
            case rust::WorkflowOp::RingImageBgr:
                out[0] = bgr.type() == CV_8UC3;
                break;
            case rust::WorkflowOp::RingHeight:
                out[0] = bgr.rows;
                break;
            case rust::WorkflowOp::RingWidth:
                out[0] = bgr.cols;
                break;
            case rust::WorkflowOp::RingInvalidImage:
                fail("协奏环图像无效");
            case rust::WorkflowOp::RingCreateMask:
                mask = cv::Mat::zeros(bgr.size(), CV_8UC1);
                break;
            case rust::WorkflowOp::RingDrawOuter:
                cv::circle(mask, {int(a[0]), int(a[1])}, int(a[2]), 255, -1);
                break;
            case rust::WorkflowOp::RingDrawInner:
                cv::circle(mask, {int(a[0]), int(a[1])}, int(a[2]), 0, -1);
                break;
            case rust::WorkflowOp::RingMaskImage:
                cv::bitwise_and(bgr, bgr, masked, mask);
                break;
            case rust::WorkflowOp::RingThreshold:
                cv::inRange(masked, color.lower, color.upper, raw);
                break;
            case rust::WorkflowOp::RingClose:
                cv::morphologyEx(raw, closed, cv::MORPH_CLOSE,
                                 cv::getStructuringElement(cv::MORPH_RECT, {3, 3}));
                break;
            case rust::WorkflowOp::RingRestoreStripe: {
                const cv::Rect stripe{int(a[0]), int(a[1]), int(a[2]), int(a[3])};
                raw(stripe).copyTo(closed(stripe));
                break;
            }
            case rust::WorkflowOp::RingComponents:
                out[0] = cv::connectedComponentsWithStats(closed, labels, stats, centroids, 8);
                break;
            case rust::WorkflowOp::RingStatistic: {
                static constexpr std::array<int, 3> columns{cv::CC_STAT_WIDTH, cv::CC_STAT_HEIGHT,
                                                            cv::CC_STAT_AREA};
                out[0] = stats.at<int>(int(a[0]), columns.at(int(a[1])));
                break;
            }
            case rust::WorkflowOp::RingSelectComponent:
                component = labels == int(a[0]);
                break;
            case rust::WorkflowOp::RingFindContours:
                cv::findContours(component, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
                break;
            case rust::WorkflowOp::RingContourCount:
                out[0] = double(contours.size());
                break;
            case rust::WorkflowOp::RingContourArcLength:
                out[0] = cv::arcLength(contours[0], true);
                break;
            case rust::WorkflowOp::RingApproxPolygon:
                cv::approxPolyDP(contours[0], polygon, a[0], true);
                break;
            case rust::WorkflowOp::RingPolygonSize:
                out[0] = double(polygon.size());
                break;
            case rust::WorkflowOp::RingPolygonConvex:
                out[0] = cv::isContourConvex(polygon);
                break;
            case rust::WorkflowOp::RingDropPolygon:
                polygon = decltype(polygon){};
                break;
            case rust::WorkflowOp::RingDropComponent:
                contours = decltype(contours){};
                component.release();
                break;
            default:
                fail("Rust 协奏圆环宿主操作无效");
            }
        });
    return {int(result[0]), result[1] != 0, int(result[2])};
}
QRect Concerto::region(QSize screen) {
    return Templates::anchored({1431, 1942, 126, 126}, {3840, 2160}, screen, true, false);
}
double Concerto::detect(const cv::Mat &frame, CharacterState &character) {
    QRect box;
    return rust::workflowRun(
        rust::WorkflowMethod::DetectConcerto, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::ConcertoRegion:
                box = region({frame.cols, frame.rows});
                break;
            case rust::WorkflowOp::ConcertoRegionContained:
                out[0] = QRect(0, 0, frame.cols, frame.rows).contains(box);
                break;
            case rust::WorkflowOp::ConcertoInvalidRegion:
                fail("协奏环区域超出画面");
            case rust::WorkflowOp::ConcertoRingIndex:
                out[0] = character.ringIndex;
                break;
            case rust::WorkflowOp::ConcertoSetRingIndex:
                character.ringIndex = int(a[0]);
                break;
            case rust::WorkflowOp::ConcertoColorRatio: {
                const auto color = concertoColors().at(int(a[0]));
                out[0] = colorPercentage(frame, box, color.lower, color.upper);
                break;
            }
            case rust::WorkflowOp::ConcertoInvalidIndex:
                fail("角色属性索引无效");
            case rust::WorkflowOp::ConcertoFrameWidth:
                out[0] = frame.cols;
                break;
            case rust::WorkflowOp::ConcertoFrameHeight:
                out[0] = frame.rows;
                break;
            case rust::WorkflowOp::ConcertoCountRing: {
                const auto ring = countRing(frame(cv::Rect(box.x(), box.y(), box.width(), box.height())),
                                            concertoColors()[character.ringIndex], a[0]);
                out[0] = ring.area;
                out[1] = ring.full;
                out[2] = ring.components;
                break;
            }
            case rust::WorkflowOp::ConcertoSetFullArea:
                fullAreas_[character.ringIndex] = a[0];
                break;
            case rust::WorkflowOp::ConcertoFullArea:
                out[0] = fullAreas_[character.ringIndex];
                break;
            case rust::WorkflowOp::ConcertoSetCurrent:
                character.currentCon = a[0];
                break;
            default:
                fail("Rust 协奏校准宿主操作无效");
            }
        })[0];
}
} // namespace okww::runtime
