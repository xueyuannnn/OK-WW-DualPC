#include "task.hpp"
#include "task_abi.hpp"
#include "workflow_abi.hpp"
#include <opencv2/imgproc.hpp>

namespace okww::runtime {
namespace {
double textRule(rust::TaskMethod method, const QVector<TextLine> &lines, const QString &pattern = {}) {
    struct Context {
        const QVector<TextLine> &lines;
        const QString &pattern;
        QString current, combined;
        QRegularExpression regex;
        std::exception_ptr error;
    } context{lines, pattern, {}, {}, {}, {}};
    const rust::TaskHost host{
        &context,
        [](void *opaque, int32_t operation, const double *a, size_t, double *out) -> int32_t {
            auto &c = *static_cast<Context *>(opaque);
            try {
                switch (static_cast<rust::TaskOp>(operation)) {
                case rust::TaskOp::TextCount:
                    out[0] = double(c.lines.size());
                    break;
                case rust::TaskOp::TextNormalize:
                    c.current = c.lines.at(int(a[0])).text;
                    c.current = c.current.remove(QRegularExpression("\\s+")).toLower();
                    break;
                case rust::TaskOp::TextRaw:
                    c.current = c.lines.at(int(a[0])).text;
                    break;
                case rust::TaskOp::RegexMatches:
                    out[0] = c.regex.match(c.current).hasMatch();
                    break;
                case rust::TaskOp::TextAppend:
                    c.combined += c.current;
                    break;
                case rust::TaskOp::TextCombined:
                    c.current = c.combined;
                    break;
                case rust::TaskOp::RequestedRegex:
                    c.regex = QRegularExpression(c.pattern, QRegularExpression::CaseInsensitiveOption);
                    if (!c.regex.isValid())
                        fail("文本匹配规则无效");
                    break;
                default:
                    fail("Rust 文本宿主操作无效");
                }
                return 1;
            } catch (...) {
                if (!c.error)
                    c.error = std::current_exception();
                return 0;
            }
        },
        [](void *opaque, int32_t kind, const uint8_t *name, size_t length, const double *, size_t count,
           double *out) -> int32_t {
            auto &c = *static_cast<Context *>(opaque);
            try {
                if (count != 0)
                    fail("Rust 文本查询参数无效");
                const auto key = QString::fromUtf8(reinterpret_cast<const char *>(name), qsizetype(length));
                switch (kind) {
                case 20:
                    out[0] = c.current.contains(key);
                    break;
                case 22:
                    c.regex = QRegularExpression(key);
                    break;
                default:
                    fail("Rust 文本查询类型无效");
                }
                return 1;
            } catch (...) {
                if (!c.error)
                    c.error = std::current_exception();
                return 0;
            }
        }};
    double out[3]{};
    const auto status = rust::okww_rs_taskflow_run(&host, int(method), nullptr, 0, out);
    if (context.error)
        std::rethrow_exception(context.error);
    if (status != 1)
        fail("Rust 文本规则执行异常");
    return out[0];
}
} // namespace
std::optional<TextLine> matchingText(const QVector<TextLine> &lines, const QString &pattern) {
    const auto index = textRule(rust::TaskMethod::MatchingText, lines, pattern);
    if (index < 0)
        return {};
    return lines.at(int(index));
}
bool monthlyTextConfirmed(const QVector<TextLine> &lines) {
    return textRule(rust::TaskMethod::MonthlyText, lines) != 0;
}
bool rewardTextConfirmed(const QVector<TextLine> &lines) {
    return textRule(rust::TaskMethod::RewardText, lines) != 0;
}
QString monthlyWindowKey(QDateTime now, int hour) {
    QDateTime popup;
    QString result;
    rust::workflowRun(
        rust::WorkflowMethod::MonthlyWindowKey, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::MonthlyWindowDay:
                out[0] = rust::okww_rs_monthly_day(hour, now.isValid(), now.time().msecsSinceStartOfDay());
                break;
            case rust::WorkflowOp::MonthlyWindowInvalidHour:
                fail("月卡时间需要为 1 到 24 点");
            case rust::WorkflowOp::MonthlyWindowCopyPopup:
                popup = now;
                break;
            case rust::WorkflowOp::MonthlyWindowSetTime:
                popup.setTime(QTime(hour % 24, 0));
                break;
            case rust::WorkflowOp::MonthlyWindowAddDays:
                popup = popup.addDays(int(a[0]));
                break;
            case rust::WorkflowOp::MonthlyWindowOffsetAllowed:
                out[0] = rust::okww_rs_monthly_offset(popup.secsTo(now));
                break;
            case rust::WorkflowOp::MonthlyWindowFormat:
                result = popup.toString("yyyy-MM-dd/HH");
                break;
            default:
                fail("Rust 月卡领取窗口宿主操作无效");
            }
        });
    return result;
}
namespace {
bool continueContourRule(const cv::Mat &frame, const std::vector<std::vector<cv::Point>> &contours) {
    struct Context {
        const cv::Mat &frame;
        const std::vector<std::vector<cv::Point>> &contours;
        std::exception_ptr error;
    } context{frame, contours, {}};
    const rust::TaskHost host{
        &context,
        [](void *opaque, int32_t operation, const double *a, size_t, double *out) -> int32_t {
            auto &c = *static_cast<Context *>(opaque);
            try {
                switch (static_cast<rust::TaskOp>(operation)) {
                case rust::TaskOp::FrameSize:
                    out[0] = c.frame.cols;
                    out[1] = c.frame.rows;
                    break;
                case rust::TaskOp::ContourCount:
                    out[0] = double(c.contours.size());
                    break;
                case rust::TaskOp::ContourMetrics: {
                    const auto &contour = c.contours.at(size_t(a[0]));
                    const auto box = cv::boundingRect(contour);
                    out[0] = box.width;
                    out[1] = box.height;
                    out[2] = cv::contourArea(contour);
                    break;
                }
                default:
                    fail("Rust 对白轮廓操作无效");
                }
                return 1;
            } catch (...) {
                if (!c.error)
                    c.error = std::current_exception();
                return 0;
            }
        },
        nullptr};
    double out[3]{};
    const auto status =
        rust::okww_rs_taskflow_run(&host, int(rust::TaskMethod::ContinueMarker), nullptr, 0, out);
    if (context.error)
        std::rethrow_exception(context.error);
    if (status != 1)
        fail("Rust 对白轮廓执行异常");
    return out[0] != 0;
}
} // namespace
bool hasContinueMarker(const cv::Mat &frame) {
    QRect rect;
    cv::Mat area, mask;
    std::vector<std::vector<cv::Point>> contours;
    return rust::workflowRun(rust::WorkflowMethod::ContinueMarkerPipeline, {},
                             [&](rust::WorkflowOp op, const double *, double *out) {
                                 switch (op) {
                                 case rust::WorkflowOp::ContinueMarkerRegion:
                                     rect = relativeBox(frame, .475, .925, .525, .98);
                                     break;
                                 case rust::WorkflowOp::ContinueMarkerRegionEmpty:
                                     out[0] = rect.isEmpty();
                                     break;
                                 case rust::WorkflowOp::ContinueMarkerPrepareArea:
                                     area = frame(cv::Rect(rect.x(), rect.y(), rect.width(), rect.height()));
                                     break;
                                 case rust::WorkflowOp::ContinueMarkerMask:
                                     mask = cv::Mat(area.rows, area.cols, CV_8U);
                                     if (!rust::okww_rs_continue_mask(area.data, area.cols, area.rows,
                                                                      area.step[0], mask.data, mask.step[0]))
                                         fail("Rust 对白像素接口异常");
                                     break;
                                 case rust::WorkflowOp::ContinueMarkerContours:
                                     cv::findContours(mask, contours, cv::RETR_EXTERNAL,
                                                      cv::CHAIN_APPROX_SIMPLE);
                                     break;
                                 case rust::WorkflowOp::ContinueMarkerExistingRule:
                                     out[0] = continueContourRule(frame, contours);
                                     break;
                                 default:
                                     fail("Rust 对白图像准备宿主操作无效");
                                 }
                             })[0] != 0;
}
} // namespace okww::runtime
