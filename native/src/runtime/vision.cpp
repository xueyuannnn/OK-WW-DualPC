#include "vision.hpp"
#include "image_geometry_abi.hpp"
#include "image_pixels_abi.hpp"
#include "workflow_abi.hpp"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace okww::runtime {
namespace {
[[noreturn]] void imagingFailure(int code, const QString &path = {}) {
    const QStringList messages{
        "无法读取图像：" + path, "图像解码失败：" + path, "无效采集尺寸",       "MJPEG 尺寸与采集设置不一致",
        "采集帧长度不完整",      "RGB 行长度不完整",      "YUV 尺寸必须为偶数", "NV12 行长度无效",
        "I420 行长度无效",       "YUV 行长度无效",        "P010 行长度无效",    "未实现的采集图像格式",
        "裁剪参数无效",          "裁剪区域越界",          "识别尺寸无效"};
    fail(messages.at(code));
}
} // namespace

cv::Mat readImage(const QString &path) {

    QFile file(path);
    QByteArray bytes;
    cv::Mat image;
    rust::workflowRun(
        rust::WorkflowMethod::ReadImage, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::OpenImageFile:
                out[0] = file.open(QIODevice::ReadOnly);
                break;
            case rust::WorkflowOp::ReadImageBytes:
                bytes = file.readAll();
                break;
            case rust::WorkflowOp::DecodeImageBytes:
                image = cv::imdecode(cv::Mat(1, int(bytes.size()), CV_8U, bytes.data()), cv::IMREAD_COLOR);
                break;
            case rust::WorkflowOp::DecodedImageEmpty:
                out[0] = image.empty();
                break;
            case rust::WorkflowOp::ImagingFailure:
                imagingFailure(int(a[0]), path);
                break;
            default:
                fail("Rust 图像文件宿主无效");
            }
        });
    return image;
}
cv::Mat decode(const Frame &f) {

    cv::Mat out, source, flat;
    auto *data = const_cast<char *>(f.bytes.constData());
    rust::workflowRun(
        rust::WorkflowMethod::DecodeFrame, {}, [&](rust::WorkflowOp op, const double *a, double *result) {
            switch (op) {
            case rust::WorkflowOp::FrameDimensions:
                result[0] = f.width;
                result[1] = f.height;
                result[2] = f.stride;
                break;
            case rust::WorkflowOp::FrameFormat:
                result[0] = int(f.format);
                break;
            case rust::WorkflowOp::FrameLengthValid:
                result[0] = int64_t(f.stride) * f.height * int(a[0]) / 2 <= f.bytes.size();
                break;
            case rust::WorkflowOp::DecodeMotionJpeg:
                out = cv::imdecode(cv::Mat(1, int(f.bytes.size()), CV_8U, data), cv::IMREAD_COLOR);
                break;
            case rust::WorkflowOp::DecodedFrameSize:
                result[0] = out.empty();
                result[1] = out.cols;
                result[2] = out.rows;
                break;
            case rust::WorkflowOp::CreateRgbFrame:
                source = cv::Mat(f.height, f.width, CV_MAKETYPE(CV_8U, int(a[0])), data, f.stride);
                break;
            case rust::WorkflowOp::ConvertBgraFrame:
                cv::cvtColor(source, out, cv::COLOR_BGRA2BGR);
                break;
            case rust::WorkflowOp::CloneRgbFrame:
                out = source.clone();
                break;
            case rust::WorkflowOp::FrameBottomUp:
                result[0] = f.bottomUp;
                break;
            case rust::WorkflowOp::FlipDecodedFrame:
                cv::flip(out, out, 0);
                break;
            case rust::WorkflowOp::ConvertNv12Frame:
                cv::cvtColorTwoPlane(
                    cv::Mat(f.height, f.width, CV_8U, data, f.stride),
                    cv::Mat(f.height / 2, f.width / 2, CV_8UC2, data + f.stride * f.height, f.stride), out,
                    cv::COLOR_YUV2BGR_NV12);
                break;
            case rust::WorkflowOp::CreateI420Frame:
                flat = cv::Mat(f.height * 3 / 2, f.width, CV_8U);
                break;
            case rust::WorkflowOp::CopyI420LumaRow:
                memcpy(flat.ptr(int(a[0])), data + int(a[0]) * f.stride, f.width);
                break;
            case rust::WorkflowOp::CopyI420ChromaRow: {
                const int plane = int(a[0]), y = int(a[1]);
                auto target = flat.data + f.width * f.height;
                memcpy(target + plane * f.width * f.height / 4 + y * f.width / 2,
                       data + f.stride * f.height + plane * f.stride * f.height / 4 + y * f.stride / 2,
                       f.width / 2);
                break;
            }
            case rust::WorkflowOp::ConvertI420Frame:
                cv::cvtColor(flat, out, cv::COLOR_YUV2BGR_I420);
                break;
            case rust::WorkflowOp::ConvertPackedYuvFrame:
                cv::cvtColor(cv::Mat(f.height, f.width, CV_8UC2, data, f.stride), out,
                             int(a[0]) == 4 ? cv::COLOR_YUV2BGR_YUY2 : cv::COLOR_YUV2BGR_UYVY);
                break;
            case rust::WorkflowOp::CreateP010Frame:
                flat = cv::Mat(f.height * 3 / 2, f.width, CV_8U);
                break;
            case rust::WorkflowOp::ReduceP010Frame:
                if (!rust::okww_rs_p010_reduce(reinterpret_cast<const uint8_t *>(data), size_t(f.width),
                                               size_t(flat.rows), size_t(f.stride), flat.data, flat.step[0]))
                    fail("Rust P010 图像接口异常");
                break;
            case rust::WorkflowOp::ConvertP010Frame:
                cv::cvtColor(flat, out, cv::COLOR_YUV2BGR_NV12);
                break;
            case rust::WorkflowOp::ImagingFailure:
                imagingFailure(int(a[0]));
                break;
            default:
                fail("Rust 采集图像宿主无效");
            }
        });
    return out;
}
cv::Mat inferenceImage(const Frame &frame, const Config &config) {

    cv::Mat image;
    QJsonArray crop;
    cv::Rect rect;
    cv::Size size;
    rust::workflowRun(
        rust::WorkflowMethod::InferenceImage, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::DecodeInferenceFrame:
                image = decode(frame);
                break;
            case rust::WorkflowOp::InferenceCropCount:
                crop = config.get("capture.crop").toArray();
                out[0] = double(crop.size());
                break;
            case rust::WorkflowOp::ReadInferenceCrop:
                rect = cv::Rect(crop[0].toInt(), crop[1].toInt(), crop[2].toInt(), crop[3].toInt());
                break;
            case rust::WorkflowOp::InferenceCropValid:
                out[0] = rect.width > 0 && rect.height > 0 &&
                         (rect & cv::Rect(0, 0, image.cols, image.rows)) == rect;
                break;
            case rust::WorkflowOp::CropInferenceFrame:
                image = image(rect);
                break;
            case rust::WorkflowOp::ReadInferenceSize:
                size = cv::Size(config.n("capture.output_width"), config.n("capture.output_height"));
                break;
            case rust::WorkflowOp::InferenceOutputSize:
                out[0] = size.width;
                out[1] = size.height;
                break;
            case rust::WorkflowOp::InferenceSizeMatches:
                out[0] = image.size() == size;
                break;
            case rust::WorkflowOp::ResizeInferenceFrame:
                cv::resize(image, image, size, 0, 0, cv::INTER_LINEAR);
                break;
            case rust::WorkflowOp::ImagingFailure:
                imagingFailure(int(a[0]));
                break;
            default:
                fail("Rust 识别图像宿主无效");
            }
        });
    return image;
}
cv::Mat InferenceFrames::get(FramePtr frame) {

    cv::Mat image;
    rust::workflowRun(rust::WorkflowMethod::CachedInferenceFrame, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::HasInferenceFrame:
                              out[0] = bool(frame);
                              break;
                          case rust::WorkflowOp::ClearInferenceSource:
                              source_.reset();
                              break;
                          case rust::WorkflowOp::ClearInferenceImage:
                              image_.release();
                              break;
                          case rust::WorkflowOp::HasInferenceSource:
                              out[0] = bool(source_);
                              break;
                          case rust::WorkflowOp::InferenceSourceFieldMatches:
                              switch (int(a[0])) {
                              case 0:
                                  out[0] = source_->sequence == frame->sequence;
                                  break;
                              case 1:
                                  out[0] = source_->width == frame->width;
                                  break;
                              case 2:
                                  out[0] = source_->height == frame->height;
                                  break;
                              case 3:
                                  out[0] = source_->format == frame->format;
                                  break;
                              }
                              break;
                          case rust::WorkflowOp::ConvertInferenceFrame:
                              image_ = inferenceImage(*frame, config_);
                              break;
                          case rust::WorkflowOp::IncrementInferenceConversions:
                              ++conversions_;
                              break;
                          case rust::WorkflowOp::StoreInferenceFrame:
                              source_ = std::move(frame);
                              break;
                          case rust::WorkflowOp::ReturnInferenceImage:
                              image = image_;
                              break;
                          default:
                              fail("Rust 识别图像缓存宿主无效");
                          }
                      });
    return image;
}
cv::Mat filtered(const cv::Mat &image, Filter filter) {

    cv::Mat out;
    const std::array args{double(int(filter))};
    rust::workflowRun(
        rust::WorkflowMethod::FilterImage, args, [&](rust::WorkflowOp op, const double *a, double *result) {
            switch (op) {
            case rust::WorkflowOp::ReturnFilterSource:
                out = image;
                break;
            case rust::WorkflowOp::FilterSourceChannels:
                result[0] = image.channels();
                break;
            case rust::WorkflowOp::FilterToGray:
                cv::cvtColor(image, out, cv::COLOR_BGR2GRAY);
                break;
            case rust::WorkflowOp::FilterThreshold:
                cv::threshold(out, out, a[0], 255, a[1] ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY);
                break;
            case rust::WorkflowOp::FilterToBgr:
                cv::cvtColor(out, out, cv::COLOR_GRAY2BGR);
                break;
            case rust::WorkflowOp::FilterInRange:
                cv::inRange(image, cv::Scalar(a[0], a[0], a[0]), cv::Scalar(a[1], a[1], a[1]), out);
                break;
            default:
                fail("Rust 图像预处理宿主无效");
            }
        });
    return out;
}
Templates::Templates(const QString &coco) { add(coco); }
void Templates::add(const QString &coco) {

    QJsonObject data;
    QMap<int, QString> images, categories;
    QJsonArray b;
    QJsonObject annotation;
    QString name, path;
    Source source;
    const QSet<QString> hc{"monthly_card",
                           "suisui_forte3",
                           "message_dialog",
                           "claim_stamina_sign",
                           "skip_dialog_check",
                           "login_close",
                           "garden_confirm",
                           "garden_continue_game",
                           "garden_unpause",
                           "garden_get_gold",
                           "garden_get_purple",
                           "garden_get_skip",
                           "garden_not_interested_confirm",
                           "garden_not_interested",
                           "a_garden_back",
                           "garden_get_confirm_gray",
                           "the_garden_max",
                           "garden_shop_close",
                           "garden_new_stage",
                           "a_garden_restart",
                           "suisui_forte2",
                           "suisui_e1",
                           "e_forte",
                           "f_break_full"};
    const QSet<QString> vc{"monthly_card", "skip_dialog_check"};

    rust::workflowRun(
        rust::WorkflowMethod::TemplateAdd, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::ReadTemplateDocument:
                data = readJson(coco);
                break;
            case rust::WorkflowOp::TemplateImageCount:
                out[0] = double(data["images"].toArray().size());
                break;
            case rust::WorkflowOp::LoadTemplateImageRecord: {
                const auto o = data["images"].toArray().at(int(a[0])).toObject();
                images[o["id"].toInt()] = o["file_name"].toString();
                break;
            }
            case rust::WorkflowOp::TemplateCategoryCount:
                out[0] = double(data["categories"].toArray().size());
                break;
            case rust::WorkflowOp::LoadTemplateCategoryRecord: {
                const auto o = data["categories"].toArray().at(int(a[0])).toObject();
                categories[o["id"].toInt()] = o["name"].toString();
                break;
            }
            case rust::WorkflowOp::TemplateAnnotationCount:
                out[0] = double(data["annotations"].toArray().size());
                break;
            case rust::WorkflowOp::LoadTemplateAnnotation:
                annotation = data["annotations"].toArray().at(int(a[0])).toObject();
                name = categories.value(annotation["category_id"].toInt());
                b = annotation["bbox"].toArray();
                break;
            case rust::WorkflowOp::TemplateAnnotationNameEmpty:
                out[0] = name.isEmpty();
                break;
            case rust::WorkflowOp::TemplateAnnotationBoxCount:
                out[0] = double(b.size());
                break;
            case rust::WorkflowOp::TemplateAnnotationPath:
                path = images.value(annotation["image_id"].toInt());
                break;
            case rust::WorkflowOp::NormalizeTemplatePath:
                path.replace('\\', '/');
                break;
            case rust::WorkflowOp::TemplatePathAbsolute:
                out[0] = QDir::isAbsolutePath(path);
                break;
            case rust::WorkflowOp::TemplatePathHasImages:
                out[0] = path.contains("/images/");
                break;
            case rust::WorkflowOp::TemplatePathRelative:
                path = "images/" + path.section("/images/", 1);
                break;
            case rust::WorkflowOp::ResolveTemplatePath:
                path = QDir(QFileInfo(coco).absolutePath()).absoluteFilePath(path);
                break;
            case rust::WorkflowOp::TemplateAnnotationOrigin:
                out[0] = b[0].toDouble();
                out[1] = b[1].toDouble();
                break;
            case rust::WorkflowOp::TemplateAnnotationSize:
                out[0] = b[2].toDouble();
                out[1] = b[3].toDouble();
                break;
            case rust::WorkflowOp::PixelRound:
                out[0] = pixelRound(a[0]);
                break;
            case rust::WorkflowOp::SetTemplateSourceBox:
                source = Source{path, {int(a[0]), int(a[1]), int(a[2]), int(a[3])}};
                break;
            case rust::WorkflowOp::TemplateHasHorizontalName:
                out[0] = name.contains("hcenter");
                break;
            case rust::WorkflowOp::TemplateHorizontalAnchor:
                out[0] = hc.contains(name);
                break;
            case rust::WorkflowOp::TemplateHasVerticalName:
                out[0] = name.contains("vcenter");
                break;
            case rust::WorkflowOp::TemplateVerticalAnchor:
                out[0] = vc.contains(name);
                break;
            case rust::WorkflowOp::SetTemplateSourceAnchors:
                source.hc = bool(a[0]);
                source.vc = bool(a[1]);
                break;
            case rust::WorkflowOp::TemplateNameKind: {
                const QHash<QString, int> kinds{
                    {"illusive_realm_exit", 1}, {"world_earth_icon", 1}, {"purple_target_distance_icon", 3},
                    {"mouse_forte", 3},         {"e_forte", 4},          {"skip_dialog", 2},
                    {"skip_dialog_new", 2}};
                out[0] = kinds.value(name, 0);
                break;
            }
            case rust::WorkflowOp::SetTemplateSourceFilter:
                source.filter = Filter(int(a[0]));
                break;
            case rust::WorkflowOp::TemplateSourceExists:
                out[0] = sources_.contains(name);
                break;
            case rust::WorkflowOp::StoreTemplateSource:
                sources_[name] = source;
                break;
            case rust::WorkflowOp::ClearTemplateCache:
                cache_.clear();
                break;
            case rust::WorkflowOp::TemplateFailure:
                fail("无效模板标注");
                break;
            default:
                fail("Rust 模板导入宿主无效");
            }
        });
}
QRect Templates::anchored(QRect r, QSize source, QSize target, bool hc, bool vc) {
    return rust::imageGeometry(0, {double(r.x()), double(r.y()), double(r.width()), double(r.height()),
                                   double(source.width()), double(source.height()), double(target.width()),
                                   double(target.height()), double(hc), double(vc)});
}
const Templates::Scaled &Templates::scaled(const QString &name, QSize size) {

    const Source *source = nullptr;
    const cv::Mat *image = nullptr;
    const Scaled *result = nullptr;
    QRect r, adjusted;
    cv::Mat resized;
    rust::workflowRun(
        rust::WorkflowMethod::TemplateScaled, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::TemplateSizeMatches:
                out[0] = size == size_;
                break;
            case rust::WorkflowOp::ClearTemplateCache:
                cache_.clear();
                break;
            case rust::WorkflowOp::SetTemplateSize:
                size_ = size;
                break;
            case rust::WorkflowOp::TemplateCached:
                out[0] = cache_.contains(name);
                break;
            case rust::WorkflowOp::ReturnCachedTemplate:
                result = &cache_[name];
                break;
            case rust::WorkflowOp::TemplateSourceExists:
                out[0] = sources_.contains(name);
                break;
            case rust::WorkflowOp::ReadTemplateSource:
                source = &sources_[name];
                break;
            case rust::WorkflowOp::TemplateImageCached:
                out[0] = images_.contains(source->path);
                break;
            case rust::WorkflowOp::LoadTemplateImage:
                images_[source->path] = readImage(source->path);
                break;
            case rust::WorkflowOp::ReadCachedTemplateImage:
                image = &images_[source->path];
                r = source->box;
                break;
            case rust::WorkflowOp::TemplateSourceBoxContained:
                out[0] = QRect(0, 0, image->cols, image->rows).contains(r);
                break;
            case rust::WorkflowOp::TemplateSourceBoxEmpty:
                out[0] = r.isEmpty();
                break;
            case rust::WorkflowOp::AnchorTemplateSource:
                adjusted = anchored(r, {image->cols, image->rows}, size, source->hc, source->vc);
                break;
            case rust::WorkflowOp::ResizeTemplateSource:
                cv::resize((*image)(cv::Rect(r.x(), r.y(), r.width(), r.height())), resized,
                           {adjusted.width(), adjusted.height()});
                break;
            case rust::WorkflowOp::StoreScaledTemplate:
                result = &(cache_[name] = {filtered(resized, source->filter), adjusted});
                break;
            case rust::WorkflowOp::TemplateFailure:
                fail(int(a[0]) == 1 ? "缺少已有模板：" + name : "模板标注越界：" + name);
                break;
            default:
                fail("Rust 模板缓存宿主无效");
            }
        });
    return *result;
}
QVector<Match> Templates::find(const cv::Mat &frame, const QString &name, MatchOptions options) {

    const Scaled *feature = nullptr;
    cv::Mat pattern, search, softSearch, softPattern, scores;
    cv::Size originalPattern;
    QRect area, r;
    QVector<Match> matches;
    const std::array args{options.threshold,
                          options.horizontal,
                          options.vertical,
                          options.templateTop,
                          options.searchHeight,
                          options.templateScale,
                          double(options.targetHeight),
                          options.rotation,
                          options.blurSigma,
                          double(options.grayscale),
                          double(options.limit),
                          double(bool(options.search))};
    rust::workflowRun(
        rust::WorkflowMethod::TemplateFind, args, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::TemplateHasCheckpoint:
                out[0] = bool(checkpoint_);
                break;
            case rust::WorkflowOp::Checkpoint:
                checkpoint_();
                break;
            case rust::WorkflowOp::TemplateFrameEmpty:
                out[0] = frame.empty();
                break;
            case rust::WorkflowOp::LoadScaledFeature:
                feature = &scaled(name, {frame.cols, frame.rows});
                break;
            case rust::WorkflowOp::CopyTemplatePattern:
                pattern = feature->mat;
                break;
            case rust::WorkflowOp::RotateTemplatePattern: {
                const auto matrix =
                    cv::getRotationMatrix2D({float(pattern.cols / 2), float(pattern.rows / 2)}, -a[0], 1);
                cv::Mat rotated;
                cv::warpAffine(pattern, rotated, matrix, pattern.size());
                pattern = std::move(rotated);
                break;
            }
            case rust::WorkflowOp::TemplatePatternSize:
                out[0] = pattern.cols;
                out[1] = pattern.rows;
                break;
            case rust::WorkflowOp::TrimTemplatePattern:
                pattern = pattern.rowRange(0, int(a[0]));
                break;
            case rust::WorkflowOp::ScaleTemplatePattern:
                cv::resize(pattern, pattern, {}, a[0], a[0], cv::INTER_AREA);
                break;
            case rust::WorkflowOp::UseSpecifiedTemplateArea:
                area = *options.search;
                break;
            case rust::WorkflowOp::CopyFeatureSearchBox:
                r = feature->box;
                break;
            case rust::WorkflowOp::TemplateFeatureRows:
                out[0] = feature->mat.rows;
                break;
            case rust::WorkflowOp::SetFeatureSearchHeight:
                r.setHeight(int(a[0]));
                break;
            case rust::WorkflowOp::TemplateSearchOrigin:
                out[0] = r.x();
                out[1] = r.y();
                break;
            case rust::WorkflowOp::TemplateSearchSize:
                out[0] = r.width();
                out[1] = r.height();
                break;
            case rust::WorkflowOp::TemplateFrameSize:
                out[0] = frame.cols;
                out[1] = frame.rows;
                break;
            case rust::WorkflowOp::PixelRound:
                out[0] = pixelRound(a[0]);
                break;
            case rust::WorkflowOp::SetTemplateSearchArea:
                area = {int(a[0]), int(a[1]), int(a[2]), int(a[3])};
                break;
            case rust::WorkflowOp::ClipTemplateSearchArea:
                area = area.intersected({0, 0, frame.cols, frame.rows});
                break;
            case rust::WorkflowOp::TemplateAreaSize:
                out[0] = area.width();
                out[1] = area.height();
                break;
            case rust::WorkflowOp::FilterTemplateSearch:
                search = filtered(frame(cv::Rect(area.x(), area.y(), area.width(), area.height())),
                                  options.filter);
                break;
            case rust::WorkflowOp::TemplateSearchChannels:
                out[0] = search.channels();
                break;
            case rust::WorkflowOp::GrayTemplateSearch:
                cv::cvtColor(search, search, cv::COLOR_BGR2GRAY);
                break;
            case rust::WorkflowOp::TemplatePatternChannels:
                out[0] = pattern.channels();
                break;
            case rust::WorkflowOp::GrayTemplatePattern:
                cv::cvtColor(pattern, pattern, cv::COLOR_BGR2GRAY);
                break;
            case rust::WorkflowOp::TemplateTypesMatch:
                out[0] = search.type() == pattern.type();
                break;
            case rust::WorkflowOp::BlurTemplateSearch:
                cv::GaussianBlur(search, softSearch, {}, a[0]);
                break;
            case rust::WorkflowOp::BlurTemplatePattern:
                cv::GaussianBlur(pattern, softPattern, {}, a[0], 0, cv::BORDER_CONSTANT);
                break;
            case rust::WorkflowOp::UseSoftTemplateImages:
                search = std::move(softSearch);
                pattern = std::move(softPattern);
                break;
            case rust::WorkflowOp::StoreOriginalTemplateSize:
                originalPattern = pattern.size();
                break;
            case rust::WorkflowOp::TemplateSearchImageSize:
                out[0] = search.cols;
                out[1] = search.rows;
                break;
            case rust::WorkflowOp::ResizeTemplateSearch:
                cv::resize(search, search, {int(a[0]), int(a[1])}, 0, 0, cv::INTER_AREA);
                break;
            case rust::WorkflowOp::MatchTemplateImages:
                cv::matchTemplate(search, pattern, scores, cv::TM_CCOEFF_NORMED);
                break;
            case rust::WorkflowOp::BestTemplateMatch: {
                double score;
                cv::Point point;
                cv::minMaxLoc(scores, nullptr, &score, nullptr, &point);
                out[0] = score;
                out[1] = point.x;
                out[2] = point.y;
                break;
            }
            case rust::WorkflowOp::AppendTemplateMatch:
                matches.append({name,
                                {area.x() + int(a[0]), area.y() + int(a[1]), originalPattern.width,
                                 originalPattern.height},
                                a[2]});
                break;
            case rust::WorkflowOp::SuppressTemplateMatch: {
                const cv::Rect suppress{int(a[0]), int(a[1]), int(a[2]), int(a[3])};
                scores(suppress & cv::Rect(0, 0, scores.cols, scores.rows)).setTo(-1);
                break;
            }
            case rust::WorkflowOp::TemplateFailure: {
                const QStringList errors{"",
                                         "",
                                         "",
                                         "模板旋转角度无效",
                                         "局部模板比例无效",
                                         "模板缩放倍率无效",
                                         "模板与图像预处理不一致：" + name,
                                         "模板平滑参数无效"};
                fail(errors.at(int(a[0])));
                break;
            }
            default:
                fail("Rust 模板搜索宿主无效");
            }
        });
    return matches;
}
std::optional<Match> Templates::one(const cv::Mat &frame, const QString &name, MatchOptions options) {

    QVector<Match> found;
    std::optional<Match> result;
    rust::workflowRun(rust::WorkflowMethod::TemplateOne, {},
                      [&](rust::WorkflowOp op, const double *, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::SetSingleTemplateLimit:
                              options.limit = 1;
                              break;
                          case rust::WorkflowOp::FindSingleTemplate:
                              found = find(frame, name, options);
                              break;
                          case rust::WorkflowOp::SingleTemplateEmpty:
                              out[0] = found.isEmpty();
                              break;
                          case rust::WorkflowOp::ReturnSingleTemplate:
                              result = found.front();
                              break;
                          default:
                              fail("Rust 单模板宿主无效");
                          }
                      });
    return result;
}
double colorPercentage(const cv::Mat &frame, QRect box, cv::Scalar lower, cv::Scalar upper) {

    cv::Mat mask;
    const auto result = rust::workflowRun(
        rust::WorkflowMethod::ColorPercentage, {}, [&](rust::WorkflowOp op, const double *, double *out) {
            switch (op) {
            case rust::WorkflowOp::ColorBoxValid:
                out[0] = !box.isEmpty() && QRect(0, 0, frame.cols, frame.rows).contains(box);
                break;
            case rust::WorkflowOp::ColorRangeMask:
                cv::inRange(frame(cv::Rect(box.x(), box.y(), box.width(), box.height())), lower, upper, mask);
                break;
            case rust::WorkflowOp::ColorMaskCounts:
                out[0] = cv::countNonZero(mask);
                out[1] = double(mask.total());
                break;
            default:
                fail("Rust 颜色比例宿主无效");
            }
        });
    return result[0];
}
QRect relativeBox(const cv::Mat &f, double x, double y, double right, double bottom) {
    return rust::imageGeometry(1, {double(f.cols), double(f.rows), x, y, right, bottom})
        .intersected({0, 0, f.cols, f.rows});
}
QPoint gamePoint(QSize size, double x, double y, bool hc, bool vc) {
    return rust::imageGeometry(2, {double(size.width()), double(size.height()), x, y, double(hc), double(vc)})
        .topLeft();
}
QRect gameBox(const cv::Mat &f, double x, double y, double right, double bottom, bool hc, bool vc) {
    return rust::imageGeometry(3, {double(f.cols), double(f.rows), x, y, right, bottom, double(hc),
                                   double(vc), double(f.empty())})
        .intersected({0, 0, f.cols, f.rows});
}
} // namespace okww::runtime
