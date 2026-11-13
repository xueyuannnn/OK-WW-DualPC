#include "backend.hpp"
#include <QTcpSocket>
#include <QtTest>
using namespace okww;
class NativeTests : public QObject {
    Q_OBJECT
  private slots:
    void localModesKeepLegacyDeviceSettingsAndValidateTheirOwnFields() {
        auto config = Config::defaults();
        QCOMPARE(config.s("capture.source"), QString("card"));
        QCOMPARE(config.s("kmbox.protocol"), QString("kmbox_net"));
        config.set("capture.source", "desktop");
        config.set("capture.local_target", QJsonObject{{"kind","monitor"},{"id","test"}});
        config.set("capture.fourcc", "P010"); // Inactive card settings cannot block desktop capture.
        config.set("capture.width", 0);
        config.validateCapture();
        config.set("capture.local_target",QJsonObject{});
        config.validateCapture(); // DXGI resolves the primary monitor without a saved target.
        config.set("capture.local_backend", "WIN32");
        config.validateCapture(); // Win32 also has a usable primary-monitor default.
        config.set("capture.local_target", QJsonObject{{"kind","monitor"},{"id","test"}});
        for (auto backend : {"DXGI", "WIN32"}) {
            config.set("capture.local_backend", backend);
            for (int fps : {1, 75, 540, 1000}) {
                config.set("capture.local_fps",fps);
                config.validateCapture();
            }
            for (double fps : {0., 1001., 60.5}) {
                config.set("capture.local_fps",fps);
                QVERIFY_THROWS_EXCEPTION(std::runtime_error, config.validateCapture());
            }
        }
        config.set("capture.dxgi_follow_refresh",false);
        config.set("capture.local_fps", 60);
        config.set("capture.local_backend", "WIN32");
        config.validateCapture();
        config.set("kmbox.protocol", "sendinput");
        config.validateBox(); // SendInput follows the foreground window without a configured target.
        config.set("kmbox.target", QJsonObject{{"kind","window"},{"title","test"}});
        config.validateBox(); // No KMBox host, UUID, or network port required.
        config.set("kmbox.target_enabled",true);
        config.validateBox();
        config.set("kmbox.target",QJsonObject{});
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,config.validateBox());
        config.set("kmbox.target_enabled",false);
        QTemporaryDir data;
        config.save(data.path()+"/local.json");
        const auto saved = Config::load(data.path()+"/local.json");
        QCOMPARE(saved.json, config.json);
        config.set("capture.source","card");
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, config.validateCapture());
    }
    void sendInputScanCodesAndVirtualDesktopMapping() {
        QCOMPARE(desktopAbsolute({-1920,0},{-1920,0,3840,1080}),QPoint(0,0));
        QCOMPARE(desktopAbsolute({1919,1079},{-1920,0,3840,1080}),QPoint(65535,65535));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, desktopAbsolute({1920,0},{-1920,0,3840,1080}));
        const auto w = hidInput(26,true);
        QCOMPARE(w.type,DWORD(INPUT_KEYBOARD));
        QCOMPARE(w.ki.wScan,WORD(0x11));
        QCOMPARE(w.ki.dwFlags,DWORD(KEYEVENTF_SCANCODE));
        QCOMPARE(w.ki.dwExtraInfo,localInputTag);
        const auto right = hidInput(79,false);
        QCOMPARE(right.ki.wScan,WORD(0x4d));
        QCOMPARE(right.ki.dwFlags,DWORD(KEYEVENTF_SCANCODE|KEYEVENTF_EXTENDEDKEY|KEYEVENTF_KEYUP));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, hidInput(200,true));
    }
    void sendInputDryRunReleasesOnlyItsOwnHeldControls() {
        auto config=Config::defaults();
        config.set("kmbox.protocol","sendinput"); config.set("kmbox.dry_run",true);
        Controller controller(config);
        controller.connect();
        QVERIFY(controller.state()["absolute"].toBool());
        QVERIFY(controller.history().isEmpty()); // Connecting must not disturb local user input.
        controller.key(26,true); controller.key(224,true); controller.button("left",true);
        controller.move(40,40); controller.absolute(1919,1079,1920,1080); controller.wheel(-2);
        QCOMPARE(controller.history().size(),6);
        INPUT absolute{}; memcpy(&absolute,controller.history()[4].constData(),sizeof(INPUT));
        QCOMPARE(absolute.mi.dx,LONG(65535)); QCOMPARE(absolute.mi.dy,LONG(65535));
        QCOMPARE(absolute.mi.dwFlags,DWORD(MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK));
        controller.release();
        QCOMPARE(controller.history().size(),9);
        QVERIFY(controller.state()["keys"].toArray().isEmpty());
        QCOMPARE(controller.state()["buttons"].toInt(),0); QCOMPARE(controller.state()["modifiers"].toInt(),0);
        controller.key(5,false); controller.button("right",false); controller.release(); controller.close();
        QCOMPARE(controller.history().size(),9); // Unowned key-up is not sent to the desktop.
    }
    void previewOwnsConvertedPixelsAfterCaptureBufferIsReused() {
        for (auto format : {PixelFormat::NV12, PixelFormat::BGR24}) {
            QImage published, copied;
            {
                Frame frame;
                frame.width = frame.height = 4;
                frame.format = format;
                frame.stride = format == PixelFormat::NV12 ? 4 : 12;
                frame.bytes = QByteArray(format == PixelFormat::NV12 ? 24 : 48, char(128));
                published = previewImage(frame, {4, 4});
                copied = published.copy();
                frame.bytes.fill(char(0));
                QCOMPARE(published, copied);
            }
            QCOMPARE(published, copied);
            auto shared = published;
            shared.setPixelColor(0, 0, Qt::red);
            QCOMPARE(published, copied);
            QVERIFY(shared != published);
        }
    }
    void captureAcceptsValidFrameAfterTruncatedDriverSample() {
        Capture capture(Config::defaults());
        QByteArray bytes(24, char(128));
        capture.publish(reinterpret_cast<const uint8_t *>(bytes.constData()), 2,
                        4, 4, 4, PixelFormat::NV12, false, nowNs());
        QVERIFY(!capture.error().isEmpty());
        QVERIFY(!capture.latest());
        capture.publish(reinterpret_cast<const uint8_t *>(bytes.constData()), bytes.size(),
                        4, 4, 4, PixelFormat::NV12, false, nowNs());
        QVERIFY(capture.latest());
        QVERIFY(capture.error().isEmpty());
        QCOMPARE(capture.next(0)->bytes, bytes);
    }
    void boxMoveTestRetainsArmedInputAndHeldControls_data() {
        QTest::addColumn<QString>("protocol");
        QTest::newRow("KMBox Net") << QString("kmbox_net");
    }
    void boxMoveTestRetainsArmedInputAndHeldControls() {
        QFETCH(QString, protocol);
        Backend backend(false);
        auto config = Config::defaults();
        config.set("kmbox.protocol", protocol);
        config.set("kmbox.counts_per_pixel_x", 1.25);
        config.set("kmbox.counts_per_pixel_y", 1.5);
        backend.start(config, true);
        backend.arm(true);
        backend.nativeCommand({{"op", "key"}, {"code", 4}, {"down", true}});
        backend.nativeCommand({{"op", "key"}, {"code", 0xe1}, {"down", true}});
        backend.nativeCommand({{"op", "button"}, {"name", "left"}, {"down", true}});
        const auto before = backend.snapshot()["box"].toObject();
        const auto result = backend.testMove();
        QCOMPARE(result["counts_x"].toInt(), 50);
        QCOMPARE(result["counts_y"].toInt(), 60);
        QVERIFY(result["dry_run"].toBool());
        QVERIFY(result["armed"].toBool());
        const auto after = backend.snapshot();
        QVERIFY(after["running"].toBool());
        QVERIFY(after["armed"].toBool());
        for (const auto &field : {"keys", "modifiers", "buttons"})
            QCOMPARE(after["box"].toObject()[field], before[field]);
        QVERIFY(backend.heartbeat());
        backend.nativeCommand({{"op", "move"}, {"x", 5}, {"y", -3}});
        backend.nativeCommand({{"op", "key"}, {"code", 4}, {"down", false}});
        backend.nativeCommand({{"op", "button"}, {"name", "left"}, {"down", false}});
        backend.arm(false);
        QVERIFY(!backend.testMove()["armed"].toBool());
        backend.stop();
        QVERIFY_EXCEPTION_THROWN(backend.testMove(), std::runtime_error);
    }
    void previewReductionPreservesThinLines_data() {
        QTest::addColumn<int>("format");
        for (auto format : {PixelFormat::BGR24, PixelFormat::BGRA32, PixelFormat::NV12,
                            PixelFormat::YUY2, PixelFormat::UYVY, PixelFormat::I420, PixelFormat::MJPG})
            QTest::newRow(qPrintable(formatName(format))) << int(format);
    }
    void previewReductionPreservesThinLines() {
        QFETCH(int, format);
        Frame frame;
        frame.width = 8;
        frame.height = 4;
        frame.format = PixelFormat(format);
        const int channels = frame.format == PixelFormat::BGR24 ? 3
            : frame.format == PixelFormat::BGRA32 ? 4
            : frame.format == PixelFormat::YUY2 || frame.format == PixelFormat::UYVY ? 2 : 1;
        frame.stride = frame.width * channels;
        const bool planar = frame.format == PixelFormat::NV12 || frame.format == PixelFormat::I420;
        frame.bytes = QByteArray(frame.stride * frame.height * (planar ? 3 : 2) / 2, char(128));
        QImage image(frame.width, frame.height, QImage::Format_RGB32);
        for (int y = 0; y < frame.height; ++y)
            for (int x = 0; x < frame.width; ++x) {
                const int gray = x % 2 ? 255 : 0;
                image.setPixelColor(x, y, QColor(gray, gray, gray));
                auto pixel = frame.bytes.data() + y * frame.stride + x * channels;
                if (channels >= 3) {
                    for (int c = 0; c < 3; ++c) pixel[c] = char(gray);
                    if (channels == 4) pixel[3] = char(255);
                } else pixel[frame.format == PixelFormat::UYVY ? 1 : 0] = char(x % 2 ? 235 : 16);
            }
        if (frame.format == PixelFormat::MJPG) {
            frame.bytes.clear();
            QBuffer buffer(&frame.bytes);
            QVERIFY(buffer.open(QIODevice::WriteOnly));
            QVERIFY(image.save(&buffer, "JPG", 100));
        }
        const auto original = frame.bytes;
        const auto preview = previewImage(frame, {4, 2});
        QCOMPARE(preview.size(), QSize(4, 2));
        // Both halves of each one-pixel stripe must contribute. Nearest-neighbour
        // sampling loses all white strokes and returns a completely black image.
        for (int y = 0; y < preview.height(); ++y)
            for (int x = 0; x < preview.width(); ++x) {
                const auto gray = preview.pixelColor(x, y);
                QVERIFY(gray.red() >= 120 && gray.red() <= 136);
                QVERIFY(gray.green() >= 120 && gray.green() <= 136);
                QVERIFY(gray.blue() >= 120 && gray.blue() <= 136);
            }
        QCOMPARE(frame.bytes, original);
    }
    void previewRetainsNativeDetailAndLeavesEnlargementToTheWindow() {
        Frame frame;
        frame.width = 1920; frame.height = 1080; frame.stride = 1920 * 3;
        frame.format = PixelFormat::BGR24;
        frame.bytes = QByteArray(frame.stride * frame.height, char(0));
        for (int y = 0; y < frame.height; ++y)
            for (int x = 0; x < frame.width; ++x)
                for (int c = 0; c < 3; ++c)
                    frame.bytes[y * frame.stride + x * 3 + c] = char(x % 2 ? 255 : 0);
        const auto native = previewImage(frame, {1920, 1080});
        const auto larger = previewImage(frame, {3840, 2160});
        QCOMPARE(native.size(), QSize(1920, 1080));
        QCOMPARE(larger.size(), native.size());
        QCOMPARE(larger, native);
        QCOMPARE(native.pixelColor(1500, 800), QColor(Qt::black));
        QCOMPARE(native.pixelColor(1501, 800), QColor(Qt::white));
        QVERIFY(previewImage(frame, {}).isNull());
    }
    void deviceDetectionReadsEveryCardsModesAndContinuesAfterDriverFailure() {
        auto config = Config::defaults();
        config.set("capture.backend", "DSHOW");
        const auto original = config.json;
        QString backend;
        QJsonArray requests;
        const auto result = captureCapabilities(config,
            [&](const QString &selected) {
                backend = selected;
                return std::vector<Device>{{0, "HDMI A", "card-a"}, {1, "Offline card", "card-b"},
                                           {2, "HDMI C", "card-c"}};
            },
            [&](const Config &selected) {
                requests.append(selected.json);
                if (selected.s("capture.device_path") == "card-b")
                    fail("fixture: device unavailable");
                return selected.n("capture.device_index") == 0
                    ? std::vector<Mode>{{1920, 1080, 120, "NV12"}, {1920, 1080, 240, "NV12"}}
                    : std::vector<Mode>{{1280, 720, 60, "YUY2"}};
            });
        QCOMPARE(backend, QString("DSHOW"));
        QCOMPARE(requests.size(), 3);
        QCOMPARE(result.size(), 3);
        QCOMPARE(result[0].toObject()["modes"].toArray().size(), 2);
        QVERIFY(result[1].toObject()["modes"].toArray().isEmpty());
        QVERIFY(result[1].toObject()["error"].toString().contains("unavailable"));
        QCOMPARE(result[2].toObject()["modes"].toArray()[0].toObject()["fourcc"].toString(), QString("YUY2"));
        QCOMPARE(Config{requests[2].toObject()}.s("capture.device_name"), QString("HDMI C"));
        QCOMPARE(Config{requests[2].toObject()}.s("capture.device_path"), QString("card-c"));
        QCOMPARE(config.json, original);
    }
    void previewKeepsPublishedFramesImmutableAndHonorsPaddedRows() {
        Frame frame;
        frame.width = 4; frame.height = 2; frame.stride = 16;
        frame.format = PixelFormat::BGR24; frame.bottomUp = true;
        frame.bytes = QByteArray(32, char(0));
        for (int row = 0; row < 2; ++row)
            for (int x = 0; x < 4; ++x) {
                auto p = frame.bytes.data() + row * 16 + x * 3;
                p[0] = char(20 + row * 80); p[1] = char(x * 20); p[2] = char(150);
            }
        const auto original = frame.bytes;
        const auto expected = frameImage(frame);
        const auto preview = previewImage(frame, {4, 2});
        QCOMPARE(frame.bytes, original);
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 4; ++x)
                QCOMPARE(preview.pixelColor(x, y), expected.pixelColor(x, y));
        frame.format = PixelFormat::NV12; frame.width = 4; frame.height = 2; frame.stride = 8;
        frame.bottomUp = false; frame.bytes = QByteArray(24, char(128));
        for (int row = 0; row < 2; ++row)
            for (int x = 0; x < 4; ++x) frame.bytes[row * 8 + x] = char(60 + x * 30);
        const auto reference = frameImage(frame);
        const auto fast = previewImage(frame, {4, 2});
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 4; ++x) {
                auto a = reference.pixelColor(x, y), b = fast.pixelColor(x, y);
                QVERIFY(std::abs(a.red() - b.red()) <= 2);
                QVERIFY(std::abs(a.green() - b.green()) <= 2);
                QVERIFY(std::abs(a.blue() - b.blue()) <= 2);
            }
    }
    void matchingUdpAcknowledgements() {
        WSADATA data;QCOMPARE(WSAStartup(MAKEWORD(2,2),&data),0);
        SOCKET server=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);QVERIFY(server!=INVALID_SOCKET);
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        QCOMPARE(bind(server,reinterpret_cast<sockaddr*>(&address),sizeof(address)),0);
        int size=sizeof(address);getsockname(server,reinterpret_cast<sockaddr*>(&address),&size);
        DWORD timeout=50;setsockopt(server,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof(timeout));
        std::atomic<bool> stop{false},drop{false};std::atomic<int> dropped{0};
        std::thread peer([&]{while(!stop){char bytes[2048];sockaddr_in client{};int length=sizeof(client);int n=recvfrom(server,bytes,sizeof(bytes),0,reinterpret_cast<sockaddr*>(&client),&length);if(n<16)continue;
            if(drop){++dropped;continue;}
            QByteArray stale(bytes,16);qToLittleEndian<uint32_t>(qFromLittleEndian<uint32_t>(stale.constData()+8)+1,stale.data()+8);
            sendto(server,stale.constData(),16,0,reinterpret_cast<sockaddr*>(&client),length);
            sendto(server,bytes,16,0,reinterpret_cast<sockaddr*>(&client),length);
        }});
        bool success=false,timeoutReported=false;int packets=0;
        {
            auto config=Config::defaults();config.set("kmbox.dry_run",false);config.set("kmbox.host","127.0.0.1");config.set("kmbox.port",ntohs(address.sin_port));config.set("kmbox.uuid","12345678");config.set("kmbox.timeout",.06);
            Controller box(config);try{box.connect();box.move(12,-34);success=box.connected();drop=true;try{box.move(1,2);}catch(const std::runtime_error &){timeoutReported=true;}packets=dropped.load();drop=false;}catch(...){}
        }
        stop=true;peer.join();closesocket(server);WSACleanup();
        QVERIFY(success);QVERIFY(timeoutReported);QCOMPARE(packets,1);
    }
    void settingsRoundTrip() {
        QTemporaryDir dir;
        auto c = Config::defaults();
        c.set("kmbox.dry_run", true);
        c.set("capture.device_path", "unique-path");
        c.set("kmbox.counts_per_pixel_x", 1.25);
        c.json["extension"] = QJsonObject{{"retained", true}};
        c.save(dir.path() + "/test.json");
        auto copy = Config::load(dir.path() + "/test.json");
        QCOMPARE(copy.s("capture.device_path"), QString("unique-path"));
        QCOMPARE(copy.d("kmbox.counts_per_pixel_x"), 1.25);
        QVERIFY(copy.json["extension"].toObject()["retained"].toBool());
    }
    void invalidSettings() {
        auto c = Config::defaults();
        c.set("capture.fps", 0);
        QVERIFY_EXCEPTION_THROWN(c.validateCapture(), std::runtime_error);
        c = Config::defaults();
        c.set("kmbox.dry_run", false);
        QVERIFY_EXCEPTION_THROWN(c.validateBox(), std::runtime_error);
    }
    void netReports() {
        auto c = Config::defaults();
        c.set("kmbox.dry_run", true);
        Controller box(c);
        box.connect();
        box.key(0xe1, true);
        box.key(4, true);
        auto h = box.history();
        auto p = h.back();
        QCOMPARE(p.size(), 28);
        QCOMPARE(uint8_t(p[16]), uint8_t(2));
        QCOMPARE(uint8_t(p[18]), uint8_t(4));
        box.move(-40, 40);
        p = box.history().back();
        QCOMPARE(p.size(), 72);
        QCOMPARE(qFromLittleEndian<int32_t>(p.constData() + 20), -40);
        QCOMPARE(qFromLittleEndian<int32_t>(p.constData() + 24), 40);
        box.release();
        auto s = box.state();
        QCOMPARE(s["buttons"].toInt(), 0);
        QCOMPARE(s["modifiers"].toInt(), 0);
        QVERIFY(s["keys"].toArray().isEmpty());
    }
    void unsupportedProtocolsNeverConnect() {
        for (const auto &protocol : {"makcu_serial", "ch9329", "kvm_card_mini", "esp32_legacy", "unknown"}) {
            for (bool dryRun : {false, true}) {
                auto config = Config::defaults();
                config.set("kmbox.protocol", protocol);
                config.set("kmbox.dry_run", dryRun);
                Controller box(config);
                QVERIFY_EXCEPTION_THROWN(box.connect(), std::runtime_error);
                QVERIFY(!box.connected());
                QVERIFY(box.history().isEmpty());
            }
        }
    }
    void supportedProfilesIgnoreRetiredUsbFields() {
        for (const auto &protocol : {"kmbox_net"}) {
            QTemporaryDir data;
            auto config = Config::defaults();
            config.set("kmbox.protocol", protocol);
            config.set("kmbox.dry_run", true);
            config.set("kmbox.serial_port", "COM_TEST");
            config.set("kmbox.hid_vid", 16701);
            config.set("kmbox.hid_pid", 8455);
            config.set("kmbox.hid_path", "retired-device");
            config.save(data.path() + "/dualpc.json");
            auto loaded = Config::load(data.path() + "/dualpc.json");
            QCOMPARE(loaded.s("kmbox.protocol"), QString(protocol));
            QVERIFY(loaded.get("kmbox.serial_port").isUndefined());
            QVERIFY(loaded.get("kmbox.hid_path").isUndefined());
            QVERIFY(loaded.get("kmbox.hid_vid").isUndefined());
            Controller box(loaded);
            box.connect();
            QVERIFY(!box.state()["absolute"].toBool());
            QVERIFY_EXCEPTION_THROWN(box.absolute(20, 20, 1920, 1080), std::runtime_error);
        }
    }
    void retiredSerialProfilePreservesNetworkAndCaptureSettings() {
        QTemporaryDir data;
        auto config = Config::defaults();
        config.set("kmbox.protocol", "makcu_serial");
        config.set("kmbox.serial_port", "COM4");
        config.set("kmbox.baudrate", 115200);
        config.set("kmbox.host", "192.168.2.188");
        config.set("kmbox.port", 8310);
        config.set("kmbox.uuid", "12345678");
        config.set("capture.fps", 240);
        config.set("extension.saved", "preserved");
        const auto path = data.path() + "/dualpc.json";
        writeJson(path, config.json);
        const auto loaded = Config::load(path);
        QCOMPARE(loaded.s("kmbox.protocol"), QString("kmbox_net"));
        QCOMPARE(loaded.s("kmbox.host"), QString("192.168.2.188"));
        QCOMPARE(loaded.n("kmbox.port"), 8310);
        QCOMPARE(loaded.s("kmbox.uuid"), QString("12345678"));
        QCOMPARE(loaded.n("capture.fps"), 240);
        QCOMPARE(loaded.s("extension.saved"), QString("preserved"));
        QVERIFY(loaded.get("kmbox.serial_port").isUndefined());
        QVERIFY(loaded.get("kmbox.baudrate").isUndefined());
        loaded.save(path);
        QCOMPARE(Config::load(path).json, loaded.json);
    }
    void bottomUpPreview() {
        Frame f;
        f.width = 2;
        f.height = 2;
        f.stride = 8;
        f.format = PixelFormat::BGR24;
        f.bottomUp = true;
        f.bytes = QByteArray(16, '\0');
        f.bytes[2] = char(255);
        f.bytes[8] = char(255);
        auto image = frameImage(f);
        QCOMPARE(image.pixelColor(0, 0), QColor(0, 0, 255));
        QCOMPARE(image.pixelColor(0, 1), QColor(255, 0, 0));
        auto preview = previewImage(f, QSize(100, 50));
        QCOMPARE(preview.size(), QSize(2, 2)); // Enlarge only at presentation, retaining original pixels.
    }
    void yuvPreview() {
        Frame f;
        f.width = 2;
        f.height = 2;
        f.stride = 2;
        f.format = PixelFormat::NV12;
        f.bytes = QByteArray(6, char(128));
        for (int i = 0; i < 4; ++i)
            f.bytes[i] = 16;
        auto image = frameImage(f);
        QCOMPARE(image.pixelColor(0, 0), QColor(0, 0, 0));
    }
    void nativeIpcAndWatchdog() {
        Backend backend;
        auto c = Config::defaults();
        backend.start(c, true);
        QTcpSocket socket;
        socket.connectToHost("127.0.0.1", quint16(backend.port()));
        QVERIFY(socket.waitForConnected(2000));
        auto request = [&](QJsonObject q) {
            q["token"] = backend.token();
            socket.write(QJsonDocument(q).toJson(QJsonDocument::Compact) + '\n');
            socket.waitForBytesWritten(1000);
            if (!socket.canReadLine())
                socket.waitForReadyRead(2000);
            return QJsonDocument::fromJson(socket.readLine()).object();
        };
        QVERIFY(request({{"op", "key"}, {"code", 4}, {"down", true}})["ok"].toBool() == false);
        backend.arm(true);
        QVERIFY(backend.heartbeat());
        QVERIFY(request({{"op", "key"}, {"code", 4}, {"down", true}})["ok"].toBool());
        auto frame = request({{"op", "frame"}})["result"].toObject();
        QVERIFY(frame["available"].toBool());
        QCOMPARE(frame["size"].toInt(), 1280 * 720 * 3);
        QTest::qWait(1750);
        auto state = backend.snapshot();
        QVERIFY(!state["armed"].toBool());
        QVERIFY(!backend.heartbeat());
        QVERIFY(state["box"].toObject()["keys"].toArray().isEmpty());
        const auto fault = state["input_fault"].toObject();
        QCOMPARE(fault["reason"].toString(), QString("heartbeat_timeout"));
        QVERIFY(fault["heartbeat_age_ms"].toDouble() >= 1500);
        QVERIFY(fault["frame_age_ms"].toDouble() < 150);
        QVERIFY(fault["keys"].toArray().contains(4));
        QVERIFY(fault["box_connected"].toBool());
        QVERIFY(state["error"].toString().contains("心跳"));
        QVERIFY(request({{"op", "key"}, {"code", 4}, {"down", false}})["ok"].toBool());
        const auto denied = request({{"op", "key"}, {"code", 4}, {"down", true}});
        QVERIFY(!denied["ok"].toBool());
        QCOMPARE(denied["error"].toString(), state["error"].toString());
        backend.arm(false);
        QCOMPARE(backend.snapshot()["input_fault"].toObject(), fault);
        backend.arm(true);
        QVERIFY(backend.snapshot()["input_fault"].toObject().isEmpty());
        QVERIFY(backend.snapshot()["error"].toString().isEmpty());
        // Active progress remains armed beyond the hang deadline. This is not
        // an unconditional background heartbeat: the earlier dead task stopped.
        for (int i = 0; i < 20; ++i) {
            QTest::qWait(100);
            QVERIFY(backend.heartbeat());
        }
        QVERIFY(backend.snapshot()["armed"].toBool());
    }
};
QTEST_GUILESS_MAIN(NativeTests)
#include "test_native.moc"
