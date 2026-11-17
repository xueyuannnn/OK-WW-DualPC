#include "reference_template_store.hpp"
#include <QCryptographicHash>
#include <QDirIterator>
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
namespace {
QJsonValue normalized(QJsonValue value, const QString &root) {
    if (value.isString()) {
        auto text = value.toString();
        text.replace(root, "<fixture>");
        text.replace(QRegularExpression("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}"),
                     "<uuid>");
        return text;
    }
    if (value.isArray()) {
        QJsonArray out;
        for (auto v : value.toArray())
            out.append(normalized(v, root));
        return out;
    }
    if (value.isObject()) {
        auto out = value.toObject();
        for (auto i = out.begin(); i != out.end(); ++i)
            i.value() = normalized(i.value(), root);
        return out;
    }
    return value;
}
QJsonArray files(const QString &root) {
    QStringList entries;
    QDirIterator walk(root, QDir::Files, QDirIterator::Subdirectories);
    while (walk.hasNext()) {
        const auto path = walk.next();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            fail("Fixture read failed");
        auto bytes = file.readAll();
        if (path.endsWith(".json"))
            bytes = QJsonDocument(normalized(QJsonDocument::fromJson(bytes).object(), root).toObject())
                        .toJson(QJsonDocument::Compact);
        const auto relative = normalized(QDir(root).relativeFilePath(path), root).toString();
        entries << relative + ":" +
                       QString::fromLatin1(
                           QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    }
    entries.sort();
    return QJsonArray::fromStringList(entries);
}
template <class T> QJsonObject replay(int scene, bool rollback) {
    QTemporaryDir fixture;
    const auto directory = fixture.path() + "/templates", assets = fixture.path() + "/assets";
    QDir().mkpath(directory);
    QImage image(16, 12, scene % 2 ? QImage::Format_ARGB32 : QImage::Format_RGB888);
    image.fill(QColor(37, 51, scene % 255, 200));
    image.setPixelColor(4, 4, QColor(133, 97, 31));
    image.save(directory + "/seed.png");
    image.save(directory + "/second.png");
    QJsonArray images{QJsonObject{{"id", 1}, {"file_name", "seed.png"}, {"width", 16}, {"height", 12}},
                      QJsonObject{{"id", scene % 17 ? 2 : 1},
                                  {"file_name", scene % 29 ? "second.png" : "../bad.png"},
                                  {"width", 16},
                                  {"height", 12}}};
    if (scene % 31 == 0)
        images.append(QJsonObject{{"id", INT_MAX}, {"file_name", "seed.png"}, {"width", 16}, {"height", 12}});
    const QStringList labels{"first", "class", "1-name", "中文", "a-b", "a b", "", "False", "λ", "quote\"\\"};
    QJsonArray categories;
    for (int i = 0; i < 1 + scene % labels.size(); ++i)
        categories.append(QJsonObject{{"id", i + 1}, {"name", labels[i]}});
    QJsonArray annotations{
        QJsonObject{{"id", 1}, {"image_id", 2}, {"category_id", 1}, {"bbox", QJsonArray{1, 1, 2, 2}}}};
    if (scene % 7 == 0)
        annotations.append(
            QJsonObject{{"id", 3}, {"image_id", 1}, {"category_id", 2}, {"bbox", QJsonArray{2, 2, 3, 4}}});
    writeJson(directory + "/coco_annotations.json",
              {{"images", images}, {"annotations", annotations}, {"categories", categories}});
    T store(directory);
    QJsonArray trace;
    auto step = [&](QString name, auto fn) {
        QJsonObject event{{"step", name}};
        try {
            event["result"] = fn();
        } catch (const std::exception &e) {
            event["error"] = errorText(e);
        }
        event["files"] = files(fixture.path());
        trace.append(normalized(event, fixture.path()));
    };
    step("images", [&] { return QJsonValue(store.images()); });
    step("annotations", [&] { return QJsonValue(store.annotations(scene % 3)); });
    HANDLE held = INVALID_HANDLE_VALUE;
    if (rollback) {
        const auto path = directory + "/coco_annotations.json";
        held = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (held == INVALID_HANDLE_VALUE)
            fail("Fixture lock failed");
    }
    int added = -1;
    step("add", [&] {
        QImage source = scene % 41 == 0   ? QImage{}
                        : scene % 43 == 0 ? QImage(16385, 1, QImage::Format_RGB888)
                                          : image;
        added = store.add(source);
        return QJsonValue(added);
    });
    QJsonArray box{1, 1, 4, 5};
    switch (scene % 13) {
    case 0:
        box = {-1, 1, 4, 5};
        break;
    case 1:
        box = {1.25, 1, 4, 5};
        break;
    case 2:
        box = {true, 1, 4, 5};
        break;
    case 3:
        box = {1, 1, 16, 12};
        break;
    case 4:
        box = {1, 1, 4};
        break;
    case 5:
        box = {1, 1, 0, 5};
        break;
    case 6:
        box = {1, 1, 2147483647, 5};
        break;
    case 7:
        box = {1, 1, 4, 2147483647};
        break;
    case 8:
        box = {1, 1, 1e30, 5};
        break;
    default:
        break;
    }
    QJsonArray regions{QJsonObject{{"name", scene % 11 ? "new label" : "first"}, {"bbox", box}}};
    if (scene % 5 == 0)
        regions.append(regions.first());
    if (scene % 23 == 0)
        regions = {};
    step("save", [&] {
        store.saveAnnotations(scene % 19 ? 1 : -1, regions);
        return QJsonValue(true);
    });
    step("remove", [&] {
        store.remove(scene % 13 ? 1 : -1);
        return QJsonValue(true);
    });
    if (held != INVALID_HANDLE_VALUE)
        CloseHandle(held);
    step("publish", [&] {
        store.publish(assets, scene % 3 ? fixture.path() + "/LabelEnum.py" : "");
        return QJsonValue(true);
    });
    step("added-annotations", [&] { return QJsonValue(store.annotations(added)); });
    return {{"trace", trace}};
}
} // namespace
class TemplateStoreContracts final : public QObject {
    Q_OBJECT
  private slots:
    void workflow_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 512; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void workflow() {
        QFETCH(int, scene);
        QCOMPARE(replay<TemplateStore>(scene, false),
                 replay<template_store_reference::OriginalTemplateStore>(scene, false));
    }
    void rollback_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 64; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void rollback() {
        QFETCH(int, scene);
        QCOMPARE(replay<TemplateStore>(scene, true),
                 replay<template_store_reference::OriginalTemplateStore>(scene, true));
    }
};
QTEST_GUILESS_MAIN(TemplateStoreContracts)
#include "test_template_store_contract.moc"
