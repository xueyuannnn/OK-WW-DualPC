#pragma once
#include "backend.hpp"
#include "app_info.hpp"
#include "classic_ui.hpp"
#include <QtConcurrent>
#include <QtWidgets>
#include <optional>
#ifdef OKWW_NATIVE_ONLY
#include "runtime/engine.hpp"
#endif
namespace okww {
class Preview final : public QWidget {
    QImage image_, reduced_;

  public:
    explicit Preview(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumSize(240, 135);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    void setImage(const QImage &image) {
        image_ = image;
        reduced_ = {};
        update();
    }
    QSize imageSize() const { return image_.size(); }
    QSize pixelSize() const {
        return {int(std::ceil(width() * devicePixelRatioF())), int(std::ceil(height() * devicePixelRatioF()))};
    }
    void paintEvent(QPaintEvent *) override;
};
class NoWheel final : public QObject {
  public:
    using QObject::QObject;
    bool eventFilter(QObject *o, QEvent *e) override;
};
class Window final : public QMainWindow {
    Q_OBJECT
    QString root_, data_, python_;
    Config config_, deviceDraftConfig_;
#ifdef OKWW_NATIVE_ONLY
    Backend backend_{false};
    std::unique_ptr<runtime::Engine> nativeEngine_;
    int nativeGeneration_ = 0;
    bool nativeRestartPending_ = false;
#else
    Backend backend_;
#endif
    QProcess engine_;
    QByteArray outputBuffer_, errorBuffer_;
    QHash<int, std::function<void(QJsonObject)>> callbacks_;
    QHash<int, std::function<void(QString)>> errorCallbacks_;
    int nextId_ = 1;
    bool engineReady_ = false, busy_ = false, loading_ = false, automation_ = false, closing_ = false;
    bool restartEngine_ = false, previewOnTop_ = false;
    bool teamOperation_ = false;
    bool devicesRunning_ = false, stopRequested_ = false, stoppingDevices_ = false, taskStopped_ = false;
    QWidget *deviceSettings_ = nullptr;
    QLabel *deviceSaveStatus_ = nullptr;
    QTimer deviceSaveTimer_;
    bool deviceAutoSaveReady_ = false, deviceSettingsDirty_ = false;
    QJsonObject deviceUiState_;
    QComboBox *inferenceResolution_ = nullptr;
    QCheckBox *cropCapture_ = nullptr;
    std::array<QSpinBox *, 4> cropSpins_{};
    struct StartRequest {
        QString task;
        bool exitAfter;
        double limitSeconds;
        Config config;
        bool connected = false;
    };
    std::optional<StartRequest> pendingStart_;
    QTimer startupTimer_, closeRetryTimer_;
    void requestExit();
    std::function<void(Backend &, const Config &)> deviceConnector_;
    std::function<QJsonArray(const Config &)> captureScanner_;
    QLabel *captureScanStatus_ = nullptr;
    QLabel *boxTestStatus_ = nullptr;
    bool minimizeToTray_ = false;
    QString hotkey_ = "F9";
    QSystemTrayIcon *tray_ = nullptr;
    QJsonArray editingTeam_;
    QString editingMember_;
    QString editingSource_, editingView_, editingMode_;
    int codeRequest_ = 0;
    QJsonObject catalog_;
    int pendingFieldWrites_ = 0;
    QJsonArray modes_;
    QJsonObject uiPreferences_;
    QString currentTask_;
    QListWidget *navigation_ = nullptr, *bottomNavigation_ = nullptr, *homeDevices_ = nullptr;
    QHash<QString, QLabel *> globalDescriptions_;
    QTableWidget *scheduleTable_ = nullptr;
    QLabel *characterPortrait_ = nullptr, *teamCompatibility_ = nullptr;
    QLabel *connectionTitle_ = nullptr, *captureConnection_ = nullptr, *boxConnection_ = nullptr,
           *frameParameters_ = nullptr, *frameAge_ = nullptr, *connectionTaskStatus_ = nullptr,
           *previewRate_ = nullptr;
    QPushButton *startConnection_ = nullptr;
    QCheckBox *manualCapture_ = nullptr;
    int previewHeight_ = 405, previewFps_ = 60;
    QElapsedTimer previewRateClock_;
    int previewFrames_ = 0;
    double actualPreviewFps_ = 0;
    std::array<QWidget *, 2> taskInfoPanels_{};
    std::array<std::array<QLabel *, 3>, 2> taskInfoValues_{};
    qint64 taskInfoElapsedSeconds_ = 0;
    QJsonObject combatInfo_;
    QPointer<QMessageBox> runtimeErrorDialog_;
    void showExecutionError(const QString &error, QJsonObject event = {});
    QElapsedTimer taskRunClock_;
    QWidget *buildTaskInfo(bool trigger);
    void refreshTaskInfo();
    void savePreviewPreferences();
    QPushButton *teamModeButton_ = nullptr, *allTeamModesButton_ = nullptr, *saveCodeButton_ = nullptr;
    QHash<QString, int> pageIndices_;
    QHash<QString, classic::ExpandCard *> taskCards_, globalCards_;
    QHash<QString, QPushButton *> taskStarts_, taskStops_, taskPauses_;
    QHash<QString, classic::Switch *> triggerSwitches_;
    QHash<QString, QWidget *> globalPages_;
    bool navigationExpanded_ = true;
    void navigate(const QString &page);
    void applyTheme();
    void renderTaskCards(bool trigger);
    void renderGlobalCards();
    void refreshTaskActions();
    QWidget *capturePage();
    QWidget *buildClassicCharacters();
    QWidget *buildClassicSettings();
    QWidget *buildClassicAbout();
    QWidget *buildClassicPreview();
    void updateCharacterPortrait();
    QString characterName(const QString &id) const;
    bool settleCodeEdits();
    void saveCurrentCode();
    void switchTeamCode(QJsonObject request);
    void requestTeamCode(const QString &source = {});
    void restoreCodeSelection();
    void refreshScheduleTable();
    QWidget *globalGroupPage(const QString &id, const QString &title);
    QWidget *aboutPage();
#ifdef OKWW_NATIVE_ONLY
    QWidget *schedulePage();
    QWidget *templatesPage();
    std::function<void()> refreshTemplates_;
    QWidget *scriptsPage();
    void openWorkshop();
    void updateWorkshop();
    void workshopRequest(QJsonObject request, std::function<void(QJsonObject)> done);
    bool workshopBusy_ = false;
    QTimer workshopTimer_;
    void refreshScripts();
    void showScript(const QString &id);
    void withSavedScript(std::function<void()> action);
    void saveScript(std::function<void()> done = {});
    void scriptAction(QJsonObject command, std::function<void(QJsonObject)> done = {});
    void recordScript();
    QComboBox *scriptChoice_ = nullptr;
    QListWidget *scriptSnippets_ = nullptr;
    QPlainTextEdit *scriptEditor_ = nullptr;
    QLabel *scriptMessage_ = nullptr;
    QSpinBox *scriptRepeat_ = nullptr;
    QCheckBox *scriptContinuous_ = nullptr;
    QString editingScript_;
    bool scriptOperation_ = false;
    bool recording_ = false;
    QFileSystemWatcher *scriptWatcher_ = nullptr;
#endif
    QStackedWidget *pages_ = nullptr;
    QLabel *banner_ = nullptr, *deviceStatus_ = nullptr, *engineStatus_ = nullptr, *taskStatus_ = nullptr;
    QLabel *teamStatus_ = nullptr;
    QPushButton *start_ = nullptr;
    QPlainTextEdit *logs_ = nullptr, *code_ = nullptr;
    QListWidget *taskList_ = nullptr, *triggerList_ = nullptr, *globalList_ = nullptr, *teamList_ = nullptr;
    QWidget *taskForm_ = nullptr, *triggerForm_ = nullptr, *globalForm_ = nullptr;
    QComboBox *backendChoice_ = nullptr, *deviceChoice_ = nullptr, *resolution_ = nullptr, *format_ = nullptr,
              *fps_ = nullptr, *protocol_ = nullptr, *member_ = nullptr;
    QComboBox *codeSource_ = nullptr, *codeView_ = nullptr;
    QStackedWidget *boxForms_ = nullptr;
    QStackedWidget *captureForms_ = nullptr;
    QComboBox *captureSource_ = nullptr, *localCaptureTarget_ = nullptr, *localCaptureFps_ = nullptr,
              *dxgiMonitor_ = nullptr, *dxgiCaptureFps_ = nullptr, *localInputTarget_ = nullptr;
    QLabel *dxgiRefresh_ = nullptr;
    void scanDesktopTargets();
    QHash<QString, QWidget *> settings_;
    Preview *preview_ = nullptr, *floatingPreview_ = nullptr;
    QWidget *floating_ = nullptr;
    QTimer previewTimer_, statusTimer_;
    uint64_t previewSequence_ = 0;
    QSize previewTarget_;
    void updatePreview();
    void buildUi();
    void buildClassicUi();
    QWidget *devicePage();
    QWidget *tasksPage(bool trigger);
    QWidget *charactersPage();
    QWidget *globalPage();
    void startEngine();
    void readEngine();
    Q_SLOT void handleEngineMessage(const QJsonObject &message);
    void setCatalog(const QJsonObject &catalog);
    void showTask(bool trigger);
    void showGlobal();
    void showTeam();
    void loadCode();
    void buildFields(QWidget *target, const QJsonArray &fields,
                     std::function<void(QString, QJsonValue)> changed);
    void flushFieldSaves();
    void send(QJsonObject cmd, std::function<void(QJsonObject)> callback = {},
              std::function<void(QString)> failed = {});
    void runAsync(std::function<QJsonObject()> work, std::function<void(QJsonObject)> done = {});
    void scanDevices();
    void selectCaptureDevice();
    void filterModes();
    void collectSettings();
    Config deviceConfiguration(bool requireSelection) const;
    void queueDeviceSave();
    void saveDeviceSettings();
    void connectDevices();
    void stopDevices();
    void toggleDevices();
    void prepareConnection(const Config &config);
    void continueStart();
    void failStartup(const QString &error);
    void updateStartControl();
    void pauseExecution();
    void stopTask();
    void pause();
    void startTask(QString id = {}, bool exitAfter = false, double limitSeconds = 0);
#ifdef OKWW_NATIVE_ONLY
    void editSchedules(const QString &task = {}, const QString &label = {}, QJsonObject existing = {});
#endif
    void updateStatus();
    void log(QString message);
    QJsonObject selectedTask(bool trigger) const;
    QJsonObject selectedTeam() const;

  protected:
    void closeEvent(QCloseEvent *e) override;
    bool nativeEvent(const QByteArray &, void *, qintptr *) override;

  public:
    Window(QString root, QString data, QString python,
           std::function<void(Backend &, const Config &)> connector = {},
           std::function<QJsonArray(const Config &)> captureScanner = {});
    ~Window() override;
    void smoke(const QString &report, bool integration);
#ifdef OKWW_NATIVE_ONLY
    bool scheduledStart(QString task, bool exitAfter, double limitSeconds);
#endif
  signals:
    void exitAccepted();
    void taskStartAccepted(const QString &task);
    void startupFailed(const QString &error);
};
} // namespace okww
