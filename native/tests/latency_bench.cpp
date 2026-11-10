#include "backend.hpp"
#include "common.hpp"
#include "runtime/execution.hpp"
#include "runtime/vision.hpp"
#include <iostream>
using namespace okww;
// Frozen NV12 branch of the previous preview loop, for a same-process comparison.
static QImage scalarPreview(const Frame &f, QSize target) {
    QSize size(f.width, f.height);
    size.scale(target, Qt::KeepAspectRatio);
    QImage image(size, QImage::Format_RGB32);
    const auto *p = reinterpret_cast<const uint8_t *>(f.bytes.constData());
    for (int y = 0; y < size.height(); ++y) {
        auto row = image.scanLine(y);
        const int sy = int(int64_t(y) * f.height / size.height());
        for (int x = 0; x < size.width(); ++x) {
            const int sx = int(int64_t(x) * f.width / size.width());
            const auto uv = p + f.stride * f.height + (sy / 2) * f.stride + (sx & ~1);
            const int yy = std::max(0, int(p[sy * f.stride + sx]) - 16) * 1220542;
            const int u = int(uv[0]) - 128, v = int(uv[1]) - 128;
            const auto clip = [](int value) {
                return uint8_t(std::clamp((value + (1 << 19)) >> 20, 0, 255));
            };
            row[x * 4] = clip(yy + 2116026 * u);
            row[x * 4 + 1] = clip(yy - 409993 * u - 852492 * v);
            row[x * 4 + 2] = clip(yy + 1673527 * v);
            row[x * 4 + 3] = 255;
        }
    }
    return image;
}
static double percentile(std::vector<double> values, double fraction) {
    std::sort(values.begin(), values.end());
    return values[size_t(std::ceil(values.size() * fraction)) - 1];
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2)
        return 2;
    Frame frame;
    frame.width = 1920;
    frame.height = 1080;
    frame.stride = 1920;
    frame.format = PixelFormat::NV12;
    frame.bytes.resize(1920 * 1080 * 3 / 2);
    for (int i = 0; i < frame.bytes.size(); ++i)
        frame.bytes[i] = char(16 + (i * 17 % 220));
    QJsonArray results;
    for (auto size : {QSize(640, 360), QSize(1280, 720), QSize(1920, 1080)}) {
        std::vector<double> old, current;
        QImage a, b;
        for (int i = 0; i < 60; ++i) {
            auto began = nowNs();
            a = scalarPreview(frame, size);
            const double oldMs = (nowNs() - began) / 1e6;
            began = nowNs();
            b = previewImage(frame, size);
            const double newMs = (nowNs() - began) / 1e6;
            if (i >= 10) {
                old.push_back(oldMs);
                current.push_back(newMs);
            }
        }
        int difference = 0;
        for (int y = 0; y < a.height(); ++y)
            for (int x = 0; x < a.width(); ++x) {
                const auto p = a.pixelColor(x, y), q = b.pixelColor(x, y);
                difference = std::max({difference, std::abs(p.red() - q.red()),
                                       std::abs(p.green() - q.green()), std::abs(p.blue() - q.blue())});
            }
        results.append(QJsonObject{{"preview_width", size.width()},
                                   {"preview_height", size.height()},
                                   {"scalar_p50_ms", percentile(old, .5)},
                                   {"current_p50_ms", percentile(current, .5)},
                                   {"scalar_p95_ms", percentile(old, .95)},
                                   {"current_p95_ms", percentile(current, .95)},
                                   {"maximum_channel_difference", difference}});
    }
    auto source = std::make_shared<Frame>(frame);
    source->sequence = 1;
    auto config = Config::defaults();
    runtime::InferenceFrames cache(config);
    cache.get(source);
    auto began = nowNs();
    for (int i = 0; i < 1000; ++i)
        cache.get(source);
    const double cachedUs = (nowNs() - began) / 1000000.;
    began = nowNs();
    for (int i = 0; i < 50; ++i)
        runtime::inferenceImage(frame, config);
    const double decodeUs = (nowNs() - began) / 50000.;
    Backend backend(false);
    backend.start(config, true);
    backend.arm(true);
    began = nowNs();
    for (int i = 0; i < 1000; ++i) {
        backend.snapshot();
        backend.nativeCommand({{"op", "heartbeat"}});
    }
    const double fullHeartbeatUs = (nowNs() - began) / 1000000.;
    began = nowNs();
    for (int i = 0; i < 1000; ++i)
        backend.heartbeat();
    const double heartbeatUs = (nowNs() - began) / 1000000.;
    backend.stop();
    writeJson(QString::fromLocal8Bit(argv[1]),
              {{"scope",
                "synthetic frames and host waits only; no capture card, box, game or HDMI latency measured"},
               {"source", "1920x1080 NV12"},
               {"preview", results},
               {"uncached_decode_us_per_read", decodeUs},
               {"cached_read_us", cachedUs},
               {"conversion_count_for_1001_reads", double(cache.conversions())},
               {"old_health_check_us", fullHeartbeatUs},
               {"current_health_check_us", heartbeatUs}});
    return 0;
}
