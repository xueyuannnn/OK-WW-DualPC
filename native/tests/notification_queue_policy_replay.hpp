#pragma once
#include "runtime/notification.hpp"
#include "runtime/workflow_abi.hpp"
namespace okww::runtime::notice_queue_test {
struct QueueState;
struct Flag {
    QJsonArray *trace;
    QString name;
    bool value = false;
    operator bool() const {
        trace->append(name + "?");
        return value;
    }
    Flag &operator=(bool x) {
        trace->append(QJsonObject{{name, x}});
        value = x;
        return *this;
    }
};
struct Mutex {
    QJsonArray &trace;
    bool owned = false;
    void lock() {
        if (owned)
            throw std::runtime_error("double lock");
        trace.append("lock");
        owned = true;
    }
    void unlock() {
        trace.append("unlock");
        owned = false;
    }
};
struct Thread {
    QJsonArray &trace;
    bool live = true;
    bool joinable() {
        trace.append("joinable");
        return live;
    }
    void join() {
        trace.append("join");
        live = false;
    }
};
struct Condition {
    QueueState &owner;
    void notify_all();
    void notify_one();
    template <class Predicate> void wait(std::unique_lock<Mutex> &lock, Predicate predicate);
    template <class Duration, class Predicate>
    void wait_for(std::unique_lock<Mutex> &lock, Duration duration, Predicate predicate);
};
struct QueueState {
    struct Pending {
        QJsonObject config;
        Notice notice;
    };
    int scene, delays = 0, reports = 0, sends = 0;
    QJsonArray trace;
    QString models_ = "fixture-models";
    NoticeTransport transport_;
    std::function<void(QString)> report_;
    Mutex mutex_{trace};
    Condition ready_{*this};
    std::deque<Pending> queue_;
    Flag stopping_{&trace, "stopping"}, busy_{&trace, "busy"};
    Thread worker_{trace};
    explicit QueueState(int s) : scene(s) {
        stopping_.value = s % 31 == 0;
        worker_.live = s % 3 != 0;
        transport_ = [this](const NoticeRequest &r, const NoticeCancelled &cancel) {
            trace.append(
                QJsonObject{{"send", r.provider}, {"body", QString::fromUtf8(r.body)}, {"cancel", cancel()}});
            ++sends;
            if (scene % 23 == 0)
                stopping_ = true;
            if (scene % 37 == 0)
                throw 27;
            if (scene % 11 == 0 && sends % 2)
                throw std::runtime_error("transport fixture");
            return QByteArray(scene % 13 ? "ok" : "bad");
        };
        if (s % 7)
            report_ = [this](QString message) {
                trace.append(QJsonObject{{"report", message}});
                if (scene % 17 == 0 && ++reports == 1)
                    throw std::runtime_error("report fixture");
            };
        if (s % 5 == 0)
            for (int i = 0; i < 32 + (s % 2); ++i)
                queue_.push_back({configuration(s + i), {"queued", "body", {"png"}}});
    }
    static QJsonObject configuration(int s) {
        QJsonObject c;
        const QStringList flags{"Discord Notification",
                                "Telegram Notification",
                                "Enterprise WeChat Webhook Notification",
                                "QQ Bot API Notification",
                                "SMTP Notification",
                                "QQ Desktop Notification (Not Reliable)",
                                "WeChat Desktop Notification (Not Reliable)"};
        for (int i = 0; i < 7; ++i) {
            if ((s >> (i % 6)) & 1)
                c[flags[i]] = true;
            else if ((s + i) % 3)
                c[flags[i]] = false;
        }
        return c;
    }
    void direct(QString provider, const NoticeCancelled &cancel) {
        trace.append(QJsonObject{{"direct", provider}, {"cancel", cancel()}});
        ++sends;
        if (scene % 23 == 0)
            stopping_ = true;
        if (scene % 37 == 0)
            throw 27;
        if (scene % 11 == 0 && sends % 2)
            throw std::runtime_error("transport fixture");
    }
    QJsonObject snapshot() const {
        QJsonArray pending;
        for (const auto &p : queue_)
            pending.append(QJsonObject{{"config", p.config}, {"title", p.notice.title}});
        return {{"stopping", stopping_.value},
                {"busy", busy_.value},
                {"pending", pending},
                {"locked", mutex_.owned},
                {"joined", !worker_.live}};
    }
};
inline void Condition::notify_all() { owner.trace.append("notify-all"); }
inline void Condition::notify_one() { owner.trace.append("notify-one"); }
template <class Predicate> void Condition::wait(std::unique_lock<Mutex> &lock, Predicate predicate) {
    owner.trace.append("wait-ready");
    if (!lock.owns_lock())
        throw std::runtime_error("unlocked wait");
    if (!predicate()) {
        if (owner.scene % 19 == 0)
            owner.stopping_ = true;
        else
            owner.queue_.push_back(
                {QueueState::configuration(owner.scene + 9), {"injected", "body", {"png"}}});
        if (!predicate())
            throw std::runtime_error("fixture predicate");
    }
}
template <class Duration, class Predicate>
void Condition::wait_for(std::unique_lock<Mutex> &lock, Duration duration, Predicate predicate) {
    owner.trace.append(QJsonObject{
        {"delay_ms", double(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count())}});
    if (!lock.owns_lock())
        throw std::runtime_error("unlocked delay");
    if (!predicate()) {
        if (++owner.delays >= 2)
            owner.stopping_ = true;
        else
            owner.queue_.push_back({QueueState::configuration(owner.scene + 11), {"delayed", "body", {}}});
        predicate();
    }
}
// The fake request/desktop SDK never opens sockets, chat windows or a device.
inline QVector<NoticeRequest> noticeRequests(const QString &provider, const QJsonObject &,
                                             const Notice &notice) {
    return {{provider, {}, {}, notice.title.toUtf8(), {}}, {provider, {}, {}, notice.text.toUtf8(), {}}};
}
inline void validateNoticeResponse(const QString &, const QByteArray &response) {
    if (response == "bad")
        throw std::runtime_error("response fixture");
}
inline thread_local QueueState *active = nullptr;
inline void sendSmtpNotice(const QJsonObject &, const Notice &, const NoticeCancelled &cancel) {
    active->direct("SMTP", cancel);
}
inline void sendDesktopNotice(bool wechat, const QJsonObject &, const Notice &, const QString &,
                              const NoticeCancelled &cancel) {
    active->direct(wechat ? "WeChat Desktop" : "QQ Desktop", cancel);
}
struct ReferenceQueue : QueueState {
    using QueueState::QueueState;
    void stop();
    bool idle();
    void submit(QJsonObject config, Notice notice);
    void run();
};
void ReferenceQueue::stop() {
    stopping_ = true;
    ready_.notify_all();
    if (worker_.joinable())
        worker_.join();
}
bool ReferenceQueue::idle() {
    std::lock_guard lock(mutex_);
    return queue_.empty() && !busy_;
}
void ReferenceQueue::submit(QJsonObject config, Notice notice) {
    static const QStringList flags{"Discord Notification",
                                   "Telegram Notification",
                                   "Enterprise WeChat Webhook Notification",
                                   "QQ Bot API Notification",
                                   "SMTP Notification",
                                   "QQ Desktop Notification (Not Reliable)",
                                   "WeChat Desktop Notification (Not Reliable)"};
    if (std::none_of(flags.begin(), flags.end(), [&](const auto &f) { return config[f].toBool(); }))
        return;
    std::lock_guard lock(mutex_);
    if (stopping_)
        return;
    if (queue_.size() >= 32) {
        if (report_)
            report_("通知队列已满，此条未发送");
        return;
    }
    queue_.push_back({std::move(config), std::move(notice)});
    ready_.notify_one();
}
void ReferenceQueue::run() {
    const NoticeCancelled cancel = [&] { return bool(stopping_); };
    while (!stopping_) {
        Pending item;
        {
            std::unique_lock lock(mutex_);
            ready_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
            if (stopping_)
                break;
            item = std::move(queue_.front());
            queue_.pop_front();
            busy_ = true;
        }
        const QVector<QPair<QString, QString>> channels{
            {"Discord", "Discord Notification"},
            {"Telegram", "Telegram Notification"},
            {"WeCom", "Enterprise WeChat Webhook Notification"},
            {"QQ Bot", "QQ Bot API Notification"},
            {"SMTP", "SMTP Notification"},
            {"QQ Desktop", "QQ Desktop Notification (Not Reliable)"},
            {"WeChat Desktop", "WeChat Desktop Notification (Not Reliable)"}};
        for (const auto &[provider, flag] : channels) {
            if (stopping_)
                break;
            if (!item.config[flag].toBool())
                continue;
            try {
                if (provider == "SMTP")
                    notice_queue_test::sendSmtpNotice(item.config, item.notice, cancel);
                else if (provider.endsWith("Desktop"))
                    notice_queue_test::sendDesktopNotice(provider.startsWith("WeChat"), item.config,
                                                         item.notice, models_, cancel);
                else
                    for (const auto &request :
                         notice_queue_test::noticeRequests(provider, item.config, item.notice))
                        notice_queue_test::validateNoticeResponse(provider, transport_(request, cancel));
                if (report_)
                    report_(provider + " 通知已提交");
            } catch (const std::exception &e) {
                if (!stopping_ && report_)
                    report_(provider + "：" + errorText(e));
            }
        }
        busy_ = false;
        std::unique_lock lock(mutex_);
        ready_.wait_for(lock, std::chrono::seconds(5), [&] { return bool(stopping_); });
    }
    busy_ = false;
}
struct NotificationQueueSdk {
    QueueState &q;
    QJsonObject *config = nullptr;
    Notice *notice = nullptr;
    const QStringList *flags = nullptr;
    std::optional<QueueState::Pending> item;
    std::optional<std::unique_lock<Mutex>> lock;
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
            notice_queue_test::sendSmtpNotice(item->config, item->notice, cancel);
            break;
        case rust::WorkflowOp::NoticeQueueDesktop:
            notice_queue_test::sendDesktopNotice(int(a[0]) == 6, item->config, item->notice, q.models_,
                                                 cancel);
            break;
        case rust::WorkflowOp::NoticeQueueRequests:
            requests =
                notice_queue_test::noticeRequests(channels.at(int(a[0])).first, item->config, item->notice);
            break;
        case rust::WorkflowOp::NoticeQueueRequestCount:
            out[0] = double(requests.size());
            break;
        case rust::WorkflowOp::NoticeQueueSend:
            response = q.transport_(requests.at(int(a[0])), cancel);
            break;
        case rust::WorkflowOp::NoticeQueueValidate:
            notice_queue_test::validateNoticeResponse(channels.at(int(a[0])).first, response);
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
inline QJsonObject outcome(int scene, bool rust) {
    ReferenceQueue q(scene);
    active = &q;
    QJsonArray snapshots;
    auto guarded = [&](auto action) {
        try {
            action();
        } catch (const std::exception &e) {
            q.trace.append(QJsonObject{{"error", errorText(e)}});
        } catch (...) {
            q.trace.append("unknown-error");
        }
        snapshots.append(q.snapshot());
    };
    for (int i = 0; i < 3; ++i) {
        auto c = QueueState::configuration(scene + i);
        Notice n{QString("submitted-%1").arg(i), "正文", {"PNG"}};
        guarded([&] {
            if (rust)
                NotificationQueueSdk{q, &c, &n}.run(rust::WorkflowMethod::NoticeQueueSubmit);
            else
                q.submit(c, n);
        });
    }
    guarded([&] {
        const bool idle =
            rust ? NotificationQueueSdk{q}.run(rust::WorkflowMethod::NoticeQueueIdle)[0] != 0 : q.idle();
        q.trace.append(QJsonObject{{"idle", idle}});
    });
    guarded([&] {
        if (rust)
            NotificationQueueSdk{q}.run(rust::WorkflowMethod::NoticeQueueRun);
        else
            q.run();
    });
    guarded([&] {
        if (rust)
            NotificationQueueSdk{q}.run(rust::WorkflowMethod::NoticeQueueStop);
        else
            q.stop();
    });
    return {{"trace", q.trace}, {"snapshots", snapshots}};
}
} // namespace okww::runtime::notice_queue_test
