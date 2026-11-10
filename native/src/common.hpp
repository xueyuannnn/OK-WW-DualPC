#pragma once
#include <winsock2.h>
#include <windows.h>
#include <objbase.h>
#include <QImage>
#include <QtCore>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>
namespace okww {
using i64 = int64_t;
inline i64 nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
[[noreturn]] inline void fail(const QString &s) { throw std::runtime_error(s.toUtf8().constData()); }
inline QString errorText(const std::exception &e) { return QString::fromUtf8(e.what()); }
inline void hr(HRESULT result, const QString &s) {
    if (FAILED(result))
        fail(s + QString(" (0x%1)").arg(uint32_t(result), 8, 16, QChar('0')));
}
struct ComInit {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComInit() {
        if (SUCCEEDED(result))
            CoUninitialize();
    }
};
struct Config {
    QJsonObject json;
    static Config defaults();
    static Config load(const QString &path);
    void save(const QString &path) const;
    QJsonValue get(const QString &path) const;
    QString s(const QString &p) const { return get(p).toString(); }
    double d(const QString &p) const { return get(p).toDouble(); }
    int n(const QString &p) const { return int(d(p)); }
    bool b(const QString &p) const { return get(p).toBool(); }
    void set(const QString &path, const QJsonValue &value);
    void validateCapture() const;
    void validateBox() const;
};
enum class PixelFormat { BGR24, BGRA32, NV12, P010, YUY2, UYVY, I420, MJPG };
struct Frame {
    QByteArray bytes;
    int width = 0, height = 0, stride = 0;
    PixelFormat format = PixelFormat::BGR24;
    bool bottomUp = false;
    uint64_t sequence = 0;
    i64 captured = 0;
};
using FramePtr = std::shared_ptr<const Frame>;
QString formatName(PixelFormat f);
QImage frameImage(const Frame &f);
QImage previewImage(const Frame &f, QSize target = QSize(960, 640));
QJsonObject readJson(const QString &path);
void writeJson(const QString &path, const QJsonObject &obj);
} // namespace okww
