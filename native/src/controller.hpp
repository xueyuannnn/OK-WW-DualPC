#pragma once
#include "common.hpp"
#include "desktop.hpp"
namespace okww {
class Controller {
    Config cfg_;
    SOCKET socket_ = INVALID_SOCKET;
    std::recursive_mutex mutex_;
    uint32_t index_ = 0, uuid_ = 0;
    bool connected_ = false;
    int buttons_ = 0, modifiers_ = 0;
    std::set<int> keys_;
    QString capability_;
    QList<QByteArray> history_;
    std::deque<double> latencyMs_;
    double latencyP50_ = 0, latencyP95_ = 0;
    std::unique_ptr<DesktopTarget> captureTarget_;
    std::unique_ptr<DesktopTarget> target_;
    bool local() const { return cfg_.s("kmbox.protocol") == "sendinput"; }
    void localSend(std::vector<INPUT> inputs, bool releasing = false);
    void noteLatency(i64 began);
    void recordPacket(const QByteArray &data);
    void exchange(uint32_t command, const QByteArray &payload = {});
    void mouse(uint32_t command, int x = 0, int y = 0, int wheel = 0);
    void keyboardReport();

  public:
    explicit Controller(Config c) : cfg_(std::move(c)) {}
    ~Controller() { close(); }
    void connect();
    void close() noexcept;
    void release();
    void probe();
    void move(int x, int y);
    void absolute(int x, int y, int width, int height);
    void button(const QString &name, bool down);
    void key(int code, bool down);
    void wheel(int steps);
    bool connected();
    QJsonObject state();
    QList<QByteArray> history() {
        std::lock_guard lock(mutex_);
        return history_;
    }
};
} // namespace okww
