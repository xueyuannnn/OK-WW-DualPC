#ifndef Q_MOC_RUN
#include "reference_common_config.hpp"
#endif
#include <QtTest>
#include <random>
using namespace okww;
namespace {
QJsonObject seeded(int scene) {
    auto c = Config::defaults();
    c.set("kmbox.host", "127.0.0.1");
    c.set("kmbox.port", 5000);
    c.set("kmbox.uuid", "deadBEEF");
    std::mt19937 rng(scene);
    const QStringList numeric{"capture.width",         "capture.height",           "capture.fps",
                              "capture.output_width",  "capture.output_height",    "capture.stale_after",
                              "capture.local_fps",     "kmbox.desktop_width",      "kmbox.desktop_height",
                              "kmbox.timeout",         "kmbox.counts_per_pixel_x", "kmbox.counts_per_pixel_y",
                              "kmbox.position_settle", "kmbox.ui_click_hold",      "kmbox.port"};
    const QList<double> numbers{0,    -1,   1,   2,   60, 60.5, 1000, 1001, 1280, 720,   1920, 1080,
                                8192, 8193, .11, .12, .1, .5,   1.0,  5,    5.01, 65535, 65536};
    const QStringList values{"card",      "desktop",      "invalid",    "DXGI",     "WIN32",
                             "DSHOW",     "MSMF",         "NV12",       "RGB4",     "kmbox_net",
                             "sendinput", "makcu_serial", "deadbeef\n", "DEADBEEF", "127.0.0.1",
                             "::1",       "window",       "monitor",    ""};
    const QStringList strings{"capture.source", "capture.local_backend", "capture.backend",
                              "capture.fourcc", "kmbox.protocol",        "kmbox.host",
                              "kmbox.uuid"};
    if (scene % 4 == 0)
        c.set("capture.source", "desktop");
    if (scene % 5 == 0)
        c.set("kmbox.protocol", "sendinput");
    if (scene % 3)
        c.set(numeric[int(rng() % numeric.size())], numbers[int(rng() % numbers.size())]);
    if (scene % 7 == 0)
        c.set(strings[int(rng() % strings.size())], values[int(rng() % values.size())]);
    c.set("kmbox.dry_run", scene % 2 != 0);
    c.set("kmbox.target_enabled", scene % 13 == 0);
    c.set("capture.local_target", QJsonObject{{"kind", values[int(rng() % values.size())]}});
    c.set("kmbox.target", QJsonObject{{"kind", values[int(rng() % values.size())]}});
    return c.json;
}
template <class C> QJsonObject validate(QJsonObject json) {
    C c{json};
    QJsonObject result;
    try {
        c.validateCapture();
        result["capture"] = true;
    } catch (const std::exception &e) {
        result["capture"] = errorText(e);
    }
    try {
        c.validateBox();
        result["box"] = true;
    } catch (const std::exception &e) {
        result["box"] = errorText(e);
    }
    result["json"] = c.json;
    return result;
}
template <class C> QJsonObject mutations(int seed) {
    C c{seeded(seed)};
    const QStringList paths{"",
                            ".",
                            "capture",
                            "capture.width",
                            "capture.fps.extra",
                            "unknown.deep.child",
                            "a.",
                            ".b",
                            "kmbox.protocol",
                            "x",
                            "capture.crop",
                            "kmbox.hid_path"};
    QJsonArray observed;
    for (int i = 0; i < paths.size(); ++i) {
        const auto path = paths[(seed + i) % paths.size()];
        const QJsonValue value = i % 3 == 0   ? QJsonValue(QJsonObject{{"nested", seed + i}})
                                 : i % 3 == 1 ? QJsonValue(QString::number(seed))
                                              : QJsonValue(i);
        c.set(path, value);
        observed.append(c.get(path));
    }
    if (seed % 3 == 0)
        c.json.remove("kmbox");
    else if (seed % 3 == 1) {
        auto box = c.json["kmbox"].toObject();
        box["protocol"] = "makcu_serial";
        box["hid_vid"] = 123;
        box["serial_port"] = "COM1";
        c.json["kmbox"] = box;
    }
    if (seed % 5 == 0)
        c.json.remove("capture");
    c.normalizeBox();
    return {{"values", observed}, {"json", c.json}};
}
} // namespace
class CommonConfigContracts final : public QObject {
    Q_OBJECT
  private slots:
    void defaults() { QCOMPARE(Config::defaults().json, common_reference::Config::defaults().json); }
    void validation_data() {
        QTest::addColumn<int>("seed");
        for (int i = 0; i < 2048; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void validation() {
        QFETCH(int, seed);
        const auto c = seeded(seed);
        QCOMPARE(validate<Config>(c), validate<common_reference::Config>(c));
    }
    void mutation_data() {
        QTest::addColumn<int>("seed");
        for (int i = 0; i < 512; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void mutation() {
        QFETCH(int, seed);
        QCOMPARE(mutations<Config>(seed), mutations<common_reference::Config>(seed));
    }
    void files_data() {
        QTest::addColumn<int>("seed");
        for (int i = 0; i < 256; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void files() {
        QFETCH(int, seed);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("配置.json");
        const auto output = seed % 7 == 0 ? directory.path() : directory.filePath("子目录/保存.json");
        const auto input = seeded(seed);
        QByteArray bytes = QJsonDocument(input).toJson();
        switch (seed % 9) {
        case 0:
            bytes.prepend("\xef\xbb\xbf");
            break;
        case 1:
            bytes = "[]";
            break;
        case 2:
            bytes = "\xef\xbb\xbf{";
            break;
        case 3:
            bytes = {};
            break;
        default:
            break;
        }
        auto outcome = [&](bool rust) {
            QJsonObject result;
            QFile initial(path);
            if (!initial.open(QIODevice::WriteOnly) || initial.write(bytes) != bytes.size())
                throw std::runtime_error("fixture write failed");
            initial.close();
            try {
                result["read"] = rust ? readJson(path) : common_reference::readJson(path);
                result["load"] = rust ? Config::load(path).json : common_reference::Config::load(path).json;
            } catch (const std::exception &e) {
                result["read-error"] = errorText(e);
            }
            try {
                if (rust)
                    writeJson(output, input);
                else
                    common_reference::writeJson(output, input);
                QFile written(output);
                if (!written.open(QIODevice::ReadOnly))
                    throw std::runtime_error("fixture read failed");
                result["written"] = QString::fromUtf8(written.readAll());
            } catch (const std::exception &e) {
                result["write-error"] = errorText(e);
            }
            return result;
        };
        QCOMPARE(outcome(true), outcome(false));
    }
};
QTEST_GUILESS_MAIN(CommonConfigContracts)
#include "test_common_config_contract.moc"
