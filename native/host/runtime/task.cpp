#include "task.hpp"
#include "task_abi.hpp"
#include <opencv2/imgproc.hpp>
namespace okww::runtime {
void TaskContext::beginWorkflow(QJsonArray steps, QString timingPath) {
    rustRun(int(rust::TaskMethod::BeginWorkflow), {}, {{"steps", steps}, {"timing_path", timingPath}});
}
void TaskContext::workflowStep(const QString &id, const QString &state, const QString &detail) {
    rustRun(int(rust::TaskMethod::WorkflowStep), {}, {{"id", id}, {"state", state}, {"detail", detail}});
}
void TaskContext::publishWorkflow() { rustRun(int(rust::TaskMethod::PublishWorkflow)); }
void TaskContext::reportProgress(QString stage, double current, double total, QString unit, QString detail) {
    rustRun(int(rust::TaskMethod::ReportProgress), {current, total},
            {{"stage", stage}, {"unit", unit}, {"detail", detail}});
}
TaskContext::TaskContext(Execution &e, Input &i, Templates &t, Profile &p, FrameSource source,
                         TextReader reader)
    : source_(std::move(source)), textReader_(std::move(reader)), execution(e), input(i), templates(t),
      profile(p) {}
void TaskContext::refresh() { rustRun(int(rust::TaskMethod::Refresh)); }
void TaskContext::sleep(double seconds) { rustRun(int(rust::TaskMethod::Sleep), {seconds}); }

QVector<TextLine> TaskContext::text(double x, double y, double right, double bottom, double threshold) {
    return text(gameBox(frame, x, y, right, bottom), threshold);
}

QString TaskContext::hotkey(const QString &setting, const QString &fallback) const {
    return profile.value("Game Hotkey", setting, fallback).toString();
}
bool TaskContext::inTeam() { return party().visible; }
PartyHud TaskContext::party() {
    const auto result = rustRun(int(rust::TaskMethod::Party));
    return {result[0] != 0, int(result[1]), int(result[2])};
}
bool TaskContext::monthlyCard() { return rustRun(int(rust::TaskMethod::MonthlyCard))[0] != 0; }
void TaskContext::monthlyWindow() { rustRun(int(rust::TaskMethod::MonthlyWindow)); }
bool TaskContext::skipConfirm() { return rustRun(int(rust::TaskMethod::SkipConfirm))[0] != 0; }
bool TaskContext::travelButton() { return rustRun(int(rust::TaskMethod::TravelButton))[0] != 0; }
bool TaskContext::fastTravel() { return rustRun(int(rust::TaskMethod::FastTravel))[0] != 0; }
bool TaskContext::login() { return rustRun(int(rust::TaskMethod::Login))[0] != 0; }
bool TaskContext::autoPick() { return rustRun(int(rust::TaskMethod::AutoPick))[0] != 0; }
bool TaskContext::autoDialog() { return rustRun(int(rust::TaskMethod::AutoDialog))[0] != 0; }
QVector<TaskDefinition> builtinTasks() {
    return {{"AutoLoginTask", true, 5, [](TaskContext &c) { return c.login(); }},
            {"FastTravelTask", true, .1, [](TaskContext &c) { return c.fastTravel(); }},
            {"AutoPickTask", true, .1, [](TaskContext &c) { return c.autoPick(); }},
            {"AutoDialogTask", true, .5, [](TaskContext &c) { return c.autoDialog(); }}};
}
} // namespace okww::runtime
