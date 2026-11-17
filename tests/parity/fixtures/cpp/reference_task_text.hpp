#pragma once
#include "runtime/task.hpp"
#include <opencv2/imgproc.hpp>
// Frozen pre-migration task recognition rules, tests only.
// Original UTF-8 LF SHA256: 596f87fbaf39c2085a75c2e7d07a086101c03d500972c86d1f34553cdcb7d641
namespace okww::runtime::reference_task_text {
inline std::optional<TextLine> matchingText(const QVector<TextLine> &lines, const QString &pattern) {
    QRegularExpression regex(pattern, QRegularExpression::CaseInsensitiveOption);
    if (!regex.isValid())
        fail("文本匹配规则无效");
    for (const auto &line : lines)
        if (regex.match(line.text).hasMatch())
            return line;
    return {};
}
inline QString monthlyWindowKey(QDateTime now, int hour) {
    if (hour < 1 || hour > 24 || !now.isValid())
        fail("月卡时间需要为 1 到 24 点");
    auto popup = now;
    popup.setTime(QTime(hour % 24, 0));
    if (hour == 24 && now.time() >= QTime(23, 59, 30))
        popup = popup.addDays(1);
    const auto offset = popup.secsTo(now);
    if (offset < -30 || offset >= 90)
        return {};
    return popup.toString("yyyy-MM-dd/HH");
}
static QString normalized(QString s) { return s.remove(QRegularExpression("\\s+")).toLower(); }
inline bool monthlyTextConfirmed(const QVector<TextLine> &lines) {
    bool claim = false, days = false;
    const QRegularExpression remaining("(剩余|剩餘)\\d+天|remaining\\d+days?|\\d+days?remaining");
    for (const auto &line : lines) {
        const auto s = normalized(line.text);
        claim |= ((s.contains("点击领取") || s.contains("點擊領取")) &&
                  (s.contains("今日") || s.contains("本日")) && (s.contains("月相") || s.contains("月卡"))) ||
                 (s.contains("claim") && (s.contains("today") || s.contains("daily")) &&
                  (s.contains("lunite") || s.contains("subscription")));
        days |= remaining.match(s).hasMatch();
    }
    return claim && days;
}
inline bool rewardTextConfirmed(const QVector<TextLine> &lines) {
    bool heading = false;
    QString combined;
    const QRegularExpression title(
        "^(获得(?:物品|奖励)?|獲得(?:物品|獎勵)?|rewardsobtained|itemsobtained|obtained)[.:：·]*$");
    for (const auto &line : lines) {
        const auto s = normalized(line.text);
        heading |= title.match(s).hasMatch();
        combined += s;
    }
    if (!heading)
        return false;
    for (const auto &word : {"点击空白", "點擊空白", "clickanywhere", "clickthedark", "tapanywhere"})
        if (combined.contains(word))
            return true;
    return false;
}
inline bool hasContinueMarker(const cv::Mat &frame) {
    auto rect = relativeBox(frame, .475, .925, .525, .98);
    if (rect.isEmpty())
        return false;
    auto area = frame(cv::Rect(rect.x(), rect.y(), rect.width(), rect.height()));
    cv::Mat mask(area.rows, area.cols, CV_8U);
    for (int y = 0; y < area.rows; ++y)
        for (int x = 0; x < area.cols; ++x) {
            auto p = area.at<cv::Vec3b>(y, x);
            const int low = std::min({p[0], p[1], p[2]}), high = std::max({p[0], p[1], p[2]});
            mask.at<uchar>(y, x) = low >= 180 && high - low < 65 ? 255 : 0;
        }
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    for (const auto &contour : contours) {
        auto b = cv::boundingRect(contour);
        const double aspect = double(b.width) / b.height, fill = cv::contourArea(contour) / b.area();
        if (b.width >= frame.cols * .006 && b.width <= frame.cols * .023 && b.height >= frame.rows * .012 &&
            b.height <= frame.rows * .04 && aspect >= .5 && aspect <= 1.9 && fill >= .12 && fill <= .8)
            return true;
    }
    return false;
}
}
