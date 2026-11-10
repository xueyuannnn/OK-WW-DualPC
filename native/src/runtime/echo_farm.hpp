#pragma once
#include "combat_session.hpp"
#include "instance_flow.hpp"
#include "stamina.hpp"
namespace okww::runtime {
struct EchoTarget {
    QString category;
    int index = 1;
};
QVector<EchoTarget> echoRotationTargets(const QJsonObject &config, int bosses = 20, int weekly = 9);
bool weeklyMaterialRewardConfirmed(const QVector<TextLine> &lines);
void awaitWeeklyMaterialReward(TaskContext &context);
class EchoFarm {
    TaskContext &c_;
    CombatSession &combat_;
    Navigation navigation_;
    WorldInteraction world_;
    QJsonObject config_;
    bool enhanced_ = false, realm_ = false, treasure_ = false, justEntered_ = false, entryAtHeal_ = false,
         bypassWait_ = false, treatAsWorld_ = false;
    QString aimBoss_, walkResult_;
    double started_ = 0, wait_ = 0, lastNight_ = -1e12;
    int drops_ = 0, totalCompleted_ = 0;
    bool teleportEnabled() const;
    void parameters();
    void checkRealm(double seconds);
    void bossLevel();
    void enterFromF();
    void teleport();
    void nearestBoss();
    void recognizeBoss();
    void interactions();
    void scrollInteract();
    bool treasureRestart();
    void treasureHunt();
    void changeNight();
    bool seekCombat(double timeout, bool move = false, bool run = false);
    void heal();
    int weeklyMaterialCount();
    int single();

  public:
    EchoFarm(TaskContext &context, CombatSession &combat, EchoDetector &detector)
        : c_(context), combat_(combat), navigation_(context),
          world_(
              context, [&combat] { return combat.active(); },
              [&detector](const cv::Mat &frame, double threshold) {
                  return detector.detect(frame, threshold);
              }) {}
    int run(bool enhanced = false, const QJsonObject &config = {});
};
} // namespace okww::runtime
