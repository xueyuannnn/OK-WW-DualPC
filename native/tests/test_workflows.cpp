#include "runtime/task.hpp"
#include "runtime/instance_flow.hpp"
#include "runtime/echo_operations.hpp"
#include "runtime/garden.hpp"
#include "runtime/echo_farm.hpp"
#include "runtime/daily.hpp"
#include "runtime/multi_account.hpp"
#include "runtime/combat_session.hpp"
#include "runtime/resource_failure.hpp"
#include <opencv2/imgproc.hpp>
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
namespace {
struct SceneClock : Clock {
    double time = 0;
    double minimumStep = 0;
    std::function<void()> tick;
    double now() const override { return time; }
    void sleep(double seconds) override {
        time += std::max(seconds, minimumStep);
        if (time > 1000)
            fail("离线回放超出时间限制");
        if (tick)
            tick();
    }
};
struct Scenes {
    const QString root = OKWW_SOURCE_ROOT;
    Templates templates{root + "/assets/coco_annotations.json"};
    QTemporaryDir directory;
    Profile profile{directory.path(), root + "/native/resources/catalog.json"};
    Config config = Config::defaults();
    SceneClock clock;
    ExecutionGate gate;
    Execution execution{gate, gate.start(), clock};
    QSize size;
    QString scene = "blank";
    QMap<QString, cv::Mat> frames;
    QMap<QString, QVector<TextLine>> words;
    QVector<QJsonObject> events;
    std::function<void(QJsonObject)> action;
    std::function<QVector<TextLine>(const cv::Mat &, QRect)> scaledText;
    QPoint pointer;
    Input input;
    TaskContext c;
    explicit Scenes(QSize dimensions = {1920, 1080})
        : size(dimensions), input(
                                settings(dimensions), dimensions, true,
                                [this](QJsonObject event) {
                                    events << event;
                                    if (event["op"] == "absolute")
                                        pointer = {event["x"].toInt(), event["y"].toInt()};
                                    if (action)
                                        action(event);
                                    return QJsonObject{};
                                },
                                execution),
          c(
              execution, input, templates, profile, [this] { return frames[scene]; },
              [this](const cv::Mat &image, QRect area, double) {
                  if (image.cols != size.width() && scaledText)
                      return scaledText(image, area);
                  QVector<TextLine> out;
                  for (auto line : words[scene])
                      if (area.contains(line.center().toPoint()))
                          out << line;
                  return out;
              }) {
        c.wallTime = [] { return QDateTime(QDate(2030, 1, 2), QTime(12, 0)); };
        frame("blank", {});
        frame("world", {"char_2_text", "char_3_text", "world_earth_icon"});
        frame("realm", {"char_2_text", "char_3_text", "illusive_realm_exit"});
    }
    Config settings(QSize dimensions) {
        config.set("capture.output_width", dimensions.width());
        config.set("capture.output_height", dimensions.height());
        config.set("kmbox.desktop_width", dimensions.width());
        config.set("kmbox.desktop_height", dimensions.height());
        return config;
    }
    void frame(QString name, QStringList features) {
        cv::Mat image(size.height(), size.width(), CV_8UC3, cv::Scalar(17, 23, 31));
        for (auto feature : features) {
            auto r = templates.box(feature, size);
            auto pattern = templates.pattern(feature, size);
            if (pattern.channels() == 1)
                cv::cvtColor(pattern, pattern, cv::COLOR_GRAY2BGR);
            if (r.x() < 0 || r.y() < 0 || r.right() >= size.width() || r.bottom() >= size.height())
                fail("回放模板超出画布：" + feature);
            pattern.copyTo(image(cv::Rect(r.x(), r.y(), r.width(), r.height())));
        }
        frames[name] = image;
    }
    TextLine word(QString text, double x, double y, double w = .12, double h = .03) {
        return {text, gameBox(frames["blank"], x, y, x + w, y + h), .99};
    }
    void show(QString name) {
        scene = name;
        c.refresh();
    }
    bool leftUp(const QJsonObject &e) const {
        return e["op"] == "button" && e["name"] == "left" && !e["down"].toBool();
    }
    bool keyUp(const QJsonObject &e, int key) const {
        return e["op"] == "key" && e["code"].toInt() == key && !e["down"].toBool();
    }
    int clicks() const {
        return int(std::count_if(events.begin(), events.end(), [&](auto e) { return leftUp(e); }));
    }
};
} // namespace
class WorkflowTests : public QObject {
    Q_OBJECT
  private slots:
    void shortTargetDistinguishesEnemyFromEmptyMarker_data() {
        QTest::addColumn<QSize>("dimensions");
        QTest::addColumn<QString>("marker");
        for (const auto size : {QSize(1280, 720), QSize(1920, 1080)})
            for (const auto &marker : {QString("has_target"), QString("no_target")})
                QTest::newRow(qPrintable(QString::number(size.height()) + '-' + marker)) << size << marker;
    }
    void shortTargetDistinguishesEnemyFromEmptyMarker() {
        QFETCH(QSize, dimensions);
        QFETCH(QString, marker);
        Scenes s(dimensions);
        const auto region = s.templates.box("target_box_short", dimensions);
        auto image = s.frames["blank"].clone();
        auto pattern = s.templates.pattern(marker, dimensions);
        if (pattern.channels() == 1) cv::cvtColor(pattern, pattern, cv::COLOR_GRAY2BGR);
        const cv::Rect at(region.x() + (region.width() - pattern.cols) / 2,
                          region.y() + (region.height() - pattern.rows) / 2, pattern.cols, pattern.rows);
        pattern.copyTo(image(at));
        s.frames["short"] = image;
        s.show("short");
        CombatVision vision(s.c, s.root + "/native/resources/characters.json");
        QVERIFY(!vision.targetObservation(false, false).matched);
        const auto observed = vision.targetObservation(true, false);
        QVERIFY(observed.matched);
        QCOMPARE(observed.locked, marker == "has_target");
        QVERIFY(!observed.extended);
        QVERIFY(s.events.isEmpty());
    }
    void memoryReserveRetriesRecognitionOnceAndKeepsOtherFailures() {
        EmergencyMemory reserve(4096);
        QVERIFY(reserve.available());
        int reads = 0, reports = 0;
        const auto result = reserve.read([&] {
            if (++reads == 1)
                throw cv::Exception(cv::Error::StsNoMem, "fixture: image allocation", "read", "fixture", 1);
            return 42;
        }, [&] { ++reports; });
        QCOMPARE(result, 42);
        QCOMPARE(reads, 2);
        QCOMPARE(reports, 1);
        QVERIFY(!reserve.available());
        QVERIFY_EXCEPTION_THROWN(reserve.read([&]() -> int {
            ++reads;
            throw std::bad_alloc();
        }, [&] { ++reports; }), std::bad_alloc);
        QCOMPARE(reads, 3);
        QCOMPARE(reports, 1);
        EmergencyMemory other(4096);
        QVERIFY_EXCEPTION_THROWN(other.read([]() -> int { throw std::runtime_error("fixture: logic error"); },
                                           [&] { ++reports; }), std::runtime_error);
        QVERIFY(other.available());
        QCOMPARE(reports, 1);
        QVERIFY(memoryFailure(std::bad_alloc()));
        QVERIFY(!memoryFailure(std::runtime_error("fixture: logic error")));
        QVERIFY(taskFailureText(std::bad_alloc()).contains("系统内存不足"));
        QCOMPARE(taskFailureText(std::runtime_error("fixture: logic error")), QString("fixture: logic error"));
        EmergencyMemory persistent(4096);
        int attempts = 0;
        QVERIFY_EXCEPTION_THROWN(persistent.read([&]() -> int { ++attempts; throw std::bad_alloc(); }, [] {}),
                                 std::bad_alloc);
        QCOMPARE(attempts, 2);
    }
    void screenshotLeaveDialogChoosesConfirmInsteadOfRestart_data() {
        QTest::addColumn<QSize>("size");
        QTest::newRow("720p") << QSize(1280, 720);
        QTest::newRow("1080p") << QSize(1920, 1080);
    }
    void screenshotLeaveDialogChoosesConfirmInsteadOfRestart() {
        QFETCH(QSize, size);
        Scenes s(size);
        s.frame("confirm", {});
        s.words["confirm"] = {s.word("提示", .24, .32), s.word("确认离开", .46, .53),
                              s.word("重新挑战", .28, .68), s.word("确认", .65, .68)};
        QVERIFY(nestExitContext(s.words["confirm"]));
        auto confirm = nestExitConfirmation(s.words["confirm"], size.width());
        QVERIFY(confirm && confirm->text == "确认");
        s.action = [&](auto event) {
            if (s.leftUp(event)) {
                QVERIFY(confirm->box.contains(s.pointer));
                s.scene = "world";
            }
        };
        s.show("confirm");
        leaveNest(s.c, false);
        QCOMPARE(s.scene, QString("world"));
        QCOMPARE(s.clicks(), 1);
        QVERIFY(s.input.idle());
    }
    void recognizedPartyAndSlotChangesPublishWithoutAnyInput() {
        Scenes s;
        const auto arrange = [&](QStringList characters) {
            auto image = s.frames["world"].clone();
            for (int i = 0; i < characters.size(); ++i) {
                auto pattern = s.templates.pattern(characters[i], s.size);
                if (pattern.channels() == 1) cv::cvtColor(pattern, pattern, cv::COLOR_GRAY2BGR);
                const auto area = s.templates.box(QString("box_char_%1").arg(i+1), s.size);
                pattern.copyTo(image(cv::Rect(area.x(), area.y(), pattern.cols, pattern.rows)));
            }
            s.frames["party"] = image;
            s.show("party");
        };
        QJsonObject reported;
        s.c.combatInfo = [&](QJsonObject value) { reported = value; };
        arrange({"char_baizhi", "char_mortefi", "char_verina"});
        CombatSession combat(s.c, s.root, s.directory.path());
        QVERIFY(combat.loadParty());
        QCOMPARE(reported["mode"].toString(), QString("builtin"));
        QCOMPARE(reported["members"].toArray().size(), 3);
        QCOMPARE(reported["members"].toArray()[0].toObject()["class"].toString(), QString("Baizhi"));
        QVERIFY(reported["members"].toArray()[0].toObject()["current"].toBool());
        arrange({"char_mortefi", "char_baizhi", "char_verina"});
        QVERIFY(combat.loadParty());
        QCOMPARE(reported["members"].toArray()[0].toObject()["class"].toString(), QString("Mortefi"));
        QVERIFY(s.events.isEmpty());
    }
    void multiAccountDailyVisitsEachSavedAccountAndStopsAtTheEnd() {
        Scenes s;
        s.frame("menu", {"esc_setting"});
        s.frame("confirm-logout", {"confirm_btn_hcenter_vcenter"});
        s.frame("account-login", {});
        s.frame("account-list", {});
        const QStringList names{"first****@mail.com", "next****@mail.com"};
        int current = 0, completed = 0;
        const auto loginWords = [&] {
            s.words["account-login"] = {s.word(names[current], .4, .42, .2), s.word("登录", .45, .58)};
        };
        loginWords();
        s.words["account-list"] = {s.word(names[0], .4, .42, .2), s.word(names[1], .4, .49, .2)};
        s.action = [&](auto event) {
            if (s.keyUp(event, Input::keyCode("esc"))) {
                if (s.scene == "world") s.scene = "menu";
                else if (s.scene == "account-list") s.scene = "account-login";
            }
            if (s.leftUp(event)) {
                if (s.scene == "menu") s.scene = "confirm-logout";
                else if (s.scene == "confirm-logout") { loginWords(); s.scene = "account-login"; }
                else if (s.scene == "account-login") {
                    if (s.pointer.y() < s.size.height() * .5) s.scene = "account-list";
                    else s.scene = "world";
                } else if (s.scene == "account-list") {
                    current = s.pointer.y() > s.size.height() * .48 ? 1 : 0;
                    loginWords(); s.scene = "account-login";
                }
            }
        };
        s.show("world"); s.c.task = "MultiAccountDailyTask";
        MultiAccountDaily(s.c).run([&] {
            QCOMPARE(s.scene, QString("world"));
            QCOMPARE(current, completed);
            ++completed;
        });
        QCOMPARE(completed, 2);
        QCOMPARE(s.scene, QString("account-login"));
        QVERIFY(s.input.idle());
    }
    void loginThenMonthlyClaimThenResultThenWorld_data() {
        QTest::addColumn<QSize>("size");
        QTest::newRow("720p") << QSize(1280, 720);
        QTest::newRow("1080p") << QSize(1920, 1080);
        QTest::newRow("1440p") << QSize(2560, 1440);
    }
    void loginThenMonthlyClaimThenResultThenWorld() {
        QFETCH(QSize, size);
        Scenes s(size);
        for (auto name : {"entry", "monthly", "reward"})
            s.frame(name, {});
        s.words["entry"] = {s.word("进入游戏", .6, .8)};
        s.words["monthly"] = {s.word("点击领取今日月相", .4, .8, .2), s.word("剩余28天", .45, .9)};
        s.words["reward"] = {s.word("获得奖励", .45, .2), s.word("月相", .45, .5),
                             s.word("点击空白处关闭", .4, .9, .2)};
        s.action = [&](auto e) {
            if (s.leftUp(e)) {
                if (s.scene == "entry")
                    s.scene = "monthly";
                else if (s.scene == "monthly")
                    s.scene = "reward";
                else if (s.scene == "reward")
                    s.scene = "world";
                else
                    fail("不应点击已经恢复的大世界");
            }
        };
        s.show("entry");
        QVERIFY(s.c.login());
        QCOMPARE(s.scene, QString("monthly"));
        QVERIFY(s.c.login());
        QCOMPARE(s.scene, QString("world"));
        QCOMPARE(s.clicks(), 3);
        QVERIFY(!s.c.login());
        QCOMPARE(s.clicks(), 3);
        QVERIFY(s.c.state->loggedIn);
        QVERIFY(s.input.idle());
    }
    void credentialsAndUnrelatedRewardScreensAreNotClicked() {
        Scenes s;
        s.words["blank"] = {s.word("登录", .4, .4), s.word("+86", .4, .5)};
        s.show("blank");
        QVERIFY(!s.c.login());
        QCOMPARE(s.clicks(), 0);
        s.words["blank"] = {s.word("领取每日任务奖励", .4, .8, .25), s.word("剩余28天", .45, .9)};
        QVERIFY(!s.c.monthlyCard());
        QCOMPARE(s.clicks(), 0);
    }
    void configuredMonthlyWindowResumesSameWaitAndDoesNotReenter() {
        Scenes s;
        s.frame("monthly", {});
        s.frame("reward", {});
        s.words["monthly"] = {s.word("点击领取今日月相", .4, .8, .2), s.word("剩余28天", .45, .9)};
        s.words["reward"] = {s.word("获得奖励", .45, .2), s.word("月相", .45, .5),
                             s.word("点击空白处关闭", .4, .9, .2)};
        s.c.wallTime = [&] {
            return QDateTime(QDate(2030, 1, 2), QTime(3, 59, 30)).addMSecs(qint64(s.clock.time * 1000));
        };
        bool appeared = false;
        s.clock.tick = [&] {
            if (!appeared && s.clock.time >= 5) {
                appeared = true;
                s.scene = "monthly";
            }
        };
        s.action = [&](auto e) {
            if (s.leftUp(e)) {
                if (s.scene == "monthly")
                    s.scene = "reward";
                else if (s.scene == "reward")
                    s.scene = "world";
                else
                    fail("月卡等待不应点击其他页面");
            }
        };
        s.show("world");
        s.c.sleep(.1);
        QCOMPARE(s.scene, QString("world"));
        QCOMPARE(s.clicks(), 2);
        QVERIFY(s.clock.time >= 5 && s.clock.time < 10);
        const auto before = s.clock.time;
        s.c.sleep(.5);
        QVERIFY(s.clock.time - before < .6);
        QVERIFY(s.input.idle());
        s.c.state->monthlyWindowHandled.clear();
        s.c.state->inCombat = true;
        const auto combatTime = s.clock.time;
        s.c.sleep(.2);
        QVERIFY(s.clock.time - combatTime < .3);
        QCOMPARE(s.clicks(), 2);
    }
    void cancelledMonthlyWaitReleasesKeys() {
        Scenes s;
        s.show("world");
        s.c.wallTime = [] { return QDateTime(QDate(2030, 1, 2), QTime(3, 59, 45)); };
        s.clock.tick = [&] {
            if (s.clock.time > 1)
                s.gate.stop();
        };
        QVERIFY_EXCEPTION_THROWN(s.c.sleep(.1), Cancelled);
        QVERIFY(s.clock.time < 1.1);
        QVERIFY(s.input.idle());
        QCOMPARE(s.clicks(), 0);
    }
    void nightmareExitUsesTemplateOnlyAfterVerifiedExitContext() {
        for (bool resource : {false, true}) {
            Scenes s;
            s.frame("confirm", {"confirm_btn_hcenter_vcenter"});
            s.words["confirm"] = {
                s.word(resource ? "是否消耗星声退出当前副本？" : "是否退出当前副本？", .3, .4, .4)};
            double leaveAt = -1;
            s.action = [&](auto e) {
                if (s.leftUp(e)) {
                    if (resource)
                        fail("资源弹窗不应使用退出模板兜底");
                    const auto expected = s.templates.box("confirm_btn_hcenter_vcenter", s.size);
                    if (!expected.contains(s.pointer))
                        fail("未点击退出按钮范围");
                    s.scene = "blank";
                    leaveAt = s.clock.time + 2;
                }
            };
            s.clock.tick = [&] {
                if (leaveAt >= 0 && s.clock.time >= leaveAt)
                    s.scene = "world";
            };
            s.show("confirm");
            if (resource) {
                QVERIFY_EXCEPTION_THROWN(leaveNest(s.c), NavigationFailure);
                QCOMPARE(s.clicks(), 0);
            } else {
                leaveNest(s.c);
                QCOMPARE(s.scene, QString("world"));
                QCOMPARE(s.clicks(), 1);
            }
            QVERIFY(s.input.idle());
        }
    }
    void repeatedDomainWaitsForLoadingInsteadOfOldHud() {
        Scenes s;
        s.frame("restart", {});
        s.words["restart"] = {s.word("重新挑战", .6, .5)};
        double requested = -1;
        s.action = [&](auto e) {
            if (s.keyUp(e, 0x29) && s.scene == "realm")
                s.scene = "restart";
            if (s.leftUp(e) && s.scene == "restart") {
                requested = s.clock.time;
                s.scene = "realm";
            }
        };
        s.clock.tick = [&] {
            if (requested < 0)
                return;
            const auto age = s.clock.time - requested;
            if (age > .75 && age < 1.75)
                s.scene = "blank";
            else if (age >= 1.75)
                s.scene = "realm";
        };
        s.show("realm");
        restartBossDomain(s.c);
        QCOMPARE(s.clicks(), 1);
        QVERIFY(s.clock.time - requested >= 1.75);
        QVERIFY(s.input.idle());
    }
    void mergeRequiresResultAndHandlesSuppressedConfirmation() {
        Scenes s;
        s.frame("merge", {});
        s.frame("result", {});
        s.words["result"] = {s.word("获得声骸", .4, .15)};
        s.show("merge");
        s.clock.tick = [&] {
            if (s.clock.time >= 1)
                s.scene = "result";
        };
        awaitEchoMergeResult(s.c);
        QCOMPARE(s.scene, QString("result"));
        QCOMPARE(s.clicks(), 0);
        s.clock.tick = {};
        s.show("merge");
        QVERIFY_EXCEPTION_THROWN(awaitEchoMergeResult(s.c), std::runtime_error);
        QCOMPARE(s.clicks(), 0);
    }
    void weeklyMaterialConfirmationIsNotARewardReceipt() {
        Scenes s;
        s.frame("result", {});
        s.words["blank"] = {s.word("确认消耗60结晶波片领取奖励？", .2, .4, .5), s.word("取消", .3, .65),
                            s.word("确认", .6, .65)};
        s.words["result"] = {s.word("获得奖励", .4, .2), s.word("再次挑战", .6, .85)};
        s.show("blank");
        QVERIFY(!weeklyMaterialRewardConfirmed(s.words["blank"]));
        s.clock.tick = [&] {
            if (s.clock.time > 1)
                s.scene = "result";
        };
        awaitWeeklyMaterialReward(s.c);
        QCOMPARE(s.scene, QString("result"));
        QCOMPARE(s.clicks(), 0);
        s.clock.tick = {};
        s.show("blank");
        QVERIFY_EXCEPTION_THROWN(awaitWeeklyMaterialReward(s.c), std::runtime_error);
        QCOMPARE(s.clicks(), 0);
        QVERIFY(s.input.idle());
    }
    void weeklyCompletedDoesNotEnterAnotherRound() {
        Scenes s;
        s.frame("book", {"gray_book_quest"});
        s.words["book"] = {s.word("本周游历值已满", .1, .8, .22)};
        bool book = false;
        int claims = 0;
        s.action = [&](auto e) {
            if (s.keyUp(e, 0x3b)) {
                s.scene = "book";
                book = true;
            }
            if (s.leftUp(e) && s.scene == "book") {
                const auto enter = gamePoint(s.size, .246, .486);
                if ((s.pointer - enter).manhattanLength() < 5)
                    fail("游历值已满仍点击开始乐园");
                const auto claim = gamePoint(s.size, .927, .893);
                if ((s.pointer - claim).manhattanLength() < 5)
                    ++claims;
            }
        };
        s.show("world");
        Garden(s.c, s.root).run();
        QVERIFY(book);
        QCOMPARE(claims, 2);
        QVERIFY(s.input.idle());
    }
    void shopBuysRecommendedThenInOrderReservesTenAndStopsOnUncertainDebit_data() {
        QTest::addColumn<bool>("confirmed");
        QTest::newRow("confirmed") << true;
        QTest::newRow("unconfirmed") << false;
    }
    void shopBuysRecommendedThenInOrderReservesTenAndStopsOnUncertainDebit() {
        QFETCH(bool, confirmed);
        Scenes s;
        s.frame("shop", {"garden_shop_close"});
        const auto base = s.frames["shop"].clone();
        const QVector<int> prices{15, 40, 10, 5, 25, 80, 90, 90};
        int balance = 65, selected = -1, closeCount = 0;
        QSet<int> sold;
        QVector<int> bought;
        auto badge = readImage(s.root + "/assets/dualpc/garden_recommended.png");
        cv::Mat crop, gray;
        cv::cvtColor(badge(cv::Rect(6, 6, 35, 36)), gray, cv::COLOR_BGR2GRAY);
        cv::resize(gray, gray, {pixelRound(35 * .72), pixelRound(36 * .72)});
        cv::cvtColor(gray, crop, cv::COLOR_GRAY2BGR);
        const auto update = [&] {
            s.frames["shop"] = base.clone();
            auto &image = s.frames["shop"];
            for (int i = 0; i < 8; ++i) {
                const double x = .198 + .080 * (i % 6), row = i / 6;
                auto box = gameBox(image, x + .006, .372 + .191 * row, x + .032, .400 + .191 * row);
                image(cv::Rect(box.x(), box.y(), box.width(), box.height()))
                    .setTo(cv::Scalar(sold.contains(i) ? 99 : i + 40, 23, 31));
                if (i == 2 && !sold.contains(i))
                    crop.copyTo(image(cv::Rect(pixelRound((x + .014) * image.cols),
                                               pixelRound((.302 - .07) * image.rows), crop.cols, crop.rows)));
            }
            s.words["shop"] = {s.word(QString::number(balance), .70, .045, .03)};
            if (selected >= 0)
                s.words["shop"] << s.word(sold.contains(selected) ? "已售罄"
                                                                  : QString("购买 %1").arg(prices[selected]),
                                          .70, .68, .1);
        };
        s.scaledText = [&](const cv::Mat &image, QRect area) -> QVector<TextLine> {
            const int i = image.at<cv::Vec3b>(0, 0)[0] - 40;
            return i >= 0 && i < 8 ? QVector<TextLine>{{QString::number(prices[i]), area, .99}}
                                   : QVector<TextLine>{};
        };
        s.action = [&](auto e) {
            if (!s.leftUp(e))
                return;
            const auto close = s.templates.box("garden_shop_close", s.size);
            if (close.contains(s.pointer)) {
                ++closeCount;
                s.scene = "world";
                return;
            }
            if (s.pointer.y() >= .66 * s.size.height()) {
                if (selected < 0 || sold.contains(selected))
                    fail("没有有效商品时执行购买");
                bought << selected;
                if (confirmed) {
                    balance -= prices[selected];
                    if (balance < 10)
                        fail("商店花掉了预留的 10 币");
                    sold.insert(selected);
                }
            } else {
                const int row = s.pointer.y() > .4 * s.size.height() ? 1 : 0;
                selected = pixelRound((double(s.pointer.x()) / s.size.width() - .198) / .080) + row * 6;
                if (selected < 0 || selected >= 8)
                    fail("商店商品点击位置无效");
            }
            update();
        };
        update();
        s.show("shop");
        const auto count = Garden(s.c, s.root).shop();
        QCOMPARE(bought, confirmed ? QVector<int>({2, 0, 3, 4}) : QVector<int>({2}));
        QCOMPARE(count, confirmed ? 4 : 0);
        QCOMPARE(balance, confirmed ? 10 : 65);
        QCOMPARE(closeCount, 1);
        QVERIFY(s.input.idle());
    }
    void weeklyRoundUsesMaxAndChecksSkillEachTurnThenReturnsToWeeklyRewards() {
        Scenes s;
        s.frame("day", {"garden_next_day"});
        s.frame("blessing", {});
        s.frame("finish", {"a_garden_back", "a_garden_restart"});
        s.frame("book", {"gray_book_quest"});
        s.words["book"] = {s.word("本周游历值已满", .1, .8, .22)};
        int day = 1, speed = 1, opened = 0, used = 0, claims = 0, advanced = 0;
        const auto update = [&] {
            const auto stage = s.word(QString("阶段1/7 第%1天").arg(day), .03, .11, .18);
            s.words["day"] = {stage, s.word(speed < 3 ? QString("x%1").arg(speed) : "MAX", .64, .04, .04)};
            s.words["blessing"] = {stage, s.word("使用祝福", .07, .72, .10),
                                   s.word(QString("剩余次数：%1").arg(day == 1 ? 1 : 0), .07, .77, .10)};
        };
        s.action = [&](auto e) {
            if (s.keyUp(e, Input::keyCode("f2"))) {
                s.scene = "book";
                return;
            }
            if (!s.leftUp(e))
                return;
            const auto nearPoint = [&](double x, double y) {
                return (s.pointer - gamePoint(s.size, x, y)).manhattanLength() < 12;
            };
            if (s.scene == "day") {
                if (s.pointer.y() < .1 * s.size.height())
                    ++speed;
                else if (nearPoint(.18, .785)) {
                    ++opened;
                    s.scene = "blessing";
                } else if (s.templates.box("garden_next_day", s.size).contains(s.pointer)) {
                    if (speed != 3 || opened != day)
                        fail("进入下一天前未完成倍速与技能检查");
                    ++advanced;
                    if (++day > 2)
                        s.scene = "finish";
                } else
                    fail("周常主界面点击位置不符合预期");
            } else if (s.scene == "blessing") {
                if (day == 1 && s.pointer.y() < .77 * s.size.height()) {
                    ++used;
                    s.scene = "day";
                } else if (day == 2 && nearPoint(.5, .8))
                    s.scene = "day";
                else
                    fail("无次数时错误使用祝福或未关闭面板");
            } else if (s.scene == "finish") {
                if (!s.templates.box("a_garden_back", s.size).contains(s.pointer))
                    fail("完成后直接重开，未返回检查周任务");
                s.scene = "world";
            } else if (s.scene == "book") {
                if (nearPoint(.246, .486))
                    fail("游历值已满后又开始一轮");
                if (nearPoint(.927, .893))
                    ++claims;
            }
            update();
        };
        update();
        s.show("day");
        Garden(s.c, s.root).run();
        QCOMPARE(speed, 3);
        QCOMPARE(opened, 2);
        QCOMPARE(used, 1);
        QCOMPARE(advanced, 2);
        QCOMPARE(claims, 2);
        QCOMPARE(s.scene, QString("book"));
        QVERIFY(s.input.idle());
    }
    void completedDailyClaimsMailPassAndWeeklyWithoutSpendingOrStartingCombat() {
        Scenes s;
        s.frame("book", {"gray_book_quest"});
        s.frame("pass", {});
        s.frame("menu", {});
        s.frame("mail", {});
        s.words["pass"] = {s.word("50", .23, .16, .03)};
        const auto dailyWords = QVector<TextLine>{s.word("180/180", .2, .4), s.word("100", .2, .85, .035)};
        s.words["book"] = dailyWords;
        for (auto key : {"Tacet Suppression Count", "Forgery Challenge Count", "Simulation Challenge Count"})
            s.profile.set("DailyTask2", key, 0);
        s.profile.set("DailyTask2", "Farm Nightmare Nest for Daily Echo", false);
        s.profile.set("DailyTask2", "Additional Tasks to Run After Daily Task",
                      QJsonArray{"Check Weekly Garden"});
        QStringList stages;
        int mailClaims = 0, passClaims = 0, weeklyClaims = 0;
        bool weekly = false;
        s.c.info = [&](QString key, QString value) {
            if (key == "当前任务")
                stages << value;
        };
        s.action = [&](auto e) {
            if (s.keyUp(e, Input::keyCode("f2"))) {
                weekly = false;
                s.words["book"] = dailyWords;
                s.scene = "book";
            }
            if (s.keyUp(e, Input::keyCode("esc")))
                s.scene = s.scene == "world" ? "menu" : "world";
            if (!s.leftUp(e))
                return;
            const auto nearPoint = [&](double x, double y) {
                return (s.pointer - gamePoint(s.size, x, y)).manhattanLength() < 10;
            };
            if (s.scene == "world" && nearPoint(.86, .05))
                s.scene = "pass";
            else if (s.scene == "menu" && nearPoint(.64, .95))
                s.scene = "mail";
            else if (s.scene == "mail" && nearPoint(.14, .9))
                ++mailClaims;
            else if (s.scene == "pass" && nearPoint(.68, .91))
                ++passClaims;
            else if (s.scene == "book") {
                if (nearPoint(.343, .129)) {
                    weekly = true;
                    s.words["book"] = {s.word("本周游历值已满", .1, .8, .22)};
                }
                if (weekly && nearPoint(.927, .893))
                    ++weeklyClaims;
                if (weekly && nearPoint(.246, .486))
                    fail("周任务已完成仍进入乐园");
            }
        };
        s.show("world");
        CombatSession combat(s.c, s.root, s.directory.path());
        EchoDetector detector(s.root + "/assets/echo_model/echo.onnx");
        Daily(s.c, combat, detector, s.root, s.directory.path()).run(true);
        QCOMPARE(stages,
                 QStringList({"补做体力目标", "每日奖励", "邮件", "先约电台", "周常乐园", "每日任务已完成"}));
        QCOMPARE(mailClaims, 1);
        QCOMPARE(passClaims, 2);
        QCOMPARE(weeklyClaims, 2);
        QVERIFY(s.input.idle());
        QVERIFY(!QFile::exists(s.directory.path() + "/configs-dualpc/DailyStaminaUsage.json"));
    }
    void combatRoundDistinguishesVictoryInterruptionDeathAndCancellation_data() {
        QTest::addColumn<QString>("scenario");
        for (auto name : {"complete", "resume", "retry-exhausted", "death", "cancel"})
            QTest::newRow(name) << QString(name);
    }
    void combatRoundDistinguishesVictoryInterruptionDeathAndCancellation() {
        QFETCH(QString, scenario);
        Scenes s;
        // Repeated reads represent fresh 50 FPS observations, not thousands
        // of identical synthetic video frames per simulated second.
        s.clock.minimumStep = .02;
        for (int i = 0; i < 3; ++i) {
            QStringList names{"has_target", "world_earth_icon"};
            for (int j = 0; j < 3; ++j)
                if (j != i)
                    names << QString("char_%1_text").arg(j + 1);
            s.frame(QString("combat%1").arg(i), names);
        }
        s.frame("death", {});
        s.words["death"] = {s.word("选择复苏物品", .3, .2, .3), s.word("复苏", .6, .8)};
        s.profile.set("AutoCombatTask", "Switch to Healer before and after Combat", false);
        s.profile.set("Task Recovery", "Recovery Retry Limit", 1);
        s.c.task = "FarmEchoTask";
        int switched = 0, interrupted = 0, recovered = 0;
        QStringList messages;
        s.c.info = [&](QString, QString text) { messages << text; };
        s.action = [&](auto e) {
            if (s.scene == "death" && s.keyUp(e, Input::keyCode("esc"))) {
                s.scene = "world";
                return;
            }
            if (s.scene.startsWith("combat") && e["op"] == "key" && !e["down"].toBool()) {
                const int slot = e["code"].toInt() - Input::keyCode("1");
                if (slot >= 0 && slot < 3) {
                    ++switched;
                    s.scene = scenario == "death" ? "death"
                              : switched >= 2     ? "world"
                                                  : QString("combat%1").arg(slot);
                }
            }
        };
        if (scenario == "cancel")
            s.clock.tick = [&] {
                if (s.clock.time > .2)
                    s.gate.stop();
            };
        s.show("combat0");
        QVERIFY(s.c.inTeam());
        QVERIFY(s.c.find("has_target"));
        CombatSession combat(s.c, s.root, s.directory.path());
        combat.targetEnemyTimeout = .05;
        if (scenario == "resume" || scenario == "retry-exhausted")
            combat.onCombatCheck = [&] {
                if (scenario == "retry-exhausted" || interrupted == 0) {
                    ++interrupted;
                    throw CombatInterrupted("回放中的短暂连招中断");
                }
                return true;
            };
        CombatOptions options;
        options.waitSeconds = .2;
        options.maximumSeconds = 40;
        options.recover = [&] { ++recovered; };
        if (scenario == "retry-exhausted") {
            QVERIFY_THROWS_EXCEPTION(NavigationFailure, combat.combatOnce(options));
            QCOMPARE(interrupted, 2);
        } else if (scenario == "death") {
            QVERIFY_THROWS_EXCEPTION(CharacterRevived, combat.combatOnce(options));
            QCOMPARE(recovered, 1);
            QCOMPARE(s.scene, QString("world"));
        } else if (scenario == "cancel") {
            QVERIFY_THROWS_EXCEPTION(Cancelled, combat.combatOnce(options));
            QCOMPARE(recovered, 0);
        } else {
            QVERIFY(combat.combatOnce(options));
            QCOMPARE(s.scene, QString("world"));
            QCOMPARE(switched, 2);
            QCOMPARE(interrupted, scenario == "resume" ? 1 : 0);
            if (scenario == "resume")
                QVERIFY(messages.contains("继续当前战斗 1/1"));
        }
        QVERIFY(!s.c.state->inCombat);
        QVERIFY(!combat.skipCombatCheck);
        QVERIFY(!combat.inLiberation);
        QVERIFY(s.input.idle());
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    WorkflowTests tests;
    try {
        return QTest::qExec(&tests, argc, argv);
    } catch (const std::exception &e) {
        qCritical().noquote() << errorText(e);
        return 1;
    }
}
#include "test_workflows.moc"
