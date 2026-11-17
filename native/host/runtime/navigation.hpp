#pragma once
#include "task.hpp"
namespace okww::runtime {
class NavigationFailure : public std::runtime_error {
  public:
    explicit NavigationFailure(const QString &reason) : std::runtime_error(reason.toUtf8().constData()) {}
};
// Recovery retries navigation/observation checkpoints only. Resource decisions
// and actions are never repeated by this wrapper.
class Navigation {
    TaskContext &c_;
    int retryLimit() const;
    void retry(const QString &step, const std::function<void()> &operation);
    bool observe(const std::function<bool()> &condition, double timeout, double settle = 0);
    std::optional<Match> waitMatch(const QStringList &names, double timeout, MatchOptions options = {},
                                   double settle = 0);
    std::optional<Match> waypoint();

  public:
    explicit Navigation(TaskContext &context) : c_(context) {}
    bool inRealm();
    bool inWorld();
    bool waitTeam(double timeout = 10, bool required = true, bool back = false);
    void ensureMain(double timeout = 30, bool back = true);
    void confirmDialog(bool right = true);
    void exitRealm(double timeout = 120);
    Match openBook(const QString &feature = "gray_book_all_monsters");
    void bookCategory(const QString &category, double after = 2);
    TextLine selectMonster(const QString &name, int maxPages = 40);
    void teleportNearest();
    void healAtBeacon();
    void startChallenge();
    void waitTravel();
    bool selectBookEntry(int serial, int total, const QVector<int> &structure = {});
};
QString normalizeNavigationText(const QString &value);
double bookScrollPosition(int serial, int total, const QVector<int> &structure, double top);
} // namespace okww::runtime
