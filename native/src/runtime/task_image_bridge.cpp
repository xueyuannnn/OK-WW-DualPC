#include "task.hpp"
#include "task_abi.hpp"
#include <opencv2/imgproc.hpp>
namespace okww::runtime {
bool TaskContext::wait(const std::function<bool()> &predicate, double seconds, double interval) {
    return execution.until(
        [&] {
            return rust::taskRun(rust::TaskMethod::WaitCondition, {},
                                 [&](rust::TaskOp op, const double *a, double *out) {
                                     switch (op) {
                                     case rust::TaskOp::Refresh: {
                                         refresh();
                                         break;
                                     }
                                     case rust::TaskOp::Predicate: {
                                         out[0] = predicate();
                                         break;
                                     }
                                     default:
                                         fail("Rust WaitCondition 宿主操作无效");
                                     }
                                 })[0] != 0;
        },
        seconds, interval);
}
std::optional<Match> TaskContext::find(const QString &name, MatchOptions options) {
    std::optional<Match> result;
    rust::taskRun(rust::TaskMethod::Find, {}, [&](rust::TaskOp op, const double *a, double *out) {
        switch (op) {
        case rust::TaskOp::Checkpoint: {
            execution.checkpoint();
            break;
        }
        case rust::TaskOp::FrameEmpty: {
            out[0] = frame.empty();
            break;
        }
        case rust::TaskOp::Refresh: {
            refresh();
            break;
        }
        case rust::TaskOp::FindOne: {
            result = templates.one(frame, name, options);
            break;
        }
        default:
            fail("Rust Find 宿主操作无效");
        }
    });
    return result;
}
std::optional<Match> TaskContext::findAny(const QStringList &names, MatchOptions options) {
    std::optional<Match> result;
    rust::taskRun(rust::TaskMethod::FindAny, {}, [&](rust::TaskOp op, const double *a, double *out) {
        switch (op) {
        case rust::TaskOp::FindNameCount: {
            out[0] = double(names.size());
            break;
        }
        case rust::TaskOp::FindByIndex: {
            result = find(names.at(int(a[0])), options);
            out[0] = result.has_value();
            break;
        }
        default:
            fail("Rust FindAny 宿主操作无效");
        }
    });
    return result;
}
QVector<TextLine> TaskContext::text(QRect area, double threshold) {
    QVector<TextLine> result;
    rust::taskRun(rust::TaskMethod::ReadText, {}, [&](rust::TaskOp op, const double *a, double *out) {
        switch (op) {
        case rust::TaskOp::Checkpoint: {
            execution.checkpoint();
            break;
        }
        case rust::TaskOp::ReadTextSdk: {
            result = textReader_(frame, area, threshold);
            break;
        }
        default:
            fail("Rust ReadText 宿主操作无效");
        }
    });
    return result;
}
QVector<TextLine> TaskContext::text(QRect area, double threshold, Filter filter) {
    QVector<TextLine> result;
    rust::taskRun(rust::TaskMethod::ReadText, {}, [&](rust::TaskOp op, const double *a, double *out) {
        switch (op) {
        case rust::TaskOp::Checkpoint: {
            execution.checkpoint();
            break;
        }
        case rust::TaskOp::ReadTextSdk: {
            result = textReader_(filtered(frame, filter), area, threshold);
            break;
        }
        default:
            fail("Rust ReadText 宿主操作无效");
        }
    });
    return result;
}
QVector<TextLine> TaskContext::textScaled(QRect area, double threshold, double scale) {
    QVector<TextLine> result;
    cv::Mat enlarged;
    TextLine *line = nullptr;
    const std::array args{scale};
    rust::taskRun(rust::TaskMethod::TextScaled, args, [&](rust::TaskOp op, const double *a, double *out) {
        switch (op) {
        case rust::TaskOp::Checkpoint: {
            execution.checkpoint();
            break;
        }
        case rust::TaskOp::ScaleError: {
            fail("文字放大倍率无效");
            break;
        }
        case rust::TaskOp::TextAreaIntersect: {
            area = area.intersected({0, 0, frame.cols, frame.rows});
            break;
        }
        case rust::TaskOp::TextAreaEmpty: {
            out[0] = area.isEmpty();
            break;
        }
        case rust::TaskOp::ResizeText: {
            cv::resize(frame(cv::Rect(area.x(), area.y(), area.width(), area.height())), enlarged, {}, a[0],
                       a[0], cv::INTER_CUBIC);
            break;
        }
        case rust::TaskOp::ReadScaledText: {
            result = textReader_(enlarged, {0, 0, enlarged.cols, enlarged.rows}, threshold);
            break;
        }
        case rust::TaskOp::TextCount: {
            out[0] = double(result.size());
            break;
        }
        case rust::TaskOp::SelectTextLine: {
            line = &result[int(a[0])];
            break;
        }
        case rust::TaskOp::TextBoxPosition: {
            out[0] = line->box.x();
            out[1] = line->box.y();
            break;
        }
        case rust::TaskOp::TextBoxSize: {
            out[0] = line->box.width();
            out[1] = line->box.height();
            break;
        }
        case rust::TaskOp::TextAreaPosition: {
            out[0] = area.x();
            out[1] = area.y();
            break;
        }
        case rust::TaskOp::PixelRound: {
            out[0] = pixelRound(a[0]);
            break;
        }
        case rust::TaskOp::StoreMappedTextBox: {
            line->box = {int(a[0]), int(a[1]), int(a[2]), int(a[3])};
            break;
        }
        default:
            fail("Rust TextScaled 宿主操作无效");
        }
    });
    return result;
}
void TaskContext::click(QPointF point, double after) {
    const std::array args{after};
    rust::taskRun(rust::TaskMethod::InputAfter, args, [&](rust::TaskOp op, const double *a, double *out) {
        switch (op) {
        case rust::TaskOp::SubmitInput: {
            input.taskClick(point);
            break;
        }
        case rust::TaskOp::Sleep: {
            sleep(a[0]);
            break;
        }
        default:
            fail("Rust InputAfter 宿主操作无效");
        }
    });
}
void TaskContext::clickRelative(double x, double y, double after, bool hcenter) {
    const std::array args{after};
    rust::taskRun(rust::TaskMethod::InputAfter, args, [&](rust::TaskOp op, const double *a, double *out) {
        switch (op) {
        case rust::TaskOp::SubmitInput: {
            input.taskClick(gamePoint({frame.cols, frame.rows}, x, y, hcenter));
            break;
        }
        case rust::TaskOp::Sleep: {
            sleep(a[0]);
            break;
        }
        default:
            fail("Rust InputAfter 宿主操作无效");
        }
    });
}
void TaskContext::key(const QString &key, double after, double hold) {
    const std::array args{after};
    rust::taskRun(rust::TaskMethod::InputAfter, args, [&](rust::TaskOp op, const double *a, double *out) {
        switch (op) {
        case rust::TaskOp::SubmitInput: {
            input.key(key, hold);
            break;
        }
        case rust::TaskOp::Sleep: {
            sleep(a[0]);
            break;
        }
        default:
            fail("Rust InputAfter 宿主操作无效");
        }
    });
}
QJsonValue TaskContext::setting(const QString &key, QJsonValue fallback) const {
    QJsonValue result;
    rust::taskRun(rust::TaskMethod::Setting, {}, [&](rust::TaskOp op, const double *a, double *out) {
        switch (op) {
        case rust::TaskOp::HasOverride: {
            out[0] = taskOverrides.contains(key);
            break;
        }
        case rust::TaskOp::ReadOverride: {
            result = taskOverrides[key];
            break;
        }
        case rust::TaskOp::ReadProfileSetting: {
            result = profile.value(task, key, fallback);
            break;
        }
        default:
            fail("Rust Setting 宿主操作无效");
        }
    });
    return result;
}
} // namespace okww::runtime
