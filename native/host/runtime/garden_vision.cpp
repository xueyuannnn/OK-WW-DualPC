#include "garden.hpp"
#include "workflow_abi.hpp"
#include <opencv2/imgproc.hpp>
#include <tuple>
namespace okww::runtime {
QString normalizeGardenStyle(QString text) {
    return text.remove(QRegularExpression("\\s+")).replace("換", "换").replace("夢", "梦").toCaseFolded();
}
QString preferredGardenStyle(const QJsonObject &config) {
    const auto value = config["Preferred Garden Style"].toString("Any Style");
    QString result;
    rust::workflowRun(rust::WorkflowMethod::PreferredGardenStyle, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::PreferredStyleCustom: {
                              out[0] = value == "Custom Style";
                              break;
                          }
                          case rust::WorkflowOp::CustomGardenStyle: {
                              result = normalizeGardenStyle(config["Garden Custom Style"].toString());
                              break;
                          }
                          case rust::WorkflowOp::MappedGardenStyle: {
                              result =
                                  normalizeGardenStyle(QMap<QString, QString>{{"Devour Dream", "吞噬之梦"},
                                                                              {"Exchange Dream", "换取之梦"},
                                                                              {"Common Dream", "共通之梦"}}
                                                           .value(value));
                              break;
                          }
                          default:
                              fail("Rust PreferredGardenStyle 周常识别宿主无效");
                          }
                      });
    return result;
}
bool gardenPointsComplete(const QVector<TextLine> &lines) {
    QString joined;
    QRegularExpressionMatchIterator iterator;
    return rust::workflowRun(
               rust::WorkflowMethod::GardenPointsComplete, {},
               [&](rust::WorkflowOp op, const double *a, double *out) {
                   switch (op) {
                   case rust::WorkflowOp::GardenLineCount: {
                       out[0] = double(lines.size());
                       break;
                   }
                   case rust::WorkflowOp::AppendGardenLine: {
                       joined += lines.at(int(a[0])).text + ' ';
                       break;
                   }
                   case rust::WorkflowOp::GardenCompleteBanner: {
                       out[0] = QRegularExpression("本[周週][游遊][历歷]值.{0,16}(?:已[满滿]|已[达達](?:到)?"
                                                   "上限|[达達]到上限)|weekly.{0,24}(?:limit reached|maxed)",
                                                   QRegularExpression::CaseInsensitiveOption)
                                    .match(joined)
                                    .hasMatch();
                       break;
                   }
                   case rust::WorkflowOp::StartGardenPointMatches: {
                       iterator = QRegularExpression("(\\d[\\d,，]*)\\s*[/／]\\s*(\\d[\\d,，]*)")
                                      .globalMatch(joined);
                       break;
                   }
                   case rust::WorkflowOp::GardenPointHasNext: {
                       out[0] = iterator.hasNext();
                       break;
                   }
                   case rust::WorkflowOp::NextGardenPointMatch: {
                       const auto match = iterator.next();
                       out[0] = QString(match.captured(1)).remove(',').remove("，").toInt();
                       out[1] = QString(match.captured(2)).remove(',').remove("，").toInt();
                       break;
                   }
                   default:
                       fail("Rust GardenPointsComplete 周常识别宿主无效");
                   }
               })[0] != 0;
}

int selectGardenCard(const QVector<GardenCard> &cards) {
    std::vector<rust::Card> input;
    input.reserve(cards.size());
    for (const auto &card : cards)
        input.push_back({card.rarity, card.recommended, card.preferred, card.box.box.x()});
    return rust::okww_rs_select_card(input.data(), input.size());
}

int gardenRarity(const cv::Mat &image, QRect label) {
    QRect area;
    cv::Mat hsv;
    const std::array args{double(label.x()), double(label.y()), double(image.cols), double(image.rows)};
    return int(rust::workflowRun(
        rust::WorkflowMethod::GardenRarity, args, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::RarityArea: {
                area = QRect(int(a[0]), int(a[1]), int(a[2]), int(a[3]))
                           .intersected({0, 0, image.cols, image.rows});
                break;
            }
            case rust::WorkflowOp::GardenAreaEmpty: {
                out[0] = area.isEmpty();
                break;
            }
            case rust::WorkflowOp::RarityHsv: {
                cv::cvtColor(image(cv::Rect(area.x(), area.y(), area.width(), area.height())), hsv,
                             cv::COLOR_BGR2HSV);
                break;
            }
            case rust::WorkflowOp::RarityPixels: {
                out[0] = rust::okww_rs_garden_pixels(hsv.data, hsv.cols, hsv.rows, hsv.step[0], 0);
                break;
            }
            default:
                fail("Rust GardenRarity 周常识别宿主无效");
            }
        })[0]);
}
double recommendationScore(const cv::Mat &image, const cv::Mat &badge, QRect area, double scale) {
    cv::Mat gray, pattern;
    const std::array args{scale};
    return rust::workflowRun(rust::WorkflowMethod::RecommendationScore, args,
                             [&](rust::WorkflowOp op, const double *a, double *out) {
                                 switch (op) {
                                 case rust::WorkflowOp::ClipRecommendationArea: {
                                     area = area.intersected({0, 0, image.cols, image.rows});
                                     break;
                                 }
                                 case rust::WorkflowOp::BadgeEmpty: {
                                     out[0] = badge.empty();
                                     break;
                                 }
                                 case rust::WorkflowOp::GardenAreaEmpty: {
                                     out[0] = area.isEmpty();
                                     break;
                                 }
                                 case rust::WorkflowOp::RecommendationGray: {
                                     cv::cvtColor(
                                         image(cv::Rect(area.x(), area.y(), area.width(), area.height())),
                                         gray, cv::COLOR_BGR2GRAY);
                                     break;
                                 }
                                 case rust::WorkflowOp::BadgeSize: {
                                     out[0] = badge.cols;
                                     out[1] = badge.rows;
                                     break;
                                 }
                                 case rust::WorkflowOp::PixelRound: {
                                     out[0] = pixelRound(a[0]);
                                     break;
                                 }
                                 case rust::WorkflowOp::ResizeRecommendation: {
                                     cv::resize(badge, pattern, {int(a[0]), int(a[1])});
                                     break;
                                 }
                                 case rust::WorkflowOp::RecommendationGraySize: {
                                     out[0] = gray.cols;
                                     out[1] = gray.rows;
                                     break;
                                 }
                                 case rust::WorkflowOp::RecommendationMaximum: {
                                     cv::Mat scores;
                                     cv::matchTemplate(gray, pattern, scores, cv::TM_CCOEFF_NORMED);
                                     double maximum = 0;
                                     cv::minMaxLoc(scores, nullptr, &maximum);
                                     out[0] = maximum;
                                     break;
                                 }
                                 default:
                                     fail("Rust RecommendationScore 周常识别宿主无效");
                                 }
                             })[0];
}

QVector<GardenItem> affordableGardenItems(QVector<GardenItem> items, int money) {
    std::vector<rust::Item> input;
    input.reserve(items.size());
    for (const auto &item : items)
        input.push_back({item.index, item.price.value_or(0), item.price.has_value(), item.recommended});
    std::vector<size_t> indices(input.size());
    const int count =
        rust::okww_rs_order_items(input.data(), input.size(), money, indices.data(), indices.size());
    if (count < 0)
        fail("Rust 商店排序接口异常");
    QVector<GardenItem> out;
    out.reserve(count);
    for (int i = 0; i < count; ++i)
        out.append(std::move(items[qsizetype(indices[size_t(i)])]));
    return out;
}

PurchaseControls gardenPurchaseControls(const QVector<TextLine> &lines) {
    PurchaseControls result;
    QString selected;
    QRegularExpressionMatch match;
    const QRegularExpression sold("^(?:已售罄|售罄|已购买|已購買|sold\\s*out|purchased)$",
                                  QRegularExpression::CaseInsensitiveOption),
        merged("^(?:[-−]?\\s*(\\d+)\\s*(?:购买|購買|buy|purchase)|(?:购买|購買|buy|purchase)\\s*[-−]?\\s*("
               "\\d+))$",
               QRegularExpression::CaseInsensitiveOption),
        buy("^(?:购买|購買|buy|purchase)$", QRegularExpression::CaseInsensitiveOption),
        number("^[-−]?\\s*(\\d+)\\s*$");
    rust::workflowRun(
        rust::WorkflowMethod::GardenPurchaseControls, {},
        [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::GardenLineCount: {
                out[0] = double(lines.size());
                break;
            }
            case rust::WorkflowOp::SelectPurchaseLine: {
                selected = lines.at(int(a[0])).text.trimmed();
                break;
            }
            case rust::WorkflowOp::SoldPurchaseLine: {
                out[0] = sold.match(selected).hasMatch();
                break;
            }
            case rust::WorkflowOp::SetPurchaseSold: {
                result.sold = true;
                break;
            }
            case rust::WorkflowOp::MergedPurchaseLine: {
                match = merged.match(selected);
                out[0] = match.hasMatch();
                break;
            }
            case rust::WorkflowOp::StorePurchaseButton: {
                result.buy = lines.at(int(a[0]));
                break;
            }
            case rust::WorkflowOp::MergedPurchasePrice: {
                out[0] = (match.captured(1).isEmpty() ? match.captured(2) : match.captured(1)).toInt();
                break;
            }
            case rust::WorkflowOp::IsPurchaseButton: {
                out[0] = buy.match(selected).hasMatch();
                break;
            }
            case rust::WorkflowOp::IsPurchaseNumber: {
                match = number.match(selected);
                out[0] = match.hasMatch();
                break;
            }
            case rust::WorkflowOp::PurchaseNumber: {
                out[0] = match.captured(1).toInt();
                break;
            }
            case rust::WorkflowOp::StorePurchasePrice: {
                result.price = int(a[0]);
                break;
            }
            default:
                fail("Rust GardenPurchaseControls 周常识别宿主无效");
            }
        });
    return result;
}
Garden::Garden(TaskContext &context, const QString &resources)
    : c_(context), navigation_(context), config_(context.profile.group("GardenTask")) {
    cv::Mat image;
    rust::workflowRun(rust::WorkflowMethod::GardenInitialize, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::ReadGardenBadge: {
                              image = readImage(resources + "/assets/dualpc/garden_recommended.png");
                              break;
                          }
                          case rust::WorkflowOp::GardenBadgeImageSize: {
                              out[0] = image.cols;
                              out[1] = image.rows;
                              break;
                          }
                          case rust::WorkflowOp::Throw: {
                              fail("周常推荐图标资源尺寸无效");
                              break;
                          }
                          case rust::WorkflowOp::ExtractGardenBadge: {
                              cv::cvtColor(image(cv::Rect(6, 6, 35, 36)), badge_, cv::COLOR_BGR2GRAY);
                              break;
                          }
                          default:
                              fail("Rust GardenInitialize 周常识别宿主无效");
                          }
                      });
}
std::optional<Match> Garden::rewardChoice() {
    QVector<TextLine> buttons, labels;
    std::optional<TextLine> confirm;
    QRect region, box;
    QString style, texts;
    QVector<GardenCard> cards;
    GardenCard chosen;
    QStringList ranks;
    std::optional<Match> result;
    rust::workflowRun(
        rust::WorkflowMethod::GardenRewardChoice, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::ReadGardenRewardButtons: {
                buttons = c_.text(.50, .73, .91, .86, .75);
                break;
            }
            case rust::WorkflowOp::FindGardenRewardConfirm: {
                confirm = matchingText(buttons, "^(?:确认|確認|confirm)$");
                out[0] = confirm.has_value();
                break;
            }
            case rust::WorkflowOp::HasGardenRewardSkip: {
                out[0] = bool(matchingText(buttons, "^(?:跳过|跳過|skip)$"));
                break;
            }
            case rust::WorkflowOp::ReadGardenRewardLabels: {
                labels = c_.text(.15, .15, .86, .38, .75);
                out[0] = double(labels.size());
                break;
            }
            case rust::WorkflowOp::GardenAllocationFailure: {
                throw std::bad_alloc();
                break;
            }
            case rust::WorkflowOp::GardenLabelIsEcho: {
                out[0] = bool(matchingText({labels.at(int(a[0]))}, "^(?:声骸|聲骸|echo)$"));
                break;
            }
            case rust::WorkflowOp::GardenLabelY: {
                out[0] = labels.at(int(a[0])).box.y();
                break;
            }
            case rust::WorkflowOp::PrepareGardenGreenRegion: {
                region = gameBox(c_.frame, .15, .62, .88, .72);
                break;
            }
            case rust::WorkflowOp::GardenSelectedGreenPixels: {
                const auto image =
                    c_.frame(cv::Rect(region.x(), region.y(), region.width(), region.height()));
                out[0] = rust::okww_rs_garden_pixels(image.data, image.cols, image.rows, image.step[0], 1);
                break;
            }
            case rust::WorkflowOp::GardenGreenRegionArea: {
                out[0] = region.width() * region.height();
                break;
            }
            case rust::WorkflowOp::StoreGardenRewardConfirm: {
                result = Match{"garden_reward_confirm_ocr", confirm->box, confirm->confidence};
                break;
            }
            case rust::WorkflowOp::ImageSize: {
                out[0] = c_.frame.cols;
                out[1] = c_.frame.rows;
                break;
            }
            case rust::WorkflowOp::ReadPreferredGardenStyle: {
                style = preferredGardenStyle(config_);
                break;
            }
            case rust::WorkflowOp::PixelRound: {
                out[0] = pixelRound(a[0]);
                break;
            }
            case rust::WorkflowOp::GardenCardBox: {
                box = QRect(int(a[0]), int(a[1]), int(a[2]), int(a[3]));
                break;
            }
            case rust::WorkflowOp::GardenCardRecommendation: {
                out[0] = recommendationScore(c_.frame, badge_,
                                             QRect(int(a[0]), int(a[1]), int(a[2]), int(a[3])), a[4]);
                break;
            }
            case rust::WorkflowOp::ClearGardenCardText: {
                texts.clear();
                break;
            }
            case rust::WorkflowOp::GardenLabelGeometry: {
                const auto &line = labels.at(int(a[0]));
                out[0] = line.box.width();
                out[1] = line.center().x();
                break;
            }
            case rust::WorkflowOp::AppendGardenCardText: {
                texts += labels.at(int(a[0])).text + ' ';
                break;
            }
            case rust::WorkflowOp::GardenCardRarity: {
                out[0] = gardenRarity(c_.frame, box);
                break;
            }
            case rust::WorkflowOp::GardenStyleEmpty: {
                out[0] = style.isEmpty();
                break;
            }
            case rust::WorkflowOp::GardenCardMatchesStyle: {
                out[0] = normalizeGardenStyle(texts).contains(style);
                break;
            }
            case rust::WorkflowOp::AppendGardenCard: {
                cards.append(
                    {{"card_" + QString::number(int(a[0]) + 1), box, 1}, int(a[1]), a[2] != 0, a[3] != 0});
                break;
            }
            case rust::WorkflowOp::SelectGardenCard: {
                out[0] = selectGardenCard(cards);
                break;
            }
            case rust::WorkflowOp::CopyChosenGardenCard: {
                chosen = cards.at(int(a[0]));
                break;
            }
            case rust::WorkflowOp::GardenCardPreferred: {
                out[0] = cards.at(int(a[0])).preferred;
                break;
            }
            case rust::WorkflowOp::NameChosenGardenCard: {
                chosen.box.name = "garden_reward_choice_" + QString(a[0] != 0 ? "style_" : "") +
                                  QString(chosen.recommended ? "recommended_" : "") +
                                  QStringList{"white", "blue", "purple", "gold"}[chosen.rarity];
                break;
            }
            case rust::WorkflowOp::HasInfo: {
                out[0] = bool(c_.info);
                break;
            }
            case rust::WorkflowOp::AppendGardenCardRank: {
                const int i = int(a[0]);
                ranks.append(QString("%1：%2%3%4")
                                 .arg(i + 1)
                                 .arg(cards[i].preferred ? "流派·" : "", cards[i].recommended ? "推荐·" : "",
                                      QStringList{"灰白", "蓝", "紫", "金"}[cards[i].rarity]));
                break;
            }
            case rust::WorkflowOp::PublishGardenCardRank: {
                c_.info("周常选卡", ranks.join(" | ") + QString(" → %1").arg(int(a[0]) + 1));
                break;
            }
            case rust::WorkflowOp::StoreChosenGardenCard: {
                result = chosen.box;
                break;
            }
            default:
                fail("Rust 周常选卡宿主无效");
            }
        });
    return result;
}
std::optional<int> gardenWeeklyPoints(const QVector<TextLine> &lines, const cv::Mat &frame) {
    QRect counter, overview;
    QVector<TextLine> local;
    QString numeric, joined;
    QRegularExpressionMatchIterator iterator;
    const auto result = rust::workflowRun(
        rust::WorkflowMethod::GardenWeeklyPoints, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::PrepareWeeklyAreas: {
                counter = gameBox(frame, .17, .81, .30, .90);
                overview = gameBox(frame, .08, .78, .36, .97);
                break;
            }
            case rust::WorkflowOp::GardenLineCount: {
                out[0] = double(lines.size());
                break;
            }
            case rust::WorkflowOp::WeeklyOverviewContains: {
                out[0] = overview.contains(lines.at(int(a[0])).center().toPoint());
                break;
            }
            case rust::WorkflowOp::AppendWeeklyLocal: {
                local.append(lines.at(int(a[0])));
                break;
            }
            case rust::WorkflowOp::WeeklyLocalComplete: {
                out[0] = gardenPointsComplete(local);
                break;
            }
            case rust::WorkflowOp::WeeklyLocalCount: {
                out[0] = double(local.size());
                break;
            }
            case rust::WorkflowOp::WeeklyCounterContains: {
                out[0] = counter.contains(local.at(int(a[0])).center().toPoint());
                break;
            }
            case rust::WorkflowOp::WeeklyIntegerMatches: {
                numeric = QString(local.at(int(a[0])).text).remove(',').remove("，").trimmed();
                out[0] = QRegularExpression("^\\d+$").match(numeric).hasMatch();
                break;
            }
            case rust::WorkflowOp::WeeklyIntegerValue: {
                bool ok = false;
                out[0] = numeric.toInt(&ok);
                out[1] = ok;
                break;
            }
            case rust::WorkflowOp::AppendWeeklyLocalText: {
                joined += local.at(int(a[0])).text + ' ';
                break;
            }
            case rust::WorkflowOp::StartGardenPointMatches: {
                iterator =
                    QRegularExpression("(\\d[\\d,，]*)\\s*[/／]\\s*(\\d[\\d,，]*)").globalMatch(joined);
                break;
            }
            case rust::WorkflowOp::GardenPointHasNext: {
                out[0] = iterator.hasNext();
                break;
            }
            case rust::WorkflowOp::NextGardenPointMatch: {
                const auto match = iterator.next();
                out[0] = QString(match.captured(1)).remove(',').remove("，").toInt();
                out[1] = QString(match.captured(2)).remove(',').remove("，").toInt();
                break;
            }
            default:
                fail("Rust GardenWeeklyPoints 周常识别宿主无效");
            }
        });
    if (result[0] == 0)
        return {};
    return int(result[1]);
}
std::optional<int> Garden::weeklyProgress() {
    QVector<TextLine> heading, lines, local;
    QRect overview;
    QJsonArray titles, strip;
    const auto result = rust::workflowRun(
        rust::WorkflowMethod::GardenWeeklyProgress, {},
        [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::ReadWeeklyHeading: {
                heading = c_.text(.09, .08, .65, .18, .75);
                break;
            }
            case rust::WorkflowOp::IsWeeklyHeading: {
                out[0] =
                    bool(matchingText(heading, "^(?:[周週]度[游遊][历歷]|[周週]常(?:[游遊][历歷]|任[务務])|"
                                               "Weekly\\s*(?:Activity|Exploration|Journey|Tasks?))$"));
                break;
            }
            case rust::WorkflowOp::ReadWeeklyStrip: {
                lines = c_.text(.05, .78, .98, .97, .75);
                break;
            }
            case rust::WorkflowOp::PrepareWeeklyOverview: {
                overview = gameBox(c_.frame, .08, .78, .36, .97);
                break;
            }
            case rust::WorkflowOp::GardenLineCount: {
                out[0] = double(lines.size());
                break;
            }
            case rust::WorkflowOp::WeeklyOverviewContains: {
                out[0] = overview.contains(lines.at(int(a[0])).center().toPoint());
                break;
            }
            case rust::WorkflowOp::AppendWeeklyLocal: {
                local.append(lines.at(int(a[0])));
                break;
            }
            case rust::WorkflowOp::WeeklyLocalComplete: {
                out[0] = gardenPointsComplete(local);
                break;
            }
            case rust::WorkflowOp::HasWeeklyCounterLabel: {
                out[0] = bool(matchingText(
                    local,
                    "^(?:[游遊][历歷]值|(?:Weekly\\s*)?(?:Exploration|Journey|Activity)\\s*Points?)$"));
                break;
            }
            case rust::WorkflowOp::WeeklyHeadingCount: {
                out[0] = double(heading.size());
                break;
            }
            case rust::WorkflowOp::AppendWeeklyHeadingJson: {
                titles.append(heading.at(int(a[0])).json());
                break;
            }
            case rust::WorkflowOp::AppendWeeklyStripJson: {
                strip.append(lines.at(int(a[0])).json());
                break;
            }
            case rust::WorkflowOp::StoreWeeklyObservation: {
                weeklyObservation_ = {{"weekly_heading", a[0] != 0},
                                      {"weekly_counter_label", a[1] != 0},
                                      {"heading_lines", titles},
                                      {"reward_strip_lines", strip}};
                break;
            }
            case rust::WorkflowOp::ReadWeeklyPoints: {
                const auto value = gardenWeeklyPoints(lines, c_.frame);
                out[0] = value.has_value();
                out[1] = value.value_or(0);
                break;
            }
            case rust::WorkflowOp::StoreWeeklyProgress: {
                weeklyObservation_["progress"] = int(a[0]);
                break;
            }
            default:
                fail("Rust GardenWeeklyProgress 周常识别宿主无效");
            }
        });
    if (result[0] == 0)
        return {};
    return int(result[1]);
}
} // namespace okww::runtime
