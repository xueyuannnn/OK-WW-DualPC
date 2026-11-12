#pragma once
#include "combat_session.hpp"
#include "instance_flow.hpp"
namespace okww::runtime {
struct NestTarget {
    QPointF point;
    QString key;
    int tab = 0;
};
std::optional<NestTarget> nextNestTarget(const QVector<TextLine> &lines, QSize screen, int tab,
                                         const QSet<QString> &unreachable);
class NightmareFarm {
    TaskContext &c_;
    CombatSession &combat_;
    Navigation navigation_;
    WorldInteraction world_;
    QSet<QString> unreachable_;
    QMap<QString, int> deaths_;
    bool capture_ = false, captured_ = false;
    void tab(int index);
    bool travel(const NestTarget &target);
    void fight(const NestTarget &target);
    bool capturedNotice();
    bool walkEcho();
    bool waitCombat(double timeout);

  public:
    NightmareFarm(TaskContext &context, CombatSession &combat, EchoDetector &detector)
        : c_(context), combat_(combat), navigation_(context),
          world_(
              context, [&combat] { return combat.active(); },
              [&detector, &context](const cv::Mat &frame, double threshold) {
                  return detector.detect(frame, threshold, 0, [&] { context.execution.checkpoint(); });
              }) {}
    bool run(bool captureOnly = false, const QJsonObject &config = {});
};
} // namespace okww::runtime
