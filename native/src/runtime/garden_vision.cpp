#include "garden.hpp"
#include <opencv2/imgproc.hpp>
#include <tuple>
namespace okww::runtime {
QString normalizeGardenStyle(QString text) {
    return text.remove(QRegularExpression("\\s+")).replace("換", "换").replace("夢", "梦").toCaseFolded();
}
QString preferredGardenStyle(const QJsonObject &config) {
    const auto value = config["Preferred Garden Style"].toString("Any Style");
    if (value == "Custom Style")
        return normalizeGardenStyle(config["Garden Custom Style"].toString());
    return normalizeGardenStyle(QMap<QString, QString>{
        {"Devour Dream", "吞噬之梦"}, {"Exchange Dream", "换取之梦"}, {"Common Dream", "共通之梦"}}
                                    .value(value));
}
bool gardenPointsComplete(const QVector<TextLine> &lines) {
    QString text;
    for (const auto &line : lines)
        text += line.text + ' ';
    if (QRegularExpression("本[周週][游遊][历歷]值.{0,16}(?:已[满滿]|已[达達](?:到)?上限|[达達]到上限)|"
                           "weekly.{0,24}(?:limit reached|maxed)",
                           QRegularExpression::CaseInsensitiveOption)
            .match(text)
            .hasMatch())
        return true;
    auto matches = QRegularExpression("(\\d[\\d,，]*)\\s*[/／]\\s*(\\d[\\d,，]*)").globalMatch(text);
    while (matches.hasNext()) {
        const auto m = matches.next();
        const int current = QString(m.captured(1)).remove(',').remove("，").toInt();
        const int maximum = QString(m.captured(2)).remove(',').remove("，").toInt();
        if (current >= 6000 && maximum == 6000)
            return true;
    }
    return false;
}
int selectGardenCard(const QVector<GardenCard> &cards) {
    if (cards.empty())
        return -1;
    const bool hasStyle = std::any_of(cards.begin(), cards.end(), [](const auto &c) { return c.preferred; });
    auto rank = [&](int i) {
        const auto &c = cards[i];
        return hasStyle ? std::tuple<int, int, int, int>{c.preferred, c.rarity, c.recommended, -c.box.box.x()}
                        : std::tuple<int, int, int, int>{c.recommended, c.rarity, 0, -c.box.box.x()};
    };
    int selected = 0;
    for (int i = 1; i < cards.size(); ++i)
        if (rank(i) > rank(selected))
            selected = i;
    return selected;
}
int gardenRarity(const cv::Mat &image, QRect label) {
    const int x = std::max(0, int(label.x() - .07 * image.cols)),
              right = std::max(0, int(label.x() - .015 * image.cols));
    const int y = std::max(0, int(label.y() - .015 * image.rows)),
              bottom = std::min(image.rows, int(label.y() + .04 * image.rows));
    const QRect area = QRect(x, y, right - x, bottom - y).intersected({0, 0, image.cols, image.rows});
    if (area.isEmpty())
        return 0;
    cv::Mat hsv;
    cv::cvtColor(image(cv::Rect(area.x(), area.y(), area.width(), area.height())), hsv, cv::COLOR_BGR2HSV);
    const auto median = [&](int channel) {
        std::vector<int> pixels;
        pixels.reserve(hsv.total());
        for (int yy = 0; yy < hsv.rows; ++yy)
            for (int xx = 0; xx < hsv.cols; ++xx)
                pixels.push_back(hsv.at<cv::Vec3b>(yy, xx)[channel]);
        std::sort(pixels.begin(), pixels.end());
        const size_t middle = pixels.size() / 2;
        return pixels.size() % 2 ? double(pixels[middle]) : (pixels[middle - 1] + pixels[middle]) * .5;
    };
    const double hue = median(0), saturation = median(1);
    if (hue >= 12 && hue <= 45 && saturation >= 100)
        return 3;
    if (hue >= 118 && hue <= 165 && saturation >= 45)
        return 2;
    if (hue >= 75 && hue < 118 && saturation >= 85)
        return 1;
    return 0;
}
double recommendationScore(const cv::Mat &image, const cv::Mat &badge, QRect area, double scale) {
    area = area.intersected({0, 0, image.cols, image.rows});
    if (badge.empty() || area.isEmpty())
        return 0;
    cv::Mat gray;
    cv::cvtColor(image(cv::Rect(area.x(), area.y(), area.width(), area.height())), gray, cv::COLOR_BGR2GRAY);
    double best = 0;
    for (double adjustment : {.85, 1., 1.1}) {
        cv::Mat pattern;
        cv::resize(badge, pattern,
                   {std::max(1, pixelRound(badge.cols * scale * adjustment)),
                    std::max(1, pixelRound(badge.rows * scale * adjustment))});
        if (gray.rows < pattern.rows || gray.cols < pattern.cols)
            continue;
        cv::Mat scores;
        cv::matchTemplate(gray, pattern, scores, cv::TM_CCOEFF_NORMED);
        double maximum = 0;
        cv::minMaxLoc(scores, nullptr, &maximum);
        if (std::isfinite(maximum))
            best = std::max(best, maximum);
    }
    return best;
}
QVector<GardenItem> affordableGardenItems(QVector<GardenItem> items, int money) {
    items.erase(std::remove_if(items.begin(), items.end(),
                               [&](const auto &i) { return i.price ? *i.price > money : !i.recommended; }),
                items.end());
    std::stable_sort(items.begin(), items.end(), [](const auto &a, const auto &b) {
        return a.recommended != b.recommended ? a.recommended > b.recommended : a.index < b.index;
    });
    return items;
}
PurchaseControls gardenPurchaseControls(const QVector<TextLine> &lines) {
    PurchaseControls out;
    QVector<int> prices;
    const QRegularExpression sold("^(?:已售罄|售罄|已购买|已購買|sold\\s*out|purchased)$",
                                  QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression merged(
        "^(?:[-−]?\\s*(\\d+)\\s*(?:购买|購買|buy|purchase)|(?:购买|購買|buy|purchase)\\s*[-−]?\\s*(\\d+))$",
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression buy("^(?:购买|購買|buy|purchase)$", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression number("^[-−]?\\s*(\\d+)\\s*$");
    for (const auto &line : lines) {
        const auto text = line.text.trimmed();
        if (sold.match(text).hasMatch())
            out.sold = true;
        auto match = merged.match(text);
        if (match.hasMatch()) {
            out.buy = line;
            prices.append((match.captured(1).isEmpty() ? match.captured(2) : match.captured(1)).toInt());
        } else if (buy.match(text).hasMatch())
            out.buy = line;
        else if ((match = number.match(text)).hasMatch())
            prices.append(match.captured(1).toInt());
    }
    if (prices.size() == 1 && prices[0] > 0)
        out.price = prices[0];
    return out;
}
Garden::Garden(TaskContext &context, const QString &resources)
    : c_(context), navigation_(context), config_(context.profile.group("GardenTask")) {
    const auto image = readImage(resources + "/assets/dualpc/garden_recommended.png");
    if (image.cols < 41 || image.rows < 42)
        fail("周常推荐图标资源尺寸无效");
    cv::cvtColor(image(cv::Rect(6, 6, 35, 36)), badge_, cv::COLOR_BGR2GRAY);
}
std::optional<Match> Garden::rewardChoice() {
    const auto buttons = c_.text(.50, .73, .91, .86, .75);
    const auto confirm = matchingText(buttons, "^(?:确认|確認|confirm)$");
    if (!confirm || !matchingText(buttons, "^(?:跳过|跳過|skip)$"))
        return {};
    const auto labels = c_.text(.15, .15, .86, .38, .75);
    QVector<int> ys;
    for (const auto &line : labels)
        if (matchingText({line}, "^(?:声骸|聲骸|echo)$"))
            ys.append(line.box.y());
    if (ys.size() < 2)
        return {};
    const auto region = gameBox(c_.frame, .15, .62, .88, .72);
    int selected = 0;
    for (int y = region.y(); y < region.y() + region.height(); ++y)
        for (int x = region.x(); x < region.x() + region.width(); ++x) {
            const auto p = c_.frame.at<cv::Vec3b>(y, x);
            selected += p[1] > 150 && p[1] > p[2] * 1.15 && p[1] > p[0] * 1.25;
        }
    if (double(selected) / (region.width() * region.height()) > .003)
        return Match{"garden_reward_confirm_ocr", confirm->box, confirm->confidence};
    std::sort(ys.begin(), ys.end());
    const int y = ys.size() % 2 ? ys[ys.size() / 2] : (ys[ys.size() / 2 - 1] + ys[ys.size() / 2]) / 2;
    const int w = c_.frame.cols, h = c_.frame.rows;
    const auto style = preferredGardenStyle(config_);
    QVector<GardenCard> cards;
    for (int i = 0; i < 3; ++i) {
        const QRect box(pixelRound(w * (.335 + .235 * i)), y, pixelRound(w * .035), pixelRound(h * .028));
        const QRect bounds(int(box.x() - .06 * w), int(y - .13 * h),
                           int(box.x() + .06 * w) - int(box.x() - .06 * w),
                           int(y + .01 * h) - int(y - .13 * h));
        const bool recommended = recommendationScore(c_.frame, badge_, bounds, w / 1920.) >= .88;
        QString texts;
        for (const auto &line : labels)
            if (line.box.width() <= .23 * w && line.center().x() >= (.15 + .235 * i) * w &&
                line.center().x() <= (.39 + .235 * i) * w)
                texts += line.text + ' ';
        cards.append({{"card_" + QString::number(i + 1), box, 1},
                      gardenRarity(c_.frame, box),
                      recommended,
                      !style.isEmpty() && normalizeGardenStyle(texts).contains(style)});
    }
    const int index = selectGardenCard(cards);
    auto chosen = cards[index];
    const bool preferred =
        std::any_of(cards.begin(), cards.end(), [](const auto &card) { return card.preferred; });
    chosen.box.name = "garden_reward_choice_" + QString(preferred ? "style_" : "") +
                      QString(chosen.recommended ? "recommended_" : "") +
                      QStringList{"white", "blue", "purple", "gold"}[chosen.rarity];
    if (c_.info) {
        QStringList ranks;
        for (int i = 0; i < 3; ++i)
            ranks.append(QString("%1：%2%3%4")
                             .arg(i + 1)
                             .arg(cards[i].preferred ? "流派·" : "", cards[i].recommended ? "推荐·" : "",
                                  QStringList{"灰白", "蓝", "紫", "金"}[cards[i].rarity]));
        c_.info("周常选卡", ranks.join(" | ") + QString(" → %1").arg(index + 1));
    }
    return chosen.box;
}
std::optional<int> Garden::weeklyProgress() {
    for (const auto &line : c_.text(.19, .83, .275, .90)) {
        const auto text = QString(line.text).remove(',').remove("，").trimmed();
        if (QRegularExpression("^\\d+$").match(text).hasMatch()) {
            bool ok = false;
            int value = text.toInt(&ok);
            if (ok && value <= 6000)
                return value;
        }
    }
    const auto lines = c_.text(.08, .68, .36, .97, .75);
    if (gardenPointsComplete(lines))
        return 6000;
    QString text;
    for (const auto &line : lines)
        text += line.text + ' ';
    auto matches = QRegularExpression("(\\d[\\d,，]*)\\s*[/／]\\s*(\\d[\\d,，]*)").globalMatch(text);
    while (matches.hasNext()) {
        const auto m = matches.next();
        const int value = QString(m.captured(1)).remove(',').remove("，").toInt();
        if (QString(m.captured(2)).remove(',').remove("，").toInt() == 6000 && value >= 0 && value <= 6000)
            return value;
    }
    return {};
}
} // namespace okww::runtime
