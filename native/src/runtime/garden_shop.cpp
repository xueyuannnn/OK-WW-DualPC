#include "garden.hpp"
namespace okww::runtime {
std::optional<int> Garden::money() {
    QVector<int> values;
    for (const auto &line : c_.text(.68, .035, .79, .09))
        if (QRegularExpression("^\\d+$").match(line.text.trimmed()).hasMatch()) {
            bool ok = false;
            const int value = line.text.trimmed().toInt(&ok);
            if (ok)
                values.append(value);
        }
    return values.size() == 1 ? std::optional(values.front()) : std::nullopt;
}
QVector<GardenItem> Garden::inventory() {
    QVector<GardenItem> items;
    const int w = c_.frame.cols, h = c_.frame.rows;
    for (int row = 0; row < 2; ++row)
        for (int column = 0; column < (row == 0 ? 6 : 2); ++column) {
            const double x = .198 + .080 * column, y = .302 + .191 * row;
            const auto labels = c_.textScaled(
                gameBox(c_.frame, x + .006, .372 + .191 * row, x + .032, .400 + .191 * row), .75);
            QVector<int> prices;
            for (const auto &line : labels) {
                const auto match = QRegularExpression("^[-−]?\\s*(\\d+)\\s*$").match(line.text.trimmed());
                if (match.hasMatch() && match.captured(1).toInt() > 0)
                    prices.append(match.captured(1).toInt());
            }
            const QRect bounds(pixelRound((x + .012) * w), pixelRound((y - .077) * h),
                               pixelRound((x + .045) * w) - pixelRound((x + .012) * w),
                               pixelRound((y - .005) * h) - pixelRound((y - .077) * h));
            const bool recommended = recommendationScore(c_.frame, badge_, bounds, w / 1920. * .72) >= .88;
            const auto price = prices.size() == 1 ? std::optional(prices[0]) : std::nullopt;
            if (!price && !recommended)
                continue;
            items.append({row * 6 + column,
                          price,
                          recommended,
                          {QString("garden_shop_item_%1").arg(row * 6 + column + 1),
                           {pixelRound((x - .018) * w), pixelRound((y - .03) * h), pixelRound(.036 * w),
                            pixelRound(.06 * h)},
                           1}});
        }
    return items;
}
bool Garden::waitPurchase(int before, int price) {
    for (int i = 0; i < 12; ++i) {
        c_.refresh();
        const auto current = money();
        if (current && *current < before)
            return *current == before - price;
        const auto receipt = rewardChoice();
        if (receipt && receipt->name == "garden_reward_confirm_ocr")
            c_.click(*receipt, .3);
        else
            c_.sleep(.2);
    }
    return false;
}
int Garden::shop() {
    QSet<int> skipped;
    int purchased = 0;
    const int reserve = std::max(0, config_["Garden Shop Reserve"].toInt(10));
    c_.sleep(.4);
    for (int attempt = 0; attempt < 30; ++attempt) {
        c_.refresh();
        const auto balance = money();
        if (!balance || *balance <= reserve)
            break;
        auto items = inventory();
        items.erase(std::remove_if(items.begin(), items.end(),
                                   [&](const auto &item) { return skipped.contains(item.index); }),
                    items.end());
        items = affordableGardenItems(items, *balance - reserve);
        if (items.empty())
            break;
        const auto item = items.front();
        if (c_.info)
            c_.info("周常商店", QString("%1 %2；余额 %3，保留 %4")
                                    .arg(item.recommended ? "推荐商品" : "顺序购买")
                                    .arg(item.index + 1)
                                    .arg(*balance)
                                    .arg(reserve));
        c_.click(item.box, .5);
        PurchaseControls detail;
        for (int read = 0; read < 3; ++read) {
            c_.refresh();
            detail = gardenPurchaseControls(c_.text(.66, .66, .86, .73));
            if (detail.sold || (detail.buy && detail.price && (!item.price || detail.price == item.price)))
                break;
            detail.buy.reset();
            detail.price.reset();
            c_.sleep(.2);
        }
        if (detail.sold || (detail.price && *detail.price > *balance - reserve)) {
            skipped.insert(item.index);
            continue;
        }
        if (!detail.buy || !detail.price) {
            if (item.recommended)
                break;
            skipped.insert(item.index);
            continue;
        }
        c_.click(*detail.buy, .4);
        if (!waitPurchase(*balance, *detail.price)) {
            if (c_.info)
                c_.info("周常商店", "扣款尚未核实，本轮停止继续购买");
            break;
        }
        ++purchased;
        skipped.insert(item.index);
    }
    const auto balance = money();
    if (auto close = c_.find("garden_shop_close", {.threshold = .82}))
        c_.click(*close, .3);
    if (c_.info)
        c_.info("周常商店", QString("已购买 %1 件；剩余 %2；保留 %3")
                                .arg(purchased)
                                .arg(balance ? QString::number(*balance) : "未识别")
                                .arg(reserve));
    return purchased;
}
} // namespace okww::runtime
