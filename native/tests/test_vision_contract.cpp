#include "reference_combat_vision.hpp"
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
namespace {
struct ClockProbe final : Clock {
    double time = 10;
    double now() const override { return time; }
    void sleep(double seconds) override { time += seconds; }
};
QJsonArray rect(QRect r) { return {r.x(), r.y(), r.width(), r.height()}; }
QJsonArray matches(const QVector<Match> &items) {
    QJsonArray out;
    for (const auto &m : items)
        out.append(QJsonObject{{"name", m.name}, {"box", rect(m.box)}, {"score", m.confidence}});
    return out;
}
template <class Vision> QJsonObject sceneTrace(int scene) {
    const int mode = scene / 32, variant = scene % 32;
    const QString root = QString(OKWW_SOURCE_ROOT);
    QTemporaryDir dir;
    Templates templates(root + "/assets/coco_annotations.json");
    Profile profile(dir.path(), root + "/native/resources/catalog.json");
    ClockProbe clock;
    ExecutionGate gate;
    Execution execution(gate, gate.start(), clock);
    Input input(Config::defaults(), {640, 360}, true, [](QJsonObject) { return QJsonObject{}; }, execution);
    const QSize sizes[]{{640, 360}, {960, 540}, {1600, 900}, {1280, 720}};
    const auto size = sizes[(variant / 8) % 4];
    cv::Mat image(size.height(), size.width(), CV_8UC3, cv::Scalar(17, 23, 31));
    const auto paint = [&](QString name, QString areaName = {}, int dy = 0) {
        auto pattern = templates.pattern(name, size);
        if (pattern.channels() == 1)
            cv::cvtColor(pattern, pattern, cv::COLOR_GRAY2BGR);
        auto area = templates.box(areaName.isEmpty() ? name : areaName, size);
        const int x = area.x(), y = area.y() + dy;
        if (x >= 0 && y >= 0 && x + pattern.cols <= image.cols && y + pattern.rows <= image.rows)
            pattern.copyTo(image(cv::Rect(x, y, pattern.cols, pattern.rows)));
    };
    const int hudMask = variant % 8;
    if (mode <= 1)
        for (int i = 0; i < 3; ++i)
            if (hudMask & (1 << i))
                paint(QString("char_%1_text").arg(i + 1));
    if (mode == 2 && variant % 4 < 3) {
        paint(variant % 2 ? "char_yinlin" : "char_verina", QString("box_char_%1").arg(variant % 3 + 1));
        if (variant & 4)
            cv::GaussianBlur(image, image, {3, 3}, .8);
    }
    if (mode == 3) {
        const auto suffix = size.width() == 1600 ? "_169" : "";
        if (variant % 5 != 4)
            paint(QString(variant & 1 ? "has_target" : "no_target") + suffix,
                  variant % 5 == 2   ? "target_box_short"
                  : variant % 5 == 3 ? "box_target_enemy_long"
                                     : "",
                  variant % 5 == 1 ? templates.box("has_target" + QString(suffix), size).height() / 2 : 0);
        if (variant & 8)
            cv::GaussianBlur(image, image, {3, 3}, .8);
    }
    if (mode == 5) {
        const int y = int(size.height() * (variant & 1 ? .05 : .35));
        const int w = int(size.width() * (variant & 1 ? .22 : .1)),
                  h = std::max(3, int(size.height() * .009));
        cv::rectangle(image, {size.width() / 3, y, w, h},
                      variant & 2 ? cv::Scalar(60, 65, 190) : cv::Scalar(20, 20, 240), -1);
    }
    if (mode == 6) {
        if (variant & 1)
            paint("mouse_forte");
        if (variant & 2)
            paint("e_forte");
        const auto area = templates.box("box_char_1", size);
        cv::rectangle(image, {area.x(), area.y(), std::min(area.width(), variant % 10 + 1), 1},
                      cv::Scalar::all(variant & 4 ? 230 : 250), -1);
    }
    QJsonArray events;
    int reads = 0;
    TaskContext context(
        execution, input, templates, profile, [&] { return image; },
        [&](const cv::Mat &frame, QRect area, double threshold) {
            ++reads;
            events.append(QJsonObject{{"read", reads},
                                      {"size", QJsonArray{frame.cols, frame.rows}},
                                      {"area", rect(area)},
                                      {"threshold", threshold}});
            if (variant == 29)
                throw std::runtime_error("OCR unavailable");
            QVector<TextLine> lines;
            if (mode == 1) {
                const QStringList values{"12.3", "0", "61", "2·5", "8, 4", "x", "1", "09.9"};
                for (int i = 0; i < variant % 5; ++i) {
                    const bool scaled = frame.cols != image.cols;
                    const auto x = size.width() * (.855 + (i % 3) * .035),
                               y = size.height() * (variant & 4 ? .89 : .88);
                    QRect b(int(x) - 2, int(y) - 2, 4, 4);
                    if (scaled)
                        b = QRect((b.x() - area.x()) * 2, (b.y() - area.y()) * 2, b.width() * 2,
                                  b.height() * 2);
                    lines.append({values[(variant + i) % values.size()], b, .9});
                }
            } else if (mode == 4) {
                const QStringList titles{"选择复苏物品", "Unconscious",         "挑战成功",
                                         "挑战失败",     "Challenge Completed", "other",
                                         "失去意识",     "Challenge Failed"};
                const QStringList buttons{"复苏",  "Confirm", "确认成绩",       "再次挑战", "Return to Tower",
                                          "other", "确认",    "Challenge Again"};
                lines.append(
                    {area.y() < size.height() * .5 ? titles[variant % 8] : buttons[(variant / 2) % 8],
                     {area.x(), area.y(), 20, 10},
                     .9});
            } else if (mode == 5) {
                const QStringList values{"Lv.88", "Lv.", "88", "xLv.88", "Lv.9999", "Lv· 8", "lv", "8"};
                const bool scaled = frame.cols != image.cols;
                const int factor = scaled ? 2 : 1, ox = scaled ? 0 : area.x(), oy = scaled ? 0 : area.y();
                lines.append(
                    {values[variant % 8], {ox + 4 * factor, oy + 4 * factor, 18 * factor, 8 * factor}, .9});
                if (variant & 8)
                    lines.append({"88", {ox + 25 * factor, oy + 4 * factor, 12 * factor, 8 * factor}, .9});
            }
            return lines;
        });
    context.frame = image;
    context.frameVersion = (uint64_t(1) << 60) + variant;
    context.task = "AutoCombatTask";
    const auto definition = readJson(root + "/native/resources/characters.json");
    auto entries = definition["characters"].toArray();
    QJsonArray chosen{entries[1], entries[2]};
    if (mode == 7) {
        if (variant & 1)
            chosen.append(QJsonObject{{"class", ""}, {"templates", QJsonArray{"char_yinlin"}}});
        if (variant & 2)
            chosen.append(QJsonObject{{"class", "x"}, {"templates", QJsonArray{}}});
    }
    QFile catalog(dir.path() + "/characters.json");
    catalog.open(QIODevice::WriteOnly);
    catalog.write(QJsonDocument(QJsonObject{{"characters", chosen}}).toJson());
    catalog.close();
    QJsonObject result;
    try {
        Vision vision(context, catalog.fileName());
        if (mode == 0) {
            for (int step = 0; step < 3; ++step) {
                const auto hud = vision.party();
                result[QString::number(step)] = QJsonArray{hud.visible, hud.current, hud.size};
                if (step == 0)
                    ++context.frameVersion;
                if (step == 1)
                    vision.reset();
            }
        }
        if (mode == 1) {
            FreezeTimeline timeline;
            timeline.add(9, 7, 1, .1);
            for (int step = 0; step < 4; ++step) {
                vision.refreshCooldowns(variant % 4 - 1);
                clock.time += step == 0 ? .05 : .31;
                if (step > 1)
                    ++context.frameVersion;
                QJsonArray cds;
                for (auto it = vision.cooldowns().begin(); it != vision.cooldowns().end(); ++it) {
                    QJsonArray seconds, observed;
                    for (int skill = 0; skill < 3; ++skill) {
                        seconds.append(it->seconds[skill]);
                        observed.append(it->observedAt[skill]);
                    }
                    cds.append(QJsonObject{{"index", it.key()},
                                           {"time", it->timestamp},
                                           {"fallback", it->fallbackAt},
                                           {"seconds", seconds},
                                           {"observed", observed}});
                }
                result[QString::number(step)] = cds;
                QJsonArray left;
                for (int skill = 0; skill < 4; ++skill)
                    left.append(vision.cooldown(Skill(skill), variant % 4 - 1, timeline, clock.time));
                result["left" + QString::number(step)] = left;
            }
            vision.reset();
            result["reset"] = vision.cooldowns().isEmpty();
        }
        if (mode == 2) {
            CharacterState previous;
            previous.name = variant & 1 ? "char_yinlin" : "char_verina";
            previous.confidence = variant & 2 ? .93 : .92;
            const auto identity = vision.character(variant % 5 - 1, variant & 8 ? &previous : nullptr);
            if (identity)
                result["identity"] = QJsonObject{{"class", identity->definition.className},
                                                 {"canonical", identity->definition.canonical},
                                                 {"confidence", identity->confidence}};
            else
                result["identity"] = QJsonValue();
        }
        if (mode == 3) {
            const auto observed = vision.targetObservation((variant & 2) != 0, (variant & 4) != 0);
            result["target"] = QJsonArray{observed.matched, observed.locked, observed.extended};
            result["has"] = vision.hasTarget((variant & 2) != 0, (variant & 4) != 0);
        }
        if (mode == 4) {
            result["death"] = vision.deathDialog();
            const auto challenge = vision.challengeResult();
            result["challenge"] = challenge ? QJsonValue(*challenge) : QJsonValue();
        }
        if (mode == 5) {
            result["normal"] = vision.healthBar(false);
            result["combat"] = vision.healthBar(true);
            result["shifted"] = vision.shiftedHealthBar();
        }
        if (mode == 6) {
            for (int kind = 0; kind < 3; ++kind)
                result[QString::number(kind)] = vision.forte(Forte(kind));
            result["white"] = vision.white("box_char_1");
        }
        result["error"] = "";
    } catch (const std::exception &e) {
        result["error"] = QString::fromUtf8(e.what());
    }
    result["events"] = events;
    result["time"] = clock.time;
    return result;
}
} // namespace
class VisionContracts : public QObject {
    Q_OBJECT
  private slots:
    void pipeline_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 256; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void pipeline() {
        QFETCH(int, scene);
        const auto expected = sceneTrace<reference_vision::CombatVision>(scene),
                   actual = sceneTrace<CombatVision>(scene);
        QVERIFY2(actual == expected,
                 qPrintable(
                     QString::fromUtf8(QJsonDocument(QJsonObject{{"expected", expected}, {"actual", actual}})
                                           .toJson(QJsonDocument::Compact))));
    }
    void nativeGauge_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 512; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void nativeGauge() {
        QFETCH(int, scene);
        const int widths[]{0, 8, 63, 64, 65, 128, 256, 527, 546, 736, 1024};
        const int w = widths[scene % 11], h = scene % 13 + 1;
        cv::Mat base(h + 2, w + 4, CV_8UC3, cv::Scalar());
        cv::Mat image = w ? base(cv::Rect(2, 1, w, h)) : cv::Mat{};
        cv::RNG random(1234 + scene);
        if (!image.empty()) {
            random.fill(image, cv::RNG::UNIFORM, 0, 256);
            for (int x = 0; x < w; ++x)
                if ((x / (scene % 8 + 1)) % 2)
                    image.col(x).setTo(cv::Scalar::all(255));
            if (scene % 7 == 0)
                image.setTo(cv::Scalar::all(scene & 1 ? 255 : 0));
        }
        const ColorRange color{{244, 244, 244}, {255, 255, 255}};
        for (int segments = -1; segments < 8; ++segments)
            for (double amplitude : {0., 25., 1000.})
                QCOMPARE(segmentedGaugeValue(image, color, segments, scene % 15, scene % 15 + 2, amplitude),
                         reference_vision::frozen_segmentedGaugeValue(image, color, segments, scene % 15,
                                                                      scene % 15 + 2, amplitude));
    }
    void bars_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 384; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void bars() {
        QFETCH(int, scene);
        const int widths[]{320, 640, 960, 1280, 1600, 1920};
        const int w = widths[scene % 6], h = w * 9 / 16;
        cv::Mat base(h + 3, w + 3, CV_8UC3, cv::Scalar(10, 20, 30));
        auto image = base(cv::Rect(1, 1, w, h));
        cv::RNG random(5678 + scene);
        for (int i = 0; i < 12; ++i) {
            const int x = random.uniform(0, w - 20), y = random.uniform(0, h - 30),
                      bw = std::min(w - x, random.uniform(10, std::max(11, w / 5))),
                      bh = random.uniform(2, 20);
            cv::rectangle(image, {x, y, bw, bh},
                          cv::Scalar(random.uniform(0, 80), random.uniform(0, 90), random.uniform(75, 256)),
                          -1);
            if (i % 3 == 0)
                cv::rectangle(image, {x + 3, y + 1, 3, 1}, cv::Scalar::all(255), -1);
        }
        QCOMPARE(matches(shiftedBarCandidates(image)),
                 matches(reference_vision::frozen_shiftedBarCandidates(image)));
        const ColorRange color{{0, 0, 120}, {85, 100, 255}};
        const std::optional<QRect> area =
            scene & 1 ? std::optional<QRect>({-4, scene % h, w / 2, h / 2}) : std::nullopt;
        for (double fill : {.5, .8, .95})
            QCOMPARE(matches(colorRectangles(image, color, scene % 50, scene % 5 + 1, scene & 2 ? -1 : 15,
                                             area, fill)),
                     matches(reference_vision::frozen_colorRectangles(image, color, scene % 50, scene % 5 + 1,
                                                                      scene & 2 ? -1 : 15, area, fill)));
    }
    void dialog_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 128; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void dialog() {
        QFETCH(int, scene);
        const QStringList values{"Windows 将根据你所输入的名称",
                                 "Type the name of a program",
                                 "打开(O):",
                                 "Open:",
                                 "浏览(B)",
                                 "Browse",
                                 "nothing"};
        QVector<TextLine> lines;
        for (int i = 0; i < 7; ++i)
            if (scene & (1 << i))
                lines.append({values[i], {i * 20, 10, 20, 10}, .9});
        QCOMPARE(systemRunDialogConfirmed(lines), reference_vision::frozen_systemRunDialogConfirmed(lines));
    }
};
QTEST_GUILESS_MAIN(VisionContracts)
#include "test_vision_contract.moc"
