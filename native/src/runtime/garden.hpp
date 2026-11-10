#pragma once
#include "navigation.hpp"
namespace okww::runtime {
struct GardenCard {
    Match box;
    int rarity = 0;
    bool recommended = false, preferred = false;
};
struct GardenItem {
    int index = 0;
    std::optional<int> price;
    bool recommended = false;
    Match box;
};
struct PurchaseControls {
    std::optional<TextLine> buy;
    std::optional<int> price;
    bool sold = false;
};
QString normalizeGardenStyle(QString text);
QString preferredGardenStyle(const QJsonObject &config);
bool gardenPointsComplete(const QVector<TextLine> &lines);
int selectGardenCard(const QVector<GardenCard> &cards);
int gardenRarity(const cv::Mat &image, QRect label);
double recommendationScore(const cv::Mat &image, const cv::Mat &badge, QRect bounds, double scale);
QVector<GardenItem> affordableGardenItems(QVector<GardenItem> items, int money);
PurchaseControls gardenPurchaseControls(const QVector<TextLine> &lines);
class Garden {
    TaskContext &c_;
    Navigation navigation_;
    cv::Mat badge_;
    QJsonObject config_;
    std::optional<QPair<int, int>> checkedTurn_;
    QMap<QPair<int, int>, int> attempts_;
    bool blessingOpen_ = false, initialBlessingChecked_ = false;
    double speedChecked_ = -1e12;
    int speedAttempts_ = 0;
    std::optional<QPair<int, int>> turn();
    std::pair<std::optional<TextLine>, std::optional<int>> blessingControls();
    bool useBlessing(bool open = false);
    bool maximizeSpeed();
    std::optional<Match> bestFeature();
    bool active();
    void loop();
    void openWeekly();
    std::optional<int> money();
    QVector<GardenItem> inventory();
    bool waitPurchase(int before, int price);

  public:
    Garden(TaskContext &context, const QString &resources);
    std::optional<Match> rewardChoice();
    std::optional<int> weeklyProgress();
    int shop();
    void run();
};
} // namespace okww::runtime
