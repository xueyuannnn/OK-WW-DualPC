#pragma once
#include "ocr.hpp"
#include <condition_variable>
#include <deque>

namespace okww::runtime {
struct Notice {
    QString title, text;
    QVector<QByteArray> images; // PNG, owned by the queue; no frame lifetime dependency.
    bool error = false;
};
struct NoticeRequest {
    QString provider;
    QUrl url;
    QByteArray contentType, body;
    QMap<QByteArray, QByteArray> headers;
};
using NoticeCancelled = std::function<bool()>;
using NoticeTransport = std::function<QByteArray(const NoticeRequest &, const NoticeCancelled &)>;
QVector<NoticeRequest> noticeRequests(const QString &provider, const QJsonObject &config,
                                      const Notice &notice);
void validateNoticeResponse(const QString &provider, const QByteArray &body);
QByteArray noticeMime(const QJsonObject &config, const Notice &notice, QByteArray boundary);
void sendSmtpNotice(const QJsonObject &config, const Notice &notice, const NoticeCancelled &cancelled);
QByteArray sendHttpNotice(const NoticeRequest &request, const NoticeCancelled &cancelled);
std::optional<QPoint> messengerContact(const QVector<TextLine> &text, QString nickname, bool popup);
// The capture closure must own its resources; a hung foreign window may outlive
// the caller's timeout. At most one capture job may remain outstanding.
cv::Mat boundedDesktopCapture(std::function<cv::Mat()> capture, const NoticeCancelled &cancelled,
                              int timeoutMs = 3000);
void sendDesktopNotice(bool wechat, const QJsonObject &config, const Notice &notice, const QString &models,
                       const NoticeCancelled &cancelled);

// Worker owns every network/desktop operation. Neither OCR nor network waits
// block the GUI or the combat loop. No retry after an uncertain delivery.
class Notifications {
    struct Pending {
        QJsonObject config;
        Notice notice;
    };
    QString models_;
    NoticeTransport transport_;
    std::function<void(QString)> report_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<Pending> queue_;
    std::atomic<bool> stopping_{false}, busy_{false};
    std::thread worker_;
    void run();

  public:
    Notifications(QString models, std::function<void(QString)> report, NoticeTransport transport = {});
    ~Notifications();
    void submit(QJsonObject config, Notice notice);
    bool idle();
    void stop();
};
} // namespace okww::runtime
