#pragma once
#include "domain.hpp"
#include "echo_farm.hpp"
#include "echo_operations.hpp"
#include "garden.hpp"
#include "nightmare.hpp"
namespace okww::runtime {
struct DailyProgress {
    std::optional<int> stamina;
    int points = 0;
    bool ready() const { return points >= 100; }
};
DailyProgress dailyProgress(const QVector<TextLine> &objectives, const QVector<TextLine> &points);
DomainKind dailyDomain(const QJsonObject &config);
class Daily {
    TaskContext &c_;
    CombatSession &combat_;
    EchoDetector &detector_;
    Navigation navigation_;
    QString root_, data_;
    QJsonObject config_;
    bool noProgress_ = false;
    int retries_ = 1;
    QStringList failed_;
    void status(const QString &step);
    bool stage(const QString &name, const std::function<void()> &run, bool retryNavigation = true);
    DailyProgress open();
    void completeStamina();
    void farmCount(DomainKind kind);
    void claim();
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
