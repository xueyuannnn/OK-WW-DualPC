#include "template_store.hpp"
#include "workflow_abi.hpp"
#include <QPainter>
namespace okww::runtime {
namespace {
[[noreturn]] void storeFailure(int code) {
    static const QStringList messages{"模板图片文件名无效",
                                      "模板图片为空或尺寸过大",
                                      "模板截图保存失败",
                                      "模板图片已不存在",
                                      "模板名称不能为空或重复，每个名称对应一个选区",
                                      "模板选区必须使用整数像素",
                                      "模板选区超出图片范围",
                                      "模板图片正在使用，未删除标注",
                                      "尚未添加模板标注",
                                      "模板图片缺失，未更新脚本资源",
                                      "脚本模板图片保存失败",
                                      "模板资源已保存，但名称枚举写入失败"};
    fail(messages.at(code));
}
int nextId(const QJsonArray &items) {
    return int(rust::workflowRun(rust::WorkflowMethod::StoreNextId, {},
                                 [&](rust::WorkflowOp op, const double *a, double *out) {
                                     switch (op) {
                                     case rust::WorkflowOp::StoreItemCount:
                                         out[0] = double(items.size());
                                         break;
                                     case rust::WorkflowOp::StoreItemId:
                                         out[0] = items.at(int(a[0])).toObject()["id"].toInt();
                                         break;
                                     default:
                                         fail("Rust 模板序号宿主无效");
                                     }
                                 })[0]);
}
QJsonArray withoutImage(const QJsonArray &items, int image, const QString &field) {
    QJsonArray result;
    rust::workflowRun(rust::WorkflowMethod::StoreWithoutImage, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::StoreItemCount:
                              out[0] = double(items.size());
                              break;
                          case rust::WorkflowOp::StoreItemMatchesImage:
                              out[0] = items.at(int(a[0])).toObject()[field].toInt() == image;
                              break;
                          case rust::WorkflowOp::StoreAppendItem:
                              result.append(items.at(int(a[0])));
                              break;
                          default:
                              fail("Rust 模板筛选宿主无效");
                          }
                      });
    return result;
}
} // namespace
QJsonObject TemplateStore::rustStore(int method, int image, const QJsonArray &regions, QString path,
                                     QString enumFile, const QImage *inputImage) const {
    QJsonObject data, result, entry, annotation, source, region;
    QJsonArray images, categories, annotations, selectedRegions, box, output;
    QMap<int, QString> indexed;
    QMap<QString, int> labels;
    QSet<QString> used;
    QString name, removal, trash, relative, label, text;
    QStringList names;
    int id = 0;
    QByteArray bytes;
    QImage sourcePixels, masked;
    std::unique_ptr<QSaveFile> file, publishFile, enumOutput;
    std::unique_ptr<QPainter> painter;
    rust::workflowRun(
        rust::WorkflowMethod(method), {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::StoreFailure:
                storeFailure(int(a[0]));
                break;
            case rust::WorkflowOp::StoreDocumentExists:
                out[0] = QFile::exists(cocoPath());
                break;
            case rust::WorkflowOp::StoreReadDocument:
                result = readJson(cocoPath());
                break;
            case rust::WorkflowOp::StoreEmptyDocument:
                result = {
                    {"images", QJsonArray{}}, {"annotations", QJsonArray{}}, {"categories", QJsonArray{}}};
                break;
            case rust::WorkflowOp::StoreNameEmpty:
                out[0] = path.isEmpty();
                break;
            case rust::WorkflowOp::StoreNameIsFilename:
                out[0] = QFileInfo(path).fileName() == path;
                break;
            case rust::WorkflowOp::StoreNameHasColon:
                out[0] = path.contains(':');
                break;
            case rust::WorkflowOp::StoreNameHasBackslash:
                out[0] = path.contains('\\');
                break;
            case rust::WorkflowOp::StoreReturnImagePath:
                result["path"] = directory_ + '/' + path;
                break;
            case rust::WorkflowOp::StoreLoadDocument:
                data = document();
                break;
            case rust::WorkflowOp::StoreCategoryCount:
                out[0] = double(std::as_const(data)["categories"].toArray().size());
                break;
            case rust::WorkflowOp::StoreIndexCategory: {
                const auto item = std::as_const(data)["categories"].toArray().at(int(a[0])).toObject();
                indexed[item["id"].toInt()] = item["name"].toString();
                break;
            }
            case rust::WorkflowOp::StoreImageCount:
                out[0] = double(std::as_const(data)["images"].toArray().size());
                break;
            case rust::WorkflowOp::StoreSelectImage:
                entry = std::as_const(data)["images"].toArray().at(int(a[0])).toObject();
                break;
            case rust::WorkflowOp::StoreImageSetPath:
                entry["path"] = imagePath(std::as_const(entry)["file_name"].toString());
                break;
            case rust::WorkflowOp::StoreClearNames:
                names.clear();
                break;
            case rust::WorkflowOp::StoreAnnotationCount:
                out[0] = double(std::as_const(data)["annotations"].toArray().size());
                break;
            case rust::WorkflowOp::StoreSelectAnnotation:
                annotation = std::as_const(data)["annotations"].toArray().at(int(a[0])).toObject();
                break;
            case rust::WorkflowOp::StoreAnnotationImageEqual:
                out[0] = std::as_const(annotation)["image_id"] == std::as_const(entry)["id"];
                break;
            case rust::WorkflowOp::StoreAppendLabel:
                names << indexed.value(std::as_const(annotation)["category_id"].toInt());
                break;
            case rust::WorkflowOp::StoreImageSetLabels:
                entry["labels"] = names.join(", ");
                break;
            case rust::WorkflowOp::StoreAppendImageResult:
                output.append(entry);
                break;
            case rust::WorkflowOp::StoreAnnotationMatchesId:
                out[0] = std::as_const(annotation)["image_id"].toInt() == image;
                break;
            case rust::WorkflowOp::StoreRegionSetName:
                annotation["name"] = indexed.value(std::as_const(annotation)["category_id"].toInt());
                break;
            case rust::WorkflowOp::StoreAppendRegionResult:
                output.append(annotation);
                break;
            case rust::WorkflowOp::StoreInputImageNull:
                out[0] = inputImage->isNull();
                break;
            case rust::WorkflowOp::StoreInputImageSize:
                out[0] = inputImage->width();
                out[1] = inputImage->height();
                break;
            case rust::WorkflowOp::StoreReadImages:
                images = std::as_const(data)["images"].toArray();
                break;
            case rust::WorkflowOp::StoreNextImageId:
                id = nextId(images);
                break;
            case rust::WorkflowOp::StoreNewImageName:
                name = QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png";
                break;
            case rust::WorkflowOp::StoreCreateDirectory:
                QDir().mkpath(directory_);
                break;
            case rust::WorkflowOp::StoreOpenImageFile:
                file = std::make_unique<QSaveFile>(imagePath(name));
                break;
            case rust::WorkflowOp::StoreImageFileOpen:
                out[0] = file->open(QIODevice::WriteOnly);
                break;
            case rust::WorkflowOp::StoreSaveInputImage:
                out[0] = inputImage->save(file.get(), "PNG");
                break;
            case rust::WorkflowOp::StoreCommitImage:
                out[0] = file->commit();
                break;
            case rust::WorkflowOp::StoreAppendNewImage:
                images.append(QJsonObject{{"id", id},
                                          {"file_name", name},
                                          {"width", inputImage->width()},
                                          {"height", inputImage->height()}});
                break;
            case rust::WorkflowOp::StoreSetImages:
                data["images"] = images;
                break;
            case rust::WorkflowOp::StoreWriteDocument:
                writeJson(cocoPath(), data);
                break;
            case rust::WorkflowOp::StoreRemoveImageFile:
                QFile::remove(imagePath(name));
                break;
            case rust::WorkflowOp::StoreReturnImageId:
                result["id"] = id;
                break;
            case rust::WorkflowOp::StoreImageMatchesId:
                out[0] =
                    std::as_const(data)["images"].toArray().at(int(a[0])).toObject()["id"].toInt() == image;
                break;
            case rust::WorkflowOp::StoreSelectSourceImage:
                source = std::as_const(data)["images"].toArray().at(int(a[0])).toObject();
                break;
            case rust::WorkflowOp::StoreSourceEmpty:
                out[0] = source.isEmpty();
                break;
            case rust::WorkflowOp::StoreExcludeAnnotations:
                annotations = withoutImage(std::as_const(data)["annotations"].toArray(), image, "image_id");
                break;
            case rust::WorkflowOp::StoreReadCategories:
                categories = std::as_const(data)["categories"].toArray();
                break;
            case rust::WorkflowOp::StoreWorkingCategoryCount:
                out[0] = double(categories.size());
                break;
            case rust::WorkflowOp::StoreIndexLabel: {
                const auto v = categories.at(int(a[0])).toObject();
                labels[v["name"].toString()] = v["id"].toInt();
                break;
            }
            case rust::WorkflowOp::StoreWorkingAnnotationCount:
                out[0] = double(annotations.size());
                break;
            case rust::WorkflowOp::StoreIndexUsedLabel:
                used.insert(labels.key(annotations.at(int(a[0])).toObject()["category_id"].toInt()));
                break;
            case rust::WorkflowOp::StoreInputRegionCount:
                out[0] = double(regions.size());
                break;
            case rust::WorkflowOp::StoreSelectInputRegion:
                region = regions.at(int(a[0])).toObject();
                name = std::as_const(region)["name"].toString().trimmed();
                box = std::as_const(region)["bbox"].toArray();
                break;
            case rust::WorkflowOp::StoreRegionNameEmpty:
                out[0] = name.isEmpty();
                break;
            case rust::WorkflowOp::StoreRegionNameUsed:
                out[0] = used.contains(name);
                break;
            case rust::WorkflowOp::StoreRegionBoxCount:
                out[0] = double(box.size());
                break;
            case rust::WorkflowOp::StoreCoordinateDouble:
                out[0] = box.at(int(a[0])).isDouble();
                break;
            case rust::WorkflowOp::StoreCoordinate:
                out[0] = box.at(int(a[0])).toDouble();
                out[1] = box.at(int(a[0])).toInt();
                break;
            case rust::WorkflowOp::StoreRegionXY:
                out[0] = box.at(0).toInt();
                out[1] = box.at(1).toInt();
                break;
            case rust::WorkflowOp::StoreRegionWH:
                out[0] = box.at(2).toInt();
                out[1] = box.at(3).toInt();
                break;
            case rust::WorkflowOp::StoreSourceSize:
                out[0] = std::as_const(source)["width"].toInt();
                out[1] = std::as_const(source)["height"].toInt();
                break;
            case rust::WorkflowOp::StoreMarkUsedName:
                used.insert(name);
                break;
            case rust::WorkflowOp::StoreLabelExists:
                out[0] = labels.contains(name);
                break;
            case rust::WorkflowOp::StoreAppendCategory: {
                const int next = nextId(categories);
                labels[name] = next;
                categories.append(QJsonObject{{"id", next}, {"name", name}, {"supercategory", ""}});
                break;
            }
            case rust::WorkflowOp::StoreAppendAnnotation:
                annotations.append(QJsonObject{{"id", nextId(annotations)},
                                               {"image_id", image},
                                               {"category_id", labels[name]},
                                               {"bbox", box},
                                               {"area", a[0]},
                                               {"iscrowd", 0}});
                break;
            case rust::WorkflowOp::StoreSetAnnotations:
                data["annotations"] = annotations;
                break;
            case rust::WorkflowOp::StoreSetCategories:
                data["categories"] = categories;
                break;
            case rust::WorkflowOp::StoreSelectRemovalPath:
                removal = imagePath(
                    std::as_const(data)["images"].toArray().at(int(a[0])).toObject()["file_name"].toString());
                break;
            case rust::WorkflowOp::StoreRemovalPathEmpty:
                out[0] = removal.isEmpty();
                break;
            case rust::WorkflowOp::StoreCreateTrash:
                QDir().mkpath(directory_ + "/.trash");
                break;
            case rust::WorkflowOp::StoreNewTrashName:
                trash = directory_ + "/.trash/" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png";
                break;
            case rust::WorkflowOp::StoreRemovalFileExists:
                out[0] = QFile::exists(removal);
                break;
            case rust::WorkflowOp::StoreMoveToTrash:
                out[0] = QFile::rename(removal, trash);
                break;
            case rust::WorkflowOp::StoreExcludeImages:
                images = withoutImage(std::as_const(data)["images"].toArray(), image, "id");
                break;
            case rust::WorkflowOp::StoreRestoreFromTrash:
                QFile::rename(trash, removal);
                break;
            case rust::WorkflowOp::StoreCreateAssetDirectory:
                QDir().mkpath(path + "/images");
                break;
            case rust::WorkflowOp::StoreLoadSelectedRegions:
                selectedRegions = this->annotations(std::as_const(entry)["id"].toInt());
                break;
            case rust::WorkflowOp::StoreSelectedRegionCount:
                out[0] = double(selectedRegions.size());
                break;
            case rust::WorkflowOp::StoreLoadSourcePixels:
                sourcePixels = QImage(imagePath(std::as_const(entry)["file_name"].toString()));
                break;
            case rust::WorkflowOp::StoreSourcePixelsNull:
                out[0] = sourcePixels.isNull();
                break;
            case rust::WorkflowOp::StoreCreateMaskedImage:
                masked = QImage(sourcePixels.size(), QImage::Format_RGB888);
                break;
            case rust::WorkflowOp::StoreFillMaskedBlack:
                masked.fill(Qt::black);
                break;
            case rust::WorkflowOp::StoreCreatePainter:
                painter = std::make_unique<QPainter>(&masked);
                break;
            case rust::WorkflowOp::StoreDrawRegion: {
                const auto b = selectedRegions.at(int(a[0])).toObject()["bbox"].toArray();
                const QRect r(b[0].toInt(), b[1].toInt(), b[2].toInt(), b[3].toInt());
                painter->drawImage(r, sourcePixels, r);
                break;
            }
            case rust::WorkflowOp::StoreEndPainter:
                painter->end();
                break;
            case rust::WorkflowOp::StoreMarkCompressed:
                masked.setText("ok_compressed", "1");
                break;
            case rust::WorkflowOp::StoreNewPublishedName:
                relative = "images/" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png";
                break;
            case rust::WorkflowOp::StoreOpenPublishFile:
                publishFile = std::make_unique<QSaveFile>(path + '/' + relative);
                break;
            case rust::WorkflowOp::StorePublishFileOpen:
                out[0] = publishFile->open(QIODevice::WriteOnly);
                break;
            case rust::WorkflowOp::StoreSaveMaskedImage:
                out[0] = masked.save(publishFile.get(), "PNG");
                break;
            case rust::WorkflowOp::StoreCommitPublishImage:
                out[0] = publishFile->commit();
                break;
            case rust::WorkflowOp::StoreImageSetPublishedName:
                entry["file_name"] = relative;
                break;
            case rust::WorkflowOp::StoreAppendWorkingImage:
                images.append(entry);
                publishFile.reset();
                painter.reset();
                break;
            case rust::WorkflowOp::StoreWritePublishedDocument:
                writeJson(path + "/coco_annotations.json", data);
                break;
            case rust::WorkflowOp::StoreEnumFileEmpty:
                out[0] = enumFile.isEmpty();
                break;
            case rust::WorkflowOp::StoreEnumHeader:
                text = "from enum import Enum\n\n\nclass LabelEnum(str, Enum):\n";
                break;
            case rust::WorkflowOp::StoreEnumSelectLabel:
                region = std::as_const(data)["categories"].toArray().at(int(a[0])).toObject();
                label = std::as_const(region)["name"].toString();
                name = label;
                break;
            case rust::WorkflowOp::StoreEnumSanitizeName:
                name.replace(QRegularExpression("[^\\p{L}\\p{N}_]"), "_");
                break;
            case rust::WorkflowOp::StoreEnumNameEmpty:
                out[0] = name.isEmpty();
                break;
            case rust::WorkflowOp::StoreEnumFirstDigit:
                out[0] = name.front().isDigit();
                break;
            case rust::WorkflowOp::StoreEnumKeyword: {
                static const QStringList keywords =
                    QString("False None True and as assert async await break class continue def del elif "
                            "else except finally for from global if import in is lambda nonlocal not or pass "
                            "raise return try while with yield")
                        .split(' ');
                out[0] = keywords.contains(name);
                break;
            }
            case rust::WorkflowOp::StoreEnumPrefix:
                name.prepend("label_");
                break;
            case rust::WorkflowOp::StoreEnumNameUsed:
                out[0] = used.contains(name);
                break;
            case rust::WorkflowOp::StoreEnumSuffixId:
                name += '_' + QString::number(std::as_const(region)["id"].toInt());
                break;
            case rust::WorkflowOp::StoreEnumMarkName:
                used.insert(name);
                break;
            case rust::WorkflowOp::StoreEnumAppendLine: {
                auto literal = QJsonDocument(QJsonArray{label}).toJson(QJsonDocument::Compact);
                text += "    " + name + " = " + QString::fromUtf8(literal.mid(1, literal.size() - 2)) + '\n';
                break;
            }
            case rust::WorkflowOp::StoreEnumOpenFile:
                enumOutput = std::make_unique<QSaveFile>(enumFile);
                break;
            case rust::WorkflowOp::StoreEnumEncodeUtf8:
                bytes = text.toUtf8();
                break;
            case rust::WorkflowOp::StoreEnumFileOpen:
                out[0] = enumOutput->open(QIODevice::WriteOnly);
                break;
            case rust::WorkflowOp::StoreEnumWriteBytes:
                out[0] = enumOutput->write(bytes) == bytes.size();
                break;
            case rust::WorkflowOp::StoreEnumCommit:
                out[0] = enumOutput->commit();
                break;
            default:
                fail("Rust 模板存储宿主无效");
            }
        });
    if (method == int(rust::WorkflowMethod::StoreImages) ||
        method == int(rust::WorkflowMethod::StoreAnnotations))
        result["items"] = output;
    return result;
}
QJsonObject TemplateStore::document() const { return rustStore(int(rust::WorkflowMethod::StoreDocument)); }
QString TemplateStore::imagePath(const QString &name) const {
    return rustStore(int(rust::WorkflowMethod::StoreImagePath), 0, {}, name)["path"].toString();
}
QJsonArray TemplateStore::images() const {
    return rustStore(int(rust::WorkflowMethod::StoreImages))["items"].toArray();
}
QJsonArray TemplateStore::annotations(int image) const {
    return rustStore(int(rust::WorkflowMethod::StoreAnnotations), image)["items"].toArray();
}
int TemplateStore::add(const QImage &image) {
    return rustStore(int(rust::WorkflowMethod::StoreAdd), 0, {}, {}, {}, &image)["id"].toInt();
}
void TemplateStore::saveAnnotations(int image, const QJsonArray &regions) {
    rustStore(int(rust::WorkflowMethod::StoreSaveAnnotations), image, regions);
}
void TemplateStore::remove(int image) { rustStore(int(rust::WorkflowMethod::StoreRemove), image); }
void TemplateStore::publish(const QString &assets, const QString &enumFile) const {
    rustStore(int(rust::WorkflowMethod::StorePublish), 0, {}, assets, enumFile);
}
} // namespace okww::runtime
