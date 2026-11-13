#pragma once
#include "common.hpp"
#include <functional>
namespace okww {
struct Device {
    int index = 0;
    QString name, path;
};
struct Mode {
    int width = 0, height = 0;
    double fps = 0;
    QString fourcc;
    QJsonObject json() const {
        return {{"width", width}, {"height", height}, {"fps", fps}, {"fourcc", fourcc}};
    }
};
std::vector<Device> devices(const QString &backend);
std::vector<Mode> modes(const Config &config);
QJsonArray captureCapabilities(
    const Config &config,
    const std::function<std::vector<Device>(const QString &)> &enumerateDevices = devices,
    const std::function<std::vector<Mode>(const Config &)> &enumerateModes = modes);
class Capture {
    Config config_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::array<std::shared_ptr<Frame>, 6> pool_;
    FramePtr latest_;
    QString error_;
    bool ready_ = false;
    std::atomic<bool> stop_{false};
    uint64_t seq_ = 0;
    double negotiatedFps_ = 0;
    std::deque<i64> arrivals_;
    void run();
    void runDs();
    void runMf();
    void runDesktop();

  public:
    explicit Capture(Config c) : config_(std::move(c)) {}
    ~Capture() { stop(); }
    void start();
    void stop();
    FramePtr next(uint64_t after);
    double fps();
    FramePtr latest();
    QJsonObject actual();
    QString error();
    void publish(const uint8_t *data, size_t length, int width, int height, int stride, PixelFormat format,
                 bool bottomUp, i64 stamp);
};
} // namespace okww
