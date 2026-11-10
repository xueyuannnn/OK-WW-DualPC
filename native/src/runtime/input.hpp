#pragma once
#include "execution.hpp"
#include <optional>

namespace okww::runtime {
using InputTransport = std::function<QJsonObject(QJsonObject)>;
class Input {
    Config config_;
    QSize rawSize_;
    InputTransport transport_;
    Execution &execution_;
    QSet<int> keys_;
    QSet<QString> buttons_;
    std::optional<QPointF> position_;
    bool absolute_;
    void send(QJsonObject command);
    void counts(int x, int y, bool precise);

  public:
    Input(Config config, QSize rawSize, bool absolute, InputTransport transport, Execution &execution)
        : config_(std::move(config)), rawSize_(rawSize), transport_(std::move(transport)),
          execution_(execution), absolute_(absolute) {}
    ~Input() { release(); }
    static int keyCode(QString key);
    QJsonObject workshopCommand(QJsonObject command);
    QPoint screenPoint(QPointF point) const;
    void keyDown(QString key);
    void keyUp(QString key);
    void key(QString key, double seconds = .02);
    void buttonDown(QString button = "left");
    void buttonUp(QString button = "left");
    void click(std::optional<QPointF> point = {}, double seconds = .05, QString button = "left");
    void move(QPointF point);
    void relative(double x, double y);
    void scroll(int steps, std::optional<QPointF> point = {});
    void swipe(QPointF from, QPointF to, double milliseconds, double settle = 0);
    void text(const QString &value);
    void release() noexcept;
    bool idle() const { return keys_.isEmpty() && buttons_.isEmpty(); }
};
} // namespace okww::runtime
