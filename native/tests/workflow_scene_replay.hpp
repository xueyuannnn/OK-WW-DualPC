#pragma once
// Shared synthetic scene harness from the existing workflow regression suite.
#include "runtime/task.hpp"
#include <QtTest>
#include <opencv2/imgproc.hpp>
using namespace okww;using namespace okww::runtime;
namespace {
struct SceneClock : Clock {
    double time = 0;
    double minimumStep = 0;
    std::function<void()> tick;
    double now() const override { return time; }
    void sleep(double seconds) override {
        time += std::max(seconds, minimumStep);
        if (time > 1000)
            fail("离线回放超出时间限制");
        if (tick)
            tick();
    }
};
struct Scenes {
    const QString root = OKWW_SOURCE_ROOT;
    Templates templates{root + "/assets/coco_annotations.json"};
    QTemporaryDir directory;
    Profile profile{directory.path(), root + "/native/resources/catalog.json"};
    Config config = Config::defaults();
    SceneClock clock;
    ExecutionGate gate;
    Execution execution{gate, gate.start(), clock};
    QSize size;
    QString scene = "blank";
    QMap<QString, cv::Mat> frames;
    QMap<QString, QVector<TextLine>> words;
    QVector<QJsonObject> events;
    std::function<void(QJsonObject)> action;
    std::function<QVector<TextLine>(const cv::Mat &, QRect)> scaledText;
    std::function<QVector<TextLine>(QRect, QVector<TextLine>)> textOverride;
    QPoint pointer;
    Input input;
    TaskContext c;
    explicit Scenes(QSize dimensions = {1920, 1080})
        : size(dimensions), input(
                                settings(dimensions), dimensions, true,
                                [this](QJsonObject event) {
                                    events << event;
                                    if (event["op"] == "absolute")
                                        pointer = {event["x"].toInt(), event["y"].toInt()};
                                    if (action)
                                        action(event);
                                    return QJsonObject{};
                                },
                                execution),
          c(
              execution, input, templates, profile, [this] { return frames[scene]; },
              [this](const cv::Mat &image, QRect area, double) {
                  if (image.cols != size.width() && scaledText)
                      return scaledText(image, area);
                  QVector<TextLine> out;
                  for (auto line : words[scene])
                      if (area.contains(line.center().toPoint()))
                          out << line;
                  if (textOverride) return textOverride(area, out);
                  return out;
              }) {
        c.wallTime = [] { return QDateTime(QDate(2030, 1, 2), QTime(12, 0)); };
        frame("blank", {});
        frame("world", {"char_2_text", "char_3_text", "world_earth_icon"});
        frame("realm", {"char_2_text", "char_3_text", "illusive_realm_exit"});
    }
    Config settings(QSize dimensions) {
        config.set("capture.output_width", dimensions.width());
        config.set("capture.output_height", dimensions.height());
        config.set("kmbox.desktop_width", dimensions.width());
        config.set("kmbox.desktop_height", dimensions.height());
        return config;
    }
    void frame(QString name, QStringList features) {
        cv::Mat image(size.height(), size.width(), CV_8UC3, cv::Scalar(17, 23, 31));
        for (auto feature : features) {
            auto r = templates.box(feature, size);
            auto pattern = templates.pattern(feature, size);
            if (pattern.channels() == 1)
                cv::cvtColor(pattern, pattern, cv::COLOR_GRAY2BGR);
            if (r.x() < 0 || r.y() < 0 || r.right() >= size.width() || r.bottom() >= size.height())
                fail("回放模板超出画布：" + feature);
            pattern.copyTo(image(cv::Rect(r.x(), r.y(), r.width(), r.height())));
        }
        frames[name] = image;
    }
    TextLine word(QString text, double x, double y, double w = .12, double h = .03) {
        return {text, gameBox(frames["blank"], x, y, x + w, y + h), .99};
    }
    void show(QString name) {
        scene = name;
        c.refresh();
    }
    bool leftUp(const QJsonObject &e) const {
        return e["op"] == "button" && e["name"] == "left" && !e["down"].toBool();
    }
    bool keyUp(const QJsonObject &e, int key) const {
        return e["op"] == "key" && e["code"].toInt() == key && !e["down"].toBool();
    }
    int clicks() const {
        return int(std::count_if(events.begin(), events.end(), [&](auto e) { return leftUp(e); }));
    }
};
} // namespace
