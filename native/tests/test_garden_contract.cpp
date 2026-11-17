#include "garden_main_replay.hpp"
#include "garden_shop_replay.hpp"
#include "reference_garden_vision.hpp"
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
namespace {
QJsonArray rectangle(QRect b) { return {b.x(), b.y(), b.width(), b.height()}; }
QJsonValue line(const std::optional<TextLine> &value) {
    return value ? QJsonValue(value->json()) : QJsonValue();
}
QJsonValue match(const std::optional<Match> &value) {
    return value ? QJsonValue(QJsonObject{{"name", value->name},
                                          {"box", rectangle(value->box)},
                                          {"confidence", value->confidence}})
                 : QJsonValue();
}
struct ClockProbe : Clock {
    double now() const override { return 0; }
    void sleep(double) override {}
};
template <class GardenType> QJsonObject pipeline(int scene) {
    const auto root = QString(OKWW_SOURCE_ROOT);
    QTemporaryDir dir;
    Templates templates(root + "/assets/coco_annotations.json");
    const QStringList styles{"Any Style", "Devour Dream", "Exchange Dream", "Custom Style"};
    // Exercise legacy saved custom styles through the original profile loader.
    writeJson(dir.path() + "/GardenTask.json",
              {{"Preferred Garden Style", styles[scene % 4]}, {"Garden Custom Style", "共通之夢"}});
    Profile profile(dir.path(), root + "/native/resources/catalog.json");
    profile.set("Monthly Card Config", "Check Monthly Card", false);
    ClockProbe clock;
    ExecutionGate gate;
    Execution execution(gate, gate.start(), clock);
    Input input(Config::defaults(), {640, 360}, true, [](QJsonObject) { return QJsonObject{}; }, execution);
    cv::Mat image(360, 640, CV_8UC3, cv::Scalar(17, 23, 31));
    QVector<TextLine> labels;
    const auto badgeImage = readImage(root + "/assets/dualpc/garden_recommended.png");
    cv::Mat badge;
    cv::cvtColor(badgeImage(cv::Rect(6, 6, 35, 36)), badge, cv::COLOR_BGR2GRAY);
    cv::resize(badge, badge, {12, 12});
    cv::cvtColor(badge, badge, cv::COLOR_GRAY2BGR);
    const QStringList names{"吞噬之梦", "换取之梦", "共通之梦"};
    for (int i = 0; i < 3; ++i) {
        const int x = pixelRound(640 * (.335 + .235 * i)), y = 72;
        const int hue[]{30, 140, 100, 0};
        cv::Mat hsv(24, 36, CV_8UC3, cv::Scalar(hue[(scene + i) % 4], (scene + i) % 4 == 3 ? 0 : 180, 220)),
            color;
        cv::cvtColor(hsv, color, cv::COLOR_HSV2BGR);
        color.copyTo(image(cv::Rect(x - 44, y - 5, 36, 24)));
        labels.append({"声骸", {x - 50, y, 60, 10}, .99});
        labels.append({names[i], {int(640 * (.18 + .235 * i)), 90, 80, 12}, .99});
        if ((scene >> i) & 1)
            badge.copyTo(image(cv::Rect(x + 5, y - 30, 12, 12)));
    }
    if (scene & 8)
        cv::rectangle(image, {120, 230, 40, 15}, cv::Scalar(20, 210, 20), -1);
    QJsonArray events;
    TaskContext context(
        execution, input, templates, profile, [&] { return image; },
        [&](const cv::Mat &, QRect area, double threshold) {
            events.append(QJsonObject{{"area", rectangle(area)}, {"threshold", threshold}});
            if (area.y() < 40)
                return QVector<TextLine>{{scene & 16 ? "周度游历" : "活跃度", {100, 40, 80, 12}, .9}};
            if (area.y() > 275)
                return QVector<TextLine>{{scene & 32 ? "活跃度" : "游历值", {120, 320, 60, 12}, .9},
                                         {scene & 64 ? "6000" : "0", {140, 300, 30, 12}, .9}};
            if (area.y() > 250)
                return QVector<TextLine>{{"确认", {450, 280, 60, 12}, .99},
                                         {scene % 11 == 0 ? "other" : "跳过", {500, 280, 60, 12}, .99}};
            return labels;
        });
    context.frame = image;
    context.task = "GardenTask";
    context.info = [&](QString key, QString value) {
        events.append(QJsonObject{{"info", key}, {"value", value}});
    };
    GardenType garden(context, root);
    QJsonObject result;
    result["choice"] = match(garden.rewardChoice());
    const auto value = garden.weeklyProgress();
    result["points"] = value ? QJsonValue(*value) : QJsonValue();
    result["observation"] = garden.weeklyObservation();
    result["events"] = events;
    return result;
}
} // namespace
class GardenContracts : public QObject {
    Q_OBJECT
  private slots:
    void lifecycle_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 16; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void lifecycle() {
        QFETCH(int, scene);
        const auto expected = garden_main_test::trace<reference_garden::Garden>(scene),
                   actual = garden_main_test::trace<Garden>(scene);
        QVERIFY2(actual == expected,
                 qPrintable(
                     QString::fromUtf8(QJsonDocument(QJsonObject{{"expected", expected}, {"actual", actual}})
                                           .toJson(QJsonDocument::Compact))));
        QVERIFY2(actual["error"].toString().isEmpty(), qPrintable(actual["error"].toString()));
    }
    void shop_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 64; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void shop() {
        QFETCH(int, scene);
        const auto expected = garden_shop_test::trace<reference_garden::Garden>(scene),
                   actual = garden_shop_test::trace<Garden>(scene);
        QVERIFY2(actual == expected,
                 qPrintable(
                     QString::fromUtf8(QJsonDocument(QJsonObject{{"expected", expected}, {"actual", actual}})
                                           .toJson(QJsonDocument::Compact))));
        if (scene == 1)
            QVERIFY(actual["count"].toInt() > 0);
        if (scene == 33)
            QVERIFY(actual["receipts"].toInt() > 0);
    }
    void text_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 1024; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void text() {
        QFETCH(int, scene);
        const QStringList texts{"本周游历值已满",
                                "Weekly limit reached",
                                "0",
                                "6000",
                                "6,000/6，000",
                                "0/6000",
                                "购买",
                                "Buy 25",
                                "-40 purchase",
                                "25",
                                "已售罄",
                                "purchased",
                                "999999999999",
                                "確認",
                                "buy 0",
                                "120/6000",
                                "游历值"};
        cv::RNG random(543 + scene);
        QVector<TextLine> lines;
        for (int i = 0; i < scene % 7; ++i)
            lines.append({texts[random.uniform(0, int(texts.size()))],
                          {random.uniform(50, 500), random.uniform(270, 350), random.uniform(1, 60), 12},
                          .9});
        QCOMPARE(gardenPointsComplete(lines), reference_garden::reference_gardenPointsComplete(lines));
        const auto a = gardenPurchaseControls(lines);
        const auto b = reference_garden::reference_gardenPurchaseControls(lines);
        QCOMPARE(a.sold, b.sold);
        QCOMPARE(a.price, b.price);
        QCOMPARE(line(a.buy), line(b.buy));
        cv::Mat frame(360, 640, CV_8UC3);
        QCOMPARE(gardenWeeklyPoints(lines, frame),
                 reference_garden::reference_gardenWeeklyPoints(lines, frame));
        const QStringList styles{"Any Style",    "Devour Dream", "Exchange Dream",
                                 "Common Dream", "Custom Style", "other"};
        QJsonObject config{{"Preferred Garden Style", styles[scene % 6]},
                           {"Garden Custom Style", " 換取 之夢 "}};
        QCOMPARE(preferredGardenStyle(config), reference_garden::reference_preferredGardenStyle(config));
    }
    void pixels_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 512; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void pixels() {
        QFETCH(int, scene);
        const int widths[]{32, 64, 128, 320, 640};
        const int w = widths[scene % 5], h = w * 9 / 16;
        cv::Mat base(h + 4, w + 8, CV_8UC3);
        cv::RNG random(8123 + scene);
        random.fill(base, cv::RNG::UNIFORM, 0, 256);
        auto image = base(cv::Rect(3, 2, w, h));
        if (scene % 4 == 0) {
            cv::Mat hsv(h, w, CV_8UC3, cv::Scalar(scene % 180, scene % 256, 200));
            cv::cvtColor(hsv, image, cv::COLOR_HSV2BGR);
        }
        const QRect label(scene % 3 == 0 ? -10 : w * 3 / 4, scene % 7 == 0 ? h + 10 : h / 2, 10, 12);
        QCOMPARE(gardenRarity(image, label), reference_garden::reference_gardenRarity(image, label));
        int green = 0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const auto p = image.at<cv::Vec3b>(y, x);
                green += p[1] > 150 && p[1] > p[2] * 1.15 && p[1] > p[0] * 1.25;
            }
        QCOMPARE(rust::okww_rs_garden_pixels(image.data, w, h, image.step[0], 1), green);
        cv::Mat badge(9 + scene % 6, 8 + scene % 7, CV_8U);
        random.fill(badge, cv::RNG::UNIFORM, 0, 256);
        if (scene % 11 == 0)
            badge = {};
        const double scales[]{0, .2, .72, 1, 1.5, -1, std::numeric_limits<double>::quiet_NaN()};
        const double scale = scales[scene % 7];
        QString ae, be;
        double av = 0, bv = 0;
        const QRect area(scene % 3 == 0 ? -4 : 2, 1, w - 2, h - 2);
        try {
            av = recommendationScore(image, badge, area, scale);
        } catch (const std::exception &e) {
            ae = QString::fromUtf8(e.what());
        }
        try {
            bv = reference_garden::reference_recommendationScore(image, badge, area, scale);
        } catch (const std::exception &e) {
            be = QString::fromUtf8(e.what());
        }
        QCOMPARE(ae, be);
        QCOMPARE(av, bv);
    }
    void pipeline_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 128; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void pipeline() {
        QFETCH(int, scene);
        try {
            const auto expected = ::pipeline<reference_garden::Garden>(scene),
                       actual = ::pipeline<Garden>(scene);
            QVERIFY2(actual == expected,
                     qPrintable(QString::fromUtf8(
                         QJsonDocument(QJsonObject{{"expected", expected}, {"actual", actual}})
                             .toJson(QJsonDocument::Compact))));
        } catch (const std::exception &error) {
            QFAIL(error.what());
        }
    }
};
QTEST_GUILESS_MAIN(GardenContracts)
#include "test_garden_contract.moc"
