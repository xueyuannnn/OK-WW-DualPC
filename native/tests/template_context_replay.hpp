#pragma once
#include "reference_imaging.hpp"
namespace okww::runtime::template_test {
struct Fixture {
    QTemporaryDir directory;
    QString catalog;
    const QStringList names{"plain",   "monthly_card", "illusive_realm_exit", "mouse_forte",
                            "e_forte", "skip_dialog",  "point_hcenter",       "point_vcenter"};
    Fixture() {
        if (!directory.isValid())
            fail("Test fixture directory unavailable");
        catalog = directory.filePath("coco.json");
        cv::Mat image(180, 320, CV_8UC3);
        for (int y = 0; y < image.rows; ++y)
            for (int x = 0; x < image.cols; ++x)
                image.at<cv::Vec3b>(y, x) = {uint8_t(x * 37 + y * 17), uint8_t(x * 13 + y * 41),
                                             uint8_t(x * 53 + y * 7)};
        std::vector<uint8_t> bytes;
        cv::imencode(".png", image, bytes);
        QFile file(directory.filePath("image.png"));
        if (!file.open(QIODevice::WriteOnly) || file.write(reinterpret_cast<const char *>(bytes.data()),
                                                           qint64(bytes.size())) != qint64(bytes.size()))
            fail("Test fixture write failed");
        file.close();
        QJsonArray categories, annotations;
        for (int i = 0; i < names.size(); ++i) {
            categories.append(QJsonObject{{"id", i + 1}, {"name", names[i]}});
            annotations.append(QJsonObject{{"id", i + 1},
                                           {"category_id", i + 1},
                                           {"image_id", 1},
                                           {"bbox", QJsonArray{30.5, 20.5, 19.3, 13.8}}});
        }
        writeJson(catalog, {{"images", QJsonArray{QJsonObject{{"id", 1}, {"file_name", "image.png"}}}},
                            {"categories", categories},
                            {"annotations", annotations}});
    }
};
template <class T> QJsonObject search(Fixture &f, int scene) {
    T templates(f.catalog);
    QJsonObject result;
    QJsonArray rows;
    int checkpoints = 0;
    const auto size = std::array<QSize, 4>{{{320, 180}, {640, 360}, {480, 270}, {400, 300}}}[scene % 4];
    const QString name = scene % 37 == 0 ? "missing" : f.names[(scene / 4) % f.names.size()];
    templates.setCheckpoint([&] {
        if (++checkpoints == scene % 4 + 1 && scene % 19 == 0)
            throw Cancelled();
    });
    try {
        auto pattern = templates.pattern(name, size);
        const auto box = templates.box(name, size);
        cv::Mat frame(size.height(), size.width(), CV_8UC3, cv::Scalar(25, 30, 45));
        if (pattern.channels() == 1)
            cv::cvtColor(pattern, pattern, cv::COLOR_GRAY2BGR);
        const auto destination = box.translated(scene % 3 - 1, scene % 5 - 2);
        pattern.copyTo(
            frame(cv::Rect(destination.x(), destination.y(), destination.width(), destination.height())));
        MatchOptions options;
        options.horizontal = .04;
        options.vertical = .04;
        options.rotation = scene % 43 == 0   ? std::numeric_limits<double>::quiet_NaN()
                           : scene % 13 == 0 ? 5
                                             : 0;
        options.templateTop = std::array{1., .75, 0., 1.5}[size_t(scene / 7) % 4];
        options.searchHeight = scene % 17 == 0 ? .5 : 1.;
        options.templateScale = std::array{1., .75, 1.25, 9., -1.}[size_t(scene / 31) % 5];
        options.blurSigma = std::array{0., .7, 1.3, 4., -1.}[size_t(scene / 11) % 5];
        options.grayscale = scene % 2;
        options.filter = Filter((scene / 8) % 7);
        options.targetHeight = scene % 5 == 0 ? 90 : 0;
        options.limit = scene % 11 == 0 ? 8 : 2;
        options.threshold = scene % 23 == 0 ? -.5 : .8;
        if (scene % 7 == 0)
            options.search = box.adjusted(-10, -10, 10, 10);
        const auto found = templates.find(frame, name, options);
        for (const auto &m : found)
            rows.append(QJsonObject{{"name", m.name},
                                    {"box", QJsonArray{m.box.x(), m.box.y(), m.box.width(), m.box.height()}},
                                    {"score", m.confidence}});
        if (scene % 11 == 0) {
            const auto one = templates.one(frame, name, options);
            result["one"] = bool(one);
            if (one)
                result["one_box"] =
                    QJsonArray{one->box.x(), one->box.y(), one->box.width(), one->box.height()};
        }
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
        result["type"] = typeid(e).name();
    }
    result["rows"] = rows;
    result["checkpoints"] = checkpoints;
    result["names"] = QJsonArray::fromStringList(templates.names());
    return result;
}
inline void extra(Fixture &f, int scene) {
    QJsonArray annotations, categories;
    const QStringList added{"second_plain", "third_hcenter", "fourth_vcenter"};
    for (int i = 0; i < 3; ++i) {
        categories.append(
            QJsonObject{{"id", i + 1}, {"name", scene % 7 == 0 && i == 1 ? QString() : added[i]}});
        annotations.append(QJsonObject{
            {"image_id", 1},
            {"category_id", i + 1},
            {"bbox", scene % 5 == 0 && i == 1 ? QJsonArray{1, 2} : QJsonArray{10.5, 20.5, 15, 12}}});
    }
    writeJson(
        f.directory.filePath("extra.json"),
        {{"images", QJsonArray{QJsonObject{
                        {"id", 1}, {"file_name", scene % 3 == 0 ? "images/missing.png" : "image.png"}}}},
         {"categories", categories},
         {"annotations", annotations}});
}
template <class T> QJsonObject catalog(Fixture &f) {
    T templates(f.catalog);
    QJsonObject result;
    try {
        templates.add(f.directory.filePath("extra.json"));
        templates.add(f.catalog);
    } catch (const std::exception &e) {
        result["add_error"] = errorText(e);
    }
    QJsonArray rows;
    try {
        for (const auto &name : templates.names()) {
            const auto box = templates.box(name, {320, 180});
            rows.append(QJsonArray{name, box.x(), box.y(), box.width(), box.height()});
        }
    } catch (const std::exception &e) {
        result["lookup_error"] = errorText(e);
    }
    result["names"] = QJsonArray::fromStringList(templates.names());
    result["boxes"] = rows;
    return result;
}
} // namespace okww::runtime::template_test
