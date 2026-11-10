#pragma once
#include "navigation.hpp"
#include "echo_detector.hpp"
namespace okww::runtime {
QRect fSearchBox(TaskContext &context);
std::optional<Match> findInteract(TaskContext &context, const QString &textPattern = {});
bool isDialogInteract(TaskContext &context);
class WorldInteraction {
    TaskContext &c_;
    std::function<bool()> inCombat_;
    std::function<QVector<Match>(const cv::Mat &, double)> detect_;

  public:
    WorldInteraction(TaskContext &context, std::function<bool()> inCombat = {},
                     std::function<QVector<Match>(const cv::Mat &, double)> detect = {})
        : c_(context), inCombat_(std::move(inCombat)), detect_(std::move(detect)) {}
    bool claimDialog();
    bool dismissClaim();
    bool pick(bool handleClaim = true, bool echoOnly = false);
    bool walkUntil(const std::function<bool()> &condition, const QString &direction, double timeout,
                   bool running = false, bool required = false, double postWalk = 0);
    bool walkToInteract(const QString &direction = "w", double timeout = 1, bool required = true,
                        double backward = 0, const QString &textPattern = {}, bool checkCombat = false,
                        bool running = false);
    QVector<Match> echoes(double threshold = .3);
    bool walkToEcho(double timeout = 8, double threshold = .5, const std::function<void()> &update = {});
    std::pair<bool, bool> findEcho(bool useColor = false, bool turn = true, double timeout = 8,
                                   double threshold = .5, const std::function<void()> &update = {});
    bool circleEcho(int circles = 3);
    std::optional<Match> treasure();
    bool walkToBox(const std::function<std::optional<Match>()> &find, const std::function<bool()> &end = {},
                   double timeout = 30, double yOffset = .05, double xThreshold = .07, bool hook = false);
    void walkToTreasure(bool requireF = true);
    void centerCamera();
    double arrowAngle();
    std::optional<double> miniMapTurn(const QString &feature, double threshold = .72, double xOffset = 0,
                                      double yOffset = 0);
    bool followBossMinimap(double timeout = 15, double threshold = .5);
};
} // namespace okww::runtime
