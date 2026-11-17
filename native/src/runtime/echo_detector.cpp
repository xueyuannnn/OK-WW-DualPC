#include "echo_detector.hpp"
#include "echo_detection_abi.hpp"
#include "echo_detection_host.hpp"
#include "inference_wait.hpp"
#include <opencv2/imgproc.hpp>
#include <openvino/openvino.hpp>
namespace okww::runtime {
QVector<Match> echoPrediction(const float *scores, size_t length, size_t channels, size_t rows, double gain,
                              int left, int top, double threshold, int label,
                              const std::function<void()> &checkpoint) {
    struct Context {
        const std::function<void()> &checkpoint;
        std::exception_ptr error;
    } context{checkpoint, {}};
    const rust::EchoCheckpoint host{&context, [](void *opaque) -> int32_t {
                                        auto &c = *static_cast<Context *>(opaque);
                                        try {
                                            c.checkpoint();
                                            return 1;
                                        } catch (...) {
                                            c.error = std::current_exception();
                                            return 0;
                                        }
                                    }};
    const rust::EchoPrediction request{scores,    length, channels, rows,  gain,
                                       threshold, left,   top,      label, int32_t(bool(checkpoint))};
    int32_t status = 0;
    std::unique_ptr<rust::EchoDetections, decltype(&rust::okww_rs_echo_detection_free)> detections(
        rust::okww_rs_echo_postprocess(&request, &host, &status), rust::okww_rs_echo_detection_free);
    if (context.error)
        std::rethrow_exception(context.error);
    if (status == -3)
        throw std::bad_alloc();
    if (status == -1)
        fail("声骸模型返回异常坐标");
    if (status != 1)
        fail("Rust 声骸后处理接口异常");
    QVector<rust::EchoDetection> items(qsizetype(rust::okww_rs_echo_detection_count(detections.get())));
    if (!rust::okww_rs_echo_detections(detections.get(), items.data(), size_t(items.size())))
        fail("Rust 声骸输出接口异常");
    QVector<Match> result;
    result.reserve(items.size());
    for (const auto &item : items)
        result.append({item.category == 0 ? "echo" : "unknown",
                       {item.x, item.y, item.width, item.height},
                       item.confidence});
    return result;
}
struct EchoDetector::Impl {
    ov::Core core;
    ov::CompiledModel model;
    ov::InferRequest request;
    int width = 0, height = 0;
    std::mutex mutex;
    explicit Impl(const QString &path) {
        ov::Shape shape;
        rust::workflowRun(rust::WorkflowMethod::EchoModelInitialize, {},
                          [&](rust::WorkflowOp op, const double *a, double *out) {
                              switch (op) {
                              case rust::WorkflowOp::EchoModelExists:
                                  out[0] = QFile::exists(path);
                                  break;
                              case rust::WorkflowOp::CompileEchoModel:
                                  model = core.compile_model(path.toStdString(), "CPU",
                                                             ov::inference_num_threads(2));
                                  shape = model.input().get_shape();
                                  break;
                              case rust::WorkflowOp::EchoModelShape:
                                  out[0] = double(shape.size());
                                  out[1] = shape.empty() ? 0 : double(shape[0]);
                                  out[2] = shape.size() < 2 ? 0 : double(shape[1]);
                                  break;
                              case rust::WorkflowOp::EchoModelDimensions:
                                  out[0] = double(shape[2]);
                                  out[1] = double(shape[3]);
                                  break;
                              case rust::WorkflowOp::EchoModelStoreDimensions:
                                  height = int(a[0]);
                                  width = int(a[1]);
                                  break;
                              case rust::WorkflowOp::CreateEchoRequest:
                                  request = model.create_infer_request();
                                  break;
                              case rust::WorkflowOp::EchoModelFailure:
                                  fail(int(a[0]) == 0 ? "声骸识别模型不存在：" + path
                                                      : QString("声骸模型输入尺寸与原模型不一致"));
                                  break;
                              default:
                                  fail("Rust 声骸模型宿主无效");
                              }
                          });
    }
};
EchoDetector::EchoDetector(const QString &model) : impl_(std::make_unique<Impl>(model)) {}
EchoDetector::~EchoDetector() = default;
QVector<Match> EchoDetector::detect(const cv::Mat &image, double threshold, int label,
                                    const std::function<void()> &checkpoint) {
    std::optional<std::unique_lock<std::mutex>> lock;
    cv::Mat resized, padded;
    ov::Tensor tensor, prediction;
    ov::Shape shape;
    double gain = 0;
    int w = 0, h = 0, left = 0, top = 0;
    QVector<Match> result;
    const std::array args{threshold, double(label)};
    rust::workflowRun(
        rust::WorkflowMethod::EchoModelDetect, args, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::EchoModelHasCheckpoint:
                out[0] = bool(checkpoint);
                break;
            case rust::WorkflowOp::Checkpoint:
                checkpoint();
                break;
            case rust::WorkflowOp::EchoModelImage:
                out[0] = image.empty();
                out[1] = image.type() == CV_8UC3;
                break;
            case rust::WorkflowOp::EchoModelFailure:
                fail(int(a[0]) == 2 ? "声骸识别图像或阈值无效" : "声骸模型输出格式无效");
                break;
            case rust::WorkflowOp::LockEchoModel:
                lock.emplace(impl_->mutex);
                break;
            case rust::WorkflowOp::EchoModelImageSize:
                out[0] = image.cols;
                out[1] = image.rows;
                break;
            case rust::WorkflowOp::EchoModelTargetSize:
                out[0] = impl_->width;
                out[1] = impl_->height;
                break;
            case rust::WorkflowOp::EchoLayoutFailure:
                fail("坐标或移动距离无效");
                break;
            case rust::WorkflowOp::StoreEchoLayout:
                gain = a[0];
                w = int(a[1]);
                h = int(a[2]);
                break;
            case rust::WorkflowOp::StoreEchoPadding:
                left = int(a[0]);
                top = int(a[1]);
                break;
            case rust::WorkflowOp::ResizeEchoImage:
                cv::resize(image, resized, {w, h}, 0, 0, cv::INTER_LINEAR);
                break;
            case rust::WorkflowOp::PadEchoImage:
                cv::copyMakeBorder(resized, padded, top, impl_->height - h - top, left,
                                   impl_->width - w - left, cv::BORDER_CONSTANT, {114, 114, 114});
                break;
            case rust::WorkflowOp::CreateEchoTensor:
                tensor = ov::Tensor(ov::element::f32, {1, 3, size_t(impl_->height), size_t(impl_->width)});
                break;
            case rust::WorkflowOp::FillEchoTensor:
                if (!rust::okww_rs_echo_tensor(padded.data, size_t(padded.cols), size_t(padded.rows),
                                               padded.step[0], tensor.data<float>(), tensor.get_size()))
                    fail("Rust 声骸图像接口异常");
                break;
            case rust::WorkflowOp::SetEchoTensor:
                impl_->request.set_input_tensor(tensor);
                break;
            case rust::WorkflowOp::InferEchoModel:
                inferWithCheckpoint(impl_->request, checkpoint, "声骸识别");
                break;
            case rust::WorkflowOp::ReadEchoTensor:
                prediction = impl_->request.get_output_tensor();
                shape = prediction.get_shape();
                break;
            case rust::WorkflowOp::EchoPredictionShape:
                out[0] = double(shape.size());
                out[1] = shape.empty() ? 0 : double(shape[0]);
                out[2] = shape.size() < 2 ? 0 : double(shape[1]);
                break;
            case rust::WorkflowOp::EchoPredictionFloat:
                out[0] = prediction.get_element_type() == ov::element::f32;
                break;
            case rust::WorkflowOp::PostprocessEchoTensor:
                result = echoPrediction(prediction.data<const float>(), prediction.get_size(), shape[1],
                                        shape[2], gain, left, top, a[0], int(a[1]), checkpoint);
                break;
            default:
                fail("Rust 声骸识别宿主无效");
            }
        });
    return result;
}
QString echoWalkDirection(const QVector<Match> &echoes, QSize screen, bool moving) {
    const QStringList directions{"w", "a", "s", "d"};
    const std::array args{double(moving)};
    const auto result = rust::workflowRun(rust::WorkflowMethod::WorldEchoDirection, args,
                                          [&](rust::WorkflowOp op, const double *, double *out) {
                                              switch (op) {
                                              case rust::WorkflowOp::WorldEchoCount:
                                                  out[0] = double(echoes.size());
                                                  break;
                                              case rust::WorkflowOp::WorldFirstEchoGeometry:
                                                  out[0] = echoes.front().center().x();
                                                  out[1] =
                                                      echoes.front().box.y() + echoes.front().box.height();
                                                  out[2] = screen.height();
                                                  break;
                                              case rust::WorkflowOp::WorldFrameWidth:
                                                  out[0] = screen.width();
                                                  break;
                                              default:
                                                  fail("Rust 声骸方向宿主无效");
                                              }
                                          });
    return directions.at(int(result[0]));
}
} // namespace okww::runtime
