#pragma once
#include "interaction.hpp"
namespace okww::runtime {
enum class RestartKind { None, Resource, Restart, Confirm };
struct RestartDialog {
    RestartKind kind = RestartKind::None;
    std::optional<TextLine> button;
};
RestartDialog restartDialog(const QVector<TextLine> &lines);
bool combatIntroVisible(TaskContext &context);
std::optional<TextLine> nestExitConfirmation(const QVector<TextLine> &lines, int width);
bool nestExitContext(const QVector<TextLine> &lines);
bool isNestExitText(QString text);
bool isNestCompleteText(QString text);
enum class RestartAction { Observe, Wait, Menu, Click, Complete, Fail };
class RestartTracker {
    rust::Restart state_;

  public:
    RestartAction step(RestartKind kind, bool hud, bool realm);
};
void restartBossDomain(TaskContext &context);
bool completedNest(TaskContext &context);
void leaveNest(TaskContext &context, bool sendEscape = true);
} // namespace okww::runtime
