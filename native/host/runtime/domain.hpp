#pragma once
#include "combat_session.hpp"
#include "interaction.hpp"
#include "stamina.hpp"
namespace okww::runtime {
enum class DomainKind { Tacet, Forgery, Simulation };
struct FarmResult {
    int staminaUsed = 0, completed = 0, recoveries = 0;
    bool noStamina = false;
};
class DomainFarm {
    TaskContext &c_;
    CombatSession &combat_;
    Navigation navigation_;
    WorldInteraction world_;
    void enter(DomainKind kind, const QJsonObject &config);
    void nextRound(DomainKind kind);
    void leave(DomainKind kind);
    FarmResult rustRun(int method, std::initializer_list<double> args, QJsonObject config = {});

  public:
    std::function<void(int)> onStaminaSpent;
    DomainFarm(TaskContext &context, CombatSession &combat)
        : c_(context), combat_(combat), navigation_(context),
          world_(context, [&combat] { return combat.active(); }) {}
    FarmResult run(DomainKind kind, bool counted = false);
    FarmResult farm(DomainKind kind, int mustUse, bool exactCount, const QJsonObject &config,
                    bool reuseBook = false);
};
int domainCost(DomainKind kind);
int domainCountLimit(int requested, int totalStamina, int cost);
} // namespace okww::runtime
