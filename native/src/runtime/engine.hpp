#pragma once
#include "task.hpp"
#include "backend.hpp"
#include "echo_detector.hpp"
#include "team_store.hpp"
#include "notification.hpp"

namespace okww::runtime {
class Engine final : public QObject {
    Q_OBJECT
    Backend &backend_;
    QString root_, data_;
    Profile profile_;
    std::shared_ptr<TeamStore> teams_;
    QVector<TaskDefinition> tasks_;
    ExecutionGate gate_;
    std::thread worker_;
    std::thread modelWorker_;
    std::atomic<bool> modelsReady_{false};
    QString modelError_;
    std::atomic<bool> paused_{true};
    std::atomic<bool> foreground_{false};
    std::atomic<bool> cancelForeground_{false};
    std::atomic<bool> workerFinished_{true}, warming_{false};
    std::mutex lifecycle_;
    std::optional<QString> pendingTask_;
    QJsonObject pendingOptions_;
    QTimer launchTimer_;
    QTimer exitTimer_;
    QElapsedTimer exitWait_;
    std::unique_ptr<Notifications> notifications_;
    std::atomic<uint64_t> generation_{0};
    std::unique_ptr<Templates> templates_;
    std::unique_ptr<Ocr> ocr_;
    std::unique_ptr<EchoDetector> echo_;
    std::shared_ptr<TaskState> taskState_ = std::make_shared<TaskState>();
    QJsonArray scripts_;
    QJsonArray scriptTemplates_;
    bool scriptsBusy_ = false;
    bool loadingScripts_ = false;
    bool teamSwitchBusy_ = false;
    QJsonArray teamSwitchSnapshot_;
    QMap<QString, std::shared_ptr<std::atomic<bool>>> scriptEnabled_;
    void scriptCommand(QJsonObject request);
    void switchTeamCode(QJsonObject request);
    void invalidateParty();
    void run(QString task, uint64_t epoch, QJsonObject options);
    void start(const QString &task, QJsonObject options = {});
    void tryLaunch();
    void pause();
    QJsonObject status(QString current = {}) const;
    QJsonObject catalog() const;
    void log(QString message);
    void notify(Notice notice);
    void postStatus(uint64_t generation, QString current = {});
    void preloadModels();

  public:
    Engine(Backend &backend, QString root, QString data, QObject *parent = nullptr);
    ~Engine() override;
    bool idle() const { return paused_ && workerFinished_ && !teamSwitchBusy_; }
    void load();
    void command(const QJsonObject &request);
  signals:
    void message(QJsonObject value);
};
} // namespace okww::runtime
