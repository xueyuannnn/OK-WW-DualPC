#include "notification.hpp"
#include "workflow_abi.hpp"
#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSslSocket>

namespace okww::runtime {
namespace {
[[noreturn]] void noticeFailure(int code, const char *key = "") {
    if (code == 0)
        fail(QString("通知配置不完整：") + key);
    static const QStringList messages{"",
                                      "通知配置中不能包含换行或空字符",
                                      "通知 Webhook 必须是有效的 HTTPS 地址",
                                      "发件人和收件人需填写单个完整邮箱地址",
                                      "通知已取消",
                                      "Telegram 机器人令牌格式错误",
                                      "企业微信图片超过 2 MB，请降低通知截图尺寸",
                                      "QQ 应用和频道 ID 应为数字",
                                      "未知网络通知渠道",
                                      "通知服务返回了无法识别的结果",
                                      "Telegram 拒绝通知，请检查令牌和接收权限",
                                      "企业微信拒绝通知，请检查 Webhook 和消息限制",
                                      "无效邮件边界"};
    fail(messages.at(code));
}
QString required(const QJsonObject &c, const char *key) {
    QString value;
    rust::workflowRun(
        rust::WorkflowMethod::NoticeRequired, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::NoticeTrimConfig:
                value = c[key].toString().trimmed();
                break;
            case rust::WorkflowOp::NoticeConfigEmpty:
                out[0] = value.isEmpty();
                break;
            case rust::WorkflowOp::NoticeConfigControl:
                out[0] =
                    value.contains(std::array<QChar, 3>{QChar('\r'), QChar('\n'), QChar(0)}.at(int(a[0])));
                break;
            case rust::WorkflowOp::NoticeFailure:
                noticeFailure(int(a[0]), key);
                break;
            default:
                fail("Rust 通知配置宿主无效");
            }
        });
    return value;
}
QString content(const Notice &n) {
    QString result;
    rust::workflowRun(rust::WorkflowMethod::NoticeContent, {},
                      [&](rust::WorkflowOp op, const double *, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::NoticeTitleEmpty:
                              out[0] = n.title.isEmpty();
                              break;
                          case rust::WorkflowOp::NoticeReturnText:
                              result = n.text;
                              break;
                          case rust::WorkflowOp::NoticeReturnTitleAndText:
                              result = n.title + '\n' + n.text;
                              break;
                          default:
                              fail("Rust 通知正文宿主无效");
                          }
                      });
    return result;
}
QStringList chunks(QString text, int length) {
    QStringList result;
    const std::array<double, 1> args{double(length)};
    rust::workflowRun(rust::WorkflowMethod::NoticeChunks, args,
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::NoticeChunkTextEmpty:
                              out[0] = text.isEmpty();
                              break;
                          case rust::WorkflowOp::NoticeChunkTextSize:
                              out[0] = int(text.size());
                              break;
                          case rust::WorkflowOp::NoticeChunkEndsHighSurrogate:
                              out[0] = text[int(a[0]) - 1].isHighSurrogate();
                              break;
                          case rust::WorkflowOp::NoticeAppendChunk:
                              result << text.left(int(a[0]));
                              break;
                          case rust::WorkflowOp::NoticeConsumeChunk:
                              text.remove(0, int(a[0]));
                              break;
                          default:
                              fail("Rust 通知分段宿主无效");
                          }
                      });
    return result;
}
QUrl webhook(QString value) {
    QUrl result;
    rust::workflowRun(rust::WorkflowMethod::NoticeWebhook, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::NoticeParseWebhook:
                              result = QUrl(value);
                              break;
                          case rust::WorkflowOp::NoticeWebhookValid:
                              out[0] = result.isValid();
                              break;
                          case rust::WorkflowOp::NoticeWebhookHttps:
                              out[0] = result.scheme() == "https";
                              break;
                          case rust::WorkflowOp::NoticeWebhookHostEmpty:
                              out[0] = result.host().isEmpty();
                              break;
                          case rust::WorkflowOp::NoticeWebhookUserEmpty:
                              out[0] = result.userInfo().isEmpty();
                              break;
                          case rust::WorkflowOp::NoticeFailure:
                              noticeFailure(int(a[0]));
                              break;
                          default:
                              fail("Rust 通知 Webhook 宿主无效");
                          }
                      });
    return result;
}
NoticeRequest json(QString provider, QUrl url, QJsonObject body) {
    return {provider, url, "application/json", QJsonDocument(body).toJson(QJsonDocument::Compact), {}};
}
NoticeRequest multipart(QString provider, QUrl url, QMap<QString, QByteArray> fields,
                        QVector<QPair<QString, QByteArray>> files) {
    QByteArray boundary, body;
    QStringList keys;
    NoticeRequest result;
    rust::workflowRun(
        rust::WorkflowMethod::NoticeMultipart, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::NoticeNewBoundary:
                boundary = "okww-" + QUuid::createUuid().toByteArray(QUuid::Id128);
                break;
            case rust::WorkflowOp::NoticeLoadFieldKeys:
                keys = fields.keys();
                break;
            case rust::WorkflowOp::NoticeFieldCount:
                out[0] = double(keys.size());
                break;
            case rust::WorkflowOp::NoticeAppendFieldPart: {
                const auto key = keys.at(int(a[0]));
                body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + key.toUtf8() +
                        "\"\r\n\r\n" + fields.value(key) + "\r\n";
                break;
            }
            case rust::WorkflowOp::NoticeFileCount:
                out[0] = double(files.size());
                break;
            case rust::WorkflowOp::NoticeAppendFilePart: {
                const auto &file = files.at(int(a[0]));
                body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" +
                        file.first.toUtf8() + "\"; filename=\"notification_" + QByteArray::number(int(a[1])) +
                        ".png\"\r\nContent-Type: image/png\r\n\r\n" + file.second + "\r\n";
                break;
            }
            case rust::WorkflowOp::NoticeCloseMultipart:
                body += "--" + boundary + "--\r\n";
                break;
            case rust::WorkflowOp::NoticeReturnMultipart:
                result = {provider, url, "multipart/form-data; boundary=" + boundary, body, {}};
                break;
            default:
                fail("Rust 通知附件宿主无效");
            }
        });
    return result;
}
QByteArray wrapped64(QByteArray data) {
    QByteArray result;
    rust::workflowRun(rust::WorkflowMethod::NoticeWrappedBase64, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::NoticeEncodeBase64:
                              data = data.toBase64();
                              break;
                          case rust::WorkflowOp::NoticeEncodedSize:
                              out[0] = double(data.size());
                              break;
                          case rust::WorkflowOp::NoticeAppendBase64Line:
                              result += data.mid(qsizetype(a[0]), qsizetype(a[1])) + "\r\n";
                              break;
                          default:
                              fail("Rust 通知 Base64 宿主无效");
                          }
                      });
    return result;
}
QString address(QString text) {
    rust::workflowRun(rust::WorkflowMethod::NoticeAddress, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::NoticeMailboxValid:
                              out[0] = QRegularExpression("^[^\\s<>@,;\\x00-\\x1f]+@[^\\s<>@,;\\x00-\\x1f]+$")
                                           .match(text)
                                           .hasMatch();
                              break;
                          case rust::WorkflowOp::NoticeFailure:
                              noticeFailure(int(a[0]));
                              break;
                          default:
                              fail("Rust 通知邮箱宿主无效");
                          }
                      });
    return text;
}
void checkpoint(const NoticeCancelled &cancel) {
    rust::workflowRun(rust::WorkflowMethod::NoticeCheckpoint, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::NoticeHasCancellation:
                              out[0] = bool(cancel);
                              break;
                          case rust::WorkflowOp::NoticeCancelled:
                              out[0] = cancel();
                              break;
                          case rust::WorkflowOp::NoticeFailure:
                              noticeFailure(int(a[0]));
                              break;
                          default:
                              fail("Rust 通知取消宿主无效");
                          }
                      });
}
} // namespace
QVector<NoticeRequest> noticeRequests(const QString &provider, const QJsonObject &c, const Notice &n) {
    QVector<NoticeRequest> out;
    QUrl url;
    QStringList parts, qqFields{QString{}, QString{}, QString{}};
    QString token, chat, base, text;
    QJsonObject payload;
    QVector<QPair<QString, QByteArray>> files;
    NoticeRequest request;
    rust::workflowRun(
        rust::WorkflowMethod::NoticeRequests, {}, [&](rust::WorkflowOp op, const double *a, double *result) {
            switch (op) {
            case rust::WorkflowOp::NoticeProviderKind:
                result[0] = QStringList{"Discord", "Telegram", "WeCom", "QQ Bot"}.indexOf(provider);
                break;
            case rust::WorkflowOp::NoticeDiscordWebhook:
                url = webhook(required(c, "Discord Webhook"));
                break;
            case rust::WorkflowOp::NoticeContentChunks:
                parts = chunks(content(n), int(a[0]));
                break;
            case rust::WorkflowOp::NoticePartCount:
                result[0] = double(parts.size());
                break;
            case rust::WorkflowOp::NoticeAppendEmptyPart:
                parts << QString{};
                break;
            case rust::WorkflowOp::NoticeDiscordPayload:
                payload = {{"content", parts.at(int(a[0]))},
                           {"username", "OKWW"},
                           {"allowed_mentions", QJsonObject{{"parse", QJsonArray{}}}}};
                break;
            case rust::WorkflowOp::NoticeClearFiles:
                files.clear();
                break;
            case rust::WorkflowOp::NoticeImageCount:
                result[0] = double(n.images.size());
                break;
            case rust::WorkflowOp::NoticeAppendDiscordImage:
                files.append({QString("files[%1]").arg(int(a[0])), n.images.at(int(a[0]))});
                break;
            case rust::WorkflowOp::NoticeAppendDiscordRequest:
                out << multipart(provider, url,
                                 {{"payload_json", QJsonDocument(payload).toJson(QJsonDocument::Compact)}},
                                 files);
                break;
            case rust::WorkflowOp::NoticeTelegramToken:
                token = required(c, "Telegram Bot Token");
                break;
            case rust::WorkflowOp::NoticeTelegramChat:
                chat = required(c, "Telegram Chat ID");
                break;
            case rust::WorkflowOp::NoticeTelegramTokenValid:
                result[0] = QRegularExpression("^[0-9]+:[A-Za-z0-9_-]+$").match(token).hasMatch();
                break;
            case rust::WorkflowOp::NoticeTelegramBase:
                base = "https://api.telegram.org/bot" + token;
                break;
            case rust::WorkflowOp::NoticeAppendTelegramText:
                out << json(provider, QUrl(base + "/sendMessage"),
                            {{"chat_id", chat}, {"text", parts.at(int(a[0]))}});
                break;
            case rust::WorkflowOp::NoticeAppendTelegramImage:
                out << multipart(provider, QUrl(base + "/sendPhoto"), {{"chat_id", chat.toUtf8()}},
                                 {{"photo", n.images.at(int(a[0]))}});
                break;
            case rust::WorkflowOp::NoticeWecomWebhook:
                url = webhook(required(c, "Enterprise WeChat Webhook URL"));
                break;
            case rust::WorkflowOp::NoticeAppendWecomText:
                out << json(
                    provider, url,
                    {{"msgtype", "markdown"}, {"markdown", QJsonObject{{"content", parts.at(int(a[0]))}}}});
                break;
            case rust::WorkflowOp::NoticeImageSize:
                result[0] = double(n.images.at(int(a[0])).size());
                break;
            case rust::WorkflowOp::NoticeAppendWecomImage: {
                const auto &image = n.images.at(int(a[0]));
                out << json(
                    provider, url,
                    {{"msgtype", "image"},
                     {"image",
                      QJsonObject{
                          {"base64", QString::fromLatin1(image.toBase64())},
                          {"md5", QString::fromLatin1(
                                      QCryptographicHash::hash(image, QCryptographicHash::Md5).toHex())}}}});
                break;
            }
            case rust::WorkflowOp::NoticeQqRequired:
                qqFields[int(a[0])] =
                    required(c, std::array<const char *, 3>{"QQ Bot API App ID", "QQ Bot API Token",
                                                            "QQ Bot API Channel ID"}
                                    .at(int(a[0])));
                break;
            case rust::WorkflowOp::NoticeQqAppNumeric:
                result[0] = QRegularExpression("^[0-9]+$").match(qqFields.at(0)).hasMatch();
                break;
            case rust::WorkflowOp::NoticeQqChannelNumeric:
                result[0] = QRegularExpression("^[0-9]+$").match(qqFields.at(2)).hasMatch();
                break;
            case rust::WorkflowOp::NoticeQqContent:
                text = content(n);
                break;
            case rust::WorkflowOp::NoticeQqImageSummary:
                text += QString("\n[%1 张截图；旧版频道接口仅发送文字]").arg(n.images.size());
                break;
            case rust::WorkflowOp::NoticeQqChunks:
                parts = chunks(text, int(a[0]));
                break;
            case rust::WorkflowOp::NoticeCreateQqRequest:
                request =
                    json(provider, QUrl("https://api.sgroup.qq.com/channels/" + qqFields.at(2) + "/messages"),
                         {{"content", parts.at(int(a[0]))}});
                break;
            case rust::WorkflowOp::NoticeQqAuthorization:
                request.headers["Authorization"] =
                    "Bot " + qqFields.at(0).toUtf8() + '.' + qqFields.at(1).toUtf8();
                break;
            case rust::WorkflowOp::NoticeAppendQqRequest:
                out << request;
                break;
            case rust::WorkflowOp::NoticeFailure:
                noticeFailure(int(a[0]));
                break;
            default:
                fail("Rust 通知渠道宿主无效");
            }
        });
    return out;
}
void validateNoticeResponse(const QString &provider, const QByteArray &body) {
    QJsonParseError error;
    QJsonDocument document;
    QJsonObject response;
    rust::workflowRun(rust::WorkflowMethod::NoticeValidateResponse, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::NoticeResponseProvider:
                              out[0] =
                                  QStringList{"Discord", "QQ Bot", "Telegram", "WeCom"}.indexOf(provider);
                              break;
                          case rust::WorkflowOp::NoticeParseResponse:
                              document = QJsonDocument::fromJson(body, &error);
                              break;
                          case rust::WorkflowOp::NoticeResponseParseOk:
                              out[0] = error.error == QJsonParseError::NoError;
                              break;
                          case rust::WorkflowOp::NoticeResponseObject:
                              out[0] = document.isObject();
                              break;
                          case rust::WorkflowOp::NoticeResponseFields:
                              response = document.object();
                              break;
                          case rust::WorkflowOp::NoticeResponseOk:
                              out[0] = response["ok"].toBool();
                              break;
                          case rust::WorkflowOp::NoticeResponseCodeNumber:
                              out[0] = response["errcode"].isDouble();
                              break;
                          case rust::WorkflowOp::NoticeResponseCode:
                              out[0] = response["errcode"].toInt(-1);
                              break;
                          case rust::WorkflowOp::NoticeFailure:
                              noticeFailure(int(a[0]));
                              break;
                          default:
                              fail("Rust 通知响应宿主无效");
                          }
                      });
}

namespace {
[[noreturn]] void noticeTransportFailure(int code, int detail = 0) {
    if (code == 7)
        fail(QString("邮件服务拒绝操作（%1）").arg(detail));
    static const QStringList messages{"",
                                      "通知返回内容过大",
                                      "邮件服务器端口无效",
                                      "邮件服务器响应超时",
                                      "邮件连接已断开",
                                      "邮件服务器返回无效响应",
                                      "邮件响应码不一致",
                                      "",
                                      "邮件发送失败"};
    fail(messages.at(code));
}
struct HttpNoticeSdk {
    const NoticeRequest &r;
    const NoticeCancelled &cancel;
    std::optional<QNetworkAccessManager> manager;
    std::optional<QNetworkRequest> request;
    QList<QByteArray> keys;
    QScopedPointer<QNetworkReply> reply{nullptr};
    std::optional<QEventLoop> loop;
    std::optional<QTimer> timer;
    std::optional<QElapsedTimer> elapsed;
    QByteArray result;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::NoticeTransportCheckpoint:
            checkpoint(cancel);
            break;
        case rust::WorkflowOp::NoticeTransportHasCancellation:
            out[0] = bool(cancel);
            break;
        case rust::WorkflowOp::NoticeTransportCancelled:
            out[0] = cancel();
            break;
        case rust::WorkflowOp::NoticeHttpCreateManager:
            manager.emplace();
            break;
        case rust::WorkflowOp::NoticeHttpCreateRequest:
            request.emplace(r.url);
            break;
        case rust::WorkflowOp::NoticeHttpContentType:
            request->setHeader(QNetworkRequest::ContentTypeHeader, r.contentType);
            break;
        case rust::WorkflowOp::NoticeHttpRedirectPolicy:
            request->setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                  QNetworkRequest::ManualRedirectPolicy);
            break;
        case rust::WorkflowOp::NoticeHttpTransferTimeout:
            request->setTransferTimeout(int(a[0]));
            break;
        case rust::WorkflowOp::NoticeHttpHeaderKeys:
            keys = r.headers.keys();
            break;
        case rust::WorkflowOp::NoticeHttpHeaderCount:
            out[0] = double(keys.size());
            break;
        case rust::WorkflowOp::NoticeHttpSetHeader: {
            const auto &key = keys.at(int(a[0]));
            request->setRawHeader(key, r.headers.value(key));
            break;
        }
        case rust::WorkflowOp::NoticeHttpPost:
            reply.reset(manager->post(*request, r.body));
            break;
        case rust::WorkflowOp::NoticeHttpCreateLoop:
            loop.emplace();
            break;
        case rust::WorkflowOp::NoticeHttpCreateTimer:
            timer.emplace();
            break;
        case rust::WorkflowOp::NoticeHttpTimerInterval:
            timer->setInterval(int(a[0]));
            break;
        case rust::WorkflowOp::NoticeHttpStartElapsed:
            elapsed.emplace();
            elapsed->start();
            break;
        case rust::WorkflowOp::NoticeHttpBindPoll:
            QObject::connect(&*timer, &QTimer::timeout, &*loop,
                             [this] { rust::workflowRun(rust::WorkflowMethod::NoticeHttpPoll, {}, *this); });
            break;
        case rust::WorkflowOp::NoticeHttpBindFinished:
            QObject::connect(reply.data(), &QNetworkReply::finished, &*loop, &QEventLoop::quit);
            break;
        case rust::WorkflowOp::NoticeHttpStartTimer:
            timer->start();
            break;
        case rust::WorkflowOp::NoticeHttpFinished:
            out[0] = reply->isFinished();
            break;
        case rust::WorkflowOp::NoticeHttpExecLoop:
            loop->exec();
            break;
        case rust::WorkflowOp::NoticeHttpElapsed:
            out[0] = double(elapsed->elapsed());
            break;
        case rust::WorkflowOp::NoticeHttpAbort:
            reply->abort();
            break;
        case rust::WorkflowOp::NoticeHttpStatus:
            out[0] = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            break;
        case rust::WorkflowOp::NoticeHttpError:
            out[0] = int(reply->error());
            break;
        case rust::WorkflowOp::NoticeHttpAvailable:
            out[0] = double(reply->bytesAvailable());
            break;
        case rust::WorkflowOp::NoticeHttpReadAll:
            result = reply->readAll();
            break;
        case rust::WorkflowOp::NoticeTransportFailure:
            switch (int(a[0])) {
            case 0:
                fail(QString("通知发送失败（HTTP %1，网络状态 %2）").arg(int(a[1])).arg(int(reply->error())));
            default:
                noticeTransportFailure(int(a[0]), int(a[1]));
            }
            break;
        default:
            fail("Rust 通知 HTTP 宿主无效");
        }
    }
};
} // namespace
QByteArray sendHttpNotice(const NoticeRequest &r, const NoticeCancelled &cancel) {
    HttpNoticeSdk sdk{r, cancel};
    rust::workflowRun(rust::WorkflowMethod::NoticeHttp, {}, sdk);
    return sdk.result;
}

QByteArray noticeMime(const QJsonObject &c, const Notice &n, QByteArray boundary) {
    QString sender, recipient, subject;
    QStringList parts;
    QByteArray out;
    rust::workflowRun(
        rust::WorkflowMethod::NoticeMime, {}, [&](rust::WorkflowOp op, const double *a, double *result) {
            switch (op) {
            case rust::WorkflowOp::NoticeBoundaryValid:
                result[0] =
                    QRegularExpression("^[a-zA-Z0-9_-]+$").match(QString::fromLatin1(boundary)).hasMatch();
                break;
            case rust::WorkflowOp::NoticeMimeSender:
                sender = address(required(c, "SMTP Default Sender"));
                break;
            case rust::WorkflowOp::NoticeMimeRecipient:
                recipient = address(required(c, "SMTP Default Recipient"));
                break;
            case rust::WorkflowOp::NoticeMimeHeader:
                out = "From: <" + sender.toUtf8() + ">\r\nTo: <" + recipient.toUtf8() + ">\r\nSubject: ";
                break;
            case rust::WorkflowOp::NoticeTitleEmpty:
                result[0] = n.title.isEmpty();
                break;
            case rust::WorkflowOp::NoticeDefaultSubject:
                subject = "OKWW 通知";
                break;
            case rust::WorkflowOp::NoticeUseSubject:
                subject = n.title;
                break;
            case rust::WorkflowOp::NoticeSubjectChunks:
                parts = chunks(subject, int(a[0]));
                break;
            case rust::WorkflowOp::NoticePartCount:
                result[0] = double(parts.size());
                break;
            case rust::WorkflowOp::NoticeAppendSubjectWord:
                out += "=?UTF-8?B?" + parts.at(int(a[0])).toUtf8().toBase64() + "?=\r\n ";
                break;
            case rust::WorkflowOp::NoticeMimeTypeHeader:
                out += "\r\nMIME-Version: 1.0\r\nContent-Type: multipart/mixed; boundary=\"" + boundary +
                       "\"\r\n\r\n";
                break;
            case rust::WorkflowOp::NoticeMimePlainPart:
                out += "--" + boundary +
                       "\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: "
                       "base64\r\n\r\n" +
                       wrapped64(n.text.toUtf8());
                break;
            case rust::WorkflowOp::NoticeImageCount:
                result[0] = double(n.images.size());
                break;
            case rust::WorkflowOp::NoticeMimeImagePart:
                out += "--" + boundary +
                       "\r\nContent-Type: image/png\r\nContent-Disposition: attachment; "
                       "filename=\"notification_" +
                       QByteArray::number(int(a[1])) + ".png\"\r\nContent-Transfer-Encoding: base64\r\n\r\n" +
                       wrapped64(n.images.at(int(a[0])));
                break;
            case rust::WorkflowOp::NoticeMimeClosingBoundary:
                out += "--" + boundary + "--\r\n";
                break;
            case rust::WorkflowOp::NoticeFailure:
                noticeFailure(int(a[0]));
                break;
            default:
                fail("Rust 通知 MIME 宿主无效");
            }
        });
    return out;
}

namespace {
struct SmtpNoticeSdk {
    const QJsonObject &c;
    const Notice &n;
    const NoticeCancelled &cancel;
    QString host, sender, recipient, user;
    int port = 0;
    std::optional<QSslSocket> socket;
    void run(rust::WorkflowMethod method, std::span<const double> args = {}, const QByteArray &payload = {},
             std::span<const int> expected = {}) {
        QElapsedTimer elapsed;
        QByteArray line;
        rust::workflowRun(method, args, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::NoticeTransportCheckpoint:
                checkpoint(cancel);
                break;
            case rust::WorkflowOp::NoticeSmtpReadHost:
                host = required(c, "SMTP Host");
                break;
            case rust::WorkflowOp::NoticeSmtpReadSender:
                sender = address(required(c, "SMTP Default Sender"));
                break;
            case rust::WorkflowOp::NoticeSmtpReadRecipient:
                recipient = address(required(c, "SMTP Default Recipient"));
                break;
            case rust::WorkflowOp::NoticeSmtpReadPort:
                out[0] = port = c["SMTP Port"].toInt(587);
                break;
            case rust::WorkflowOp::NoticeSmtpCreateSocket:
                socket.emplace();
                break;
            case rust::WorkflowOp::NoticeSmtpVerifyPeer:
                socket->setPeerVerifyMode(QSslSocket::VerifyPeer);
                break;
            case rust::WorkflowOp::NoticeSmtpTlsProtocol:
                socket->setProtocol(QSsl::TlsV1_2OrLater);
                break;
            case rust::WorkflowOp::NoticeSmtpUseTls:
                out[0] = c["SMTP Use TLS"].toBool(true);
                break;
            case rust::WorkflowOp::NoticeSmtpConnectEncrypted:
                socket->connectToHostEncrypted(host, quint16(port));
                break;
            case rust::WorkflowOp::NoticeSmtpConnectPlain:
                socket->connectToHost(host, quint16(port));
                break;
            case rust::WorkflowOp::NoticeSmtpUntilNested:
                until(int(a[0]));
                break;
            case rust::WorkflowOp::NoticeSmtpWelcome:
                response({220});
                break;
            case rust::WorkflowOp::NoticeSmtpEhlo:
                command("EHLO okww.local", {250});
                break;
            case rust::WorkflowOp::NoticeSmtpStartTlsCommand:
                command("STARTTLS", {220});
                break;
            case rust::WorkflowOp::NoticeSmtpStartEncryption:
                socket->startClientEncryption();
                break;
            case rust::WorkflowOp::NoticeSmtpReadUser:
                user = c["SMTP Username"].toString();
                break;
            case rust::WorkflowOp::NoticeSmtpUserEmpty:
                out[0] = user.isEmpty();
                break;
            case rust::WorkflowOp::NoticeSmtpAuthLogin:
                command("AUTH LOGIN", {334});
                break;
            case rust::WorkflowOp::NoticeSmtpAuthUser:
                command(user.toUtf8().toBase64(), {334});
                break;
            case rust::WorkflowOp::NoticeSmtpAuthPassword:
                command(c["SMTP Password"].toString().toUtf8().toBase64(), {235});
                break;
            case rust::WorkflowOp::NoticeSmtpMailFrom:
                command("MAIL FROM:<" + sender.toUtf8() + ">", {250});
                break;
            case rust::WorkflowOp::NoticeSmtpRecipient:
                command("RCPT TO:<" + recipient.toUtf8() + ">", {250, 251});
                break;
            case rust::WorkflowOp::NoticeSmtpData:
                command("DATA", {354});
                break;
            case rust::WorkflowOp::NoticeSmtpWriteMime:
                write(noticeMime(c, n, "okww-" + QUuid::createUuid().toByteArray(QUuid::Id128)) + ".\r\n");
                break;
            case rust::WorkflowOp::NoticeSmtpAccepted:
                response({250});
                break;
            case rust::WorkflowOp::NoticeSmtpWriteQuit:
                socket->write("QUIT\r\n");
                break;
            case rust::WorkflowOp::NoticeSmtpWaitQuitWritten:
                socket->waitForBytesWritten(int(a[0]));
                break;
            case rust::WorkflowOp::NoticeSmtpDisconnect:
                socket->disconnectFromHost();
                break;
            case rust::WorkflowOp::NoticeSmtpStartElapsed:
                elapsed.start();
                break;
            case rust::WorkflowOp::NoticeSmtpElapsed:
                out[0] = double(elapsed.elapsed());
                break;
            case rust::WorkflowOp::NoticeSmtpWaitPredicate:
                switch (int(a[0])) {
                case 0:
                    out[0] = socket->state() == QAbstractSocket::ConnectedState;
                    break;
                case 1:
                    out[0] = socket->isEncrypted();
                    break;
                case 2:
                    out[0] = socket->canReadLine();
                    break;
                case 3:
                    out[0] = socket->bytesToWrite() == 0;
                    break;
                }
                break;
            case rust::WorkflowOp::NoticeSmtpWaitPoll:
                switch (int(a[0])) {
                case 0:
                    socket->waitForConnected(int(a[1]));
                    break;
                case 1:
                    socket->waitForEncrypted(int(a[1]));
                    break;
                case 2:
                    socket->waitForReadyRead(int(a[1]));
                    break;
                case 3:
                    socket->waitForBytesWritten(int(a[1]));
                    break;
                }
                break;
            case rust::WorkflowOp::NoticeSmtpDisconnected:
                out[0] = socket->state() == QAbstractSocket::UnconnectedState;
                break;
            case rust::WorkflowOp::NoticeSmtpReadLine:
                line = socket->readLine(qint64(a[0]));
                break;
            case rust::WorkflowOp::NoticeSmtpLineSize:
                out[0] = double(line.size());
                break;
            case rust::WorkflowOp::NoticeSmtpLineEndsLf:
                out[0] = line.endsWith('\n');
                break;
            case rust::WorkflowOp::NoticeSmtpParseCode: {
                bool valid = false;
                out[0] = line.left(3).toInt(&valid);
                out[1] = valid;
                break;
            }
            case rust::WorkflowOp::NoticeSmtpLineContinues:
                out[0] = line[3] == '-';
                break;
            case rust::WorkflowOp::NoticeSmtpLineSpace:
                out[0] = line[3] == ' ';
                break;
            case rust::WorkflowOp::NoticeSmtpExpectedCount:
                out[0] = double(expected.size());
                break;
            case rust::WorkflowOp::NoticeSmtpExpectedAt:
                out[0] = expected[size_t(a[0])];
                break;
            case rust::WorkflowOp::NoticeSmtpWritePayload:
                out[0] = double(socket->write(payload));
                break;
            case rust::WorkflowOp::NoticeSmtpWriteCommand:
                write(payload + "\r\n");
                break;
            case rust::WorkflowOp::NoticeSmtpCommandResponse:
                run(rust::WorkflowMethod::NoticeSmtpResponse, {}, {}, expected);
                break;
            case rust::WorkflowOp::NoticeTransportFailure:
                noticeTransportFailure(int(a[0]), int(a[1]));
                break;
            default:
                fail("Rust 通知 SMTP 宿主无效");
            }
        });
    }
    void until(int kind) {
        const std::array<double, 1> args{double(kind)};
        run(rust::WorkflowMethod::NoticeSmtpUntil, args);
    }
    void response(std::initializer_list<int> expected) {
        run(rust::WorkflowMethod::NoticeSmtpResponse, {}, {}, {expected.begin(), expected.size()});
    }
    void write(const QByteArray &payload) { run(rust::WorkflowMethod::NoticeSmtpWrite, {}, payload); }
    void command(const QByteArray &payload, std::initializer_list<int> expected) {
        run(rust::WorkflowMethod::NoticeSmtpCommand, {}, payload, {expected.begin(), expected.size()});
    }
};
} // namespace
void sendSmtpNotice(const QJsonObject &c, const Notice &n, const NoticeCancelled &cancel) {
    SmtpNoticeSdk sdk{c, n, cancel};
    sdk.run(rust::WorkflowMethod::NoticeSmtp);
}

namespace {
NoticeTransport notificationTransport(NoticeTransport &input) {
    NoticeTransport result;
    rust::workflowRun(rust::WorkflowMethod::NoticeQueueSelectTransport, {},
                      [&](rust::WorkflowOp op, const double *, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::NoticeQueueHasOverride:
                              out[0] = bool(input);
                              break;
                          case rust::WorkflowOp::NoticeQueueMoveOverride:
                              result = std::move(input);
                              break;
                          case rust::WorkflowOp::NoticeQueueDefaultTransport:
                              result = sendHttpNotice;
                              break;
                          default:
                              fail("Rust 通知传输选择宿主无效");
                          }
                      });
    return result;
}
} // namespace
struct NotificationQueueSdk {
    Notifications &q;
    QJsonObject *config = nullptr;
    Notice *notice = nullptr;
    const QStringList *flags = nullptr;
    std::optional<Notifications::Pending> item;
    std::optional<std::unique_lock<std::mutex>> lock;
    QVector<QPair<QString, QString>> channels;
    QVector<NoticeRequest> requests;
    QByteArray response;
    NoticeCancelled cancel;
    void operator()(rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &error) {
        switch (op) {
        case rust::WorkflowOp::NoticeQueueSetStopping:
            q.stopping_ = true;
            break;
        case rust::WorkflowOp::NoticeQueueNotifyAll:
            q.ready_.notify_all();
            break;
        case rust::WorkflowOp::NoticeQueueJoinable:
            out[0] = q.worker_.joinable();
            break;
        case rust::WorkflowOp::NoticeQueueJoin:
            q.worker_.join();
            break;
        case rust::WorkflowOp::NoticeQueueLock:
            lock.emplace(q.mutex_);
            break;
        case rust::WorkflowOp::NoticeQueueUnlock:
            lock.reset();
            break;
        case rust::WorkflowOp::NoticeQueueEmpty:
            out[0] = q.queue_.empty();
            break;
        case rust::WorkflowOp::NoticeQueueBusy:
            out[0] = q.busy_;
            break;
        case rust::WorkflowOp::NoticeQueueStopping:
            out[0] = q.stopping_;
            break;
        case rust::WorkflowOp::NoticeQueueFlags: {
            static const QStringList values{"Discord Notification",
                                            "Telegram Notification",
                                            "Enterprise WeChat Webhook Notification",
                                            "QQ Bot API Notification",
                                            "SMTP Notification",
                                            "QQ Desktop Notification (Not Reliable)",
                                            "WeChat Desktop Notification (Not Reliable)"};
            flags = &values;
            break;
        }
        case rust::WorkflowOp::NoticeQueueSubmittedFlag:
            out[0] = (*config)[flags->at(int(a[0]))].toBool();
            break;
        case rust::WorkflowOp::NoticeQueueSize:
            out[0] = double(q.queue_.size());
            break;
        case rust::WorkflowOp::NoticeQueueHasReporter:
            out[0] = bool(q.report_);
            break;
        case rust::WorkflowOp::NoticeQueueReportFull:
            q.report_("通知队列已满，此条未发送");
            break;
        case rust::WorkflowOp::NoticeQueuePush:
            q.queue_.push_back({std::move(*config), std::move(*notice)});
            break;
        case rust::WorkflowOp::NoticeQueueNotifyOne:
            q.ready_.notify_one();
            break;
        case rust::WorkflowOp::NoticeQueueCancellation:
            cancel = [owner = &q] { return bool(owner->stopping_); };
            break;
        case rust::WorkflowOp::NoticeQueueNewItem:
            item.emplace();
            break;
        case rust::WorkflowOp::NoticeQueueDropItem:
            item.reset();
            break;
        case rust::WorkflowOp::NoticeQueueWaitReady:
            q.ready_.wait(*lock, [this] {
                return rust::workflowRun(rust::WorkflowMethod::NoticeQueueReadyPredicate, {}, *this)[0] != 0;
            });
            break;
        case rust::WorkflowOp::NoticeQueueTakeFront:
            *item = std::move(q.queue_.front());
            break;
        case rust::WorkflowOp::NoticeQueuePopFront:
            q.queue_.pop_front();
            break;
        case rust::WorkflowOp::NoticeQueueSetBusy:
            q.busy_ = bool(a[0]);
            break;
        case rust::WorkflowOp::NoticeQueueChannels:
            channels = {{"Discord", "Discord Notification"},
                        {"Telegram", "Telegram Notification"},
                        {"WeCom", "Enterprise WeChat Webhook Notification"},
                        {"QQ Bot", "QQ Bot API Notification"},
                        {"SMTP", "SMTP Notification"},
                        {"QQ Desktop", "QQ Desktop Notification (Not Reliable)"},
                        {"WeChat Desktop", "WeChat Desktop Notification (Not Reliable)"}};
            break;
        case rust::WorkflowOp::NoticeQueueDropChannels:
            channels = {};
            break;
        case rust::WorkflowOp::NoticeQueueItemFlag:
            out[0] = item->config[channels.at(int(a[0])).second].toBool();
            break;
        case rust::WorkflowOp::NoticeQueueSmtp:
            sendSmtpNotice(item->config, item->notice, cancel);
            break;
        case rust::WorkflowOp::NoticeQueueDesktop:
            sendDesktopNotice(int(a[0]) == 6, item->config, item->notice, q.models_, cancel);
            break;
        case rust::WorkflowOp::NoticeQueueRequests:
            requests = noticeRequests(channels.at(int(a[0])).first, item->config, item->notice);
            break;
        case rust::WorkflowOp::NoticeQueueRequestCount:
            out[0] = double(requests.size());
            break;
        case rust::WorkflowOp::NoticeQueueSend:
            response = q.transport_(requests.at(int(a[0])), cancel);
            break;
        case rust::WorkflowOp::NoticeQueueValidate:
            validateNoticeResponse(channels.at(int(a[0])).first, response);
            response = {};
            break;
        case rust::WorkflowOp::NoticeQueueClearRequests:
            response = {};
            requests = {};
            break;
        case rust::WorkflowOp::NoticeQueueReportSuccess:
            q.report_(channels.at(int(a[0])).first + " 通知已提交");
            break;
        case rust::WorkflowOp::NoticeQueueReportError:
            try {
                std::rethrow_exception(error);
            } catch (const std::exception &e) {
                q.report_(channels.at(int(a[0])).first + "：" + errorText(e));
            }
            break;
        case rust::WorkflowOp::NoticeQueueDelay:
            q.ready_.wait_for(*lock, std::chrono::seconds(int(a[0])), [this] {
                return rust::workflowRun(rust::WorkflowMethod::NoticeQueueDelayPredicate, {}, *this)[0] != 0;
            });
            break;
        default:
            fail("Rust 通知队列宿主无效");
        }
    }
    std::array<double, 3> run(rust::WorkflowMethod method) { return rust::workflowRun(method, {}, *this); }
};
Notifications::Notifications(QString models, std::function<void(QString)> report, NoticeTransport transport)
    : models_(std::move(models)), transport_(notificationTransport(transport)), report_(std::move(report)),
      worker_(&Notifications::run, this) {}
Notifications::~Notifications() { stop(); }
void Notifications::stop() { NotificationQueueSdk{*this}.run(rust::WorkflowMethod::NoticeQueueStop); }
bool Notifications::idle() {
    return NotificationQueueSdk{*this}.run(rust::WorkflowMethod::NoticeQueueIdle)[0] != 0;
}
void Notifications::submit(QJsonObject config, Notice notice) {
    NotificationQueueSdk{*this, &config, &notice}.run(rust::WorkflowMethod::NoticeQueueSubmit);
}
void Notifications::run() { NotificationQueueSdk{*this}.run(rust::WorkflowMethod::NoticeQueueRun); }
} // namespace okww::runtime
