#pragma once
#include "capture.hpp"
#include "controller.hpp"
namespace okww {
class Backend {
    mutable std::recursive_mutex mutex_;
    Config cfg_;
    std::unique_ptr<Capture> capture_;
    std::unique_ptr<Controller> box_;
    bool running_ = false, simulation_ = false, armed_ = false;
    QString error_;
    QJsonObject inputFault_;
    i64 heartbeat_ = 0;
    FramePtr simulated_;
    std::atomic<bool> exit_{false};
    std::thread monitor_, server_;
    SOCKET listen_ = INVALID_SOCKET;
    HANDLE mapping_ = nullptr;
    void *mapped_ = nullptr;
    QString token_, mappingName_;
    int port_ = 0;
    WSADATA wsa_{};
    void serve();
    void monitor();
    QJsonObject dispatch(const QJsonObject &q);
    FramePtr frameLocked();
    FramePtr rawFrameLocked();
    void disarmFaultLocked(const QString &reason, const QString &message);

  public:
    static constexpr int mappingCapacity = 64 * 1024 * 1024;
    explicit Backend(bool compatibilityChannel = true);
    ~Backend();
    void start(const Config &cfg, bool simulation = false);
    bool configurationMatches(const Config &cfg) const;
    void stop();
    void arm(bool enabled);
    bool heartbeat();
    QJsonObject testMove();
    QJsonObject snapshot();
    FramePtr latestFrame();
    QJsonObject nativeCommand(QJsonObject command);
    QImage preview(QSize target, uint64_t &after);
    QString token() const { return token_; }
    QString mappingName() const { return mappingName_; }
    int port() const { return port_; }
};
} // namespace okww
