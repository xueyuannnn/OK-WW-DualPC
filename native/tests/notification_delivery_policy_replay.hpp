#pragma once
#include "runtime/notification.hpp"
#include "runtime/workflow_abi.hpp"
namespace okww::runtime::notice_delivery_test {
inline HWND handle(int v) { return reinterpret_cast<HWND>(uintptr_t(v)); }
struct State {
    int scene, finds = 0, clicks = 0;
    QJsonArray trace;
    explicit State(int s) : scene(s) {}
};
inline thread_local State *active = nullptr;
struct Timer {
    int ticks = 0;
    void start() {
        active->trace.append("timer-start");
        ticks = 0;
    }
    qint64 elapsed() {
        active->trace.append("elapsed");
        return ++ticks * 4000;
    }
};
inline BOOL GetClientRect(HWND h, LPRECT r) {
    active->trace.append(QJsonObject{{"client", double(uintptr_t(h))}});
    *r = {0, 0, (active->scene % 9) * 200, (active->scene % 7) * 150};
    return true;
}
inline UINT GetDpiForWindow(HWND h) {
    active->trace.append(QJsonObject{{"dpi", double(uintptr_t(h))}});
    return active->scene % 6 == 0 ? 0 : 96 + (active->scene % 4) * 48;
}
inline BOOL IsWindowVisible(HWND h) {
    active->trace.append(QJsonObject{{"visible", double(uintptr_t(h))}});
    return active->scene % 3 != 0;
}
inline QVector<HWND> popups(HWND h, bool contact) {
    active->trace.append(QJsonObject{{"popups", contact}, {"root", double(uintptr_t(h))}});
    return active->scene % 3 ? QVector<HWND>{handle(2), handle(3)} : QVector<HWND>{};
}
inline HWND renderSurface(HWND h) {
    active->trace.append("render");
    return active->scene % 2 ? h : handle(4);
}
inline std::optional<QPoint> messengerContact(const QVector<TextLine> &lines, QString nickname, bool popup) {
    active->trace.append(QJsonObject{{"contact", nickname},
                                     {"popup", popup},
                                     {"source", lines.isEmpty() ? QString{} : lines.front().text}});
    if (active->scene % 4 == 0 || ((popup || active->finds > 0) && active->scene % 5))
        return QPoint(101, 202);
    return {};
}
struct ClipboardImage {
    explicit ClipboardImage(const QByteArray &png) {
        active->trace.append(QJsonObject{{"clipboard", QString::fromUtf8(png)}});
        if (active->scene % 13 == 0)
            throw std::runtime_error("clipboard fixture");
    }
    ~ClipboardImage() { active->trace.append("restore-clipboard"); }
};
struct Base {
    bool wechat_, wechat;
    HWND root_ = handle(1);
    QString nickname_ = "target first";
    double dpi_ = 1;
    QRect search_, contacts_, header_, send_;
    explicit Base() : wechat_(active->scene % 2), wechat(wechat_) {}
    void background() { active->trace.append("background"); }
    void wait(double seconds) { active->trace.append(QJsonObject{{"wait", seconds}}); }
    void confirmRecipient() {
        active->trace.append("confirm");
        if (active->scene % 31 == 0)
            throw std::runtime_error("recipient fixture");
    }
    void click(HWND h, QPoint point, bool right = false, bool surface = false) {
        active->trace.append(QJsonObject{{"click", double(uintptr_t(h))},
                                         {"point", QJsonArray{point.x(), point.y()}},
                                         {"right", right},
                                         {"surface", surface}});
        ++active->clicks;
        if (active->scene % 17 == 0 && active->clicks == 5)
            throw std::runtime_error("click fixture");
        if (active->scene % 37 == 0 && active->clicks == 3)
            throw 27;
    }
    void clear(HWND h, bool search) {
        active->trace.append(QJsonObject{{"clear", double(uintptr_t(h))}, {"search", search}});
    }
    void type(HWND h, QString text) {
        active->trace.append(QJsonObject{{"type", double(uintptr_t(h))}, {"text", text}});
    }
    void tap(HWND h, int key) {
        active->trace.append(QJsonObject{{"tap", key}, {"h", double(uintptr_t(h))}});
    }
    QVector<TextLine> read(HWND h, QRect region = {}) {
        active->trace.append(
            QJsonObject{{"read", double(uintptr_t(h))},
                        {"rect", QJsonArray{region.x(), region.y(), region.width(), region.height()}}});
        TextLine line;
        line.text = QString::number(uintptr_t(h));
        return {line};
    }
    std::optional<QPoint> find(HWND h, QStringList names, QRect region, double timeout, bool exact = true,
                               bool suffix = false) {
        active->trace.append(
            QJsonObject{{"find", QJsonArray::fromStringList(names)},
                        {"h", double(uintptr_t(h))},
                        {"rect", QJsonArray{region.x(), region.y(), region.width(), region.height()}},
                        {"timeout", timeout},
                        {"exact", exact},
                        {"suffix", suffix}});
        ++active->finds;
        if (timeout == 0.05) {
            if (active->scene % 11)
                return QPoint(333, 444);
            return {};
        }
        if (timeout == 0.6) {
            if (active->scene % 4 == 1)
                return QPoint(100, 200);
            return {};
        }
        if (timeout == 1.5) {
            if (active->scene % 3)
                return QPoint(20, 30);
            return {};
        }
        if (timeout == 8) {
            if (active->scene % 7)
                return QPoint(555, 666);
            return {};
        }
        return {};
    }
    QJsonObject geometry() const {
        auto rect = [](QRect r) { return QJsonArray{r.x(), r.y(), r.width(), r.height()}; };
        return {{"dpi", dpi_},
                {"search", rect(search_)},
                {"contacts", rect(contacts_)},
                {"header", rect(header_)},
                {"send", rect(send_)}};
    }
};
struct Original : Base {
    void image(const QByteArray &png, QPoint composer, QPoint send) {
        confirmRecipient();
        ClipboardImage clipboard(png);
        click(root_, composer, false, true);
        wait(1);
        click(root_, composer, true, true);
        wait(1);
        std::optional<QPoint> paste;
        HWND menu = nullptr;
        Timer timer;
        timer.start();
        do {
            auto candidates = popups(root_, false);
            auto render = renderSurface(root_);
            if (render != root_)
                candidates.prepend(render);
            candidates << root_;
            for (auto candidate : candidates) {
                paste = find(candidate, {"Paste", "粘贴", "貼上"}, {}, .05, true, !wechat_);
                if (paste) {
                    menu = candidate;
                    break;
                }
            }
            if (paste)
                break;
            wait(.15);
        } while (timer.elapsed() < 8000);
        if (!paste)
            fail("无法识别聊天菜单中的粘贴按钮，截图未发送");
        click(menu, *paste, false, menu == root_);
        wait(1);
        confirmRecipient();
        click(root_, send);
        wait(1);
    }
    void initialize() {
        RECT r{};
        notice_delivery_test::GetClientRect(root_, &r);
        dpi_ = notice_delivery_test::GetDpiForWindow(root_) / 96.;
        if (dpi_ <= 0)
            dpi_ = 1;
        const int panel = int((wechat ? 295 : 377) * dpi_), left = int(55 * dpi_);
        search_ = {left, 0, std::max(1, panel - left), int(120 * dpi_)};
        contacts_ = {left, int(75 * dpi_), std::max(1, panel - left),
                     std::max(1, int(r.bottom) - int(75 * dpi_))};
        header_ = {panel, 0, std::max(1, int(r.right) - panel), int(120 * dpi_)};
        const int sendLeft = std::max(panel, int(r.right) - int(260 * dpi_));
        send_ = {sendLeft, std::max(0, int(r.bottom) - int(150 * dpi_)), std::max(1, int(r.right) - sendLeft),
                 int(150 * dpi_)};
    }
    void send(const Notice &notice) {
        background();
        wait(5);
        auto contact = notice_delivery_test::messengerContact(read(root_, contacts_), nickname_, false);
        if (contact)
            click(root_, *contact);
        else if (!(find(root_, {"Send", "发送"}, send_, .6, false) &&
                   find(root_, {nickname_}, header_, .6))) {
            auto search = find(root_, {"Search", "搜索"}, search_, 1.5, false);
            click(root_, search.value_or(
                             QPoint(int((wechat_ ? 163 : 200) * dpi_), int((wechat_ ? 56 : 65) * dpi_))));
            wait(1);
            clear(root_, true);
            wait(1);
            type(root_, wechat_ ? nickname_.section(QRegularExpression("\\s+"), 0, 0) : nickname_);
            wait(1);
            HWND selected = nullptr;
            Timer timer;
            timer.start();
            do {
                auto candidates = popups(root_, true);
                candidates << root_;
                for (auto candidate : candidates) {
                    contact = notice_delivery_test::messengerContact(
                        read(candidate, candidate == root_ ? contacts_ : QRect{}), nickname_,
                        candidate != root_);
                    if (contact) {
                        selected = candidate;
                        break;
                    }
                }
                if (selected)
                    break;
                wait(.3);
            } while (timer.elapsed() < 8000);
            if (!selected)
                fail("没有找到唯一匹配的聊天联系人");
            click(selected, *contact);
            wait(1);
            if (!wechat_ && selected != root_ && notice_delivery_test::IsWindowVisible(selected))
                tap(root_, VK_ESCAPE);
        }
        auto send = find(root_, {"Send", "发送"}, send_, 8, false);
        if (!send)
            fail("无法识别聊天发送按钮");
        confirmRecipient();
        const QPoint composer(std::max(20, send->x() - 80), std::max(20, send->y() - 45));
        click(root_, composer);
        wait(1);
        clear(root_, false);
        wait(1);
        const auto text =
            "OKWW:\n" + (notice.title.isEmpty() ? notice.text : notice.title + '\n' + notice.text);
        type(root_, text);
        wait(1);
        confirmRecipient();
        click(root_, *send);
        wait(1);
        for (const auto &png : notice.images)
            image(png, composer, *send);
    }
};
struct Current : Base {
    struct DeliveryCall {
        std::optional<ClipboardImage> clipboard;
        const Notice *notice = nullptr;
        const QByteArray *png = nullptr;
        QPoint composer, send;
        std::optional<QPoint> contact, search, paste;
        HWND menu = nullptr, selected = nullptr, candidate = nullptr, render = nullptr;
        QVector<HWND> candidates;
        Timer timer;
        QString text;
    };
    void delivery(rust::WorkflowMethod method, DeliveryCall &c, std::span<const double> args = {}) {
        rust::workflowRun(method, args, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::NoticeDeliveryConfirm:
                confirmRecipient();
                break;
            case rust::WorkflowOp::NoticeDeliveryClipboard:
                c.clipboard.emplace(*c.png);
                break;
            case rust::WorkflowOp::NoticeDeliveryComposerSurface:
                click(root_, c.composer, bool(a[0]), true);
                break;
            case rust::WorkflowOp::NoticeDeliveryWait:
                wait(a[0]);
                break;
            case rust::WorkflowOp::NoticeDeliveryTimerStart:
                c.timer.start();
                break;
            case rust::WorkflowOp::NoticeDeliveryElapsed:
                out[0] = double(c.timer.elapsed());
                break;
            case rust::WorkflowOp::NoticeDeliveryPopupCandidates:
                c.candidates = popups(root_, bool(a[0]));
                break;
            case rust::WorkflowOp::NoticeDeliveryRenderSurface:
                c.render = renderSurface(root_);
                break;
            case rust::WorkflowOp::NoticeDeliveryRenderIsRoot:
                out[0] = c.render == root_;
                break;
            case rust::WorkflowOp::NoticeDeliveryPrependRender:
                c.candidates.prepend(c.render);
                break;
            case rust::WorkflowOp::NoticeDeliveryAppendRoot:
                c.candidates << root_;
                break;
            case rust::WorkflowOp::NoticeDeliveryCandidateCount:
                out[0] = double(c.candidates.size());
                break;
            case rust::WorkflowOp::NoticeDeliveryCandidate:
                c.candidate = c.candidates.at(qsizetype(a[0]));
                break;
            case rust::WorkflowOp::NoticeDeliveryFindPaste:
                c.paste = find(c.candidate, {"Paste", "粘贴", "貼上"}, {}, .05, true, !wechat_);
                out[0] = c.paste.has_value();
                break;
            case rust::WorkflowOp::NoticeDeliveryUseMenu:
                c.menu = c.candidate;
                break;
            case rust::WorkflowOp::NoticeDeliveryHasPaste:
                out[0] = c.paste.has_value();
                break;
            case rust::WorkflowOp::NoticeDeliveryDropCandidates:
                c.candidates = {};
                break;
            case rust::WorkflowOp::NoticeDeliveryClickPaste:
                click(c.menu, *c.paste, false, c.menu == root_);
                break;
            case rust::WorkflowOp::NoticeDeliveryClickSend:
                click(root_, c.send);
                break;
            case rust::WorkflowOp::NoticeDeliveryClientSize: {
                RECT r{};
                notice_delivery_test::GetClientRect(root_, &r);
                out[0] = r.right;
                out[1] = r.bottom;
                break;
            }
            case rust::WorkflowOp::NoticeDeliveryQueryDpi:
                dpi_ = notice_delivery_test::GetDpiForWindow(root_) / 96.;
                break;
            case rust::WorkflowOp::NoticeDeliveryDpi:
                out[0] = dpi_;
                break;
            case rust::WorkflowOp::NoticeDeliverySetDpi:
                dpi_ = a[0];
                break;
            case rust::WorkflowOp::NoticeDeliverySetSearchRect:
                search_ = {int(a[0]), int(a[1]), int(a[2]), int(a[3])};
                break;
            case rust::WorkflowOp::NoticeDeliverySetContactsRect:
                contacts_ = {int(a[0]), int(a[1]), int(a[2]), int(a[3])};
                break;
            case rust::WorkflowOp::NoticeDeliverySetHeaderRect:
                header_ = {int(a[0]), int(a[1]), int(a[2]), int(a[3])};
                break;
            case rust::WorkflowOp::NoticeDeliverySetSendRect:
                send_ = {int(a[0]), int(a[1]), int(a[2]), int(a[3])};
                break;
            case rust::WorkflowOp::NoticeDeliveryBackground:
                background();
                break;
            case rust::WorkflowOp::NoticeDeliveryInitialContact:
                c.contact = notice_delivery_test::messengerContact(read(root_, contacts_), nickname_, false);
                out[0] = c.contact.has_value();
                break;
            case rust::WorkflowOp::NoticeDeliveryClickInitialContact:
                click(root_, *c.contact);
                break;
            case rust::WorkflowOp::NoticeDeliveryFindReadySend:
                out[0] = find(root_, {"Send", "发送"}, send_, a[0], false).has_value();
                break;
            case rust::WorkflowOp::NoticeDeliveryFindReadyHeader:
                out[0] = find(root_, {nickname_}, header_, a[0]).has_value();
                break;
            case rust::WorkflowOp::NoticeDeliveryFindSearch:
                c.search = find(root_, {"Search", "搜索"}, search_, a[0], false);
                break;
            case rust::WorkflowOp::NoticeDeliveryClickSearch:
                click(root_, c.search.value_or(
                                 QPoint(int((wechat_ ? 163 : 200) * dpi_), int((wechat_ ? 56 : 65) * dpi_))));
                break;
            case rust::WorkflowOp::NoticeDeliveryClear:
                clear(root_, bool(a[0]));
                break;
            case rust::WorkflowOp::NoticeDeliveryIsWechat:
                out[0] = wechat_;
                break;
            case rust::WorkflowOp::NoticeDeliveryTypeWechatNickname:
                type(root_, nickname_.section(QRegularExpression("\\s+"), 0, 0));
                break;
            case rust::WorkflowOp::NoticeDeliveryTypeFullNickname:
                type(root_, nickname_);
                break;
            case rust::WorkflowOp::NoticeDeliveryCandidateIsRoot:
                out[0] = c.candidate == root_;
                break;
            case rust::WorkflowOp::NoticeDeliveryFindContact:
                c.contact = notice_delivery_test::messengerContact(
                    read(c.candidate, bool(a[0]) ? contacts_ : QRect{}), nickname_, !bool(a[0]));
                out[0] = c.contact.has_value();
                break;
            case rust::WorkflowOp::NoticeDeliveryUseSelected:
                c.selected = c.candidate;
                break;
            case rust::WorkflowOp::NoticeDeliveryHasSelected:
                out[0] = c.selected != nullptr;
                break;
            case rust::WorkflowOp::NoticeDeliveryClickSelected:
                click(c.selected, *c.contact);
                break;
            case rust::WorkflowOp::NoticeDeliverySelectedIsRoot:
                out[0] = c.selected == root_;
                break;
            case rust::WorkflowOp::NoticeDeliverySelectedVisible:
                out[0] = notice_delivery_test::IsWindowVisible(c.selected);
                break;
            case rust::WorkflowOp::NoticeDeliveryEscape:
                tap(root_, VK_ESCAPE);
                break;
            case rust::WorkflowOp::NoticeDeliveryFindSend: {
                auto point = find(root_, {"Send", "发送"}, send_, a[0], false);
                out[0] = point.has_value();
                c.send = point.value_or(QPoint{});
                break;
            }
            case rust::WorkflowOp::NoticeDeliverySendPoint:
                out[0] = c.send.x();
                out[1] = c.send.y();
                break;
            case rust::WorkflowOp::NoticeDeliverySetComposer:
                c.composer = {int(a[0]), int(a[1])};
                break;
            case rust::WorkflowOp::NoticeDeliveryClickComposer:
                click(root_, c.composer);
                break;
            case rust::WorkflowOp::NoticeDeliveryTitleEmpty:
                out[0] = c.notice->title.isEmpty();
                break;
            case rust::WorkflowOp::NoticeDeliveryBodyText:
                c.text = "OKWW:\n" + c.notice->text;
                break;
            case rust::WorkflowOp::NoticeDeliveryTitleText:
                c.text = "OKWW:\n" + c.notice->title + '\n' + c.notice->text;
                break;
            case rust::WorkflowOp::NoticeDeliveryTypeText:
                type(root_, c.text);
                break;
            case rust::WorkflowOp::NoticeDeliveryImageCount:
                out[0] = double(c.notice->images.size());
                break;
            case rust::WorkflowOp::NoticeDeliverySendImage:
                image(c.notice->images.at(qsizetype(a[0])), c.composer, c.send);
                break;
            case rust::WorkflowOp::NoticeDeliveryFailure: {
                static const QStringList errors{"无法识别聊天菜单中的粘贴按钮，截图未发送",
                                                "没有找到唯一匹配的聊天联系人", "无法识别聊天发送按钮",
                                                "请配置桌面通知接收人的完整昵称"};
                fail(errors.at(int(a[0])));
            }
            default:
                fail("Rust 桌面通知发送宿主无效");
            }
        });
    }
    void image(const QByteArray &png, QPoint composer, QPoint send) {
        DeliveryCall c;
        c.png = &png;
        c.composer = composer;
        c.send = send;
        delivery(rust::WorkflowMethod::NoticeDeliveryImage, c);
    }

    void initialize() {
        DeliveryCall c;
        const std::array<double, 1> a{double(wechat_)};
        delivery(rust::WorkflowMethod::NoticeDeliveryInitialize, c, a);
    }
    void send(const Notice &notice) {
        DeliveryCall c;
        c.notice = &notice;
        delivery(rust::WorkflowMethod::NoticeDeliverySend, c);
    }
};
template <class T> QJsonObject outcome(int scene, int method) {
    State state(scene);
    active = &state;
    T messenger;
    QJsonObject result;
    try {
        messenger.initialize();
        result["geometry"] = messenger.geometry();
        if (method == 1)
            messenger.send({scene % 3 ? QString("标题") : QString{},
                            "正文",
                            {QByteArray("PNG-A"), QByteArray("PNG-B")}});
        if (method == 2)
            messenger.image("PNG", {50, 60}, {500, 600});
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    } catch (...) {
        result["error"] = "unknown";
    }
    result["trace"] = state.trace;
    return result;
}
} // namespace okww::runtime::notice_delivery_test
