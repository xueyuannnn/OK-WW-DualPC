#pragma once
#include "reference_garden_shop.hpp"
namespace okww::runtime::garden_shop_test {
struct ShopClock : Clock {
    double time = 0;
    double now() const override { return time; }
    void sleep(double seconds) override { time += seconds; }
};
template <class GardenType> QJsonObject trace(int scene) {
    const QString root = OKWW_SOURCE_ROOT;
    const QSize size(1920, 1080);
    QTemporaryDir dir;
    Templates templates(root + "/assets/coco_annotations.json");
    Profile profile(dir.path(), root + "/native/resources/catalog.json");
    profile.set("Monthly Card Config", "Check Monthly Card", false);
    const int reserve = scene % 3 * 5;
    profile.set("GardenTask", "Garden Shop Reserve", reserve);
    ShopClock clock;
    ExecutionGate gate;
    Execution execution(gate, gate.start(), clock);
    QPoint pointer;
    QJsonArray events;
    int balance = 65 + scene % 5 * 10, selected = -1, closes = 0;
    bool closed = false, receipt = false;
    int receipts = 0;
    const QVector<int> prices{15, 40, 10, 5, 25, 80, 90, 90};
    QSet<int> sold;
    QVector<int> bought;
    cv::Mat base(1080, 1920, CV_8UC3, cv::Scalar(17, 23, 31)), image;
    const auto closeBox = templates.box("garden_shop_close", size);
    auto closePattern = templates.pattern("garden_shop_close", size);
    if (closePattern.channels() == 1)
        cv::cvtColor(closePattern, closePattern, cv::COLOR_GRAY2BGR);
    closePattern.copyTo(base(cv::Rect(closeBox.x(), closeBox.y(), closeBox.width(), closeBox.height())));
    const auto badge = readImage(root + "/assets/dualpc/garden_recommended.png");
    cv::Mat gray, crop;
    cv::cvtColor(badge(cv::Rect(6, 6, 35, 36)), gray, cv::COLOR_BGR2GRAY);
    cv::resize(gray, gray, {pixelRound(35 * .72), pixelRound(36 * .72)});
    cv::cvtColor(gray, crop, cv::COLOR_GRAY2BGR);
    const auto update = [&] {
        image = base.clone();
        if (receipt)
            cv::rectangle(image, {400, 700, 150, 15}, cv::Scalar(20, 210, 20), -1);
        for (int i = 0; i < 8; ++i) {
            const double x = .198 + .080 * (i % 6), row = i / 6;
            const auto box = gameBox(image, x + .006, .372 + .191 * row, x + .032, .400 + .191 * row);
            image(cv::Rect(box.x(), box.y(), box.width(), box.height()))
                .setTo(cv::Scalar(sold.contains(i) ? 99 : i + 40, 23, 31));
            if (i == scene % 8 && !sold.contains(i))
                crop.copyTo(
                    image(cv::Rect(pixelRound((x + .014) * 1920),
                                   pixelRound((.302 + .191 * row - .07) * 1080), crop.cols, crop.rows)));
        }
    };
    Input input(
        Config::defaults(), size, true,
        [&](QJsonObject event) {
            event["time"] = clock.time;
            events.append(event);
            if (event["op"] == "absolute")
                pointer = {event["x"].toInt(), event["y"].toInt()};
            if (event["op"] == "button" && event["name"] == "left" && !event["down"].toBool()) {
                if (closeBox.contains(pointer)) {
                    ++closes;
                    closed = true;
                    return QJsonObject{};
                }
                if (receipt && pointer.y() > .73 * 1080) {
                    balance -= prices[selected];
                    sold.insert(selected);
                    receipt = false;
                    ++receipts;
                    update();
                    return QJsonObject{};
                }
                if (pointer.y() >= .66 * 1080) {
                    if (selected < 0 || sold.contains(selected))
                        throw std::runtime_error("fixture purchase without item");
                    bought.append(selected);
                    if (scene & 1) {
                        if (scene & 32) {
                            receipt = true;
                        } else {
                            balance -= prices[selected];
                            if (balance < reserve)
                                throw std::runtime_error("fixture reserve exceeded");
                            sold.insert(selected);
                        }
                    }
                } else {
                    const int row = pointer.y() > .4 * 1080 ? 1 : 0;
                    selected = pixelRound((double(pointer.x()) / 1920 - .198) / .080) + row * 6;
                    if (selected < 0 || selected >= 8)
                        throw std::runtime_error("fixture item outside grid");
                }
                update();
            }
            return QJsonObject{};
        },
        execution);
    QJsonArray reads;
    TaskContext context(
        execution, input, templates, profile, [&] { return image; },
        [&](const cv::Mat &picture, QRect area, double threshold) -> QVector<TextLine> {
            reads.append(QJsonObject{{"area", QJsonArray{area.x(), area.y(), area.width(), area.height()}},
                                     {"image", QJsonArray{picture.cols, picture.rows}},
                                     {"threshold", threshold},
                                     {"time", clock.time}});
            if (picture.cols != size.width()) {
                const int index = picture.at<cv::Vec3b>(0, 0)[0] - 40;
                if (index < 0 || index >= 8 || (scene & 4 && index == scene % 8))
                    return {};
                return {{QString::number(prices[index]), area, .99}};
            }
            QVector<TextLine> lines;
            if (area.y() > 780) {
                if (receipt)
                    return {{"确认", gameBox(image, .65, .78, .73, .82), .99},
                            {"跳过", gameBox(image, .80, .78, .88, .82), .99}};
                return {};
            }
            if (receipt && area.y() > 140 && area.y() < 500)
                return {{"声骸", {500, 200, 80, 20}, .99}, {"声骸", {1000, 200, 80, 20}, .99}};
            if (area.y() < 100) {
                if (!bought.isEmpty() && scene % 7 == 0)
                    return {};
                const auto b = gameBox(image, .70, .045, .73, .075);
                lines.append({QString::number(balance), b, .99});
            }
            if (selected >= 0 && area.y() > 650 && area.y() < 800) {
                const auto b = gameBox(image, .70, .68, .80, .71);
                const auto text = sold.contains(selected)
                                      ? QString("已售罄")
                                      : QString("购买 %1").arg(prices[selected] + (scene & 8 ? 1 : 0));
                lines.append({text, b, .99});
            }
            return lines;
        });
    context.task = "GardenTask";
    context.wallTime = [] { return QDateTime(QDate(2030, 1, 2), QTime(12, 0)); };
    context.info = [&](QString key, QString value) {
        events.append(QJsonObject{{"info", key}, {"value", value}, {"time", clock.time}});
    };
    update();
    context.refresh();
    QJsonObject result;
    try {
        result["count"] = GardenType(context, root).shop();
        result["error"] = "";
    } catch (const std::exception &e) {
        result["error"] = QString::fromUtf8(e.what());
    }
    QJsonArray order;
    for (int i : bought)
        order.append(i);
    result["bought"] = order;
    result["balance"] = balance;
    result["closes"] = closes;
    result["receipts"] = receipts;
    result["closed"] = closed;
    result["events"] = events;
    result["reads"] = reads;
    result["time"] = clock.time;
    return result;
}
} // namespace okww::runtime::garden_shop_test
