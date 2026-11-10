#pragma once
#include "daily.hpp"
namespace okww::runtime {
QVector<TaskDefinition> businessTasks(const std::function<bool(const QString &)> &dispatch);
class TaskRunner {
    TaskContext &c_;
    CombatSession combat_;
    EchoDetector &detector_;
    QString root_, data_;
    int deathRecoveries_ = 0;
    double recoveredAt_ = -1e12;

  public:
    TaskRunner(TaskContext &context, EchoDetector &detector, QString root, QString data,
               std::shared_ptr<TeamStore> teams = {})
        : c_(context), combat_(context, root, data + "/configs-dualpc", std::move(teams)),
          detector_(detector), root_(std::move(root)), data_(std::move(data)) {}
    bool dispatch(const QString &id);
    void pollControls() { combat_.pollControls(); }
};
} // namespace okww::runtime
