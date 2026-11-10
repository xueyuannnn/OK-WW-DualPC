#include "template_store.hpp"
#include <QPainter>
namespace okww::runtime {
namespace {
int nextId(const QJsonArray &items) {
    int result = 1;
    for (auto item : items)
        result = std::max(result, item.toObject()["id"].toInt() + 1);
    return result;
}
QJsonArray withoutImage(const QJsonArray &items, int image, const QString &field) {
    QJsonArray result;
    for (auto item : items)
        if (item.toObject()[field].toInt() != image)
            result.append(item);
    return result;
}
} // namespace
QJsonObject TemplateStore::document() const {
    return QFile::exists(cocoPath()) ? readJson(cocoPath())
                                     : QJsonObject{{"images", QJsonArray{}},
                                                   {"annotations", QJsonArray{}},
                                                   {"categories", QJsonArray{}}};
}
QString TemplateStore::imagePath(const QString &name) const {
    if (name.isEmpty() || QFileInfo(name).fileName() != name || name.contains(':') || name.contains('\\'))
        fail("模板图片文件名无效");
    return directory_ + '/' + name;
}
QJsonArray TemplateStore::images() const {
    const auto data = document();
    QMap<int, QString> labels;
    for (auto item : data["categories"].toArray())
        labels[item.toObject()["id"].toInt()] = item.toObject()["name"].toString();
    QJsonArray result;
    for (auto value : data["images"].toArray()) {
        auto image = value.toObject();
        image["path"] = imagePath(image["file_name"].toString());
        QStringList names;
        for (auto annotation : data["annotations"].toArray()) {
            const auto a = annotation.toObject();
            if (a["image_id"] == image["id"])
                names << labels.value(a["category_id"].toInt());
        }
        image["labels"] = names.join(", ");
        result.append(image);
    }
    return result;
}
QJsonArray TemplateStore::annotations(int image) const {
    const auto data = document();
    QMap<int, QString> names;
    for (auto item : data["categories"].toArray())
        names[item.toObject()["id"].toInt()] = item.toObject()["name"].toString();
    QJsonArray result;
    for (auto value : data["annotations"].toArray()) {
        auto region = value.toObject();
        if (region["image_id"].toInt() == image) {
            region["name"] = names.value(region["category_id"].toInt());
            result.append(region);
        }
    }
    return result;
}
int TemplateStore::add(const QImage &image) {
    if (image.isNull() || image.width() > 16384 || image.height() > 16384)
        fail("模板图片为空或尺寸过大");
    auto data = document();
    auto images = data["images"].toArray();
    const int id = nextId(images);
    const QString name = QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png";
    QDir().mkpath(directory_);
    QSaveFile file(imagePath(name));
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit())
        fail("模板截图保存失败");
    images.append(
        QJsonObject{{"id", id}, {"file_name", name}, {"width", image.width()}, {"height", image.height()}});
    data["images"] = images;
    try {
        writeJson(cocoPath(), data);
    } catch (...) {
        QFile::remove(imagePath(name));
        throw;
    }
    return id;
}
void TemplateStore::saveAnnotations(int image, const QJsonArray &regions) {
    auto data = document();
    QJsonObject source;
    for (auto entry : data["images"].toArray())
        if (entry.toObject()["id"].toInt() == image)
            source = entry.toObject();
    if (source.isEmpty())
        fail("模板图片已不存在");
    auto annotations = withoutImage(data["annotations"].toArray(), image, "image_id");
    auto categories = data["categories"].toArray();
    QMap<QString, int> labels;
    QSet<QString> used;
    for (auto entry : categories)
        labels[entry.toObject()["name"].toString()] = entry.toObject()["id"].toInt();
    for (auto entry : annotations)
        used.insert(labels.key(entry.toObject()["category_id"].toInt()));
    for (auto entry : regions) {
        const auto region = entry.toObject();
        const auto name = region["name"].toString().trimmed();
        const auto b = region["bbox"].toArray();
        if (name.isEmpty() || used.contains(name) || b.size() != 4)
            fail("模板名称不能为空或重复，每个名称对应一个选区");
        for (auto coordinate : b)
            if (!coordinate.isDouble() || !std::isfinite(coordinate.toDouble()) ||
                coordinate.toDouble() != coordinate.toInt())
                fail("模板选区必须使用整数像素");
        const int x = b[0].toInt(), y = b[1].toInt(), w = b[2].toInt(), h = b[3].toInt();
        if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > source["width"].toInt() ||
            y + h > source["height"].toInt())
            fail("模板选区超出图片范围");
        used.insert(name);
        if (!labels.contains(name)) {
            const int id = nextId(categories);
            labels[name] = id;
            categories.append(QJsonObject{{"id", id}, {"name", name}, {"supercategory", ""}});
        }
        annotations.append(QJsonObject{{"id", nextId(annotations)},
                                       {"image_id", image},
                                       {"category_id", labels[name]},
                                       {"bbox", b},
                                       {"area", w * h},
                                       {"iscrowd", 0}});
    }
    data["annotations"] = annotations;
    data["categories"] = categories;
    writeJson(cocoPath(), data);
}
void TemplateStore::remove(int image) {
    auto data = document();
    QString path;
    for (auto value : data["images"].toArray())
        if (value.toObject()["id"].toInt() == image)
            path = imagePath(value.toObject()["file_name"].toString());
    if (path.isEmpty())
        fail("模板图片已不存在");
    // Keep the original image in profile trash in case a label was deleted by mistake.
    QDir().mkpath(directory_ + "/.trash");
    const auto trash = directory_ + "/.trash/" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png";
    if (QFile::exists(path) && !QFile::rename(path, trash))
        fail("模板图片正在使用，未删除标注");
    data["images"] = withoutImage(data["images"].toArray(), image, "id");
    data["annotations"] = withoutImage(data["annotations"].toArray(), image, "image_id");
    try {
        writeJson(cocoPath(), data);
    } catch (...) {
        QFile::rename(trash, path);
        throw;
    }
}
void TemplateStore::publish(const QString &assets, const QString &enumFile) const {
    auto data = document();
    if (data["annotations"].toArray().isEmpty())
        fail("尚未添加模板标注");
    QDir().mkpath(assets + "/images");
    QJsonArray images;
    for (auto value : data["images"].toArray()) {
        auto entry = value.toObject();
        const auto regions = annotations(entry["id"].toInt());
        if (regions.isEmpty())
            continue;
        const QImage source(imagePath(entry["file_name"].toString()));
        if (source.isNull())
            fail("模板图片缺失，未更新脚本资源");
        // Retain the original coordinates, keeping only labelled pixels.
        QImage image(source.size(), QImage::Format_RGB888);
        image.fill(Qt::black);
        QPainter painter(&image);
        for (auto value : regions) {
            const auto b = value.toObject()["bbox"].toArray();
            const QRect r(b[0].toInt(), b[1].toInt(), b[2].toInt(), b[3].toInt());
            painter.drawImage(r, source, r);
        }
        painter.end();
        image.setText("ok_compressed", "1");
        const auto relative = "images/" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png";
        QSaveFile output(assets + '/' + relative);
        if (!output.open(QIODevice::WriteOnly) || !image.save(&output, "PNG") || !output.commit())
            fail("脚本模板图片保存失败");
        entry["file_name"] = relative;
        images.append(entry);
    }
    data["images"] = images;
    writeJson(assets + "/coco_annotations.json", data);
    if (!enumFile.isEmpty()) {
        QString text = "from enum import Enum\n\n\nclass LabelEnum(str, Enum):\n";
        QSet<QString> names;
        for (auto value : data["categories"].toArray()) {
            const auto label = value.toObject()["name"].toString();
            QString name = label;
            name.replace(QRegularExpression("[^\\p{L}\\p{N}_]"), "_");
            static const QStringList keywords =
                QString("False None True and as assert async await break class continue def del elif else "
                        "except finally for from global if import in is lambda nonlocal not or pass raise "
                        "return try while with yield")
                    .split(' ');
            if (name.isEmpty() || name.front().isDigit() || keywords.contains(name))
                name.prepend("label_");
            if (names.contains(name))
                name += '_' + QString::number(value.toObject()["id"].toInt());
            names.insert(name);
            auto literal = QJsonDocument(QJsonArray{label}).toJson(QJsonDocument::Compact);
            text += "    " + name + " = " + QString::fromUtf8(literal.mid(1, literal.size() - 2)) + '\n';
        }
        QSaveFile file(enumFile);
        const auto bytes = text.toUtf8();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            fail("模板资源已保存，但名称枚举写入失败");
    }
}
} // namespace okww::runtime
