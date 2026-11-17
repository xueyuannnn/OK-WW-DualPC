// PP-OCR preprocessing and DB/CTC postprocessing follow the project's OnnxOCR
// implementation (PaddlePaddle Authors, Apache-2.0). Inference is native OpenVINO.
#include "ocr.hpp"
#include "clipper.hpp"
#include "inference_wait.hpp"
#include "ocr_geometry_host.hpp"
#include "ocr_numeric_abi.hpp"
#include <numeric>
#include <opencv2/imgproc.hpp>
#include <openvino/openvino.hpp>

namespace okww::runtime {
using Quad = std::array<cv::Point2f, 4>;

std::pair<Quad, float> miniBox(const std::vector<cv::Point2f> &points) {
    cv::RotatedRect rect;
    Quad p, result;
    const auto out = rust::workflowRun(
        rust::WorkflowMethod::OcrMiniBox, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::OcrMinimumAreaRect:
                rect = cv::minAreaRect(points);
                rect.points(p.data());
                break;
            case rust::WorkflowOp::OcrSortRectPoints:
                std::sort(p.begin(), p.end(), [](auto a, auto b) { return a.x < b.x; });
                break;
            case rust::WorkflowOp::OcrRectPointYs:
                out[0] = p[0].y;
                out[1] = p[1].y;
                break;
            case rust::WorkflowOp::OcrOtherRectPointYs:
                out[0] = p[2].y;
                out[1] = p[3].y;
                break;
            case rust::WorkflowOp::OcrOrderRectPoints:
                result[0] = p[size_t(a[0])];
                result[1] = p[size_t(a[1])];
                result[2] = p[size_t(a[2])];
                break;
            case rust::WorkflowOp::OcrOrderLastRectPoint:
                result[3] = p[size_t(a[0])];
                break;
            case rust::WorkflowOp::OcrRotatedRectSize:
                out[0] = rect.size.width;
                out[1] = rect.size.height;
                break;
            default:
                fail("Rust OCR 最小框宿主无效");
            }
        });
    return {result, float(out[0])};
}
double boxScore(const cv::Mat &scores, const Quad &quad) {
    cv::Rect area;
    cv::Mat mask;
    std::vector<cv::Point> polygon;
    const auto result = rust::workflowRun(
        rust::WorkflowMethod::OcrBoxScore, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::OcrGeometryPoint:
                out[0] = quad.at(size_t(a[0])).x;
                out[1] = quad.at(size_t(a[0])).y;
                break;
            case rust::WorkflowOp::OcrGeometryScoreSize:
                out[0] = scores.cols;
                out[1] = scores.rows;
                break;
            case rust::WorkflowOp::OcrSetScoreArea:
                area.x = int(a[0]);
                area.y = int(a[1]);
                area.width = int(a[2]);
                break;
            case rust::WorkflowOp::OcrSetScoreAreaHeight:
                area.height = int(a[0]);
                break;
            case rust::WorkflowOp::OcrCreateScoreMask:
                mask = cv::Mat::zeros(area.size(), CV_8U);
                break;
            case rust::WorkflowOp::OcrAppendScorePoint:
                polygon.emplace_back(int(a[0]), int(a[1]));
                break;
            case rust::WorkflowOp::OcrFillScoreMask:
                cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{polygon}, cv::Scalar(1));
                break;
            case rust::WorkflowOp::OcrMeanScore:
                out[0] = cv::mean(scores(area), mask)[0];
                break;
            default:
                fail("Rust OCR 框评分宿主无效");
            }
        });
    return result[0];
}
std::vector<cv::Point2f> unclip(const Quad &points) {
    std::vector<cv::Point2f> polygon, expanded;
    ClipperLib::Path path;
    ClipperLib::Paths paths;
    ClipperLib::ClipperOffset offset;
    rust::workflowRun(rust::WorkflowMethod::OcrUnclip, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::OcrGeometryPolygon:
                              polygon.assign(points.begin(), points.end());
                              break;
                          case rust::WorkflowOp::OcrPolygonPerimeter:
                              out[0] = cv::arcLength(polygon, true);
                              break;
                          case rust::WorkflowOp::OcrPolygonArea:
                              out[0] = cv::contourArea(polygon);
                              break;
                          case rust::WorkflowOp::OcrGeometryPoint:
                              out[0] = points.at(size_t(a[0])).x;
                              out[1] = points.at(size_t(a[0])).y;
                              break;
                          case rust::WorkflowOp::OcrAppendClipperPoint:
                              path.emplace_back(ClipperLib::cInt(a[0]), ClipperLib::cInt(a[1]));
                              break;
                          case rust::WorkflowOp::OcrAddClipperPath:
                              offset.AddPath(path, ClipperLib::jtRound, ClipperLib::etClosedPolygon);
                              break;
                          case rust::WorkflowOp::OcrExpandClipperPath:
                              offset.Execute(paths, a[0]);
                              break;
                          case rust::WorkflowOp::OcrReadExpandedPath:
                              for (const auto &item : paths)
                                  for (auto p : item)
                                      expanded.emplace_back(float(p.X), float(p.Y));
                              break;
                          default:
                              fail("Rust OCR 扩框宿主无效");
                          }
                      });
    return expanded;
}
cv::Mat textCrop(const cv::Mat &image, const Quad &p) {
    cv::Mat crop;
    rust::workflowRun(
        rust::WorkflowMethod::OcrTextCrop, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::OcrCropWidthNorms:
                out[0] = cv::norm(p[0] - p[1]);
                out[1] = cv::norm(p[2] - p[3]);
                break;
            case rust::WorkflowOp::OcrCropHeightNorms:
                out[0] = cv::norm(p[0] - p[3]);
                out[1] = cv::norm(p[1] - p[2]);
                break;
            case rust::WorkflowOp::OcrWarpCrop: {
                const int w = int(a[0]), h = int(a[1]);
                const Quad target{{{0, 0}, {float(w), 0}, {float(w), float(h)}, {0, float(h)}}};
                cv::warpPerspective(image, crop, cv::getPerspectiveTransform(p.data(), target.data()), {w, h},
                                    cv::INTER_CUBIC, cv::BORDER_REPLICATE);
                break;
            }
            case rust::WorkflowOp::OcrRotateTallCrop:
                cv::rotate(crop, crop, cv::ROTATE_90_COUNTERCLOCKWISE);
                break;
            default:
                fail("Rust OCR 裁剪宿主无效");
            }
        });
    return crop;
}
struct Ocr::Impl {
    ov::Core core;
    ov::CompiledModel det, rec, cls;
    ov::InferRequest detRequest, recRequest, clsRequest;
    QStringList alphabet;
    QString models;
    std::mutex mutex;
    static void infer(ov::InferRequest &request, const std::function<void()> &checkpoint) {
        inferWithCheckpoint(request, checkpoint, "OCR ");
    }
    explicit Impl(const QString &path) : models(path) {

        QFile dictionary(path + "/ppocrv5_dict.txt");
        QByteArray line;
        rust::workflowRun(rust::WorkflowMethod::OcrInitialize, {},
                          [&](rust::WorkflowOp op, const double *, double *out) {
                              switch (op) {
                              case rust::WorkflowOp::OcrOpenDictionary:
                                  out[0] = dictionary.open(QIODevice::ReadOnly);
                                  break;
                              case rust::WorkflowOp::OcrFailure:
                                  fail("OCR 字典不存在：" + dictionary.fileName());
                                  break;
                              case rust::WorkflowOp::OcrAlphabetBlank:
                                  alphabet.append("");
                                  break;
                              case rust::WorkflowOp::OcrDictionaryEnd:
                                  out[0] = dictionary.atEnd();
                                  break;
                              case rust::WorkflowOp::OcrReadDictionaryLine:
                                  line = dictionary.readLine();
                                  break;
                              case rust::WorkflowOp::OcrDictionaryLineEnding:
                                  out[0] = line.endsWith('\n') || line.endsWith('\r');
                                  break;
                              case rust::WorkflowOp::OcrChopDictionaryLine:
                                  line.chop(1);
                                  break;
                              case rust::WorkflowOp::OcrAppendDictionaryLine:
                                  alphabet.append(QString::fromUtf8(line));
                                  break;
                              case rust::WorkflowOp::OcrAlphabetSpace:
                                  alphabet.append(" ");
                                  break;
                              case rust::WorkflowOp::OcrCompileDetector:
                                  det = compile(path + "/det/det.onnx");
                                  break;
                              case rust::WorkflowOp::OcrCompileRecognizer:
                                  rec = compile(path + "/rec/rec.onnx");
                                  break;
                              case rust::WorkflowOp::OcrCreateDetectorRequest:
                                  detRequest = det.create_infer_request();
                                  break;
                              case rust::WorkflowOp::OcrCreateRecognizerRequest:
                                  recRequest = rec.create_infer_request();
                                  break;
                              default:
                                  fail("Rust OCR 字典宿主无效");
                              }
                          });
    }
    ov::CompiledModel compile(const QString &path) {

        ov::CompiledModel model;
        rust::workflowRun(
            rust::WorkflowMethod::OcrCompile, {}, [&](rust::WorkflowOp op, const double *, double *out) {
                switch (op) {
                case rust::WorkflowOp::OcrModelExists:
                    out[0] = QFile::exists(path);
                    break;
                case rust::WorkflowOp::OcrFailure:
                    fail("OCR 模型不存在：" + path);
                    break;
                case rust::WorkflowOp::OcrCompileModel:
                    model = core.compile_model(path.toStdString(), "CPU", ov::inference_num_threads(2));
                    break;
                default:
                    fail("Rust OCR 模型宿主无效");
                }
            });
        return model;
    }
    static ov::Tensor normalized(const cv::Mat &image, int height, int width, bool detector) {

        ov::Tensor tensor;
        cv::Mat resized;
        const std::array args{double(height), double(width), double(detector)};
        rust::workflowRun(
            rust::WorkflowMethod::OcrNormalize, args, [&](rust::WorkflowOp op, const double *a, double *out) {
                switch (op) {
                case rust::WorkflowOp::OcrAllocateNormalized:
                    tensor = ov::Tensor(ov::element::f32, {1, 3, size_t(a[0]), size_t(a[1])});
                    break;
                case rust::WorkflowOp::OcrZeroNormalized:
                    std::memset(tensor.data(), 0, tensor.get_byte_size());
                    break;
                case rust::WorkflowOp::OcrImageSize:
                    out[0] = image.cols;
                    out[1] = image.rows;
                    break;
                case rust::WorkflowOp::OcrResizeNormalized:
                    cv::resize(image, resized, {int(a[0]), int(a[1])});
                    break;
                case rust::WorkflowOp::OcrFillNormalized:
                    if (!rust::okww_rs_ocr_normalized(resized.data, size_t(resized.cols),
                                                      size_t(resized.rows), resized.step[0], size_t(width),
                                                      int32_t(a[0]), tensor.data<float>(), tensor.get_size()))
                        fail("Rust OCR 图像接口异常");
                    break;
                default:
                    fail("Rust OCR 预处理宿主无效");
                }
            });
        return tensor;
    }
    std::vector<Quad> detect(const cv::Mat &image, const std::function<void()> &checkpoint) {

        ov::Tensor tensor;
        ov::Shape shape;
        cv::Mat scores, mask;
        std::vector<std::vector<cv::Point>> contours;
        std::vector<Quad> boxes;
        std::vector<cv::Point2f> contour, expanded;
        Quad quad;
        float side = 0;
        rust::workflowRun(
            rust::WorkflowMethod::OcrDetect, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
                switch (op) {
                case rust::WorkflowOp::OcrImageSize:
                    out[0] = image.cols;
                    out[1] = image.rows;
                    break;
                case rust::WorkflowOp::PixelRound:
                    out[0] = pixelRound(a[0]);
                    break;
                case rust::WorkflowOp::OcrHasCheckpoint:
                    out[0] = bool(checkpoint);
                    break;
                case rust::WorkflowOp::Checkpoint:
                    checkpoint();
                    break;
                case rust::WorkflowOp::OcrSetDetectorInput:
                    detRequest.set_input_tensor(normalized(image, int(a[0]), int(a[1]), true));
                    break;
                case rust::WorkflowOp::OcrInferDetector:
                    infer(detRequest, checkpoint);
                    break;
                case rust::WorkflowOp::OcrReadDetectionTensor:
                    tensor = detRequest.get_output_tensor();
                    shape = tensor.get_shape();
                    break;
                case rust::WorkflowOp::OcrDetectionShape:
                    out[0] = double(shape.size());
                    out[1] = shape.empty() ? 0 : double(shape[0]);
                    out[2] = shape.size() < 2 ? 0 : double(shape[1]);
                    break;
                case rust::WorkflowOp::OcrDetectionFloat:
                    out[0] = tensor.get_element_type() == ov::element::f32;
                    break;
                case rust::WorkflowOp::OcrFailure:
                    fail("OCR 检测输出格式不支持");
                    break;
                case rust::WorkflowOp::OcrDetectionMask:
                    scores = cv::Mat(int(shape[2]), int(shape[3]), CV_32F, tensor.data<float>());
                    cv::compare(scores, .3, mask, cv::CMP_GT);
                    break;
                case rust::WorkflowOp::OcrFindContours:
                    cv::findContours(mask, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
                    break;
                case rust::WorkflowOp::OcrContourCount:
                    out[0] = double(contours.size());
                    break;
                case rust::WorkflowOp::OcrLoadContour:
                    contour.clear();
                    for (auto p : contours.at(size_t(a[0])))
                        contour.emplace_back(float(p.x), float(p.y));
                    break;
                case rust::WorkflowOp::OcrContourMiniBox:
                    std::tie(quad, side) = miniBox(contour);
                    out[0] = side;
                    break;
                case rust::WorkflowOp::OcrContourScore:
                    out[0] = boxScore(scores, quad);
                    break;
                case rust::WorkflowOp::OcrExpandContour:
                    expanded = unclip(quad);
                    break;
                case rust::WorkflowOp::OcrExpandedCount:
                    out[0] = double(expanded.size());
                    break;
                case rust::WorkflowOp::OcrExpandedMiniBox:
                    std::tie(quad, side) = miniBox(expanded);
                    out[0] = side;
                    break;
                case rust::WorkflowOp::OcrDetectionSize:
                    out[0] = scores.cols;
                    out[1] = scores.rows;
                    break;
                case rust::WorkflowOp::OcrQuadPoint:
                    out[0] = quad.at(size_t(a[0])).x;
                    out[1] = quad.at(size_t(a[0])).y;
                    break;
                case rust::WorkflowOp::OcrStoreQuadPoint:
                    quad.at(size_t(a[0])) = {float(a[1]), float(a[2])};
                    break;
                case rust::WorkflowOp::OcrQuadNorms:
                    out[0] = cv::norm(quad[0] - quad[1]);
                    out[1] = cv::norm(quad[0] - quad[3]);
                    break;
                case rust::WorkflowOp::OcrAppendQuad:
                    boxes.push_back(quad);
                    break;
                case rust::WorkflowOp::OcrSortQuads:
                    std::sort(boxes.begin(), boxes.end(), [](const auto &a, const auto &b) {
                        return a[0].y == b[0].y ? a[0].x < b[0].x : a[0].y < b[0].y;
                    });
                    break;
                case rust::WorkflowOp::OcrQuadCount:
                    out[0] = double(boxes.size());
                    break;
                case rust::WorkflowOp::OcrAdjacentQuads: {
                    const auto j = size_t(a[0]);
                    out[0] = boxes[j + 1][0].y - boxes[j][0].y;
                    out[1] = boxes[j + 1][0].x;
                    out[2] = boxes[j][0].x;
                    break;
                }
                case rust::WorkflowOp::OcrSwapQuads:
                    std::swap(boxes.at(size_t(a[0]) + 1), boxes.at(size_t(a[0])));
                    break;
                default:
                    fail("Rust OCR 检测宿主无效");
                }
            });
        return boxes;
    }
    void orient(cv::Mat &image, const std::function<void()> &checkpoint) {

        ov::Tensor tensor;
        rust::workflowRun(rust::WorkflowMethod::OcrOrient, {},
                          [&](rust::WorkflowOp op, const double *, double *out) {
                              switch (op) {
                              case rust::WorkflowOp::OcrClassifierReady:
                                  out[0] = bool(cls);
                                  break;
                              case rust::WorkflowOp::OcrCompileClassifier:
                                  cls = compile(models + "/cls/cls.onnx");
                                  break;
                              case rust::WorkflowOp::OcrCreateClassifierRequest:
                                  clsRequest = cls.create_infer_request();
                                  break;
                              case rust::WorkflowOp::OcrSetClassifierInput:
                                  clsRequest.set_input_tensor(normalized(image, 48, 192, false));
                                  break;
                              case rust::WorkflowOp::OcrInferClassifier:
                                  infer(clsRequest, checkpoint);
                                  break;
                              case rust::WorkflowOp::OcrReadClassifierTensor:
                                  tensor = clsRequest.get_output_tensor();
                                  break;
                              case rust::WorkflowOp::OcrClassifierSize:
                                  out[0] = double(tensor.get_size());
                                  break;
                              case rust::WorkflowOp::OcrClassifierFloat:
                                  out[0] = tensor.get_element_type() == ov::element::f32;
                                  break;
                              case rust::WorkflowOp::OcrFailure:
                                  fail("OCR 方向输出无效");
                                  break;
                              case rust::WorkflowOp::OcrClassifierScores: {
                                  const auto scores = tensor.data<float>();
                                  out[0] = scores[0];
                                  out[1] = scores[1];
                                  break;
                              }
                              case rust::WorkflowOp::OcrRotateImage:
                                  cv::rotate(image, image, cv::ROTATE_180);
                                  break;
                              default:
                                  fail("Rust OCR 方向宿主无效");
                              }
                          });
    }
    std::vector<std::pair<QString, double>> recognize(const std::vector<cv::Mat> &images,
                                                      const std::function<void()> &checkpoint) {

        std::vector<size_t> indices;
        std::vector<std::pair<QString, double>> results;
        ov::Tensor batch, output;
        ov::Shape shape;
        QString text;
        rust::workflowRun(
            rust::WorkflowMethod::OcrRecognize, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
                switch (op) {
                case rust::WorkflowOp::OcrImageCount:
                    out[0] = double(images.size());
                    break;
                case rust::WorkflowOp::OcrIndexedImageSize: {
                    const auto &im = images.at(size_t(a[0]));
                    out[0] = im.cols;
                    out[1] = im.rows;
                    break;
                }
                case rust::WorkflowOp::OcrAppendImageIndex:
                    indices.push_back(size_t(a[0]));
                    break;
                case rust::WorkflowOp::OcrAllocateRecognitionResults:
                    results.resize(size_t(a[0]));
                    break;
                case rust::WorkflowOp::OcrHasCheckpoint:
                    out[0] = bool(checkpoint);
                    break;
                case rust::WorkflowOp::Checkpoint:
                    checkpoint();
                    break;
                case rust::WorkflowOp::OcrSortedImageSize: {
                    const auto &im = images.at(indices.at(size_t(a[0])));
                    out[0] = im.cols;
                    out[1] = im.rows;
                    break;
                }
                case rust::WorkflowOp::OcrAllocationFailure:
                    throw std::bad_alloc();
                    break;
                case rust::WorkflowOp::OcrAllocateBatch:
                    batch = ov::Tensor(ov::element::f32, {size_t(a[0]), 3, 48, size_t(a[1])});
                    break;
                case rust::WorkflowOp::OcrNormalizeBatchItem: {
                    auto item = normalized(images.at(indices.at(size_t(a[0]))), 48, int(a[2]), false);
                    memcpy(batch.data<float>() + (size_t(a[0]) - size_t(a[1])) * item.get_size(), item.data(),
                           item.get_byte_size());
                    break;
                }
                case rust::WorkflowOp::OcrSetRecognizerInput:
                    recRequest.set_input_tensor(batch);
                    break;
                case rust::WorkflowOp::OcrInferRecognizer:
                    infer(recRequest, checkpoint);
                    break;
                case rust::WorkflowOp::OcrReadRecognitionTensor:
                    output = recRequest.get_output_tensor();
                    shape = output.get_shape();
                    break;
                case rust::WorkflowOp::OcrRecognitionShape:
                    out[0] = double(shape.size());
                    out[1] = shape.empty() ? 0 : double(shape[0]);
                    out[2] = shape.size() < 3 ? 0 : double(shape[2]);
                    break;
                case rust::WorkflowOp::OcrAlphabetSize:
                    out[0] = double(alphabet.size());
                    break;
                case rust::WorkflowOp::OcrRecognitionFloat:
                    out[0] = output.get_element_type() == ov::element::f32;
                    break;
                case rust::WorkflowOp::OcrRecognitionSteps:
                    out[0] = double(shape[1]);
                    break;
                case rust::WorkflowOp::OcrClearRecognizedText:
                    text = QString();
                    break;
                case rust::WorkflowOp::OcrBestToken: {
                    const auto row =
                        output.data<float>() + (size_t(a[0]) * shape[1] + size_t(a[1])) * shape[2];
                    // Keep the SDK extremum primitive's platform-specific
                    // floating-point behavior; CTC state and timing live in Rust.
                    const size_t index = size_t(std::max_element(row, row + shape[2]) - row);
                    const float score = row[index];
                    out[0] = double(index);
                    out[1] = score;
                    break;
                }
                case rust::WorkflowOp::OcrAppendToken:
                    text += alphabet.at(int(a[0]));
                    break;
                case rust::WorkflowOp::OcrStoreRecognizedText:
                    results.at(indices.at(size_t(a[0]))) = {text, a[1]};
                    break;
                case rust::WorkflowOp::OcrFailure:
                    fail(int(a[0]) == 4 ? "OCR 文本行过长" : "OCR 识别模型与字典不匹配");
                    break;
                default:
                    fail("Rust OCR 解码宿主无效");
                }
            });
        return results;
    }
};
Ocr::Ocr(const QString &directory) : impl_(std::make_unique<Impl>(directory)) {}
Ocr::~Ocr() = default;
QVector<TextLine> Ocr::read(const cv::Mat &image, std::optional<QRect> area, double threshold, bool detection,
                            bool angle, const std::function<void()> &checkpoint) {

    std::optional<std::unique_lock<std::mutex>> lock;
    QRect bounds;
    cv::Mat source, crop;
    QVector<TextLine> result;
    std::vector<Quad> boxes;
    std::vector<cv::Mat> crops;
    std::vector<QRect> rectangles;
    std::vector<std::pair<QString, double>> recognized;
    std::pair<QString, double> single;
    const std::array args{threshold, double(detection), double(angle)};
    rust::workflowRun(
        rust::WorkflowMethod::OcrRead, args, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::OcrHasCheckpoint:
                out[0] = bool(checkpoint);
                break;
            case rust::WorkflowOp::Checkpoint:
                checkpoint();
                break;
            case rust::WorkflowOp::OcrImageEmpty:
                out[0] = image.empty();
                break;
            case rust::WorkflowOp::OcrImageBgr:
                out[0] = image.type() == CV_8UC3;
                break;
            case rust::WorkflowOp::OcrFailure:
                fail("OCR 图像或阈值无效");
                break;
            case rust::WorkflowOp::OcrLock:
                lock.emplace(impl_->mutex);
                break;
            case rust::WorkflowOp::OcrSetBounds:
                bounds = area.value_or(QRect(0, 0, image.cols, image.rows))
                             .intersected({0, 0, image.cols, image.rows});
                break;
            case rust::WorkflowOp::OcrBoundsEmpty:
                out[0] = bounds.isEmpty();
                break;
            case rust::WorkflowOp::OcrSetSource:
                source = image(cv::Rect(bounds.x(), bounds.y(), bounds.width(), bounds.height()));
                break;
            case rust::WorkflowOp::OcrCloneSource:
                crop = source.clone();
                break;
            case rust::WorkflowOp::OcrOrientCrop:
                impl_->orient(crop, checkpoint);
                break;
            case rust::WorkflowOp::OcrRecognizeSingle:
                single = impl_->recognize({crop}, checkpoint).front();
                break;
            case rust::WorkflowOp::OcrSingleScore:
                out[0] = single.second;
                break;
            case rust::WorkflowOp::OcrSingleEmpty:
                out[0] = single.first.isEmpty();
                break;
            case rust::WorkflowOp::OcrAppendSingleResult:
                result.append({single.first, bounds, single.second});
                break;
            case rust::WorkflowOp::OcrDetectSource:
                boxes = impl_->detect(source, checkpoint);
                break;
            case rust::WorkflowOp::OcrDetectedQuadCount:
                out[0] = double(boxes.size());
                break;
            case rust::WorkflowOp::OcrCropDetectedQuad:
                crop = textCrop(source, boxes.at(size_t(a[0])));
                break;
            case rust::WorkflowOp::OcrCropEmpty:
                out[0] = crop.empty();
                break;
            case rust::WorkflowOp::OcrStoreCrop: {
                const auto &q = boxes.at(size_t(a[0]));
                const auto r = cv::boundingRect(std::vector<cv::Point2f>(q.begin(), q.end()));
                crops.push_back(crop);
                rectangles.emplace_back(r.x + bounds.x(), r.y + bounds.y(), r.width, r.height);
                break;
            }
            case rust::WorkflowOp::OcrRecognizeCrops:
                recognized = impl_->recognize(crops, checkpoint);
                break;
            case rust::WorkflowOp::OcrRecognizedCropCount:
                out[0] = double(recognized.size());
                break;
            case rust::WorkflowOp::OcrCropScore:
                out[0] = recognized.at(size_t(a[0])).second;
                break;
            case rust::WorkflowOp::OcrCropTextEmpty:
                out[0] = recognized.at(size_t(a[0])).first.isEmpty();
                break;
            case rust::WorkflowOp::OcrAppendCropResult: {
                const auto i = size_t(a[0]);
                result.append({recognized.at(i).first, rectangles.at(i), recognized.at(i).second});
                break;
            }
            default:
                fail("Rust OCR 读取宿主无效");
            }
        });
    return result;
}
} // namespace okww::runtime
