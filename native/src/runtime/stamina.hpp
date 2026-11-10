#pragma once
#include "task.hpp"
namespace okww::runtime {
struct StaminaValues {
    int current = -1, reserve = -1, total = -1;
};
StaminaValues staminaValues(const QVector<TextLine> &lines);
StaminaValues readStamina(TaskContext &context);
struct StaminaSpend {
    int used = 0;
    bool repeat = false;
};
StaminaSpend planStaminaSpend(StaminaValues before, int once, int remaining, bool exactCount);
StaminaSpend claimStamina(TaskContext &context, int once, int remaining, bool exactCount = false);
class CubeBudget {
    QString path_, day_;
    int limit_;
    QJsonObject data_;
    bool owned_ = false;
    QJsonObject read(bool allowPending) const;

  public:
    CubeBudget(QString path, int limit = 4, QDateTime now = QDateTime::currentDateTime());
    int remaining() const { return std::max(0, limit_ - data_["reserved"].toInt()); }
    int used() const { return data_["reserved"].toInt(); }
    int limit() const { return limit_; }
    bool pending() const { return data_["pending"].toBool(); }
    void reserve();
    void verify();
};
// This is an explicit material action, never wrapped in navigation retries.
bool useOneDailyCube(TaskContext &context, CubeBudget &budget, bool consume = true);
} // namespace okww::runtime
