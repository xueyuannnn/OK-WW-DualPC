#include "garden.hpp"
#include "workflow_abi.hpp"
namespace okww::runtime {
std::optional<int> Garden::money() {
    QVector<TextLine> lines;
    const auto result = rust::workflowRun(
        rust::WorkflowMethod::GardenMoney, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::ReadGardenMoneyText: {
                lines = c_.text(.68, .035, .79, .09);
                out[0] = double(lines.size());
                break;
            }
            case rust::WorkflowOp::GardenMoneyMatches: {
                out[0] = QRegularExpression("^\\d+$").match(lines.at(int(a[0])).text.trimmed()).hasMatch();
                break;
            }
            case rust::WorkflowOp::GardenMoneyValue: {
                bool ok = false;
                out[0] = lines.at(int(a[0])).text.trimmed().toInt(&ok);
                out[1] = ok;
                break;
            }
            default:
                fail("Rust GardenMoney 周常商店宿主无效");
            }
        });
    if (result[0] == 0)
        return {};
    return int(result[1]);
}
QVector<GardenItem> Garden::inventory() {
    QVector<GardenItem> items;
    QVector<TextLine> labels;
    QRegularExpressionMatch match;
    rust::workflowRun(
        rust::WorkflowMethod::GardenInventory, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::ImageSize: {
                out[0] = c_.frame.cols;
                out[1] = c_.frame.rows;
                break;
            }
            case rust::WorkflowOp::ReadGardenItemPrices: {
                labels = c_.textScaled(gameBox(c_.frame, a[0], a[1], a[2], a[3]), a[4]);
                out[0] = double(labels.size());
                break;
            }
            case rust::WorkflowOp::GardenItemPriceMatches: {
                match =
                    QRegularExpression("^[-−]?\\s*(\\d+)\\s*$").match(labels.at(int(a[0])).text.trimmed());
                out[0] = match.hasMatch();
                break;
            }
            case rust::WorkflowOp::GardenItemPrice: {
                out[0] = match.captured(1).toInt();
                break;
            }
            case rust::WorkflowOp::PixelRound: {
                out[0] = pixelRound(a[0]);
                break;
            }
            case rust::WorkflowOp::GardenItemRecommendation: {
                out[0] = recommendationScore(c_.frame, badge_,
                                             QRect(int(a[0]), int(a[1]), int(a[2]), int(a[3])), a[4]);
                break;
            }
            case rust::WorkflowOp::AppendGardenItem: {
                const int index = int(a[0]);
                const auto price = a[1] != 0 ? std::optional<int>(int(a[2])) : std::nullopt;
                items.append({index,
                              price,
                              a[3] != 0,
                              {QString("garden_shop_item_%1").arg(index + 1),
                               {int(a[4]), int(a[5]), int(a[6]), int(a[7])},
                               1}});
                break;
            }
            default:
                fail("Rust GardenInventory 周常商店宿主无效");
            }
        });
    return items;
}
bool Garden::waitPurchase(int before, int price) {
    std::optional<Match> receipt;
    const std::array args{double(before), double(price)};
    return rust::workflowRun(rust::WorkflowMethod::GardenWaitPurchase, args,
                             [&](rust::WorkflowOp op, const double *a, double *out) {
                                 switch (op) {
                                 case rust::WorkflowOp::Refresh: {
                                     c_.refresh();
                                     break;
                                 }
                                 case rust::WorkflowOp::ReadGardenBalance: {
                                     const auto value = money();
                                     out[0] = value.has_value();
                                     out[1] = value.value_or(0);
                                     break;
                                 }
                                 case rust::WorkflowOp::ReadGardenReceipt: {
                                     receipt = rewardChoice();
                                     out[0] = receipt.has_value();
                                     break;
                                 }
                                 case rust::WorkflowOp::GardenReceiptConfirm: {
                                     out[0] = receipt->name == "garden_reward_confirm_ocr";
                                     break;
                                 }
                                 case rust::WorkflowOp::ClickGardenReceipt: {
                                     c_.click(*receipt, a[0]);
                                     break;
                                 }
                                 case rust::WorkflowOp::Sleep: {
                                     c_.sleep(a[0]);
                                     break;
                                 }
                                 default:
                                     fail("Rust GardenWaitPurchase 周常商店宿主无效");
                                 }
                             })[0] != 0;
}
int Garden::shop() {
    QSet<int> skipped;
    std::optional<int> balance;
    QVector<GardenItem> items, available;
    GardenItem item;
    PurchaseControls detail;
    std::optional<Match> close;
    return int(rust::workflowRun(
        rust::WorkflowMethod::GardenShop, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::GardenShopReserve: {
                out[0] = config_["Garden Shop Reserve"].toInt(10);
                break;
            }
            case rust::WorkflowOp::Sleep: {
                c_.sleep(a[0]);
                break;
            }
            case rust::WorkflowOp::Refresh: {
                c_.refresh();
                break;
            }
            case rust::WorkflowOp::ReadGardenBalance: {
                balance = money();
                out[0] = balance.has_value();
                out[1] = balance.value_or(0);
                break;
            }
            case rust::WorkflowOp::ReadGardenInventory: {
                items = inventory();
                available.clear();
                out[0] = double(items.size());
                break;
            }
            case rust::WorkflowOp::GardenItemSkipped: {
                out[0] = skipped.contains(items.at(int(a[0])).index);
                break;
            }
            case rust::WorkflowOp::AppendAvailableGardenItem: {
                available.append(items.at(int(a[0])));
                break;
            }
            case rust::WorkflowOp::AffordableGardenInventory: {
                items = affordableGardenItems(available, int(a[0]));
                out[0] = double(items.size());
                break;
            }
            case rust::WorkflowOp::SelectGardenShopItem: {
                item = items.front();
                break;
            }
            case rust::WorkflowOp::HasInfo: {
                out[0] = bool(c_.info);
                break;
            }
            case rust::WorkflowOp::GardenShopItemInfo: {
                c_.info("周常商店", QString("%1 %2；余额 %3，保留 %4")
                                        .arg(item.recommended ? "推荐商品" : "顺序购买")
                                        .arg(item.index + 1)
                                        .arg(int(a[0]))
                                        .arg(int(a[1])));
                break;
            }
            case rust::WorkflowOp::ClickGardenShopItem: {
                c_.click(item.box, a[0]);
                break;
            }
            case rust::WorkflowOp::ReadGardenPurchaseDetail: {
                detail = gardenPurchaseControls(c_.text(.66, .66, .86, .73));
                break;
            }
            case rust::WorkflowOp::GardenDetailSold: {
                out[0] = detail.sold;
                break;
            }
            case rust::WorkflowOp::GardenDetailBuy: {
                out[0] = detail.buy.has_value();
                break;
            }
            case rust::WorkflowOp::GardenDetailPrice: {
                out[0] = detail.price.has_value();
                break;
            }
            case rust::WorkflowOp::GardenSelectedItemPrice: {
                out[0] = item.price.has_value();
                break;
            }
            case rust::WorkflowOp::GardenDetailPriceMatches: {
                out[0] = detail.price == item.price;
                break;
            }
            case rust::WorkflowOp::ClearGardenDetailBuy: {
                detail.buy.reset();
                break;
            }
            case rust::WorkflowOp::ClearGardenDetailPrice: {
                detail.price.reset();
                break;
            }
            case rust::WorkflowOp::GardenDetailPriceValue: {
                out[0] = *detail.price;
                break;
            }
            case rust::WorkflowOp::SkipSelectedGardenItem: {
                skipped.insert(item.index);
                break;
            }
            case rust::WorkflowOp::GardenSelectedItemRecommended: {
                out[0] = item.recommended;
                break;
            }
            case rust::WorkflowOp::ClickGardenDetailBuy: {
                c_.click(*detail.buy, a[0]);
                break;
            }
            case rust::WorkflowOp::WaitGardenPurchase: {
                out[0] = waitPurchase(int(a[0]), int(a[1]));
                break;
            }
            case rust::WorkflowOp::UnverifiedGardenPurchaseInfo: {
                c_.info("周常商店", "扣款尚未核实，本轮停止继续购买");
                break;
            }
            case rust::WorkflowOp::FinalGardenBalance: {
                balance = money();
                break;
            }
            case rust::WorkflowOp::FindGardenShopClose: {
                close = c_.find("garden_shop_close", {.threshold = a[0]});
                out[0] = close.has_value();
                break;
            }
            case rust::WorkflowOp::ClickGardenShopClose: {
                c_.click(*close, a[0]);
                break;
            }
            case rust::WorkflowOp::GardenShopSummary: {
                c_.info("周常商店", QString("已购买 %1 件；剩余 %2；保留 %3")
                                        .arg(int(a[0]))
                                        .arg(balance ? QString::number(*balance) : "未识别")
                                        .arg(int(a[1])));
                break;
            }
            default:
                fail("Rust GardenShop 周常商店宿主无效");
            }
        })[0]);
}
} // namespace okww::runtime
