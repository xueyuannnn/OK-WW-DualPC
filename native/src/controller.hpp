#pragma once
#include "common.hpp"
namespace okww {
class Controller {
    Config cfg_;
    SOCKET socket_ = INVALID_SOCKET;
    HANDLE device_ = INVALID_HANDLE_VALUE;
    std::recursive_mutex mutex_;
    uint32_t index_ = 0, uuid_ = 0;
    bool connected_ = false, keyboard_ = true;
    int buttons_ = 0, modifiers_ = 0;
    std::set<int> keys_;
    QString capability_;
    QList<QByteArray> history_;
    std::deque<double> latencyMs_;
    double latencyP50_ = 0, latencyP95_ = 0;
    void noteLatency(i64 began);
    void write(const QByteArray &data);
    void exchange(uint32_t command, const QByteArray &payload = {});
    QString makcu(const QStringList &commands = {});
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
    static QJsonArray serialDevices();
};
} // namespace okww
