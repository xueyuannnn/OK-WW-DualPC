#include "runtime/scheduler.hpp"
#include "runtime/scripts.hpp"
#include "runtime/template_store.hpp"
#include "window.hpp"
#include <QtTest>
#include <QDesktopServices>
#include <QUrlQuery>
using namespace okww;
class MailRecorder final : public QObject {
    Q_OBJECT
  public:
    QUrl opened;
    int count = 0;
  public slots:
    void record(const QUrl &url) { opened = url; ++count; }
};
class ClassicUiTests final : public QObject {
    Q_OBJECT
    const QString root = OKWW_SOURCE_ROOT;
    std::unique_ptr<Window> create(QTemporaryDir &data,
                                   std::function<void(Backend &, const Config &)> connector = {}) {
        auto config = Config::defaults();
        config.set("kmbox.dry_run", true);
        config.save(data.path() + "/dualpc.json");
        auto window = std::make_unique<Window>(root, data.path(), QString{}, std::move(connector));
        window->show();
        return window;
    }
    static classic::ExpandCard *card(Window &window, const QString &id) {
        return dynamic_cast<classic::ExpandCard *>(window.findChild<QFrame *>("task-" + id));
    }
    static void navigate(Window &window, const QString &id) {
        for (auto nav : window.findChildren<QListWidget *>())
            for (int i = 0; i < nav->count(); ++i)
                if (nav->item(i)->data(Qt::UserRole) == id) {
                    nav->setCurrentRow(i);
                    return;
                }
        QFAIL("原版导航入口缺失");
    }
    runtime::Profile profile(const QTemporaryDir &data) {
        return runtime::Profile(data.path() + "/configs-dualpc", root + "/native/resources/catalog.json");
    }
  private slots:
    void boxMoveTestKeepsTheRunningConnectionAndShowsDryRunFeedback() {
        QTemporaryDir data;
        Backend *connected = nullptr;
        int connections = 0;
        auto window = create(data, [&](Backend &backend, const Config &config) {
            backend.start(config, true);
            connected = &backend;
            ++connections;
        });
        QTRY_VERIFY(card(*window, "AutoCombatTask"));
        auto test = window->findChild<QPushButton *>("boxTest");
        QVERIFY(test && !test->isEnabled());
        window->findChild<QPushButton *>("previewConnection")->click();
        QTRY_VERIFY(test->isEnabled());
        QVERIFY(connected);
        connected->arm(true);
        connected->nativeCommand({{"op", "key"}, {"code", 4}, {"down", true}});
        connected->nativeCommand({{"op", "button"}, {"name", "left"}, {"down", true}});
        const auto before = connected->snapshot()["box"].toObject();
        test->click();
        QVERIFY(!test->isEnabled());
        auto feedback = window->findChild<QLabel *>("boxTestStatus");
        QVERIFY(feedback);
        QTRY_VERIFY(feedback->text().contains("演练完成"));
        QVERIFY(feedback->text().contains("不向实物发送"));
        QTRY_VERIFY(test->isEnabled());
        const auto after = connected->snapshot();
        QVERIFY(after["running"].toBool());
        QVERIFY(after["armed"].toBool());
        QCOMPARE(after["box"].toObject()["keys"], before["keys"]);
        QCOMPARE(after["box"].toObject()["buttons"], before["buttons"]);
        QCOMPARE(connections, 1);
        window->findChild<QPushButton *>("stopThread")->click();
        QTRY_VERIFY(!connected->snapshot()["running"].toBool());
        QTRY_VERIFY(!test->isEnabled());
    }
    void previewFitsPhysicalPixelsAndSharesTheLargestVisibleFrame() {
        QTemporaryDir data;
        Backend *connected = nullptr;
        auto window = create(data, [&](Backend &backend, const Config &config) {
            backend.start(config, true);
            connected = &backend;
        });
        QTRY_VERIFY(card(*window, "AutoCombatTask"));
        auto embedded = dynamic_cast<Preview *>(window->findChild<QWidget *>("embeddedPreview"));
        QVERIFY(embedded);
        window->findChild<QPushButton *>("previewConnection")->click();
        QTRY_VERIFY(connected && connected->snapshot()["running"].toBool());
        QTRY_VERIFY(window->findChild<QPushButton *>("previewConnection")->isEnabled());
        auto toggle = window->findChild<QPushButton *>("previewToggle");
        toggle->click();
        QVERIFY(toggle->isChecked());
        QVERIFY(embedded->isVisible());
        const QSize source(1280, 720); // The mock backend's published frame.
        const auto expected = [&](QSize requested) {
            return source.width() > requested.width() || source.height() > requested.height()
                ? source.scaled(requested, Qt::KeepAspectRatio) : source;
        };
        QTRY_COMPARE(embedded->imageSize(), expected(embedded->pixelSize()));
        QCOMPARE(embedded->pixelSize(), QSize(int(std::ceil(embedded->width() * embedded->devicePixelRatioF())),
                                              int(std::ceil(embedded->height() * embedded->devicePixelRatioF()))));
        window->findChild<QPushButton *>("floatingPreviewToggle")->click();
        // The floating window is independent of the main window's QObject tree.
        Preview *floating = nullptr;
        for (auto top : QApplication::topLevelWidgets())
            if (auto view = dynamic_cast<Preview *>(top->findChild<QWidget *>("floatingPreview")))
                floating = view;
        QVERIFY(floating);
        toggle->click();
        floating->window()->resize(2000, 1200);
        QTRY_COMPARE(floating->imageSize(), source);
        floating->window()->resize(600, 360);
        QTRY_COMPARE(floating->imageSize(), expected(floating->pixelSize()));
        toggle->click();
        QTRY_COMPARE(floating->imageSize(), expected(floating->pixelSize().expandedTo(embedded->pixelSize())));
        QCOMPARE(floating->imageSize(), embedded->imageSize());
        floating->window()->close();
        QTRY_COMPARE(embedded->imageSize(), expected(embedded->pixelSize()));
        window->findChild<QPushButton *>("stopThread")->click();
        QTRY_VERIFY(!connected->snapshot()["running"].toBool());
    }
    void previewEnlargementIsSmoothAndKeepsTheCompleteAspectRatio() {
        Preview view;
        view.resize(400, 300);
        QImage source(8, 4, QImage::Format_RGB32);
        for (int y = 0; y < source.height(); ++y)
            for (int x = 0; x < source.width(); ++x)
                source.setPixelColor(x, y, x % 2 ? Qt::white : Qt::black);
        view.setImage(source);
        view.show();
        QTest::qWait(50);
        const auto shown = view.grab().toImage();
        QCOMPARE(shown.pixelColor(shown.width()/2, 5), QColor("#111827"));
        QCOMPARE(shown.pixelColor(shown.width()/2, shown.height()-6), QColor("#111827"));
        int interpolated = 0;
        for (int x = 0; x < shown.width(); ++x) {
            const auto gray = shown.pixelColor(x, shown.height()/2).red();
            interpolated += gray > 10 && gray < 245;
        }
        QVERIFY(interpolated > shown.width()/2);
        const auto artifacts = qEnvironmentVariable("OKWW_UI_ARTIFACTS");
        if (!artifacts.isEmpty()) shown.save(artifacts+"/preview-letterbox.png");
        QImage thinLines(view.pixelSize().width()*3, view.pixelSize().width()*3/2, QImage::Format_RGB32);
        thinLines.fill(Qt::black);
        {
            QPainter painter(&thinLines);
            painter.setPen(Qt::white);
            for (int x = 1; x < thinLines.width(); x += 3)
                painter.drawLine(x, 0, x, thinLines.height()-1);
        }
        view.setImage(thinLines);
        const auto reduced = view.grab().toImage();
        for (int x = 0; x < reduced.width(); ++x) {
            const auto gray = reduced.pixelColor(x, reduced.height()/2).red();
            QVERIFY(gray >= 80 && gray <= 90);
        }
        if (!artifacts.isEmpty()) reduced.save(artifacts+"/preview-thin-lines.png");
    }
    void deviceParametersAutoSaveDraftsAndRestoreEveryEditorWithoutConnecting() {
        QTemporaryDir data;
        int connections = 0;
        auto window = create(data, [&](Backend &, const Config &) { ++connections; });
        QTRY_VERIFY(card(*window, "AutoCombatTask"));
        const auto applied = readJson(data.path()+"/dualpc.json");
        QVERIFY(!window->findChild<QPushButton *>("saveDevices"));
        window->findChild<QCheckBox *>("manualCapture")->setChecked(true);
        const QJsonObject values{{"capture.device_name", "Saved Capture"}, {"capture.device_index", 3},
            {"capture.width", 2560}, {"capture.height", 1440}, {"capture.fps", 165.}, {"capture.fourcc", "NV12"},
            {"capture.stale_after", .35}, {"kmbox.host", "192.168."}, {"kmbox.port", 12345}, {"kmbox.uuid", "12"},
            {"kmbox.baudrate", 2000000}, {"kmbox.desktop_width", 2560}, {"kmbox.desktop_height", 1440},
            {"kmbox.home_counts", 16000}, {"kmbox.move_chunk", 256}, {"kmbox.home_wait", .08},
            {"kmbox.move_interval", .003}, {"kmbox.timeout", .45},
            {"kmbox.counts_per_pixel_x", 1.25}, {"kmbox.counts_per_pixel_y", 1.5}};
        for (auto it = values.begin(); it != values.end(); ++it) {
            auto widget = window->findChild<QWidget *>(it.key());
            QVERIFY2(widget, qPrintable(it.key()));
            if (auto input = qobject_cast<QLineEdit *>(widget)) input->setText(it.value().toString());
            else if (auto input = qobject_cast<QSpinBox *>(widget)) input->setValue(it.value().toInt());
            else if (auto input = qobject_cast<QDoubleSpinBox *>(widget)) input->setValue(it.value().toDouble());
        }
        window->findChild<QCheckBox *>("kmbox.dry_run")->setChecked(false);
        window->findChild<QComboBox *>("boxSerialPort")->setCurrentText("COM9");
        auto protocol = window->findChild<QComboBox *>("boxProtocol");
        protocol->setCurrentIndex(protocol->findData("makcu_serial"));
        protocol->setCurrentIndex(protocol->findData("kmbox_net"));
        auto inference = window->findChild<QComboBox *>("inferenceResolution");
        inference->setCurrentIndex(inference->findData(QSize(1280, 720)));
        window->findChild<QCheckBox *>("captureCrop")->setChecked(true);
        const std::array<int, 4> crop{50, 20, 1280, 720};
        for (int n = 0; n < 4; ++n)
            window->findChild<QSpinBox *>(QString("captureCrop-%1").arg(n))->setValue(crop[n]);
        QPointer<QLineEdit> host = window->findChild<QLineEdit *>("kmbox.host");
        host->setFocus();
        const auto draft = [&] {
            return QFile::exists(data.path()+"/native-device-settings.json")
                ? readJson(data.path()+"/native-device-settings.json") : QJsonObject{};
        };
        QTRY_VERIFY(draft()["manual_capture"].toBool());
        const Config saved{draft()["config"].toObject()};
        for (auto it = values.begin(); it != values.end(); ++it) QCOMPARE(saved.get(it.key()), it.value());
        QCOMPARE(saved.get("capture.crop").toArray(), QJsonArray({50, 20, 1280, 720}));
        QCOMPARE(saved.n("capture.output_width"), 1280);
        QCOMPARE(saved.s("kmbox.serial_port"), QString("COM9"));
        QVERIFY(!saved.b("kmbox.dry_run"));
        QVERIFY(host && host->hasFocus());
        QVERIFY(window->findChild<QLabel *>("deviceSaveStatus")->text().contains("草稿已自动保存"));
        QCOMPARE(readJson(data.path()+"/dualpc.json"), applied);
        QCOMPARE(connections, 0);
        // Exit flushes an edit made before the debounce timer fires.
        window->findChild<QSpinBox *>("kmbox.port")->setValue(12500);
        QSignalSpy exited(window.get(), &Window::exitAccepted);
        window->findChild<QAction *>("trayExit")->trigger();
        QTRY_COMPARE(exited.count(), 1);
        QCOMPARE(draft()["config"].toObject()["kmbox"].toObject()["port"].toInt(), 12500);
        window.reset();
        auto restored = std::make_unique<Window>(root, data.path(), QString{},
            [&](Backend &, const Config &) { ++connections; });
        restored->show();
        QTRY_VERIFY(card(*restored, "AutoCombatTask"));
        QVERIFY(restored->findChild<QCheckBox *>("manualCapture")->isChecked());
        QVERIFY(restored->findChild<QCheckBox *>("captureCrop")->isChecked());
        for (auto it = values.begin(); it != values.end(); ++it) {
            auto widget = restored->findChild<QWidget *>(it.key());
            QJsonValue actual;
            if (auto input = qobject_cast<QLineEdit *>(widget)) actual = input->text();
            else if (auto input = qobject_cast<QSpinBox *>(widget)) actual = input->value();
            else if (auto input = qobject_cast<QDoubleSpinBox *>(widget)) actual = input->value();
            QCOMPARE(actual, it.key() == "kmbox.port" ? QJsonValue(12500) : it.value());
        }
        QCOMPARE(restored->findChild<QComboBox *>("inferenceResolution")->currentData().toSize(), QSize(1280, 720));
        QCOMPARE(restored->findChild<QComboBox *>("boxSerialPort")->currentText(), QString("COM9"));
        QSignalSpy failed(restored.get(), &Window::startupFailed);
        for (auto button : restored->findChildren<QPushButton *>())
            if (button->property("automationControl").toBool()) button->click();
        QCOMPARE(failed.count(), 1);
        QCOMPARE(connections, 0);
        QCOMPARE(readJson(data.path()+"/dualpc.json"), applied);
        // Turning optional modes off must retain the user's values for next time.
        restored->findChild<QCheckBox *>("manualCapture")->setChecked(false);
        restored->findChild<QCheckBox *>("captureCrop")->setChecked(false);
        QSignalSpy closed(restored.get(), &Window::exitAccepted);
        restored->findChild<QAction *>("trayExit")->trigger();
        QTRY_COMPARE(closed.count(), 1);
        restored.reset();
        auto inactive = std::make_unique<Window>(root, data.path(), QString{},
            [&](Backend &, const Config &) { ++connections; });
        QVERIFY(!inactive->findChild<QCheckBox *>("manualCapture")->isChecked());
        QVERIFY(!inactive->findChild<QCheckBox *>("captureCrop")->isChecked());
        QCOMPARE(inactive->findChild<QSpinBox *>("capture.width")->value(), 2560);
        QCOMPARE(inactive->findChild<QSpinBox *>("capture.height")->value(), 1440);
        QCOMPARE(inactive->findChild<QDoubleSpinBox *>("capture.fps")->value(), 165.);
        for (int n = 0; n < 4; ++n)
            QCOMPARE(inactive->findChild<QSpinBox *>(QString("captureCrop-%1").arg(n))->value(), crop[n]);
        QCOMPARE(connections, 0);
    }
    void deviceAutoSaveKeepsTheRunningConnectionUntilExplicitApply() {
        QTemporaryDir data;
        Backend *connected = nullptr;
        int connections = 0;
        auto window = create(data, [&](Backend &backend, const Config &config) {
            backend.start(config, true);
            connected = &backend;
            ++connections;
        });
        QTRY_VERIFY(card(*window, "AutoCombatTask"));
        const auto original = Config::load(data.path()+"/dualpc.json");
        auto apply = window->findChild<QPushButton *>("previewConnection");
        apply->click();
        QTRY_COMPARE(connections, 1);
        QTRY_VERIFY(apply->isEnabled());
        QVERIFY(connected->configurationMatches(original));
        window->findChild<QDoubleSpinBox *>("kmbox.timeout")->setValue(.8);
        QTRY_VERIFY(QFile::exists(data.path()+"/native-device-settings.json") &&
            readJson(data.path()+"/native-device-settings.json")["config"].toObject()["kmbox"].toObject()["timeout"].toDouble() == .8);
        QCOMPARE(connections, 1);
        QCOMPARE(Config::load(data.path()+"/dualpc.json").d("kmbox.timeout"), original.d("kmbox.timeout"));
        QVERIFY(connected->configurationMatches(original));
        apply->click();
        QTRY_COMPARE(connections, 2);
        QTRY_VERIFY(apply->isEnabled());
        const auto updated = Config::load(data.path()+"/dualpc.json");
        QCOMPARE(updated.d("kmbox.timeout"), .8);
        QVERIFY(connected->configurationMatches(updated));
        window->findChild<QPushButton *>("stopThread")->click();
        QTRY_VERIFY(!connected->snapshot()["running"].toBool());
    }
    void deviceAutoSaveReportsWriteFailuresWithoutChangingAppliedConfiguration() {
        QTemporaryDir data;
        auto window = create(data);
        QTRY_VERIFY(card(*window, "AutoCombatTask"));
        const auto applied = readJson(data.path()+"/dualpc.json");
        QVERIFY(QDir().mkdir(data.path()+"/native-device-settings.json"));
        window->findChild<QDoubleSpinBox *>("kmbox.timeout")->setValue(.7);
        QTRY_VERIFY(window->findChild<QLabel *>("deviceSaveStatus")->text().contains("自动保存失败"));
        QCOMPARE(readJson(data.path()+"/dualpc.json"), applied);
    }
    void aboutPageShowsContactEmailAndCopiesItWithoutSendingMail() {
        QTemporaryDir data;
        auto window = create(data);
        QTRY_VERIFY(card(*window, "AutoCombatTask"));
        navigate(*window, "about");
        auto email = window->findChild<QLabel *>("authorEmail");
        QVERIFY(email && email->text() == "lidada0202@gmail.com");
        auto contact = window->findChild<QPushButton *>("contactAuthor");
        QVERIFY(contact);
        const QUrl url(contact->property("linkUrl").toString());
        QCOMPARE(url.scheme(), QString("mailto"));
        QCOMPARE(url.path(), email->text());
        auto copy = window->findChild<QPushButton *>("copyAuthorEmail");
        QVERIFY(copy);
        copy->click();
        QCOMPARE(QApplication::clipboard()->text(), email->text());
        QTest::qWait(100); // Let the newly shown scroll page finish its width-dependent layout.
        const auto artifacts = qEnvironmentVariable("OKWW_UI_ARTIFACTS");
        if (!artifacts.isEmpty()) window->grab().save(artifacts+"/about-contact.png");
    }
    void triggerFieldsAutoSaveWhileFocusedRejectPartialJsonAndFlushOnExit() {
        QTemporaryDir data;
        const auto created = runtime::scriptManage(root, data.path(), {{"op", "create"}, {"name", "AutoSave"}});
        const auto id = created["selected"].toString();
        const QString code = "from ok import TriggerTask\nclass AutoSave(TriggerTask):\n"
            "    def __init__(self, *args, **kwargs):\n        super().__init__(*args, **kwargs)\n"
            "        self.default_config = {'Count': 2, 'Rate': 0.5, 'Title': 'old', 'Payload': {'x': 1}}\n"
            "        self.config_type = {'Count': {'minimum': 1, 'maximum': 100}}\n"
            "    def run(self):\n        return False\n";
        runtime::scriptManage(root, data.path(), {{"op", "save"}, {"script", id}, {"code", code}});
        const auto saved = [&] {
            return QFile::exists(data.path()+"/native-scripts.json")
                ? readJson(data.path()+"/native-scripts.json")[id].toObject()["values"].toObject() : QJsonObject{};
        };
        auto window = create(data);
        QTRY_VERIFY_WITH_TIMEOUT(card(*window, id), 10000);
        navigate(*window, "triggers");
        auto trigger = card(*window, id);
        trigger->setExpanded(true);
        QPointer<QSpinBox> count = trigger->findChild<QSpinBox *>("field-Count");
        QVERIFY(count);
        count->setFocus();
        auto focused = qApp->focusWidget();
        count->setValue(7);
        QTRY_COMPARE(saved()["Count"].toInt(), 7);
        QVERIFY(count);
        QCOMPARE(qApp->focusWidget(), focused);
        count->setValue(2);
        QTRY_COMPARE(saved()["Count"].toInt(), 2);
        auto rate = trigger->findChild<QDoubleSpinBox *>("field-Rate");
        rate->setFocus();
        rate->setValue(.75);
        QTRY_COMPARE(saved()["Rate"].toDouble(), .75);
        QPointer<QLineEdit> title = trigger->findChild<QLineEdit *>("field-Title");
        title->setFocus();
        title->selectAll();
        QTest::keyClicks(title, "latest-title");
        QTRY_COMPARE(saved()["Title"].toString(), QString("latest-title"));
        QVERIFY(title && title->hasFocus());
        auto json = trigger->findChild<QWidget *>("field-Payload")->findChild<QPlainTextEdit *>();
        json->setFocus();
        json->setPlainText("{");
        QTest::qWait(450);
        QVERIFY(!saved().contains("Payload"));
        QVERIFY(trigger->findChild<QLabel *>("configSaveStatus")->text().contains("输入不完整"));
        json->setPlainText("{\"x\": 2}");
        QTRY_COMPARE(saved()["Payload"].toObject()["x"].toInt(), 2);
        QVERIFY(trigger->expanded());
        for (auto button : trigger->body()->findChildren<QPushButton *>()) QVERIFY(button->text() != "保存");
        QSignalSpy exited(window.get(), &Window::exitAccepted);
        count->setFocus();
        count->setValue(9);
        window->findChild<QAction *>("trayExit")->trigger();
        QTRY_COMPARE(exited.count(), 1);
        QCOMPARE(saved()["Count"].toInt(), 9);
        window.reset();
        auto reopened = create(data);
        QTRY_VERIFY_WITH_TIMEOUT(card(*reopened, id), 10000);
        auto restored = card(*reopened, id);
        QCOMPARE(restored->findChild<QSpinBox *>("field-Count")->value(), 9);
        QCOMPARE(restored->findChild<QLineEdit *>("field-Title")->text(), QString("latest-title"));
        QCOMPARE(saved()["Payload"].toObject()["x"].toInt(), 2);
    }
    void taskStatusShowsRecognizedPartyAndModeAndPreservesTheLastResultWhenPaused() {
        QTemporaryDir data;
        auto window = create(data);
        QTRY_VERIFY(card(*window, "DailyTask2"));
        navigate(*window, "tasks");
        QTRY_VERIFY(card(*window, "DailyTask2")->height() >= 76);
        auto send = [&](const QJsonObject &message) {
            QVERIFY(QMetaObject::invokeMethod(window.get(), "handleEngineMessage", Qt::DirectConnection,
                                             Q_ARG(QJsonObject, message)));
        };
        send({{"event", "status"}, {"data", QJsonObject{{"paused", false}, {"current", "DailyTask2"}}}});
        QJsonObject info{{"members", QJsonArray{
            QJsonObject{{"class", "Baizhi"}, {"current", true}}, QJsonObject{{"class", "Mortefi"}},
            QJsonObject{{"class", "Verina"}}}}, {"mode", "python"}, {"code_state", "ready"},
            {"bridge", QJsonObject{{"startup_ms", 550}, {"ping_ms", .4}, {"frame_requests", 20}, {"frame_copies", 12}}}};
        send({{"event", "combat_info"}, {"data", info}});
        QLabel *summary = nullptr;
        for (auto label : window->findChildren<QLabel *>("taskPartySummary"))
            if (label->isVisible()) summary = label;
        QVERIFY(summary);
        const auto text = summary->property("fullTeamText").toString();
        QVERIFY(text.contains("白芷（当前）") && text.contains("莫特斐") && text.contains("维里奈"));
        QVERIFY(text.contains("Python 工坊"));
        QVERIFY(summary->toolTip().contains("不包含采集卡"));
        const auto artifacts = qEnvironmentVariable("OKWW_UI_ARTIFACTS");
        if (!artifacts.isEmpty()) window->grab().save(artifacts + "/tasks-team.png");
        send({{"event", "status"}, {"data", QJsonObject{{"paused", true}}}});
        QVERIFY(summary->property("fullTeamText").toString().startsWith("上次识别"));
        info["mode"] = "builtin";
        send({{"event", "combat_info"}, {"data", info}});
        QVERIFY(summary->property("fullTeamText").toString().contains("内置 C++"));
    }
    void oneDetectionLoadsAllParametersAndSwitchesCardsWithoutRescanning() {
        QTemporaryDir data;
        auto config = Config::defaults();
        config.set("capture.device_path", "card-b");
        config.set("capture.device_name", "Capture B");
        config.set("capture.device_index", 1);
        config.set("capture.width", 1920);
        config.set("capture.height", 1080);
        config.set("capture.fourcc", "NV12");
        config.set("capture.fps", 240);
        config.set("kmbox.dry_run", true);
        config.save(data.path() + "/dualpc.json");
        std::atomic<int> scans{0};
        const QJsonArray detected{
            QJsonObject{{"name", "Capture A"}, {"path", "card-a"}, {"index", 0},
                {"modes", QJsonArray{Mode{1280, 720, 60, "YUY2"}.json(), Mode{1920, 1080, 30, "MJPG"}.json()}}},
            QJsonObject{{"name", "Capture B"}, {"path", "card-b"}, {"index", 1},
                {"modes", QJsonArray{Mode{1920, 1080, 120, "NV12"}.json(), Mode{1920, 1080, 240, "NV12"}.json(),
                                     Mode{3840, 2160, 60, "NV12"}.json()}}},
            QJsonObject{{"name", "Offline"}, {"path", "card-c"}, {"index", 2},
                {"modes", QJsonArray{}}, {"error", "设备占用"}}};
        Window window(root, data.path(), {}, {}, [&](const Config &) { ++scans; return detected; });
        window.show();
        auto refresh = window.findChild<QPushButton *>("refreshDevices");
        QCOMPARE(refresh->text(), QString("检测设备与参数"));
        refresh->click();
        auto device = window.findChild<QComboBox *>("captureDevice");
        auto resolution = window.findChild<QComboBox *>("captureResolution");
        auto format = window.findChild<QComboBox *>("captureFormat");
        auto fps = window.findChild<QComboBox *>("captureFps");
        QTRY_COMPARE(device->count(), 3);
        QCOMPARE(scans.load(), 1);
        QCOMPARE(device->currentText(), QString("Capture B"));
        QCOMPARE(fps->currentData().toDouble(), 240.);
        QCOMPARE(resolution->currentData().toList(), (QVariantList{1920, 1080}));
        device->setCurrentIndex(0);
        QCOMPARE(format->currentText(), QString("MJPG"));
        QCOMPARE(fps->currentData().toDouble(), 30.);
        resolution->setCurrentIndex(resolution->findData(QVariantList{1280, 720}));
        QCOMPARE(format->currentText(), QString("YUY2"));
        QCOMPARE(fps->currentData().toDouble(), 60.);
        device->setCurrentIndex(1);
        QVERIFY(fps->findData(240.) >= 0);
        fps->setCurrentIndex(fps->findData(240.));
        QTRY_VERIFY(QFile::exists(data.path()+"/native-device-settings.json") &&
            readJson(data.path()+"/native-device-settings.json")["config"].toObject()["capture"].toObject()["fps"].toDouble() == 240.);
        const Config selected{readJson(data.path()+"/native-device-settings.json")["config"].toObject()};
        QCOMPARE(selected.s("capture.device_path"), QString("card-b"));
        QCOMPARE(selected.s("capture.fourcc"), QString("NV12"));
        QCOMPARE(selected.n("capture.width"), 1920);
        QCOMPARE(selected.n("capture.height"), 1080);
        QCOMPARE(scans.load(), 1);
        const auto artifacts = qEnvironmentVariable("OKWW_UI_ARTIFACTS");
        if (!artifacts.isEmpty()) window.grab().save(artifacts + "/capture-detection.png");
        device->setCurrentIndex(2);
        QCOMPARE(resolution->count(), 0);
        QCOMPARE(format->count(), 0);
        QCOMPARE(fps->count(), 0);
        QVERIFY(window.findChild<QLabel *>("captureScanStatus")->text().contains("设备占用"));
        window.findChild<QCheckBox *>("manualCapture")->setChecked(true);
        QTRY_VERIFY(QFile::exists(data.path()+"/native-device-settings.json") &&
                    readJson(data.path()+"/native-device-settings.json")["manual_capture"].toBool());
        const Config saved{readJson(data.path()+"/native-device-settings.json")["config"].toObject()};
        QCOMPARE(saved.n("capture.width"), 1920);
        QCOMPARE(saved.d("capture.fps"), 240.);
        QVERIFY(saved.s("capture.device_path").isEmpty());
    }
    void unsupportedWorkshopOffersAddressedMailDraftAndCopiesFeedback() {
        QTemporaryDir data;
        QDir().mkpath(data.path() + "/workshop-library");
        const QJsonArray entries{
            QJsonObject{{"id", "known"}, {"team", "Baizhi, Mortefi, Verina"}, {"name", "已支持方案"},
                        {"author", "OriginalAuthor"}, {"version", "1.0"}, {"description", "测试"}},
            QJsonObject{{"id", "future"}, {"team", "Baizhi, FutureCharacter, Verina"}, {"name", "测试 & 方案"},
                        {"author", "WorkshopAuthor"}, {"version", "2.0"}, {"description", "新角色方案"}}};
        writeJson(data.path() + "/workshop-library/index.json", {{"entries", entries}});
        writeJson(data.path() + "/workshop-library/settings.json", {{"auto_update", false}, {"interval_hours", 24}});
        auto window = create(data);
        navigate(*window, "characters");
        MailRecorder recorder;
        QDesktopServices::setUrlHandler("mailto", &recorder, "record");
        const auto cleanup = qScopeGuard([] { QDesktopServices::unsetUrlHandler("mailto"); });
        bool verified = false;
        QTimer timer;
        timer.setInterval(20);
        connect(&timer, &QTimer::timeout, window.get(), [&] {
            auto dialog = window->findChild<QDialog *>("workshopDialog");
            if (!dialog) return;
            auto table = dialog->findChild<QTableWidget *>("workshopTable");
            if (table->rowCount() != 2) return;
            auto notice = dialog->findChild<QWidget *>("workshopCompatibility");
            table->selectRow(0);
            const bool hiddenForKnown = !notice->isVisible();
            table->selectRow(1);
            dialog->layout()->activate();
            auto preview = dialog->findChild<QScrollArea *>("workshopPreviewScroll");
            preview->widget()->layout()->activate();
            auto importButton = dialog->findChild<QPushButton *>("workshopImport");
            auto feedback = dialog->findChild<QPushButton *>("workshopFeedback");
            auto feedbackHint = dialog->findChild<QLabel *>("workshopCompatibilityHint");
            const bool shownForUnknown = notice->isVisible() && feedback->isVisible() && !importButton->isEnabled() &&
                feedbackHint->height() >= feedbackHint->heightForWidth(feedbackHint->width());
            const auto artifacts = qEnvironmentVariable("OKWW_UI_ARTIFACTS");
            if (!artifacts.isEmpty()) dialog->grab().save(artifacts + "/workshop-feedback.png");
            feedback->click();
            const QUrlQuery query(recorder.opened);
            const auto body = query.queryItemValue("body", QUrl::FullyDecoded);
            dialog->findChild<QPushButton *>("workshopCopyFeedback")->click();
            const auto copied = QApplication::clipboard()->text();
            verified = hiddenForKnown && shownForUnknown && recorder.count == 1 &&
                recorder.opened.scheme() == "mailto" && recorder.opened.path() == "lidada0202@gmail.com" &&
                query.queryItems().size() == 2 && body.contains("FutureCharacter") &&
                body.contains("测试 & 方案") && body.contains("WorkshopAuthor") && body.contains("2.0") &&
                copied.contains("lidada0202@gmail.com") && copied.contains(body);
            table->selectRow(0);
            verified &= !notice->isVisible();
            dialog->accept();
        });
        timer.start();
        QTimer::singleShot(5000, window.get(), [&] {
            if (auto dialog = window->findChild<QDialog *>("workshopDialog")) dialog->reject();
        });
        window->findChild<QPushButton *>("openWorkshop")->click();
        QVERIFY(verified);
    }
    void trayExitAcceptsHiddenAndMinimizedWindows() {
        for (const bool hidden : {true, false}) {
            QTemporaryDir data;
            auto window = create(data);
            QTRY_VERIFY(card(*window, "DailyTask2"));
            QSignalSpy exited(window.get(), &Window::exitAccepted);
            if (hidden) window->hide();
            else window->showMinimized();
            auto exit = window->findChild<QAction *>("trayExit");
            QVERIFY(exit);
            exit->trigger();
            QCOMPARE(exited.count(), 1);
            QVERIFY(!window->isVisible());
        }
    }
    void trayExitWaitsForPendingDevicesAndDoesNotStartTheTask() {
        QTemporaryDir data;
        std::atomic<bool> finish{false};
        std::atomic<int> connections{0};
        Backend *connectedBackend = nullptr;
        auto window = create(data, [&](Backend &backend, const Config &config) {
            connectedBackend = &backend;
            ++connections;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (!finish && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            backend.start(config, true);
        });
        QSignalSpy accepted(window.get(), &Window::taskStartAccepted);
        QSignalSpy exited(window.get(), &Window::exitAccepted);
        for (auto button : window->findChildren<QPushButton *>())
            if (button->property("automationControl").toBool()) button->click();
        QTRY_COMPARE(connections.load(), 1);
        window->hide();
        window->findChild<QAction *>("trayExit")->trigger();
        QCOMPARE(exited.count(), 0);
        finish = true;
        QTRY_COMPARE_WITH_TIMEOUT(exited.count(), 1, 5000);
        QCOMPARE(accepted.count(), 0);
        QVERIFY(connectedBackend && !connectedBackend->snapshot()["running"].toBool());
    }
    void hiddenTrayExitCanBeCancelledAndThenSaveTheScript() {
        QTemporaryDir data;
        const auto result = runtime::scriptManage(root, data.path(), {{"op", "create"}, {"name", "TrayExitScript"}});
        auto window = create(data);
        QTRY_VERIFY(card(*window, result["selected"].toString()));
        navigate(*window, "scripts");
        auto editor = window->findChild<QPlainTextEdit *>("scriptEditor");
        QTRY_VERIFY(editor->toPlainText().contains("class TrayExitScript"));
        editor->appendPlainText("# retained when quitting from tray");
        editor->document()->setModified(true);
        window->hide();
        QSignalSpy exited(window.get(), &Window::exitAccepted);
        auto exit = window->findChild<QAction *>("trayExit");
        const auto answer = [](QMessageBox::StandardButton choice) {
            QTimer::singleShot(0, [choice] {
                if (auto message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                    message->button(choice)->click();
            });
        };
        answer(QMessageBox::Cancel);
        exit->trigger();
        QCOMPARE(exited.count(), 0);
        QVERIFY(editor->document()->isModified());
        answer(QMessageBox::Save);
        exit->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(exited.count(), 1, 5000);
        QVERIFY(!editor->document()->isModified());
        QFile saved(data.path() + "/ok_tasks/TrayExitScript.py");
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QVERIFY(saved.readAll().contains("# retained when quitting from tray"));
    }
    void workshopPortraitAliasesResolveToTheirActualCharacterImages() {
        for (const auto &pair : QList<QPair<QString, QString>>{{"Buling", "Douling"}, {"Lynae", "Linnai"},
                 {"Sigrika", "Xigelika"}, {"Yangyang: Xuanling", "YangYangSp"}, {"Shorekeeper", "ShoreKeeper"}}) {
            const auto original = classic::characterPortrait(root, pair.first);
            QVERIFY2(!original.isNull(), qPrintable(pair.first));
            QCOMPARE(original.cacheKey(), classic::characterPortrait(root, pair.second).cacheKey());
        }
        for (const auto &name : QStringList{"Suoming", "Aalto", "Lingyang", "Lumi", "Yangyang"})
            QVERIFY2(!classic::characterPortrait(root, name).isNull(), qPrintable(name));
        QVERIFY(classic::characterPortrait(root, "UnknownCharacter").isNull());
    }
    void chineseActionControlsInsertTheOriginalExecutableCall() {
        QTemporaryDir data;
        const auto result = runtime::scriptManage(root, data.path(), {{"op", "create"}, {"name", "ChineseActions"}});
        auto window = create(data);
        QTRY_VERIFY_WITH_TIMEOUT(card(*window, result["selected"].toString()), 10000);
        navigate(*window, "scripts");
        auto snippets = window->findChild<QListWidget *>("scriptSnippets");
        QTRY_VERIFY_WITH_TIMEOUT(snippets->count() >= 81, 10000);
        QListWidgetItem *chosen = nullptr;
        for (int i = 0; i < snippets->count(); ++i) {
            QVERIFY(snippets->item(i)->text().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]")));
            if (snippets->item(i)->data(Qt::UserRole).toJsonObject()["name"] == "click_relative")
                chosen = snippets->item(i);
        }
        QVERIFY(chosen);
        bool shown = false;
        QTimer::singleShot(80, window.get(), [&] {
            auto dialog = window->findChild<QDialog *>("scriptActionDialog");
            if (!dialog)
                return;
            shown = dialog->windowTitle() == "按画面比例点击";
            auto x = dialog->findChild<QDoubleSpinBox *>("parameterNumber-x");
            auto y = dialog->findChild<QDoubleSpinBox *>("parameterNumber-y");
            auto moveBack = dialog->findChild<QComboBox *>("parameterChoice-move_back");
            auto key = dialog->findChild<QComboBox *>("parameterChoice-key");
            if (!x || !y || !moveBack || !key) { dialog->reject(); return; }
            x->setValue(.25);
            y->setValue(.75);
            moveBack->setCurrentIndex(moveBack->findData("True"));
            key->setCurrentIndex(key->findData("'right'"));
            const auto artifacts = qEnvironmentVariable("OKWW_UI_ARTIFACTS");
            if (!artifacts.isEmpty())
                dialog->grab().save(artifacts + "/script-action.png");
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        QTimer::singleShot(6000, window.get(), [&] {
            if (auto dialog = window->findChild<QDialog *>("scriptActionDialog")) dialog->reject();
        });
        QMetaObject::invokeMethod(snippets, "itemDoubleClicked", Qt::DirectConnection, Q_ARG(QListWidgetItem *, chosen));
        QVERIFY(shown);
        auto editor = window->findChild<QPlainTextEdit *>("scriptEditor");
        QTRY_VERIFY_WITH_TIMEOUT(editor->toPlainText().contains("self.click_relative(0.25, 0.75"), 10000);
        QVERIFY(editor->toPlainText().contains("move_back=True"));
        QVERIFY(editor->toPlainText().contains("key='right'"));
    }
    void sharedDialogButtonsAndPageThemesAreChineseAndKeepNavigation() {
        for (const auto &theme : {QString("light"), QString("dark")}) {
            QTemporaryDir data;
            writeJson(data.path() + "/native-ui.json", {{"theme", theme}});
            auto window = create(data);
            QTRY_VERIFY(card(*window, "DailyTask2"));
            QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel | QDialogButtonBox::Close);
            QCOMPARE(buttons.button(QDialogButtonBox::Save)->text().remove('&'), QString("保存"));
            QCOMPARE(buttons.button(QDialogButtonBox::Cancel)->text().remove('&'), QString("取消"));
            QCOMPARE(buttons.button(QDialogButtonBox::Close)->text().remove('&'), QString("关闭"));
            QCOMPARE(qApp->palette().color(QPalette::Highlight), QColor(theme == "light" ? "#3269e8" : "#8caeff"));
            const auto artifacts = qEnvironmentVariable("OKWW_UI_ARTIFACTS");
            for (const auto &id : {"dualpc", "capture", "tasks", "triggers", "characters", "scripts", "templates", "schedule"}) {
                navigate(*window, id);
                QTest::qWait(30);
                QVERIFY(window->findChild<QListWidget *>("navigation")->currentRow() >= 0);
                if (!artifacts.isEmpty())
                    window->grab().save(artifacts + '/' + theme + '-' + id + ".png");
            }
            window->resize(1000, 680);
            navigate(*window, "dualpc");
            QTest::qWait(30);
            QVERIFY(window->width() <= 1020);
            if (!artifacts.isEmpty())
                window->grab().save(artifacts + '/' + theme + "-compact.png");
        }
    }
    void workshopShowsCachedVersionsWithoutNetworkOrChangingCurrentTeams() {
        QTemporaryDir data;
        QDir().mkpath(data.path() + "/workshop-library");
        QJsonObject index{{"entries", QJsonArray{QJsonObject{{"id", "test"},
                                                      {"team", "Baizhi, Mortefi, Verina"},
                                                      {"name", "Baizhi_Mortefi_Verina"},
                                                      {"author", "Author"},
                                                      {"version", "1.0"},
                                                      {"description", "离线测试方案\n选中队伍后，可在右侧查看适用场景、角色要求和完整说明。"}}}}};
        const auto fixture = qEnvironmentVariable("OKWW_UI_WORKSHOP_FIXTURE");
        if (!fixture.isEmpty()) {
            index = readJson(fixture);
            QDir source(QFileInfo(fixture).absolutePath() + "/archives");
            QDir().mkpath(data.path() + "/workshop-library/archives");
            for (const auto &name : source.entryList({"*.zip"}, QDir::Files))
                QVERIFY(QFile::copy(source.filePath(name), data.path() + "/workshop-library/archives/" + name));
        }
        const auto expected = index["entries"].toArray();
        writeJson(data.path() + "/workshop-library/index.json", index);
        writeJson(data.path() + "/native-ui.json", {{"theme", "light"}});
        auto window = create(data);
        navigate(*window, "characters");
        bool shown = false;
        bool settingsSent = false;
        QTimer timer;
        timer.setInterval(20);
        connect(&timer, &QTimer::timeout, window.get(), [&] {
            auto dialog = window->findChild<QDialog *>("workshopDialog");
            if (!dialog)
                return;
            auto table = dialog->findChild<QTableWidget *>("workshopTable");
            if (table->rowCount() != expected.size())
                return;
            if (!settingsSent) {
                settingsSent = true;
                dialog->findChild<QSpinBox *>("workshopInterval")->setValue(6);
                dialog->findChild<QPushButton *>("workshopSave")->click();
                return;
            }
            const auto settings = data.path() + "/workshop-library/settings.json";
            if (!QFile::exists(settings) || readJson(settings)["interval_hours"].toInt() != 6)
                return;
            shown = table->item(0, 1)->text() == expected.first().toObject()["author"].toString() &&
                    table->item(0, 0)->text().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]")) &&
                    dialog->findChild<QLabel *>("workshopMeta")->text().contains("作者：" + expected.first().toObject()["author"].toString()) &&
                    dialog->findChild<QLineEdit *>("workshopSource")->text().contains("yamadafz/ok-ww-char-code");
            for (int slot = 0; slot < 3; ++slot) {
                auto portrait = dialog->findChild<QWidget *>("workshopPortrait-" + QString::number(slot));
                shown &= portrait && portrait->isVisible() && !portrait->accessibleName().isEmpty();
            }
            const auto artifacts = qEnvironmentVariable("OKWW_UI_ARTIFACTS");
            if (!artifacts.isEmpty()) {
                dialog->grab().save(artifacts + "/workshop.png");
                for (auto radio : window->findChildren<QRadioButton *>())
                    if (radio->text() == "深色") radio->click();
                dialog->grab().save(artifacts + "/workshop-dark.png");
            }
            dialog->accept();
        });
        timer.start();
        QTimer::singleShot(5000, window.get(), [&] {
            if (auto dialog = window->findChild<QDialog *>("workshopDialog"))
                dialog->reject();
        });
        window->findChild<QPushButton *>("openWorkshop")->click();
        QVERIFY(shown);
        QVERIFY(!QFile::exists(data.path() + "/configs-dualpc/custom_teams"));
    }
    void generalScriptEditorSavesLoopsAndKeepsUnsavedEditsAcrossCatalogUpdates() {
        QTemporaryDir data;
        const auto result =
            runtime::scriptManage(root, data.path(), {{"op", "create"}, {"name", "UiExample"}});
        const auto id = result["selected"].toString();
        auto window = create(data);
        QTRY_VERIFY_WITH_TIMEOUT(card(*window, id), 10000);
        navigate(*window, "scripts");
        auto editor = window->findChild<QPlainTextEdit *>("scriptEditor");
        auto repeat = window->findChild<QSpinBox *>("scriptRepeat");
        auto continuous = window->findChild<QCheckBox *>("scriptContinuous");
        QVERIFY(editor && repeat && continuous);
        QVERIFY(editor->toPlainText().contains("class UiExample"));
        editor->appendPlainText("# unsaved marker");
        editor->document()->setModified(true);
        repeat->setValue(3);
        QMetaObject::invokeMethod(repeat, "editingFinished");
        QTRY_VERIFY(QFile::exists(data.path() + "/native-scripts.json"));
        QTRY_COMPARE(readJson(data.path() + "/native-scripts.json")[id].toObject()["repeat"].toInt(), 3);
        QVERIFY(editor->toPlainText().contains("# unsaved marker"));
        QVERIFY(editor->document()->isModified());
        continuous->click();
        QVERIFY(!repeat->isEnabled());
        window->findChild<QPushButton *>("scriptSave")->click();
        QTRY_VERIFY_WITH_TIMEOUT(!editor->document()->isModified(), 10000);
        QFile file(data.path() + "/ok_tasks/UiExample.py");
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("# unsaved marker"));
        const auto artifacts = qEnvironmentVariable("OKWW_UI_ARTIFACTS");
        if (!artifacts.isEmpty())
            window->grab().save(artifacts + "/scripts.png");
    }
    void templateGalleryAndMarkupPersistAChangedRegion() {
        QTemporaryDir data;
        runtime::TemplateStore store(data.path() + "/ok_templates");
        QImage image(1280, 720, QImage::Format_RGB888);
        image.fill(QColor("#24465a"));
        const int id = store.add(image);
        store.saveAnnotations(
            id, QJsonArray{QJsonObject{{"name", "old_name"}, {"bbox", QJsonArray{100, 50, 40, 30}}}});
        auto window = create(data);
        navigate(*window, "templates");
        auto grid = window->findChild<QListWidget *>("templateImages");
        QCOMPARE(grid->count(), 1);
        grid->setCurrentRow(0);
        bool opened = false;
        QTimer::singleShot(10, window.get(), [&] {
            auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            auto regions = dialog->findChild<QTableWidget *>("templateRegions");
            if (!regions) {
                dialog->reject();
                return;
            }
            opened = true;
            regions->item(0, 0)->setText("renamed_button");
            qobject_cast<QSpinBox *>(regions->cellWidget(0, 1))->setValue(123);
            const auto artifacts = qEnvironmentVariable("OKWW_UI_ARTIFACTS");
            if (!artifacts.isEmpty())
                dialog->grab().save(artifacts + "/template-markup.png");
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        });
        window->findChild<QPushButton *>("templateMarkup")->click();
        QVERIFY(opened);
        QCOMPARE(store.annotations(id)[0].toObject()["name"].toString(), QString("renamed_button"));
        QCOMPARE(store.annotations(id)[0].toObject()["bbox"].toArray()[0].toInt(), 123);
        QVERIFY(!QFile::exists(data.path() + "/native-last-run.json"));
    }
    void initTestCase() {
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
        qApp->setFont(QFont("Microsoft YaHei UI", 10));
    }
    void dropdownOpensBelowAndPreservesSelectionUntilChosen() {
        QWidget host;
        host.setStyleSheet(classic::stylesheet(false));
        host.setGeometry(20, 20, 420, 420);
        classic::ComboBox combo(&host);
        combo.setGeometry(24, 24, 280, 36);
        for (int i = 0; i < 40; ++i)
            combo.addItem(QString("选项 %1").arg(i), i);
        combo.setCurrentIndex(35);
        host.show();
        QSignalSpy activated(&combo, &QComboBox::activated);
        combo.showPopup();
        auto popup = combo.view()->window();
        QTRY_VERIFY(popup->isVisible());
        QVERIFY(popup->y() >= combo.mapToGlobal(QPoint(0, combo.height())).y());
        QCOMPARE(combo.currentIndex(), 35);
        QVERIFY(popup->height() < 40 * 28);
        const auto artifacts = qEnvironmentVariable("OKWW_UI_ARTIFACTS");
        if (!artifacts.isEmpty()) {
            QDir().mkpath(artifacts);
            popup->grab().save(artifacts + "/dropdown.png");
        }
        const auto index = combo.model()->index(2, 0);
        combo.view()->scrollTo(index, QAbstractItemView::PositionAtCenter);
        QTest::qWait(10);
        QTest::mouseClick(combo.view()->viewport(), Qt::LeftButton, Qt::NoModifier,
                          combo.view()->visualRect(index).center());
        QTRY_COMPARE(combo.currentData().toInt(), 2);
        QCOMPARE(activated.count(), 1);
        combo.showPopup();
        QTest::keyClick(combo.view(), Qt::Key_Escape);
        QCOMPARE(combo.currentData().toInt(), 2);
        combo.setEditable(true);
        combo.setCurrentText("手动输入");
        combo.showPopup();
        QTest::keyClick(combo.view(), Qt::Key_Escape);
        QCOMPARE(combo.currentText(), QString("手动输入"));
    }
    void numericSliderAndPreciseInputShareTheSameValueAndIgnoreWheel() {
        QWidget host;
        host.setStyleSheet(classic::stylesheet(false));
        auto number = new QSpinBox;
        number->setObjectName("boundedNumber");
        number->setRange(0, 4);
        number->setValue(2);
        auto layout = new QVBoxLayout(&host);
        auto editor = classic::sliderEditor(number);
        layout->addWidget(editor);
        host.show();
        auto slider = editor->findChild<QSlider *>("boundedNumber-slider");
        QVERIFY(slider);
        QSignalSpy committed(number, &QAbstractSpinBox::editingFinished);
        slider->setValue(4);
        QTRY_COMPARE(number->value(), 4);
        QTRY_VERIFY(committed.count() > 0);
        number->setValue(1);
        QCOMPARE(slider->value(), 1);
        QWheelEvent wheel(QPointF(8, 8), QPointF(slider->mapToGlobal(QPoint(8, 8))), QPoint{}, QPoint(0, 120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        // The same application guard used by the main window covers sliders.
        NoWheel guard;
        qApp->installEventFilter(&guard);
        QApplication::sendEvent(slider, &wheel);
        qApp->removeEventFilter(&guard);
        QCOMPARE(number->value(), 1);
        QCOMPARE(slider->value(), 1);
        const auto artifacts = qEnvironmentVariable("OKWW_UI_ARTIFACTS");
        if (!artifacts.isEmpty())
            host.grab().save(artifacts + "/slider-input.png");
    }
    void numericButtonsHaveSeparateHitAreasAndStepBothWays() {
        for (bool dark : {false, true}) {
            QWidget host;
            host.setStyleSheet(classic::stylesheet(dark));
            auto layout = new QVBoxLayout(&host);
            QSpinBox integer;
            QDoubleSpinBox decimal;
            QTimeEdit time(QTime(12, 30));
            integer.setRange(0, 10);
            integer.setValue(5);
            decimal.setRange(0, 10);
            decimal.setValue(5);
            decimal.setSingleStep(.25);
            time.setDisplayFormat("HH:mm");
            time.setCurrentSection(QDateTimeEdit::MinuteSection);
            for (auto spin : QList<QAbstractSpinBox *>{&integer, &decimal, &time})
                layout->addWidget(spin);
            host.resize(260, 180);
            host.show();
            QTest::qWait(10);
            for (auto spin : QList<QAbstractSpinBox *>{&integer, &decimal, &time}) {
                QStyleOptionSpinBox option;
                option.initFrom(spin);
                option.rect = spin->rect();
                option.stepEnabled = QAbstractSpinBox::StepUpEnabled | QAbstractSpinBox::StepDownEnabled;
                const auto up =
                    spin->style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, spin);
                const auto down =
                    spin->style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxDown, spin);
                QVERIFY2(!up.isEmpty() && !down.isEmpty() && !up.intersects(down),
                         "Increase and decrease buttons overlap");
                QTest::mouseClick(spin, Qt::LeftButton, Qt::NoModifier, up.center());
                if (spin == &integer)
                    QCOMPARE(integer.value(), 6);
                else if (spin == &decimal)
                    QCOMPARE(decimal.value(), 5.25);
                else
                    QCOMPARE(time.time(), QTime(12, 31));
                QTest::mouseClick(spin, Qt::LeftButton, Qt::NoModifier, down.center());
                if (spin == &integer)
                    QCOMPARE(integer.value(), 5);
                else if (spin == &decimal)
                    QCOMPARE(decimal.value(), 5.);
                else
                    QCOMPARE(time.time(), QTime(12, 30));
            }
            // Avoid deleting stack widgets when the parent layout is destroyed.
            integer.setParent(nullptr);
            decimal.setParent(nullptr);
            time.setParent(nullptr);
        }
    }
    void scheduleDropdownsHandleMonthLengthsAndTimeAndIgnoreWheel() {
        QWidget host;
        host.setStyleSheet(classic::stylesheet(false));
        auto layout = new QVBoxLayout(&host);
        auto editor = new classic::DateTimeEditor(QDateTime(QDate(2035, 1, 31), QTime(9, 30, 47)));
        layout->addWidget(editor);
        host.resize(560, 160);
        host.show();
        auto year = editor->findChild<QComboBox *>("scheduleYear");
        auto month = editor->findChild<QComboBox *>("scheduleMonth");
        auto day = editor->findChild<QComboBox *>("scheduleDay");
        auto time = editor->findChild<QTimeEdit *>("scheduleTime");
        QVERIFY(year && month && day && time);
        QVERIFY(editor->findChildren<QCalendarWidget *>().isEmpty());
        QCOMPARE(editor->dateTime(), QDateTime(QDate(2035, 1, 31), QTime(9, 30)));
        int changed = 0;
        editor->onChanged = [&] { ++changed; };
        month->setCurrentIndex(month->findData(2));
        QCOMPARE(editor->date(), QDate(2035, 2, 28));
        QCOMPARE(day->count(), 28);
        year->setCurrentIndex(year->findData(2036));
        QCOMPARE(day->count(), 29);
        day->setCurrentIndex(day->findData(29));
        QCOMPARE(editor->date(), QDate(2036, 2, 29));
        year->setCurrentIndex(year->findData(2035));
        QCOMPARE(editor->date(), QDate(2035, 2, 28));
        editor->findChild<QPushButton *>("scheduleTomorrow")->click();
        QCOMPARE(editor->date(), QDate::currentDate().addDays(1));
        QCOMPARE(editor->dateTime().time(), QTime(9, 30));
        editor->findChild<QPushButton *>("scheduleToday")->click();
        QCOMPARE(editor->date(), QDate::currentDate());
        QVERIFY(changed >= 6);
        time->setFocus();
        time->setSelectedSection(QDateTimeEdit::HourSection);
        QTest::keyClicks(time, "18");
        time->setSelectedSection(QDateTimeEdit::MinuteSection);
        QTest::keyClicks(time, "45");
        QTest::keyClick(time, Qt::Key_Enter);
        QCOMPARE(editor->dateTime().time(), QTime(18, 45));
        NoWheel guard;
        qApp->installEventFilter(&guard);
        const auto before = editor->dateTime();
        for (auto field : QList<QWidget *>{year, month, day, time}) {
            QWheelEvent wheel(QPointF(8, 8), QPointF(field->mapToGlobal(QPoint(8, 8))), QPoint{},
                              QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(field, &wheel);
        }
        qApp->removeEventFilter(&guard);
        QCOMPARE(editor->dateTime(), before);
        const auto artifacts = qEnvironmentVariable("OKWW_UI_ARTIFACTS");
        if (!artifacts.isEmpty()) host.grab().save(artifacts + "/date-dropdowns.png");
    }
    void cancellingConnectionDisconnectsAndNeverStartsTheTaskLate() {
        QTemporaryDir data;
        std::atomic<int> connections{0};
        std::atomic<bool> release{false};
        auto window = create(data, [&](Backend &backend, const Config &config) {
            ++connections;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!release && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            backend.start(config, true);
        });
        QPushButton *start = nullptr;
        for (auto button : window->findChildren<QPushButton *>())
            if (button->property("automationControl").toBool())
                start = button;
        QVERIFY(start);
        QSignalSpy accepted(window.get(), &Window::taskStartAccepted);
        QTRY_VERIFY_WITH_TIMEOUT(card(*window, "AutoCombatTask"), 5000);
        QTest::mouseClick(start, Qt::LeftButton);
        QTRY_COMPARE(connections.load(), 1);
        QVERIFY(start->property("startupPending").toBool());
        QVERIFY(start->isEnabled());
        QVERIFY(!window->findChild<QPushButton *>("refreshDevices")->isEnabled());
        QVERIFY(!card(*window, "AutoCombatTask")->body()->isEnabled());
        for (auto button : card(*window, "AutoCombatTask")->findChildren<QAbstractButton *>())
            if (button->accessibleName().startsWith("启用"))
                QVERIFY(!button->isEnabled());
        QElapsedTimer cancellation;
        cancellation.start();
        QTest::mouseClick(start, Qt::LeftButton);
        QVERIFY(cancellation.elapsed() < 500);
        QVERIFY(!start->property("startupPending").toBool());
        release = true;
        QTRY_VERIFY_WITH_TIMEOUT(!start->property("deviceBusy").toBool(), 10000);
        QCOMPARE(accepted.count(), 0);
        QTest::mouseClick(start, Qt::LeftButton);
        QTRY_COMPARE(accepted.count(), 1);
        QCOMPARE(connections.load(), 2);
        QCOMPARE(window->findChild<QPlainTextEdit *>("logs")->toPlainText().count("C++ 执行器已就绪"), 1);
        QTest::mouseClick(start, Qt::LeftButton);
        QTRY_VERIFY(!start->property("deviceBusy").toBool());
        QVERIFY(!start->property("devicesRunning").toBool());
    }
    void devicesAreFirstAndStartIsInTheirFixedHeader() {
        QTemporaryDir data;
        auto window = create(data);
        QTRY_VERIFY(card(*window, "DailyTask2"));
        QList<QPushButton *> controls;
        for (auto button : window->findChildren<QPushButton *>())
            if (button->property("automationControl").toBool())
                controls.append(button);
        QCOMPARE(controls.size(), 1);
        auto nav = window->findChild<QListWidget *>("navigation");
        QCOMPARE(nav->item(0)->data(Qt::UserRole).toString(), QString("dualpc"));
        QCOMPARE(nav->item(1)->data(Qt::UserRole).toString(), QString("capture"));
        QCOMPARE(nav->currentRow(), 0);
        QVERIFY(controls[0]->isVisible());
        QVERIFY(controls[0]->isEnabled());
        auto editor = window->findChild<QWidget *>("deviceSettings");
        QVERIFY(editor);
        QVERIFY(!editor->isAncestorOf(controls[0]));
        QVERIFY(controls[0]->mapTo(window.get(), QPoint{}).y() < 140);
        QVERIFY(controls[0]->mapTo(window.get(), QPoint{}).x() > window->width() / 2);
        navigate(*window, "capture");
        QVERIFY(!controls[0]->isVisible());
        navigate(*window, "dualpc");
        auto debug = dynamic_cast<classic::ExpandCard *>(window->findChild<QFrame *>("deviceDiagnostics"));
        QVERIFY(debug);
        QVERIFY(!debug->body()->isVisible());
        auto preview = window->findChild<QPushButton *>("previewConnection");
        QVERIFY(preview);
        QCOMPARE(preview->text(), QString("仅连接预览"));
        QVERIFY(!preview->isVisible());
    }
    void taskPauseAndStopKeepDevicesUntilDevicePageStopsThem() {
        QTemporaryDir data;
        Backend *connectedBackend = nullptr;
        int connections = 0;
        auto window = create(data, [&](Backend &backend, const Config &config) {
            backend.start(config, true);
            connectedBackend = &backend;
            ++connections;
        });
        QTRY_VERIFY(card(*window, "DailyTask2"));
        auto daily = card(*window, "DailyTask2");
        QPushButton *run = nullptr, *pauseTask = nullptr, *stopTask = nullptr, *main = nullptr;
        for (auto button : daily->findChildren<QPushButton *>()) {
            if (button->text() == "开始")
                run = button;
            if (button->text() == "暂停")
                pauseTask = button;
            if (button->property("taskActionRole") == "stop")
                stopTask = button;
        }
        for (auto button : window->findChildren<QPushButton *>())
            if (button->property("automationControl").toBool())
                main = button;
        QVERIFY(run && pauseTask && stopTask && main);
        QSignalSpy accepted(window.get(), &Window::taskStartAccepted);
        run->click();
        QTRY_COMPARE(accepted.count(), 1);
        QVERIFY(connectedBackend);
        QVERIFY(connectedBackend->snapshot()["running"].toBool());
        QCOMPARE(main->text(), QString("停止"));
        pauseTask->click();
        QVERIFY(connectedBackend->snapshot()["running"].toBool());
        QVERIFY(!connectedBackend->snapshot()["armed"].toBool());
        run->click();
        QTRY_COMPARE(accepted.count(), 2);
        connectedBackend->arm(true);
        connectedBackend->nativeCommand({{"op", "key"}, {"code", 4}, {"down", true}});
        stopTask->click();
        const auto stopped = connectedBackend->snapshot();
        QVERIFY(stopped["running"].toBool());
        QVERIFY(stopped["capture_connected"].toBool());
        QVERIFY(stopped["box"].toObject()["connected"].toBool());
        QVERIFY(!stopped["armed"].toBool());
        QVERIFY(stopped["box"].toObject()["keys"].toArray().isEmpty());
        QTRY_VERIFY(connectedBackend->latestFrame());
        const auto sequence = connectedBackend->latestFrame()->sequence;
        QTRY_VERIFY(connectedBackend->latestFrame()->sequence > sequence);
        QVERIFY(window->findChild<QPushButton *>("boxTest")->isEnabled());
        QCOMPARE(main->text(), QString("停止"));
        run->click();
        QTRY_COMPARE(accepted.count(), 3);
        QCOMPARE(connections, 1);
        navigate(*window, "dualpc");
        QVERIFY(main->isEnabled());
        QCOMPARE(main->text(), QString("停止"));
        QTest::mouseClick(main, Qt::LeftButton);
        QTRY_VERIFY(!main->property("deviceBusy").toBool());
        QVERIFY(!main->property("devicesRunning").toBool());
        const auto state = connectedBackend->snapshot();
        QVERIFY(!state["running"].toBool());
        QVERIFY(!state["capture_connected"].toBool());
        QVERIFY(!state["box"].toObject()["connected"].toBool());
        QVERIFY(!state["armed"].toBool());
        QCOMPARE(main->text(), QString("开始(F9)"));
    }
    void taskCancellationDuringConnectionKeepsTheConnectionWithoutStartingLate() {
        QTemporaryDir data;
        std::atomic<int> connections{0};
        std::atomic<bool> release{false};
        Backend *connected = nullptr;
        auto window = create(data, [&](Backend &backend, const Config &config) {
            ++connections;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!release && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            backend.start(config, true);
            connected = &backend;
        });
        QTRY_VERIFY(card(*window, "DailyTask2"));
        auto daily = card(*window, "DailyTask2");
        QPushButton *run = nullptr;
        for (auto button : daily->findChildren<QPushButton *>())
            if (button->property("taskActionRole") == "run") run = button;
        QPushButton *main = nullptr;
        for (auto button : window->findChildren<QPushButton *>())
            if (button->property("automationControl").toBool()) main = button;
        QVERIFY(run && main);
        QSignalSpy accepted(window.get(), &Window::taskStartAccepted);
        run->click();
        QTRY_COMPARE(connections.load(), 1);
        QCOMPARE(run->text(), QString("取消启动"));
        QElapsedTimer cancellation;
        cancellation.start();
        run->click();
        QVERIFY(cancellation.elapsed() < 500);
        QVERIFY(!main->property("startupPending").toBool());
        release = true;
        QTRY_VERIFY(!main->property("deviceBusy").toBool());
        QVERIFY(connected);
        QVERIFY(connected->snapshot()["running"].toBool());
        QVERIFY(connected->snapshot()["capture_connected"].toBool());
        QVERIFY(connected->snapshot()["box"].toObject()["connected"].toBool());
        QVERIFY(!connected->snapshot()["armed"].toBool());
        QTest::qWait(250);
        QCOMPARE(accepted.count(), 0);
        run->click();
        QTRY_COMPARE(accepted.count(), 1);
        QCOMPARE(connections.load(), 1);
        main->click();
        QTRY_VERIFY(!main->property("deviceBusy").toBool());
        QVERIFY(!connected->snapshot()["running"].toBool());
    }
    void failedConnectionDoesNotAcceptOrSilentlyRetryTheTask() {
        QTemporaryDir data;
        std::atomic<int> connections{0};
        auto window = create(data, [&](Backend &, const Config &) {
            ++connections;
            throw std::runtime_error("offline connection failure");
        });
        QPushButton *start = nullptr;
        for (auto button : window->findChildren<QPushButton *>())
            if (button->property("automationControl").toBool())
                start = button;
        QVERIFY(start);
        QSignalSpy accepted(window.get(), &Window::taskStartAccepted);
        QSignalSpy failed(window.get(), &Window::startupFailed);
        start->click();
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(!start->property("startupPending").toBool());
        QCOMPARE(accepted.count(), 0);
        QCOMPARE(connections.load(), 1);
    }
    void scheduledRequestUsesTheCancellableStartupPipelineBeforeEngineReady() {
        QTemporaryDir data;
        int connections = 0;
        auto window = create(data, [&](Backend &backend, const Config &config) {
            ++connections;
            backend.start(config, true);
        });
        QSignalSpy accepted(window.get(), &Window::taskStartAccepted);
        QVERIFY(!window->scheduledStart("AutoCombatTask", false, 0));
        QVERIFY(!window->scheduledStart("missing-task", false, 0));
        QVERIFY(window->scheduledStart("DailyTask2", false, 3600));
        QVERIFY(!window->scheduledStart("DailyTask", false, 0));
        QTRY_COMPARE_WITH_TIMEOUT(accepted.count(), 1, 10000);
        QCOMPARE(accepted[0][0].toString(), QString("DailyTask2"));
        QCOMPARE(connections, 1);
        for (auto button : window->findChildren<QPushButton *>())
            if (button->property("automationControl").toBool())
                button->click();
    }
    void scheduleCreateAndModifyUseOneFormAndRetainOriginalValues() {
        QTemporaryDir data;
        runtime::Scheduler fixture(QCoreApplication::applicationFilePath(), root, data.path(), "S-1-5-21-123",
                                   [](const QStringList &) { return QByteArray{}; });
        QJsonObject spec{{"task", "DailyTask2"}, {"label", "每日任务"}, {"start", "2035-01-02T09:30:00"},
                         {"kind", "Hours"},      {"interval", 500},     {"limit_hours", 3},
                         {"exit_after", false}};
        const auto name = fixture.add(spec);
        fixture.setEnabled(name, false);
        auto window = create(data);
        QTRY_VERIFY(card(*window, "DailyTask2"));
        navigate(*window, "schedule");
        auto modify = window->findChild<QPushButton *>("scheduleModify");
        QVERIFY(modify);
        bool retained = false;
        QTimer::singleShot(60, [&] {
            if (auto dialog = window->findChild<QDialog *>("scheduleEditor")) {
                retained = dialog->findChild<QComboBox *>("scheduleTask")->currentData() == "DailyTask2" &&
                           !dialog->findChild<QComboBox *>("scheduleTask")->isEnabled() &&
                           dialog->findChild<QComboBox *>("scheduleKind")->currentData() == "Hours" &&
                           dialog->findChild<QSpinBox *>("scheduleInterval")->value() == 500 &&
                           dialog->findChild<QSpinBox *>("scheduleLimit")->value() == 3 &&
                           !dialog->findChild<QCheckBox *>("scheduleExit")->isChecked();
                const auto artifacts = qEnvironmentVariable("OKWW_UI_ARTIFACTS");
                if (!artifacts.isEmpty())
                    dialog->grab().save(artifacts + "/schedule-modify.png");
                dialog->reject();
            }
        });
        modify->click();
        QVERIFY(retained);
        bool singleForm = false;
        QTimer::singleShot(60, [&] {
            if (auto dialog = window->findChild<QDialog *>("scheduleEditor")) {
                singleForm = dialog->findChild<QComboBox *>("scheduleTask")->count() > 0 &&
                             dialog->findChild<QComboBox *>("scheduleTask")->isEnabled() &&
                             dialog->findChild<QPushButton *>("scheduleSave")->text() == "创建" &&
                             dialog->findChildren<QListWidget *>().isEmpty();
                auto time = dialog->findChild<QTimeEdit *>("scheduleTime");
                time->setFocus();
                time->setSelectedSection(QDateTimeEdit::MinuteSection);
                QTest::keyClicks(time, "45");
                QTest::keyClick(time, Qt::Key_Enter);
                singleForm &= dialog->isVisible();
                auto start = dynamic_cast<classic::DateTimeEditor *>(dialog->findChild<QWidget *>("scheduleStart"));
                auto section = start->parentWidget();
                singleForm &= section->rect().contains(start->geometry()) && start->height() >= start->minimumSizeHint().height();
                singleForm &= dialog->findChildren<QCalendarWidget *>().isEmpty();
                const auto artifacts = qEnvironmentVariable("OKWW_UI_ARTIFACTS");
                if (!artifacts.isEmpty())
                    dialog->grab().save(artifacts + "/schedule-create.png");
                dialog->reject();
            }
        });
        window->findChild<QPushButton *>("scheduleCreate")->click();
        QVERIFY(singleForm);
        QCOMPARE(fixture.list().size(), 1);
        QVERIFY(!fixture.list()[0].toObject()["enabled"].toBool());
    }
    void taskSearchKeepsTheExpandedCardAndItsSavedConfiguration() {
        QTemporaryDir data;
        auto window = create(data);
        QTRY_VERIFY(card(*window, "DailyTask2"));
        navigate(*window, "tasks");
        auto daily = card(*window, "DailyTask2");
        auto nest = card(*window, "NightmareNestTask");
        auto search = window->findChild<QLineEdit *>("taskSearch");
        auto empty = window->findChild<QLabel *>("tasksEmpty");
        QVERIFY(daily && nest && search && empty);
        daily->setExpanded(true);
        auto number = daily->findChild<QSpinBox *>("field-Daily Stamina Cube Limit");
        number->setValue(3);
        QMetaObject::invokeMethod(number, "editingFinished", Qt::DirectConnection);
        QTRY_COMPARE(profile(data).value("DailyTask2", "Daily Stamina Cube Limit").toInt(), 3);
        search->setText("噩梦巢穴任务");
        QVERIFY(daily->isHidden());
        QVERIFY(!nest->isHidden());
        QVERIFY(daily->expanded());
        QVERIFY(empty->isHidden());
        search->setText("进入BOSS");
        QVERIFY(!card(*window, "FarmEchoTask2")->isHidden());
        search->setText("there-is-no-such-task");
        QVERIFY(!empty->isHidden());
        search->clear();
        QCOMPARE(card(*window, "DailyTask2"), daily);
        QVERIFY(!daily->isHidden() && daily->expanded());
        QCOMPARE(daily->findChild<QSpinBox *>("field-Daily Stamina Cube Limit")->value(), 3);
        QVERIFY(empty->isHidden());
    }
    void taskCardStartsItsOwnTaskAfterConnectingAndSlidersPersistBothWays() {
        QTemporaryDir data;
        std::atomic<int> connections{0};
        std::atomic<bool> connected{false};
        auto window = create(data, [&](Backend &backend, const Config &config) {
            ++connections;
            backend.start(config, true);
            connected = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(card(*window, "DailyTask2"), 5000);
        navigate(*window, "tasks");
        auto daily = card(*window, "DailyTask2");
        QFrame *statusPanel = nullptr;
        for (auto panel : window->findChildren<QFrame *>("taskRunPanel"))
            if (panel->isVisible()) statusPanel = panel;
        QVERIFY(statusPanel);
        QCOMPARE(statusPanel->height(), 108);
        QVERIFY(statusPanel->findChild<QLabel *>("taskInfoTitle")->text().contains("待机"));
        daily->setExpanded(true);
        auto slider = daily->findChild<QSlider *>("field-Daily Stamina Cube Limit-slider");
        QVERIFY(slider);
        slider->setValue(2);
        QTRY_COMPARE(profile(data).value("DailyTask2", "Daily Stamina Cube Limit").toInt(), 2);
        QTest::qWait(10);
        auto number = daily->findChild<QSpinBox *>("field-Daily Stamina Cube Limit");
        QVERIFY(number);
        number->setValue(3);
        QMetaObject::invokeMethod(number, "editingFinished", Qt::DirectConnection);
        QTRY_COMPARE(profile(data).value("DailyTask2", "Daily Stamina Cube Limit").toInt(), 3);
        QTest::qWait(10);
        QCOMPARE(daily->findChild<QSlider *>("field-Daily Stamina Cube Limit-slider")->value(), 3);
        QSignalSpy accepted(window.get(), &Window::taskStartAccepted);
        QPushButton *run = nullptr, *homeStart = nullptr;
        for (auto button : daily->findChildren<QPushButton *>())
            if (button->text() == "开始")
                run = button;
        for (auto button : window->findChildren<QPushButton *>())
            if (button->property("automationControl").toBool())
                homeStart = button;
        QVERIFY(run);
        QVERIFY(homeStart);
        QCOMPARE(run->size(), QSize(76, 32));
        const auto artifacts = qEnvironmentVariable("OKWW_UI_ARTIFACTS");
        if (!artifacts.isEmpty()) window->grab().save(artifacts + "/tasks-expanded.png");
        const auto taskPosition = daily->mapTo(window.get(), QPoint{});
        run->click();
        QCOMPARE(run->text(), QString("取消启动"));
        QVERIFY(run->width() >= run->fontMetrics().horizontalAdvance(run->text()) + run->iconSize().width() + 16);
        QTRY_COMPARE_WITH_TIMEOUT(accepted.count(), 1, 10000);
        QCOMPARE(accepted[0][0].toString(), QString("DailyTask2"));
        QVERIFY(connected.load());
        QCOMPARE(connections.load(), 1);
        QTRY_VERIFY(statusPanel->findChild<QLabel *>("taskInfoTitle")->text().contains("正在运行"));
        QCOMPARE(daily->mapTo(window.get(), QPoint{}), taskPosition);
        QCOMPARE(statusPanel->height(), 108);
        daily->setExpanded(false);
        QTest::qWait(40);
        if (!artifacts.isEmpty()) window->grab().save(artifacts + "/tasks-running.png");
        homeStart->click();
        QTRY_VERIFY(statusPanel->findChild<QLabel *>("taskInfoTitle")->text().contains("已停止"));
        QVERIFY(statusPanel->isVisible());
        QCOMPARE(daily->mapTo(window.get(), QPoint{}), taskPosition);
        QCOMPARE(statusPanel->height(), 108);
    }
    void boxConfigurationOnlyOffersMakcuAndKmbox() {
        QTemporaryDir data;
        auto window = create(data);
        auto protocols = window->findChild<QComboBox *>("boxProtocol");
        auto forms = window->findChild<QStackedWidget *>("boxConnectionForms");
        QVERIFY(protocols);
        QVERIFY(forms);
        QCOMPARE(protocols->count(), 2);
        QCOMPARE(forms->count(), 2);
        QCOMPARE(protocols->itemData(0).toString(), QString("kmbox_net"));
        QCOMPARE(protocols->itemData(1).toString(), QString("makcu_serial"));
        for (int i = 0; i < 2; ++i) {
            protocols->setCurrentIndex(i);
            QCOMPARE(forms->currentIndex(), i);
        }
        QVERIFY(!window->findChild<QSpinBox *>("kmbox.hid_vid"));
    }
    void completePortraitAssetsCoverEveryBuiltInCharacter() {
        const auto manifest = readJson(root + "/native/resources/portraits/manifest.json");
        QSet<QString> classes;
        for (auto value : manifest["portraits"].toArray()) {
            const auto entry = value.toObject();
            const auto id = entry["class"].toString();
            QCOMPARE(entry["file"].toString(), id + ".png");
            QCOMPARE(entry["crop"].toString(), QString("none"));
            QFile file(root + "/native/resources/portraits/" + entry["file"].toString());
            QVERIFY(file.open(QIODevice::ReadOnly));
            const auto bytes = file.readAll();
            QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())
                         .toUpper(),
                     entry["sha256"].toString());
            const auto image = QImage::fromData(bytes);
            QVERIFY2(!image.isNull() && image.width() >= 80 && image.height() >= 80, qPrintable(id));
            classes.insert(id);
        }
        const auto characters = readJson(root + "/native/resources/characters.json")["characters"].toArray();
        QCOMPARE(classes.size(), characters.size());
        for (auto value : characters)
            QVERIFY(classes.contains(value.toObject()["class"].toString()));
    }
    void portraitKeepsAllFourCornersForWideAndTallImagesAtDifferentDpi() {
        for (const QSize sourceSize : {QSize(200, 100), QSize(100, 200)})
            for (int dpi : {1, 2}) {
                QPixmap source(sourceSize);
                source.fill(Qt::white);
                QPainter markers(&source);
                markers.fillRect(0, 0, 20, 20, Qt::red);
                markers.fillRect(source.width() - 20, 0, 20, 20, Qt::green);
                markers.fillRect(0, source.height() - 20, 20, 20, Qt::blue);
                markers.fillRect(source.width() - 20, source.height() - 20, 20, 20, Qt::yellow);
                markers.end();
                classic::AspectImageLabel label;
                label.setFixedSize(52, 52);
                label.setContentsMargins(2, 2, 2, 2);
                label.setPixmap(source);
                QImage output(52 * dpi, 52 * dpi, QImage::Format_ARGB32_Premultiplied);
                output.setDevicePixelRatio(dpi);
                output.fill(Qt::transparent);
                label.render(&output);
                QSizeF size(sourceSize);
                size.scale(QSizeF(48, 48), Qt::KeepAspectRatio);
                const QRectF target((52 - size.width()) / 2, (52 - size.height()) / 2, size.width(),
                                    size.height());
                const auto sample = [&](qreal x, qreal y) {
                    return output.pixelColor(qRound(x * dpi), qRound(y * dpi));
                };
                QCOMPARE(sample(target.left() + 2, target.top() + 2), QColor(Qt::red));
                QCOMPARE(sample(target.right() - 2, target.top() + 2), QColor(Qt::green));
                QCOMPARE(sample(target.left() + 2, target.bottom() - 2), QColor(Qt::blue));
                QCOMPARE(sample(target.right() - 2, target.bottom() - 2), QColor(Qt::yellow));
            }
    }
    void originalNavigationAndInlineCardsAreRestored() {
        QTemporaryDir data;
        auto window = create(data);
        QTRY_VERIFY_WITH_TIMEOUT(card(*window, "AutoCombatTask"), 5000);
        auto nav = window->findChild<QListWidget *>("navigation");
        QCOMPARE(nav->item(0)->text(), QString("双机设置"));
        QCOMPARE(nav->item(1)->text(), QString("截图"));
        QCOMPARE(nav->item(2)->text(), QString("实时触发"));
        QCOMPARE(nav->item(3)->text(), QString("任务"));
        auto bottom = window->findChild<QListWidget *>("bottomNavigation");
        QVERIFY(bottom);
        QCOMPARE(bottom->count(), 3);
        QCOMPARE(bottom->item(0)->text(), QString("通知"));
        QCOMPARE(bottom->item(1)->text(), QString("设置"));
        QCOMPARE(bottom->item(2)->text(), QString("关于"));
        navigate(*window, "tasks");
        auto daily = card(*window, "DailyTask2");
        QVERIFY(daily);
        QVERIFY(!daily->expanded());
        QVERIFY(daily->property("originalTitle").toString().startsWith("⭐"));
        for (auto frame : window->findChildren<QFrame *>())
            if (frame->objectName().startsWith("task-") &&
                !frame->property("originalTitle").toString().front().isLetterOrNumber()) {
                auto symbol = frame->findChild<QLabel *>("taskSymbol");
                QVERIFY2(symbol && !symbol->pixmap().isNull(), qPrintable(frame->objectName()));
            }
        daily->setExpanded(true);
        QVERIFY(daily->body()->isVisible());
        QVERIFY(daily->body()->findChild<QSpinBox *>("field-Tacet Suppression Count"));
        int startButtons = 0;
        for (auto button : daily->findChildren<QPushButton *>())
            if (button->text() == "开始")
                ++startButtons;
        QCOMPARE(startButtons, 1);
        navigate(*window, "triggers");
        QVERIFY(card(*window, "AutoCombatTask")->isVisible());
        navigate(*window, "Game Hotkey");
        QVERIFY(window->findChild<QWidget *>("Game Hotkey")->isVisible());
    }
    void characterPageRestoresPortraitAndKeepsEditingBehindTheNativeStore() {
        QTemporaryDir data;
        writeJson(data.path() + "/configs-dualpc/custom_teams/Aemeath__Linnai__Mornye/team.json",
                  {{"name", "test"},
                   {"team", QJsonArray{"Aemeath", "Linnai", "Mornye"}},
                   {"code_mode", "builtin"}});
        auto window = create(data);
        QTRY_VERIFY_WITH_TIMEOUT(card(*window, "AutoCombatTask"), 5000);
        navigate(*window, "characters");
        auto image = window->findChild<QLabel *>("characterPortrait");
        QVERIFY(image);
        QVERIFY(!image->pixmap().isNull());
        QVERIFY(window->findChild<QPlainTextEdit *>("codeEditor"));
        auto teams = window->findChild<QListWidget *>("teamList");
        QCOMPARE(teams->count(), 1);
        QVERIFY(teams->item(0)->text().contains("爱弥斯"));
        QVERIFY(!QFile::exists(data.path() + "/native-last-run.json"));
    }
    void changingOneCardPersistsWithoutCollapsingOrLosingTriggerState() {
        QTemporaryDir data;
        auto window = create(data);
        QTRY_VERIFY_WITH_TIMEOUT(card(*window, "AutoCombatTask"), 5000);
        navigate(*window, "triggers");
        auto combat = card(*window, "AutoCombatTask");
        combat->setExpanded(true);
        auto toggle = combat->body()->findChild<QAbstractButton *>("field-Use Liberation");
        QVERIFY(toggle);
        const bool initial = profile(data).value("AutoCombatTask", "Use Liberation").toBool();
        toggle->click();
        QCOMPARE(profile(data).value("AutoCombatTask", "Use Liberation").toBool(), !initial);
        QCOMPARE(card(*window, "AutoCombatTask"), combat);
        QVERIFY(combat->expanded());
        QAbstractButton *enable = nullptr;
        for (auto button : combat->findChildren<QAbstractButton *>())
            if (button->accessibleName().startsWith("启用"))
                enable = button;
        QVERIFY(enable);
        const bool enabled = enable->isChecked();
        enable->click();
        QCOMPARE(profile(data).value("AutoCombatTask", "_enabled").toBool(), !enabled);
        navigate(*window, "Task Recovery");
        auto spin = window->findChild<QSpinBox *>("field-Recovery Retry Limit");
        QVERIFY(spin);
        spin->setValue(4);
        QMetaObject::invokeMethod(spin, "editingFinished", Qt::DirectConnection);
        QCOMPARE(profile(data).value("Task Recovery", "Recovery Retry Limit").toInt(), 4);
        QCOMPARE(profile(data).value("AutoCombatTask", "_enabled").toBool(), !enabled);
        QVERIFY(combat->expanded());
    }
    void characterSelectorUsesTeamMembersAndShowsTheirActualBuiltinCode() {
        QTemporaryDir data;
        writeJson(data.path() + "/configs-dualpc/custom_teams/Cartethyia__Chisa__Ciaccona/team.json",
                  {{"team", QJsonArray{"Cartethyia", "Chisa", "Ciaccona"}}, {"code_mode", "builtin"}});
        auto window = create(data);
        QTRY_VERIFY_WITH_TIMEOUT(card(*window, "AutoCombatTask"), 5000);
        navigate(*window, "characters");
        auto member = window->findChild<QComboBox *>("characterMember");
        auto source = window->findChild<QComboBox *>("characterSource");
        auto code = window->findChild<QPlainTextEdit *>("codeEditor");
        auto portrait = window->findChild<QLabel *>("characterPortrait");
        QVERIFY(member && source && code && portrait);
        QCOMPARE(member->count(), 3);
        for (const auto &id : QStringList{"Cartethyia", "Chisa", "Ciaccona"}) {
            const auto index = member->findData(id);
            QVERIFY(index >= 0);
            member->setCurrentIndex(index);
            QTRY_VERIFY(code->toPlainText().contains("class " + id + " final"));
            QVERIFY(code->isReadOnly());
            QCOMPARE(portrait->toolTip(), member->currentText());
            QVERIFY(!portrait->pixmap().isNull());
            QVERIFY(!member->currentText().contains(".cpp"));
            QVERIFY(source->count() >= 3);
        }
        QVERIFY(!QFile::exists(data.path() + "/native-last-run.json"));
    }
    void importedFilesDoNotChangePortraitAndDraftsStayWithTheirOriginalTeam() {
        const auto manifestPath = root + "/native/personal/manifests/aemeath.json";
        if (!QFile::exists(manifestPath))
            QSKIP("Ordinary source package intentionally excludes personal modules");
        QTemporaryDir data;
        const auto manifest = readJson(manifestPath);
        const auto first = data.path() + "/configs-dualpc/custom_teams/Aemeath__Linnai__Mornye";
        const auto second = data.path() + "/configs-dualpc/custom_teams/Cartethyia__Chisa__Ciaccona";
        writeJson(first + "/team.json", {{"team", manifest["team"]}, {"code_mode", "import"}});
        writeJson(first + "/native-source.json",
                  {{"id", manifest["id"]}, {"original_sources", manifest["original_sources"]}});
        writeJson(second + "/team.json",
                  {{"team", QJsonArray{"Cartethyia", "Chisa", "Ciaccona"}}, {"code_mode", "builtin"}});
        auto window = create(data);
        QTRY_VERIFY_WITH_TIMEOUT(card(*window, "AutoCombatTask"), 5000);
        navigate(*window, "characters");
        auto members = window->findChild<QComboBox *>("characterMember");
        auto sources = window->findChild<QComboBox *>("characterSource");
        auto views = window->findChild<QComboBox *>("characterCodeView");
        auto code = window->findChild<QPlainTextEdit *>("codeEditor");
        auto portrait = window->findChild<QLabel *>("characterPortrait");
        auto teams = window->findChild<QListWidget *>("teamList");
        QVERIFY(members && sources && views && code && portrait && teams);
        QCOMPARE(members->count(), 3);
        const auto artifacts = qEnvironmentVariable("OKWW_UI_ARTIFACTS");
        if (!artifacts.isEmpty()) window->grab().save(artifacts + "/character-team-avatars.png");
        members->setCurrentIndex(members->findData("Linnai"));
        QTRY_COMPARE(portrait->toolTip(), QString("琳奈"));
        const auto parameters = sources->findData("generated/Aemeath__Linnai__Mornye_Mornye.hpp");
        QVERIFY(parameters >= 0);
        sources->setCurrentIndex(parameters);
        QMetaObject::invokeMethod(sources, "activated", Qt::DirectConnection, Q_ARG(int, parameters));
        QCOMPARE(portrait->toolTip(), QString("琳奈"));
        QVERIFY(code->toPlainText().contains("Aemeath__Linnai__Mornye_Mornye"));
        const auto modeButton = [&]() -> QPushButton * {
            for (auto button : window->findChildren<QPushButton *>())
                if (button->text() == "切换为内置代码" || button->text() == "切换为导入代码")
                    return button;
            return nullptr;
        };
        QVERIFY(modeButton());
        modeButton()->click();
        QTRY_VERIFY(code->toPlainText().contains("class Linnai final"));
        QVERIFY(code->isReadOnly());
        QCOMPARE(views->count(), 2);
        views->setCurrentIndex(views->findData("import"));
        QMetaObject::invokeMethod(views, "activated", Qt::DirectConnection,
                                  Q_ARG(int, views->currentIndex()));
        QVERIFY(code->toPlainText().contains("class AemeathTeam"));
        QVERIFY(code->isReadOnly());
        QCOMPARE(readJson(first + "/team.json")["code_mode"].toString(), QString("builtin"));
        views->setCurrentIndex(views->findData("active"));
        QMetaObject::invokeMethod(views, "activated", Qt::DirectConnection,
                                  Q_ARG(int, views->currentIndex()));
        modeButton()->click();
        QTRY_VERIFY(!code->isReadOnly());
        QCOMPARE(sources->currentData().toString(), QString("aemeath.cpp"));
        code->setPlainText("// draft bound to the original team and source");
        code->document()->setModified(true);
        QTimer::singleShot(0, [] {
            if (auto dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                dialog->button(QMessageBox::Save)->click();
        });
        int secondIndex = -1;
        for (int i = 0; i < teams->count(); ++i)
            if (teams->item(i)->data(Qt::UserRole).toJsonObject()["team"].toArray().contains("Chisa"))
                secondIndex = i;
        QVERIFY(secondIndex >= 0);
        teams->setCurrentRow(secondIndex);
        QTRY_VERIFY(code->isReadOnly());
        QFile draft(first + "/drafts/aemeath.cpp");
        QVERIFY(draft.open(QIODevice::ReadOnly));
        QCOMPARE(draft.readAll(), QByteArray("// draft bound to the original team and source"));
        QVERIFY(!QDir(second + "/drafts").exists());
        QVERIFY(!QFile::exists(data.path() + "/native-last-run.json"));
    }
    void wheelDoesNotEditAndResetOnlyChangesConfiguration() {
        QTemporaryDir data;
        auto window = create(data);
        QTRY_VERIFY_WITH_TIMEOUT(card(*window, "DailyTask2"), 5000);
        navigate(*window, "tasks");
        auto daily = card(*window, "DailyTask2");
        daily->setExpanded(true);
        auto spin = daily->findChild<QSpinBox *>("field-Tacet Suppression Count");
        QVERIFY(spin);
        const int original = spin->value();
        QWheelEvent wheel(QPointF(8, 8), QPointF(spin->mapToGlobal(QPoint(8, 8))), QPoint{}, QPoint(0, 120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(spin, &wheel);
        QCOMPARE(spin->value(), original);
        spin->setValue(original + 1);
        QMetaObject::invokeMethod(spin, "editingFinished", Qt::DirectConnection);
        QCOMPARE(profile(data).value("DailyTask2", "Tacet Suppression Count").toInt(), original + 1);
        QTest::qWait(1);
        auto reset = daily->findChildren<QPushButton *>();
        bool pressed = false;
        for (auto button : reset)
            if (button->text() == "重置配置" && button->isVisible()) {
                button->click();
                pressed = true;
                break;
            }
        QVERIFY(pressed);
        QCOMPARE(profile(data).value("DailyTask2", "Tacet Suppression Count").toInt(), original);
        QVERIFY(!QFile::exists(data.path() + "/native-last-run.json"));
    }
    void pythonWorkshopEditorShowsTheSelectedMemberAndSwitchesBackToNativeBuiltin() {
        if (!QFile::exists(root + "/native/bridge/workshop_worker.py"))
            QSKIP("Python SDK absent");
        QTemporaryDir data;
        const auto folder = data.path() + "/configs-dualpc/custom_teams/Baizhi__Mortefi__Verina";
        QDir().mkpath(folder);
        writeJson(folder + "/team.json",
                  {{"team", QJsonArray{"Baizhi", "Mortefi", "Verina"}}, {"code_mode", "import"}});
        for (const auto &name : QStringList{"Baizhi", "Mortefi", "Verina"}) {
            const auto text = "from src.char." + name + " import " + name + " as Native\nclass " + name +
                              "(Native):\n    pass\n";
            for (const auto &suffix : {".py", ".import.py"}) {
                QFile file(folder + '/' + name + suffix);
                QVERIFY(file.open(QIODevice::WriteOnly));
                file.write(text.toUtf8());
            }
        }
        auto window = create(data);
        QTRY_VERIFY_WITH_TIMEOUT(card(*window, "AutoCombatTask"), 5000);
        navigate(*window, "characters");
        auto members = window->findChild<QComboBox *>("characterMember");
        auto code = window->findChild<QPlainTextEdit *>("codeEditor");
        auto source = window->findChild<QComboBox *>("characterSource");
        QCOMPARE(members->count(), 3);
        for (int i = 0; i < 3; ++i) {
            members->setCurrentIndex(i);
            const auto name = members->currentData().toString();
            QTRY_VERIFY(code->toPlainText().contains("class " + name + "(Native)"));
            QVERIFY(!code->isReadOnly());
            QCOMPARE(source->currentData().toString(), name + ".py");
            QCOMPARE(code->document()->property("codeLanguage").toString(), QString("python"));
        }
        QVERIFY(QMetaObject::invokeMethod(window.get(), "handleEngineMessage", Qt::DirectConnection,
            Q_ARG(QJsonObject, (QJsonObject{{"event", "combat_info"}, {"data", QJsonObject{
                {"members", QJsonArray{QJsonObject{{"class", "Baizhi"}, {"current", true}}}},
                {"mode", "python"}}}}))));
        QPushButton *mode = nullptr;
        for (auto button : window->findChildren<QPushButton *>())
            if (button->text() == "切换为内置代码")
                mode = button;
        QVERIFY(mode);
        QElapsedTimer elapsed;
        elapsed.start();
        mode->click();
        QVERIFY2(elapsed.elapsed() < 250, "Switching code blocked the UI click handler");
        QVERIFY(!mode->isEnabled());
        QTRY_VERIFY(code->isReadOnly());
        QVERIFY(code->toPlainText().contains("class Verina final"));
        for (auto summary : window->findChildren<QLabel *>("taskPartySummary")) {
            QVERIFY(summary->property("fullTeamText").toString().contains("下次启动重新识别"));
            QVERIFY(!summary->property("fullTeamText").toString().contains("白芷"));
        }
        QTRY_VERIFY(mode->isEnabled());
        mode->click();
        QTRY_VERIFY(!code->isReadOnly());
        QVERIFY(code->toPlainText().contains("class Verina(Native)"));
        QVERIFY(!QFile::exists(data.path() + "/native-last-run.json"));
        QSignalSpy exited(window.get(), &Window::exitAccepted);
        mode->click();
        window->findChild<QAction *>("trayExit")->trigger();
        QCOMPARE(exited.count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(exited.count(), 1, 10000);
        QCOMPARE(readJson(folder+"/team.json")["code_mode"].toString(), QString("builtin"));
        QVERIFY(QFile::exists(folder+"/Verina.import.py"));
    }
};
QTEST_MAIN(ClassicUiTests)
#include "test_classic_ui.moc"
