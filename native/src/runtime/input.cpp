#include "input.hpp"
#include "controller.hpp"
#include "diagnostics.hpp"
#include "workflow_abi.hpp"
namespace okww::runtime {
int Input::keyCode(QString key) {
    key = key.toLower();
    const auto bytes = key.toUtf8();
    const int result =
        rust::okww_rs_key_code(reinterpret_cast<const uint8_t *>(bytes.constData()), size_t(bytes.size()));
    if (result < 0)
        fail("不支持的 USB HID 按键：" + key);
    return result;
}

std::array<double, 3> Input::rustInput(int method, std::initializer_list<double> args, RustRequest request,
                                       QJsonObject *reply) {
    QString blockedText, stroke;
    bool dismissBlocked = false;
    QJsonObject result;
    QVector<QPair<QString, bool>> strokes;
    QRectF crop(0, 0, rawSize_.width(), rawSize_.height());
    QJsonArray cropValues;
    const auto out = rust::workflowRun(
        rust::WorkflowMethod(method), {args.begin(), args.size()},
        [&](rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &error) {
            switch (op) {
            case rust::WorkflowOp::Now:
                out[0] = execution_.now();
                break;
            case rust::WorkflowOp::Checkpoint:
                execution_.checkpoint();
                break;
            case rust::WorkflowOp::ExecutionWait:
                execution_.wait(a[0]);
                break;
            case rust::WorkflowOp::PixelRound:
                out[0] = pixelRound(a[0]);
                break;
            case rust::WorkflowOp::InputFailure: {
                const QStringList messages{"输入自动恢复超过 120 秒，仍未返回可操作状态；已保留当前任务现场",
                                           "鼠标分段参数无效",
                                           "鼠标移动距离过大",
                                           "工坊输入请求无效",
                                           "无效鼠标按键",
                                           "拖动前需要已知鼠标起点",
                                           "拖动时间无效",
                                           "点击坐标超出识别画面",
                                           "裁剪参数无效",
                                           "裁剪区域超出采集画面",
                                           "游戏机桌面分辨率无效"};
                if (int(a[0]) == 0)
                    throw ExecutionFailure(messages[0], blockedText);
                fail(messages.at(int(a[0])));
                break;
            }
            case rust::WorkflowOp::InputConfigNumber: {
                const QStringList keys{"kmbox.move_chunk",         "kmbox.move_interval",
                                       "kmbox.home_counts",        "kmbox.home_wait",
                                       "kmbox.counts_per_pixel_x", "kmbox.counts_per_pixel_y",
                                       "kmbox.position_settle"};
                out[0] = (int(a[0]) == 0 || int(a[0]) == 2) ? config_.n(keys.at(int(a[0])))
                                                            : config_.d(keys.at(int(a[0])));
                break;
            }
            case rust::WorkflowOp::InputTransportCommand:
                result = transport_(request.command);
                break;
            case rust::WorkflowOp::InputTransmitNested:
                result = transmit(request.command);
                break;
            case rust::WorkflowOp::InputIsBlocked:
            case rust::WorkflowOp::InputClassifyBlocked:
                try {
                    if (error)
                        std::rethrow_exception(error);
                    out[0] = false;
                } catch (const LocalInputBlocked &e) {
                    out[0] = true;
                    if (op == rust::WorkflowOp::InputClassifyBlocked) {
                        blockedText = errorText(e);
                        dismissBlocked = e.recovery == LocalInputBlocked::Recovery::DismissRunDialog;
                    }
                } catch (...) {
                    out[0] = false;
                }
                break;
            case rust::WorkflowOp::InputDismissBlocked:
                out[0] = dismissBlocked;
                break;
            case rust::WorkflowOp::InputHasRecovery:
                out[0] = bool(recovery);
                break;
            case rust::WorkflowOp::InputRecoveryReady:
                recovery("输入已恢复，继续当前任务");
                break;
            case rust::WorkflowOp::InputRecoveryNotice:
                recovery(QString("正在自动恢复输入 · %1 秒 · %2").arg(int(a[0])).arg(blockedText));
                break;
            case rust::WorkflowOp::InputTransportEscape:
                transport_({{"op", "key"}, {"code", keyCode("esc")}, {"down", bool(a[0])}});
                break;
            case rust::WorkflowOp::InputRelease:
                release();
                break;
            case rust::WorkflowOp::InputCommandKind: {
                const QHash<QString, int> kinds{{"release", 0},   {"key", 1},   {"button", 2}, {"move", 3},
                                                {"absolute", 3},  {"wheel", 4}, {"state", 4},  {"connect", 4},
                                                {"heartbeat", 4}, {"probe", 4}};
                out[0] = kinds.value(request.command["op"].toString(), -1);
                break;
            }
            case rust::WorkflowOp::InputCommandDown:
                out[0] = request.command["down"].toBool();
                break;
            case rust::WorkflowOp::InputTrackCommandKey: {
                const int code = request.command["code"].toInt();
                if (request.command["down"].toBool())
                    keys_.insert(code);
                else
                    keys_.remove(code);
                break;
            }
            case rust::WorkflowOp::InputTrackCommandButton: {
                const auto name = request.command["name"].toString();
                if (request.command["down"].toBool())
                    buttons_.insert(name);
                else
                    buttons_.remove(name);
                break;
            }
            case rust::WorkflowOp::InputForgetPosition:
                position_.reset();
                positionReadyAt_ = -1;
                break;
            case rust::WorkflowOp::InputButtonsEmpty:
                out[0] = buttons_.isEmpty();
                break;
            case rust::WorkflowOp::InputSettle:
                settlePosition();
                break;
            case rust::WorkflowOp::InputPositionReadyAt:
                out[0] = positionReadyAt_;
                break;
            case rust::WorkflowOp::InputSetReadyAt:
                positionReadyAt_ = a[0];
                break;
            case rust::WorkflowOp::InputKeyCode:
                out[0] = keyCode(request.key);
                break;
            case rust::WorkflowOp::InputKeyHeld:
                out[0] = keys_.contains(int(a[0]));
                break;
            case rust::WorkflowOp::InputTrackKey:
                if (a[1])
                    keys_.insert(int(a[0]));
                else
                    keys_.remove(int(a[0]));
                break;
            case rust::WorkflowOp::InputSendKey:
                send({{"op", "key"}, {"code", int(a[0])}, {"down", bool(a[1])}});
                break;
            case rust::WorkflowOp::InputTransportKey:
                transport_({{"op", "key"}, {"code", int(a[0])}, {"down", bool(a[1])}});
                break;
            case rust::WorkflowOp::InputKeyDownNested:
                keyDown(request.key);
                break;
            case rust::WorkflowOp::InputKeyUpNested:
                keyUp(request.key);
                break;
            case rust::WorkflowOp::InputTrackButton:
                if (a[0])
                    buttons_.insert(request.key);
                else
                    buttons_.remove(request.key);
                break;
            case rust::WorkflowOp::InputSendButton:
                send({{"op", "button"}, {"name", request.key}, {"down", bool(a[0])}});
                break;
            case rust::WorkflowOp::InputTransportButton:
                transport_({{"op", "button"}, {"name", request.key}, {"down", bool(a[0])}});
                break;
            case rust::WorkflowOp::InputButtonValid:
                out[0] = QStringList{"left", "right", "middle"}.contains(request.key);
                break;
            case rust::WorkflowOp::InputLocal:
                out[0] = config_.s("kmbox.protocol") == "sendinput";
                break;
            case rust::WorkflowOp::InputSendMove:
                send({{"op", "move"}, {"x", int(a[0])}, {"y", int(a[1])}});
                break;
            case rust::WorkflowOp::InputScreenPoint: {
                const auto p = screenPoint(*request.point);
                out[0] = p.x();
                out[1] = p.y();
                break;
            }
            case rust::WorkflowOp::InputOriginPoint: {
                const auto p = screenPoint(*position_);
                out[0] = p.x();
                out[1] = p.y();
                break;
            }
            case rust::WorkflowOp::InputAbsolute:
                out[0] = absolute_;
                break;
            case rust::WorkflowOp::InputSendAbsolute:
                send({{"op", "absolute"},
                      {"x", int(a[0])},
                      {"y", int(a[1])},
                      {"width", config_.n("kmbox.desktop_width")},
                      {"height", config_.n("kmbox.desktop_height")}});
                break;
            case rust::WorkflowOp::InputHasPosition:
                out[0] = bool(position_);
                break;
            case rust::WorkflowOp::InputStorePosition:
                position_ = request.point;
                break;
            case rust::WorkflowOp::InputCountsNested:
                counts(int(a[0]), int(a[1]), bool(a[2]));
                break;
            case rust::WorkflowOp::InputHasRequestedPoint:
                out[0] = bool(request.point);
                break;
            case rust::WorkflowOp::InputMoveNested:
                move(*request.point);
                break;
            case rust::WorkflowOp::InputButtonDownNested:
                buttonDown(request.key);
                break;
            case rust::WorkflowOp::InputButtonUpNested:
                buttonUp(request.key);
                break;
            case rust::WorkflowOp::InputSendWheel:
                send({{"op", "wheel"}, {"steps", int(a[0])}});
                break;
            case rust::WorkflowOp::InputMoveInterpolated:
                move(*request.point + (request.to - *request.point) * a[0]);
                break;
            case rust::WorkflowOp::InputTextLength:
                out[0] = double(request.text.size());
                break;
            case rust::WorkflowOp::InputPrepareStroke: {
                const QString shifted = "!@#$%^&*()_+{}|:\"~<>?", base = "1234567890-=[]\\;'`,./";
                const QChar c = request.text.at(int(a[0]));
                const auto pos = shifted.indexOf(c);
                QString k = pos >= 0 ? QString(base[pos]) : QString(c.toLower());
                if (c == ' ')
                    k = "space";
                else if (c == '\n')
                    k = "enter";
                else if (c == '\t')
                    k = "tab";
                keyCode(k);
                strokes.append({k, c.isUpper() || pos >= 0});
                break;
            }
            case rust::WorkflowOp::InputLoadStroke: {
                const auto &s = strokes.at(int(a[0]));
                stroke = s.first;
                out[0] = s.second;
                break;
            }
            case rust::WorkflowOp::InputShiftDown:
                keyDown("shift");
                break;
            case rust::WorkflowOp::InputShiftUp:
                keyUp("shift");
                break;
            case rust::WorkflowOp::InputStroke:
                key(stroke);
                break;
            case rust::WorkflowOp::InputRawSize:
                out[0] = rawSize_.width();
                out[1] = rawSize_.height();
                break;
            case rust::WorkflowOp::InputRequestedPoint:
                out[0] = request.point->x();
                out[1] = request.point->y();
                break;
            case rust::WorkflowOp::InputOutputSize:
                out[0] = config_.n("capture.output_width");
                out[1] = config_.n("capture.output_height");
                break;
            case rust::WorkflowOp::InputCropCount:
                cropValues = config_.get("capture.crop").toArray();
                out[0] = double(cropValues.size());
                break;
            case rust::WorkflowOp::InputLoadCrop:
                crop = QRectF(cropValues[0].toDouble(), cropValues[1].toDouble(), cropValues[2].toDouble(),
                              cropValues[3].toDouble());
                break;
            case rust::WorkflowOp::InputCropValid:
                out[0] = !crop.isEmpty() && QRectF(QPointF(0, 0), rawSize_).contains(crop);
                break;
            case rust::WorkflowOp::InputCropGeometry:
                out[0] = crop.x();
                out[1] = crop.y();
                break;
            case rust::WorkflowOp::InputCropSize:
                out[0] = crop.width();
                out[1] = crop.height();
                break;
            case rust::WorkflowOp::InputDesktopSize:
                out[0] = config_.n("kmbox.desktop_width");
                out[1] = config_.n("kmbox.desktop_height");
                break;
            default:
                fail("Rust 输入宿主操作无效");
            }
        });
    if (reply)
        *reply = result;
    return out;
}

QJsonObject Input::workshopCommand(QJsonObject command) {
    RustRequest r;
    r.command = std::move(command);
    QJsonObject reply;
    rustInput(int(rust::WorkflowMethod::InputWorkshopCommand), {}, std::move(r), &reply);
    return reply;
}
void Input::send(QJsonObject command) {
    RustRequest r;
    r.command = std::move(command);
    rustInput(int(rust::WorkflowMethod::InputSend), {}, std::move(r));
}
QJsonObject Input::transmit(QJsonObject command) {
    RustRequest r;
    r.command = std::move(command);
    QJsonObject reply;
    rustInput(int(rust::WorkflowMethod::InputTransmit), {}, std::move(r), &reply);
    return reply;
}
QPoint Input::screenPoint(QPointF point) const {
    RustRequest r;
    r.point = point;
    const auto out =
        const_cast<Input *>(this)->rustInput(int(rust::WorkflowMethod::InputScreenPoint), {}, std::move(r));
    return {int(out[0]), int(out[1])};
}
void Input::counts(int x, int y, bool precise) {
    rustInput(int(rust::WorkflowMethod::InputCounts), {double(x), double(y), double(precise)}, {});
}
void Input::settlePosition() { rustInput(int(rust::WorkflowMethod::InputSettle), {}, {}); }
void Input::relative(double x, double y) { rustInput(int(rust::WorkflowMethod::InputRelative), {x, y}, {}); }
void Input::text(const QString &value) {
    RustRequest r;
    r.text = value;
    rustInput(int(rust::WorkflowMethod::InputText), {}, std::move(r));
}
void Input::move(QPointF point) {
    RustRequest r;
    r.point = point;
    rustInput(int(rust::WorkflowMethod::InputMove), {}, std::move(r));
}
void Input::click(std::optional<QPointF> point, double seconds, QString button) {
    RustRequest r;
    r.point = point;
    r.key = std::move(button);
    rustInput(int(rust::WorkflowMethod::InputClick), {seconds}, std::move(r));
}
void Input::scroll(int steps, std::optional<QPointF> point) {
    RustRequest r;
    r.point = point;
    rustInput(int(rust::WorkflowMethod::InputScroll), {double(steps)}, std::move(r));
}
void Input::swipe(QPointF from, QPointF to, double milliseconds, double settle) {
    RustRequest r;
    r.point = from;
    r.to = to;
    r.key = "left";
    rustInput(int(rust::WorkflowMethod::InputSwipe), {milliseconds, settle}, std::move(r));
}
void Input::keyDown(QString value) {
    RustRequest r;
    r.key = std::move(value);
    rustInput(int(rust::WorkflowMethod::InputKeyDown), {}, std::move(r));
}
void Input::keyUp(QString value) {
    RustRequest r;
    r.key = std::move(value);
    rustInput(int(rust::WorkflowMethod::InputKeyUp), {}, std::move(r));
}
void Input::buttonDown(QString value) {
    RustRequest r;
    r.key = std::move(value);
    rustInput(int(rust::WorkflowMethod::InputButtonDown), {}, std::move(r));
}
void Input::buttonUp(QString value) {
    RustRequest r;
    r.key = std::move(value);
    rustInput(int(rust::WorkflowMethod::InputButtonUp), {}, std::move(r));
}
void Input::key(QString value, double seconds) {
    RustRequest r;
    r.key = std::move(value);
    rustInput(int(rust::WorkflowMethod::InputKey), {seconds}, std::move(r));
}
void Input::release() noexcept {
    try {
        transport_({{"op", "release"}});
    } catch (...) {
    }
    keys_.clear();
    buttons_.clear();
    position_.reset();
    positionReadyAt_ = -1;
}
} // namespace okww::runtime
