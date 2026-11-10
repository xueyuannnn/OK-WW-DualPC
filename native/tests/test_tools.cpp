#include "runtime/multi_account.hpp"
#include "runtime/scripts.hpp"
#include "runtime/template_store.hpp"
#include "runtime/workshop.hpp"
#include <QPainter>
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
class ToolTests final : public QObject {
    Q_OBJECT
  private slots:
    void recognitionConvertsEachPublishedFrameOnlyOnce() {
        auto config = Config::defaults();
        config.set("capture.output_width", 1280);
        config.set("capture.output_height", 720);
        InferenceFrames cache(config);
        auto frame = std::make_shared<Frame>();
        frame->width = 1280;
        frame->height = 720;
        frame->stride = 1280 * 3;
        frame->format = PixelFormat::BGR24;
        frame->sequence = 1;
        frame->bytes = QByteArray(frame->stride * frame->height, char(20));
        const auto first = cache.get(frame);
        for (int i = 0; i < 100; ++i)
            QCOMPARE(cache.get(frame).data, first.data);
        QCOMPARE(cache.conversions(), uint64_t(1));
        auto next = std::make_shared<Frame>(*frame);
        next->sequence = 2;
        next->bytes.fill(char(40));
        QCOMPARE(cache.get(next).at<cv::Vec3b>(0, 0)[0], uint8_t(40));
        QCOMPARE(cache.conversions(), uint64_t(2));
        QVERIFY(cache.get({}).empty());
        QCOMPARE(first.at<cv::Vec3b>(0, 0)[0], uint8_t(20));
    }
    void templatesPersistValidatePublishAndRemainRecognizable() {
        QTemporaryDir data;
        TemplateStore store(data.path() + "/templates");
        QImage image(1280, 720, QImage::Format_RGB888);
        image.fill(QColor("#203044"));
        QPainter paint(&image);
        paint.fillRect(QRect(100, 50, 40, 30), Qt::green);
        paint.drawLine(101, 50, 138, 79);
        paint.end();
        const int id = store.add(image);
        const QJsonArray regions{
            QJsonObject{{"name", "custom_button"}, {"bbox", QJsonArray{100, 50, 40, 30}}}};
        store.saveAnnotations(id, regions);
        QCOMPARE(store.annotations(id).size(), 1);
        const auto before = readJson(store.cocoPath());
        QVERIFY_THROWS_EXCEPTION(
            std::runtime_error,
            store.saveAnnotations(
                id, QJsonArray{QJsonObject{{"name", "bad"}, {"bbox", QJsonArray{1270, 50, 40, 30}}}}));
        QCOMPARE(readJson(store.cocoPath()), before);
        const auto assets = data.path() + "/ok_tasks/assets";
        store.publish(assets, data.path() + "/ok_tasks/LabelEnum.py");
        Templates templates(assets + "/coco_annotations.json");
        cv::Mat frame(720, 1280, CV_8UC3, image.bits(), image.bytesPerLine());
        const auto found = templates.one(frame, "custom_button");
        QVERIFY(found);
        QCOMPARE(found->box, QRect(100, 50, 40, 30));
        auto published = readJson(assets + "/coco_annotations.json");
        QImage pattern(assets + '/' + published["images"].toArray()[0].toObject()["file_name"].toString());
        QCOMPARE(pattern.pixelColor(0, 0), QColor(Qt::black));
        QCOMPARE(QImage(store.images()[0].toObject()["path"].toString()).pixelColor(0, 0), QColor("#203044"));
        store.remove(id);
        QVERIFY(store.images().empty());
        QCOMPARE(QDir(data.path() + "/templates/.trash").entryList({"*.png"}, QDir::Files).size(), 1);
    }
    void completedAccountsAreNeverSelectedAgain() {
        AccountCycle cycle;
        cycle.complete("USER0****@Example.CON");
        QVERIFY(cycle.completed("usero****@example.com"));
        QVector<TextLine> accounts{{"USER0****@Example.CON", {}, 1}, {"next****@example.com", {}, 1}};
        QCOMPARE(cycle.next(accounts)->text, QString("next****@example.com"));
        cycle.complete(accounts[1].text);
        QVERIFY(!cycle.next(accounts));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, cycle.complete(""));
    }
    void recordingKeepsChordsMouseHoldsAndTiming() {
        const QJsonArray events{
            QJsonObject{{"op", "key_down"}, {"key", "ctrl"}, {"time", .1}},
            QJsonObject{{"op", "key_down"}, {"key", "a"}, {"time", .2}},
            QJsonObject{{"op", "key_up"}, {"key", "a"}, {"time", .4}},
            QJsonObject{{"op", "key_up"}, {"key", "ctrl"}, {"time", .41}},
            QJsonObject{{"op", "mouse_down"}, {"key", "left"}, {"x", .3}, {"y", .4}, {"time", .5}},
            QJsonObject{{"op", "mouse_up"}, {"key", "left"}, {"time", 1.5}}};
        const auto code = recordedScript(events);
        QVERIFY(code.contains("self.send_key_down(\"ctrl\")"));
        QVERIFY(code.contains("self.sleep(1.0000)"));
        QVERIFY(code.indexOf("self.mouse_down") < code.indexOf("self.mouse_up"));
    }
    void originalPythonTaskUsesOnlyHostInputAndCanBeCancelled() {
        const QString root = OKWW_SOURCE_ROOT;
        if (workshopPython(root).isEmpty())
            QSKIP("Python SDK absent");
        QTemporaryDir data;
        Config config = Config::defaults();
        config.set("capture.output_width", 1280);
        config.set("capture.output_height", 720);
        config.set("capture.crop", QJsonArray{100, 100, 1600, 900});
        config.set("kmbox.dry_run", true);
        config.save(data.path()+"/dualpc.json");
        auto created = scriptManage(root, data.path(), {{"op", "create"}, {"name", "Example"}});
        const auto id = created["selected"].toString();
        const QString code =
            "from ok import BaseTask\nclass Example(BaseTask):\n"
            "    def __init__(self, *args, **kwargs):\n        super().__init__(*args, **kwargs)\n        "
            "self.default_config = {'Amount': 3 + 4}\n"
            "    def run(self):\n"
            "        self.send_key_down('a')\n        self.click_relative(.5, .5, down_time=.04)\n"
            "        self.send_key_up('a')\n        self.info_set('checked', 'yes')\n";
        auto saved = scriptManage(root, data.path(), {{"op", "save"}, {"script", id}, {"code", code}});
        auto entry = saved["scripts"].toArray()[0].toObject();
        QCOMPARE(entry["fields"].toArray()[0].toObject()["value"].toInt(), 7);
        Templates templates(root + "/assets/coco_annotations.json");
        Profile profile(data.path() + "/configs-dualpc", root + "/native/resources/catalog.json");
        SteadyClock clock;
        ExecutionGate gate;
        QElapsedTimer elapsed;
        elapsed.start();
        Execution execution(gate, gate.start(), clock, [&] {
            if (elapsed.elapsed() > 45000)
                fail("Script test timed out");
        });
        QVector<QJsonObject> events;
        QJsonArray keys;
        int buttons = 0;
        std::atomic<bool> heldW{false};
        Input input(
            config, {1920, 1080}, true,
            [&](QJsonObject event) {
                events.append(event);
                if (event["op"] == "key") {
                    if (event["code"].toInt() == 26 && event["down"].toBool())
                        heldW = true;
                    if (event["down"].toBool())
                        keys.append(event["code"]);
                    else
                        keys = {};
                }
                if (event["op"] == "button")
                    buttons = event["down"].toBool() ? 1 : 0;
                return QJsonObject{
                    {"capture_connected", true},
                    {"armed", true},
                    {"running", true},
                    {"box", QJsonObject{{"connected", true}, {"keyboard", true}, {"absolute", true}}},
                    {"keys", keys},
                    {"buttons", buttons},
                    {"modifiers", 0},
                    {"actual", QJsonObject{}}};
            },
            execution);
        TaskContext context(
            execution, input, templates, profile,
            [] { return cv::Mat(720, 1280, CV_8UC3, cv::Scalar(20, 30, 40)); },
            [](const auto &, QRect, double) { return QVector<TextLine>{}; });
        bool checked = false;
        context.info = [&](QString key, QString value) {
            if (key == "checked" && value == "yes")
                checked = true;
        };
        {
            ScriptRuntime worker(context, root, data.path());
            worker.run(entry);
            QVERIFY(checked);
            const auto point =
                std::find_if(events.begin(), events.end(), [](auto e) { return e["op"] == "absolute"; });
            QVERIFY(point != events.end());
            QCOMPARE((*point)["x"].toInt(), 900); // native crop mapping, not an uncropped 960
            QCOMPARE((*point)["y"].toInt(), 550);
            QVERIFY(keys.empty());
        }
        const QString holding = "from ok import BaseTask\nimport time\nclass Example(BaseTask):\n    def "
                                "run(self):\n        self.send_key_down('w')\n        time.sleep(100)\n";
        saved = scriptManage(root, data.path(), {{"op", "save"}, {"script", id}, {"code", holding}});
        entry = saved["scripts"].toArray()[0].toObject();
        std::jthread interrupter([&](std::stop_token stop) {
            while (!heldW && !stop.stop_requested())
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if (heldW) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                gate.stop();
            }
        });
        // Stop as soon as the held W reaches the host, even though Python sleeps.
        context.info = {};
        bool cancelled = false;
        {
            ScriptRuntime worker(context, root, data.path());
            try {
                worker.run(entry);
            } catch (const Cancelled &) {
                cancelled = true;
            }
        }
        interrupter.request_stop();
        interrupter.join();
        QVERIFY(heldW);
        QVERIFY(cancelled);
        QVERIFY(input.idle());
        QCOMPARE(events.last()["op"].toString(), QString("release"));
    }
};
QTEST_GUILESS_MAIN(ToolTests)
#include "test_tools.moc"
