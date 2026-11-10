#pragma once
#include "character.hpp"
#include "task.hpp"
namespace okww::runtime {
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
    std::array<double, 3> seconds{};
};
struct TargetObservation {
    bool matched = false, locked = false, extended = false;
};
QVector<Match> shiftedBarCandidates(const cv::Mat &frame);
int segmentedGaugeValue(const cv::Mat &image, ColorRange color, int segments, int minFrequency,
                        int maxFrequency, double minAmplitude);
QVector<Match> colorRectangles(const cv::Mat &frame, ColorRange color, double minWidth, double minHeight,
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
    bool forte(Forte kind);
    double white(const QString &name);
    static double cdX(int width, int height, double fraction);
};
} // namespace okww::runtime
