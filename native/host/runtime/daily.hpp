#pragma once
#include "domain.hpp"
#include "echo_farm.hpp"
#include "echo_operations.hpp"
#include "garden.hpp"
#include "nightmare.hpp"
namespace okww::runtime {
struct DailyProgress {
    std::optional<int> stamina;
    int points = -1;
    std::optional<bool> echoNeeded;
    bool ready() const;
};
DailyProgress dailyProgress(const QVector<TextLine> &objectives, const QVector<TextLine> &points);
DailyProgress observeDailyProgress(TaskContext &context, QJsonObject *observation = nullptr);
DomainKind dailyDomain(const QJsonObject &config);
int dailyStaminaBatch(int progress, int cost, int available, bool preferDouble);
class Daily {
    friend struct DailyContractAccess;
    TaskContext &c_;
    CombatSession &combat_;
    EchoDetector &detector_;
    Navigation navigation_;
    QString root_, data_;
    QJsonObject config_;
    bool noProgress_ = false;
    int retries_ = 1;
    QStringList failed_;
    QMap<QString, int> dailySpent_;
    int claimedAtPoints_ = -1;
    int extrasDone_ = 0, extrasTotal_ = 0;
    QJsonObject dailyObservation_;
    DailyProgress rustActions(int method, std::initializer_list<double> args);
    void status(const QString &step);
    bool stage(const QString &name, const std::function<void()> &run, bool retryNavigation = true);
    DailyProgress open(bool collectObjectives = false);
    [[noreturn]] void observationFailure(const QString &reason);
    void completeStamina();
    void farmCount(DomainKind kind);
    DailyProgress claim();
    void mail();
    void battlePass();
    void validate();

  public:
    Daily(TaskContext &context, CombatSession &combat, EchoDetector &detector, QString root, QString data)
        : c_(context), combat_(combat), detector_(detector), navigation_(context), root_(std::move(root)),
          data_(std::move(data)) {}
    void run(bool enhanced = false);
};
} // namespace okww::runtime
