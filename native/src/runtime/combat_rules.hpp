#pragma once
#include "vision.hpp"
#include <array>
#include <span>

namespace okww::runtime {
// These rules are shared by native characters and task combat. Times are from
// Execution's monotonic clock; never mix persisted wall-clock timestamps here.
struct Freeze {
    double start, duration, threshold;
};
class FreezeTimeline {
    QVector<Freeze> freezes_;

  public:
    void add(double now, double start, double duration = -1, double threshold = .1);
    double elapsed(double now, double start, bool introMotion = false) const;
    void clear() { freezes_.clear(); }
    const QVector<Freeze> &entries() const { return freezes_; }
};
enum class Role { MainDps, SubDps, Healer, Unclassified };
enum class SwitchPriority : int { No = 0, Low = 100, Normal = 200, High = 300, Must = 400 };
struct CharacterState {
    int index = -1;
    QString name;
    QString className;
    Role role = Role::MainDps;
    int ringIndex = -1;
    double buffTime = 14, lastBuff = -1, lastSwitch = -1, lastSwitchIn = -1, lastFullConSwitch = -1;
    double lastResonance = -1, lastEcho = -1, lastLiberation = -1, lastPerform = 0, lastOutro = -1;
    double currentCon = 0, fullRingArea = 0, confidence = 1;
    bool hasIntro = false, hasSubDpsIntro = false, current = false;
    bool resonanceReady = false, echoReady = false, liberationReady = false, hasToolBox = false;
    bool hasBuff(const FreezeTimeline &time, double now) const;
    double buffRemaining(const FreezeTimeline &time, double now) const;
    bool healerLocked(const FreezeTimeline &time, double now) const;
    bool switchCoolingDown(const FreezeTimeline &time, double now) const;
    void switchOut(bool fullCon, double now);
};
struct SwitchCandidate {
    const CharacterState *character;
    int priority = int(SwitchPriority::Normal);
};
struct SwitchDecision {
    int index;
    QString reason;
};
SwitchDecision chooseSwitch(const CharacterState &current, std::span<const SwitchCandidate> candidates,
                            bool hasIntro, const FreezeTimeline &time, double now);
void applyIntro(const CharacterState &previous, CharacterState &next, bool hasIntro);

struct ColorRange {
    cv::Scalar lower, upper;
};
const std::array<ColorRange, 6> &concertoColors();
struct RingResult {
    int area = 0;
    bool full = false;
    int components = 0;
};
RingResult countRing(const cv::Mat &bgr, ColorRange color, double minBoundingArea);
// Full-ring calibration stays per element, as in _con_full_size.json.
class Concerto {
    std::array<double, 6> fullAreas_{};

  public:
    explicit Concerto(std::array<double, 6> areas = {}) : fullAreas_(areas) {}
    double detect(const cv::Mat &frame, CharacterState &character);
    const std::array<double, 6> &fullAreas() const { return fullAreas_; }
    static QRect region(QSize screen);
};
} // namespace okww::runtime
