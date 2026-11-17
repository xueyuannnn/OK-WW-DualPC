#pragma once
#include "reference_stamina.hpp"
#include <opencv2/imgproc.hpp>
namespace okww::runtime::stamina_actions_test {
struct ClockProbe : Clock {
    double time = 0;
    std::function<void()> tick;
    double now() const override { return time; }
    void sleep(double seconds) override {
        time += seconds;
        if (tick)
            tick();
    }
};
QJsonArray rectangle(QRect box) { return {box.x(), box.y(), box.width(), box.height()}; }
template <bool Rust> QJsonObject cube(int scene) {
    using Budget = std::conditional_t<Rust, CubeBudget, reference_stamina::CubeBudget>;
    const auto root = QString(OKWW_SOURCE_ROOT);
    QTemporaryDir directory;
    Templates templates(root + "/assets/coco_annotations.json");
    Profile profile(directory.path(), root + "/native/resources/catalog.json");
    profile.set("Monthly Card Config", "Check Monthly Card", false);
    ClockProbe clock;
    ExecutionGate gate;
    Execution execution(gate, gate.start(), clock);
    clock.tick = [&] {
        if ((scene & 64) && clock.time > 2)
            gate.stop();
    };
    QJsonArray events;
    QPointF mouse;
    bool dialog = (scene & 1) != 0;
    int current = (scene % 4) * 40, stock = scene % 10, confirmations = 0, sources = 0, reads = 0;
    Input input(
        Config::defaults(), {1920, 1080}, true,
        [&](QJsonObject event) {
            event["time"] = clock.time;
            events.append(event);
            if (event["op"] == "absolute")
                mouse = {event["x"].toDouble(), event["y"].toDouble()};
            if (event["op"] == "button" && !event["down"].toBool()) {
                if (mouse.y() < 100)
                    dialog = true;
                else if (mouse.x() > 1100 && mouse.y() > 700) {
                    ++confirmations;
                    dialog = (scene & 8) != 0;
                    if (scene % 7 != 0)
                        current += 60;
                    if (scene % 11 != 0)
                        --stock;
                } else if (mouse.x() < 800 && mouse.y() > 700)
                    dialog = false;
            }
            return QJsonObject{};
        },
        execution);
    cv::Mat frame = cv::Mat::zeros(1080, 1920, CV_8UC3);
    TaskContext context(
        execution, input, templates, profile,
        [&] {
            ++sources;
            return frame;
        },
        [&](const cv::Mat &image, QRect area, double threshold) -> QVector<TextLine> {
            ++reads;
            events.append(QJsonObject{{"read", reads},
                                      {"image", QJsonArray{image.cols, image.rows}},
                                      {"area", rectangle(area)},
                                      {"threshold", threshold},
                                      {"time", clock.time}});
            if (image.rows < 500) {
                if (scene % 17 == 0)
                    return {{"x", {0, 0, 30, 30}, .99}};
                QVector<TextLine> values{{QString::number(stock), {0, 0, 30, 30}, .99}};
                if (scene % 19 == 0)
                    values.append({"2", {30, 0, 20, 30}, .99});
                return values;
            }
            if (area.y() == 0) {
                if (scene % 23 == 0)
                    return {};
                return {{QString("%1/240").arg(current), {1000, 20, 100, 30}, .99},
                        {"25", {900, 20, 50, 30}, .99}};
            }
            if (!dialog)
                return {};
            if (area.y() < 250)
                return {{"补充结晶波片", area, .99}};
            if (area.y() < 650)
                return {{scene % 13 == 0 ? "消耗2结晶溶剂补充60结晶波片" : "消耗1结晶溶剂补充60结晶波片",
                         area, .99}};
            if (area.x() > 900 && scene % 29 == 0)
                return {};
            return {{area.x() > 900 ? "确认" : "取消", area, .99}};
        });
    context.task = "DailyTask2";
    context.frame = frame;
    context.frameVersion = 1;
    context.wallTime = [] { return QDateTime(QDate(2030, 1, 2), QTime(12, 0)); };
    if (scene & 16)
        context.info = [&](QString key, QString value) {
            events.append(QJsonObject{{"info", key}, {"value", value}, {"time", clock.time}});
        };
    const auto file = directory.path() + "/cube.json";
    Budget budget(file, scene % 4 + 1, QDateTime::fromString("2026-11-16T05:00:00Z", Qt::ISODate));
    QJsonObject output;
    try {
        bool result;
        if constexpr (Rust)
            result = useOneDailyCube(context, budget, (scene & 2) != 0);
        else
            result = reference_stamina::reference_useOneDailyCube(context, budget, (scene & 2) != 0);
        output["result"] = result;
        output["error"] = "";
    } catch (const std::exception &e) {
        output["error"] = QString::fromUtf8(e.what()).replace(directory.path(), "DATA");
    }
    output["events"] = events;
    output["time"] = clock.time;
    output["current"] = current;
    output["stock"] = stock;
    output["confirmations"] = confirmations;
    output["sources"] = sources;
    output["used"] = budget.used();
    output["pending"] = budget.pending();
    output["remaining"] = budget.remaining();
    if (QFile::exists(file))
        output["journal"] = readJson(file);
    return output;
}
QJsonObject claim(int scene, bool rust) {
    const auto root = QString(OKWW_SOURCE_ROOT);
    QTemporaryDir directory;
    Templates templates(root + "/assets/coco_annotations.json");
    Profile profile(directory.path(), root + "/native/resources/catalog.json");
    profile.set("Monthly Card Config", "Check Monthly Card", false);
    ClockProbe clock;
    ExecutionGate gate;
    Execution execution(gate, gate.start(), clock);
    QJsonArray events;
    QString stage = "claim";
    QPointF pointer;
    int clicks = 0, reads = 0, sources = 0;
    QMap<QString, cv::Mat> images;
    const auto frame = [&](QString name, QStringList features) {
        cv::Mat image(1080, 1920, CV_8UC3, cv::Scalar(17, 23, 31));
        for (const auto &feature : features) {
            const auto box = templates.box(feature, {1920, 1080});
            auto pattern = templates.pattern(feature, {1920, 1080});
            if (pattern.channels() == 1)
                cv::cvtColor(pattern, pattern, cv::COLOR_GRAY2BGR);
            pattern.copyTo(image(cv::Rect(box.x(), box.y(), box.width(), box.height())));
        }
        images[name] = image;
    };
    frame("claim", {"claim_stamina_sign", "confirm_btn_hcenter_vcenter", "cancel_button_hcenter_vcenter"});
    frame("world", {});
    frame("refill", {"gem_add_stamina"});
    frame("retry", scene & 16 ? QStringList{"claim_stamina_sign"}
                              : QStringList{"claim_stamina_sign", "confirm_btn_hcenter_vcenter",
                                            "cancel_button_hcenter_vcenter"});
    Input input(
        Config::defaults(), {1920, 1080}, true,
        [&](QJsonObject event) {
            event["time"] = clock.time;
            events.append(event);
            if (event["op"] == "absolute")
                pointer = {event["x"].toDouble(), event["y"].toDouble()};
            if (event["op"] == "button" && !event["down"].toBool()) {
                ++clicks;
                if (stage == "claim")
                    stage = scene & 1 ? "refill" : "world";
                else if (stage == "retry" && !(scene & 32))
                    stage = "world";
            }
            if (event["op"] == "key" && !event["down"].toBool() &&
                event["code"].toInt() == Input::keyCode("esc") && stage == "refill")
                stage = "retry";
            return QJsonObject{};
        },
        execution);
    const int currents[]{0, 40, 60, 120};
    const int current = currents[(scene / 2) % 4], reserve = scene & 4 ? 120 : 0;
    TaskContext context(
        execution, input, templates, profile,
        [&] {
            ++sources;
            return images[stage];
        },
        [&](const cv::Mat &, QRect area, double threshold) {
            ++reads;
            events.append(QJsonObject{
                {"read", reads}, {"area", rectangle(area)}, {"threshold", threshold}, {"time", clock.time}});
            return QVector<TextLine>{{QString("%1/240").arg(current), {1000, 20, 100, 30}, .99},
                                     {QString::number(reserve), {900, 20, 50, 30}, .99}};
        });
    context.task = "TacetTask2";
    context.taskOverrides = {{"Reward Claim Mode", scene & 2 ? "Single" : "Prefer Double"}};
    context.frame = images[stage];
    context.frameVersion = 1;
    context.wallTime = [] { return QDateTime(QDate(2030, 1, 2), QTime(12, 0)); };
    if (scene & 8)
        context.info = [&](QString key, QString value) {
            events.append(QJsonObject{{"info", key}, {"value", value}, {"time", clock.time}});
        };
    QJsonObject output;
    const int limits[]{0, 60, 120, 180};
    const int remaining = limits[(scene / 4) % 4];
    try {
        if (rust) {
            const auto result = claimStamina(context, 60, remaining, (scene & 8) != 0);
            output["result"] = QJsonArray{result.used, result.repeat};
        } else {
            const auto result =
                reference_stamina::reference_claimStamina(context, 60, remaining, (scene & 8) != 0);
            output["result"] = QJsonArray{result.used, result.repeat};
        }
        output["error"] = "";
    } catch (const std::exception &e) {
        output["error"] = QString::fromUtf8(e.what());
    }
    output["events"] = events;
    output["time"] = clock.time;
    output["stage"] = stage;
    output["clicks"] = clicks;
    output["sources"] = sources;
    return output;
}
} // namespace okww::runtime::stamina_actions_test
