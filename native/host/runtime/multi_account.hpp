#pragma once
#include "navigation.hpp"
namespace okww::runtime {
QString normalizeAccount(QString value);
class AccountCycle {
    QSet<QString> completed_;

  public:
    void complete(const QString &account);
    bool completed(const QString &account) const;
    std::optional<TextLine> next(const QVector<TextLine> &accounts) const;
    int count() const;
};
class MultiAccountDaily {
    TaskContext &c_;
    Navigation navigation_;
    QString activeAccount_;
    std::optional<TextLine> rustRun(int method, AccountCycle *cycle = nullptr,
                                    const std::function<void()> &daily = {},
                                    QVector<TextLine> *result = nullptr);
    QVector<TextLine> accounts();
    void logout();
    TextLine currentAccount();
    std::optional<TextLine> choose(AccountCycle &cycle);

  public:
    explicit MultiAccountDaily(TaskContext &context) : c_(context), navigation_(context) {}
    void run(const std::function<void()> &daily);
    QString activeAccount() const { return activeAccount_; }
};
QJsonObject accountDailyOverrides(const Profile &profile, const QString &account);
} // namespace okww::runtime
