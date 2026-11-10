#pragma once
#include "team_macro.hpp"
#include <QProcess>
namespace okww::runtime {
QString workshopRoot(const QString &root);
QString workshopPython(const QString &root);
QJsonObject workshopManage(const QString &root, const QString &data, QJsonObject request);
// File management has its own thread and process, independent of combat/input.
class WorkshopFileSession {
    struct State;
    std::unique_ptr<State> state_;
  public:
    WorkshopFileSession(QString root, QString data);
    ~WorkshopFileSession();
    QJsonObject command(QJsonObject request);
    qint64 processId() const;
};
inline constexpr int workshopProtocolVersion = 2, workshopApiVersion = 1;
QJsonObject workshopCombatState(CombatHost &host);
void applyWorkshopCombatState(CombatHost &host, const QJsonObject &state);
void validateWorkshopHandshake(const QJsonObject &hello);
class PythonTeamMacro final : public TeamMacro {
    QString root_, data_, pythonRoot_;
    QProcess process_;
    QByteArray buffer_, errors_;
    HANDLE mapping_ = nullptr;
    void *mapped_ = nullptr;
    QString mappingName_;
    QString session_, requestId_;
    uint64_t requestSequence_ = 0, frameSequence_ = UINT64_MAX, frameCopies_ = 0, frameRequests_ = 0;
    QSize frameSize_;
    double startupMs_ = 0, pingMs_ = 0, copyMs_ = 0;
    i64 lastFrameAt_ = 0;
    QVector<double> pingSamples_, frameInputSamples_;
    QElapsedTimer telemetryClock_;
    bool loaded_ = false;
    QVector<QPair<Character *, Role>> originalRoles_;
    void stop() noexcept;
    void startWorker();
    void write(QJsonObject value);
    QJsonObject receive(int timeoutMs);
    QJsonObject call(QJsonObject request);
    QJsonObject rpc(QJsonObject request);
    void sync(const QJsonObject &state);
    void report();

  public:
    PythonTeamMacro(CombatHost &host, QString root, QString data);
    ~PythonTeamMacro() override;
    void configure() override;
    void reset() override;
    void combatEnd() override;
    bool perform(Character &entry) override;
    std::optional<int> priority(const Character &c, bool intro) override;
};
} // namespace okww::runtime
