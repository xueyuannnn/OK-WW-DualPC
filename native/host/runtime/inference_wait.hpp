#pragma once
#include "workflow_abi.hpp"
#include <openvino/openvino.hpp>
namespace okww::runtime {
inline void inferWithCheckpoint(ov::InferRequest &request, const std::function<void()> &checkpoint,
                                const QString &name) {
    QElapsedTimer elapsed;
    const std::array args{double(bool(checkpoint))};
    rust::workflowRun(rust::WorkflowMethod::InferenceWait, args,
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::Checkpoint:
                              checkpoint();
                              break;
                          case rust::WorkflowOp::InferenceSynchronous:
                              request.infer();
                              break;
                          case rust::WorkflowOp::InferenceStartTimer:
                              elapsed.start();
                              break;
                          case rust::WorkflowOp::InferenceStart:
                              request.start_async();
                              break;
                          case rust::WorkflowOp::InferenceReady:
                              out[0] = request.wait_for(std::chrono::milliseconds(int(a[0])));
                              break;
                          case rust::WorkflowOp::InferenceElapsedMs:
                              out[0] = double(elapsed.elapsed());
                              break;
                          case rust::WorkflowOp::InferenceTimeout:
                              fail(name + "推理超过 10 秒，自动任务已停止");
                              break;
                          case rust::WorkflowOp::InferenceCancel:
                              request.cancel();
                              break;
                          case rust::WorkflowOp::InferenceDrain:
                              request.wait();
                              break;
                          default:
                              fail("Rust 推理等待宿主无效");
                          }
                      });
}
} // namespace okww::runtime
