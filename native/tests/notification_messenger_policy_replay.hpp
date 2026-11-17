#pragma once
#include "runtime/notification.hpp"
#include "runtime/workflow_abi.hpp"
namespace okww::runtime::notice_messenger_test {
inline HWND handle(int v) { return reinterpret_cast<HWND>(uintptr_t(v)); }
struct State {
    int scene, foreground = 0, checks = 0, messages = 0;
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
        return ++ticks * (active->scene % 29 == 0 ? 60000 : 100);
    }
};
template <class Duration> void sleepFor(Duration d) {
    active->trace.append(
        QJsonObject{{"sleep", double(std::chrono::duration_cast<std::chrono::milliseconds>(d).count())}});
}
inline BOOL IsWindow(HWND h) {
    active->trace.append(QJsonObject{{"window", double(uintptr_t(h))}});
    return active->scene % 19 != 0;
}
inline HWND GetForegroundWindow() {
    active->trace.append("foreground");
    return handle(active->scene % 29 == 0 || ++active->foreground <= 2 ? 1 : 4);
}
inline DWORD processId(HWND h) {
    active->trace.append(QJsonObject{{"pid", double(uintptr_t(h))}});
    return DWORD(uintptr_t(h));
}
inline UINT MapVirtualKeyW(UINT key, UINT kind) {
    active->trace.append(QJsonObject{{"map-key", int(key)}, {"kind", int(kind)}});
    return key + 10;
}
inline LRESULT SendMessageTimeoutW(HWND h, UINT msg, WPARAM w, LPARAM l, UINT flags, UINT timeout,
                                   PDWORD_PTR result) {
    active->trace.append(QJsonObject{{"message", int(msg)},
                                     {"h", double(uintptr_t(h))},
                                     {"w", QString::number(quint64(w))},
                                     {"l", QString::number(qint64(l))},
                                     {"flags", int(flags)},
                                     {"timeout", int(timeout)}});
    *result = 0;
    if (active->scene % 37 == 0 && ++active->messages == 2)
        throw std::runtime_error("message fixture");
    return active->scene % 11 ? 1 : 0;
}
inline HWND renderSurface(HWND h) {
    active->trace.append(QJsonObject{{"render", double(uintptr_t(h))}});
    return active->scene % 2 ? h : handle(2);
}
inline int MapWindowPoints(HWND from, HWND to, LPPOINT point, UINT n) {
    active->trace.append(
        QJsonObject{{"map-point", int(n)}, {"from", double(uintptr_t(from))}, {"to", double(uintptr_t(to))}});
    point->x -= 3;
    point->y += 4;
    return 1;
}
inline cv::Mat capture(HWND h) {
    active->trace.append(QJsonObject{{"capture", double(uintptr_t(h))}});
    if (active->scene % 17 == 0)
        throw std::runtime_error("capture fixture");
    return cv::Mat::ones(2, 3, CV_8U);
}
inline cv::Mat boundedDesktopCapture(std::function<cv::Mat()> fn, const NoticeCancelled &, int = 3000) {
    active->trace.append("bounded-capture");
    return fn();
}
struct Ocr {
    QVector<TextLine> read(const cv::Mat &frame, std::optional<QRect> region, double score) {
        active->trace.append(QJsonObject{
            {"ocr", score},
            {"size", QJsonArray{frame.cols, frame.rows}},
            {"region", region ? QJsonArray{region->x(), region->y(), region->width(), region->height()}
                              : QJsonArray{}}});
        QVector<TextLine> lines;
        const QStringList values{"unrelated", " TARGET ", "send button", "prefix-Paste", "搜索"};
        for (int i = 0; i < 3; ++i) {
            TextLine line;
            line.text = values[(active->scene + i) % values.size()];
            line.box = {10 + i, 20 + i, 31 + i, 42 + i};
            lines << line;
        }
        return lines;
    }
};
struct Base {
    HWND root_ = handle(1);
    QString nickname_ = "target";
    QRect header_{1, 2, 3, 4};
    NoticeCancelled cancel_;
    Ocr ocr_;
    explicit Base() {
        if (active->scene % 5)
            cancel_ = [] {
                active->trace.append("cancel");
                return active->scene % 13 == 0 && ++active->checks >= 4;
            };
    }
};
struct Original : Base {
    void check() {
        if (cancel_ && cancel_())
            fail("桌面通知已取消");
        if (!notice_messenger_test::IsWindow(root_))
            fail("桌面聊天窗口已关闭");
    }
    void wait(double seconds) {
        Timer timer;
        timer.start();
        while (timer.elapsed() < seconds * 1000) {
            check();
            sleepFor(std::chrono::milliseconds(50));
        }
    }
    void background() {
        Timer timer;
        timer.start();
        while (notice_messenger_test::GetForegroundWindow() == root_ ||
               processId(notice_messenger_test::GetForegroundWindow()) == processId(root_)) {
            check();
            if (timer.elapsed() > 120000)
                fail("桌面聊天窗口正在使用，通知超时未发送");
            wait(.25);
        }
        check();
    }
    void message(HWND h, UINT msg, WPARAM w = 0, LPARAM l = 0) {
        background();
        DWORD_PTR result = 0;
        if (!notice_messenger_test::SendMessageTimeoutW(h, msg, w, l, SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000,
                                                        &result))
            fail("桌面聊天控件没有响应");
    }
    void key(HWND h, int vk, bool down) {
        LPARAM flags = 1 | (notice_messenger_test::MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16);
        if (!down)
            flags |= (LPARAM(1) << 30) | (LPARAM(1) << 31);
        message(h, down ? WM_KEYDOWN : WM_KEYUP, vk, flags);
    }
    void tap(HWND h, int vk) {
        key(h, vk, true);
        key(h, vk, false);
    }
    void click(HWND h, QPoint point, bool right = false, bool surface = false) {
        HWND target = surface ? renderSurface(h) : h;
        if (target != h) {
            POINT p{point.x(), point.y()};
            notice_messenger_test::MapWindowPoints(h, target, &p, 1);
            point = {p.x, p.y};
        }
        const auto pos = MAKELPARAM(point.x(), point.y());
        message(target, WM_MOUSEMOVE, 0, pos);
        message(target, right ? WM_RBUTTONDOWN : WM_LBUTTONDOWN, right ? MK_RBUTTON : MK_LBUTTON, pos);
        message(target, right ? WM_RBUTTONUP : WM_LBUTTONUP, 0, pos);
    }
    void type(HWND h, QString text) {
        for (const auto c : text)
            message(h, WM_CHAR, c.unicode());
    }
    QVector<TextLine> read(HWND h, QRect region = {}) {
        background();
        auto frame = notice_messenger_test::boundedDesktopCapture([h] { return capture(h); }, cancel_);
        check();
        auto lines = ocr_.read(frame, region.isEmpty() ? std::optional<QRect>{} : region, .1);
        check();
        return lines;
    }
    std::optional<QPoint> find(HWND h, QStringList names, QRect region, double timeout, bool exact = true,
                               bool suffix = false) {
        for (auto &n : names)
            n = n.trimmed().toCaseFolded();
        Timer timer;
        timer.start();
        do {
            for (const auto &line : read(h, region)) {
                const auto text = line.text.trimmed().toCaseFolded();
                if (std::any_of(names.begin(), names.end(), [&](const auto &n) {
                        return suffix ? text.endsWith(n) : exact ? text == n : text.contains(n);
                    }))
                    return QPoint(line.box.x() + line.box.width() / 2, line.box.y() + line.box.height() / 2);
            }
            wait(.15);
        } while (timer.elapsed() < timeout * 1000);
        return {};
    }
    void confirmRecipient() {
        if (!find(root_, {nickname_}, header_, 2))
            fail("聊天标题与接收昵称不符，通知没有发送");
    }
    void clear(HWND h, bool search) {
        if (search) {
            key(h, VK_CONTROL, true);
            try {
                tap(h, 'A');
            } catch (...) {
                try {
                    key(h, VK_CONTROL, false);
                } catch (...) {
                }
                throw;
            }
            key(h, VK_CONTROL, false);
            tap(h, VK_BACK);
            tap(h, VK_DELETE);
        } else {
            tap(h, VK_END);
            for (int i = 0; i < 256; ++i)
                tap(h, VK_BACK);
            tap(h, VK_HOME);
            for (int i = 0; i < 256; ++i)
                tap(h, VK_DELETE);
        }
    }
};
struct Current : Base {
    struct Call {
        HWND h = nullptr, target = nullptr;
        UINT msg = 0;
        WPARAM w = 0;
        LPARAM l = 0;
        int vk = 0;
        QPoint point;
        QRect region;
        QString text, folded;
        QStringList names;
        cv::Mat frame;
        QVector<TextLine> lines;
        std::optional<QPoint> found;
        Timer timer;
    };
    void workflow(rust::WorkflowMethod method, Call &c, std::span<const double> args = {}) {
        rust::workflowRun(method, args, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::NoticeMessengerHasCancellation:
                out[0] = bool(cancel_);
                break;
            case rust::WorkflowOp::NoticeMessengerCancelled:
                out[0] = cancel_();
                break;
            case rust::WorkflowOp::NoticeMessengerWindowExists:
                out[0] = notice_messenger_test::IsWindow(root_);
                break;
            case rust::WorkflowOp::NoticeMessengerStartTimer:
                c.timer.start();
                break;
            case rust::WorkflowOp::NoticeMessengerElapsed:
                out[0] = double(c.timer.elapsed());
                break;
            case rust::WorkflowOp::NoticeMessengerCheck:
                check();
                break;
            case rust::WorkflowOp::NoticeMessengerSleep:
                sleepFor(std::chrono::milliseconds(int(a[0])));
                break;
            case rust::WorkflowOp::NoticeMessengerForegroundRoot:
                out[0] = notice_messenger_test::GetForegroundWindow() == root_;
                break;
            case rust::WorkflowOp::NoticeMessengerForegroundProcess:
                out[0] = processId(notice_messenger_test::GetForegroundWindow()) == processId(root_);
                break;
            case rust::WorkflowOp::NoticeMessengerWait:
                wait(a[0]);
                break;
            case rust::WorkflowOp::NoticeMessengerBackground:
                background();
                break;
            case rust::WorkflowOp::NoticeMessengerSendMessage: {
                DWORD_PTR result = 0;
                out[0] = notice_messenger_test::SendMessageTimeoutW(
                             c.h, c.msg, c.w, c.l, SMTO_ABORTIFHUNG | SMTO_BLOCK, UINT(a[0]), &result) != 0;
                break;
            }
            case rust::WorkflowOp::NoticeMessengerKeyScanFlags:
                out[0] = 1 | (notice_messenger_test::MapVirtualKeyW(c.vk, MAPVK_VK_TO_VSC) << 16);
                break;
            case rust::WorkflowOp::NoticeMessengerKeyFlags:
                c.l = LPARAM(uint32_t(a[0]));
                break;
            case rust::WorkflowOp::NoticeMessengerKeyDown:
                message(c.h, WM_KEYDOWN, c.vk, c.l);
                break;
            case rust::WorkflowOp::NoticeMessengerKeyUp:
                message(c.h, WM_KEYUP, c.vk, c.l);
                break;
            case rust::WorkflowOp::NoticeMessengerKey:
                key(c.h, c.vk, bool(a[0]));
                break;
            case rust::WorkflowOp::NoticeMessengerRenderTarget:
                c.target = renderSurface(c.h);
                break;
            case rust::WorkflowOp::NoticeMessengerOriginalTarget:
                c.target = c.h;
                break;
            case rust::WorkflowOp::NoticeMessengerTargetIsOriginal:
                out[0] = c.target == c.h;
                break;
            case rust::WorkflowOp::NoticeMessengerMapPoint: {
                POINT p{c.point.x(), c.point.y()};
                notice_messenger_test::MapWindowPoints(c.h, c.target, &p, 1);
                c.point = {p.x, p.y};
                break;
            }
            case rust::WorkflowOp::NoticeMessengerPointParameter:
                c.l = MAKELPARAM(c.point.x(), c.point.y());
                break;
            case rust::WorkflowOp::NoticeMessengerMouseMove:
                message(c.target, WM_MOUSEMOVE, 0, c.l);
                break;
            case rust::WorkflowOp::NoticeMessengerMouseDown: {
                const UINT msgs[]{WM_LBUTTONDOWN, WM_RBUTTONDOWN};
                const WPARAM buttons[]{MK_LBUTTON, MK_RBUTTON};
                message(c.target, msgs[int(a[0])], buttons[int(a[0])], c.l);
                break;
            }
            case rust::WorkflowOp::NoticeMessengerMouseUp: {
                const UINT msgs[]{WM_LBUTTONUP, WM_RBUTTONUP};
                message(c.target, msgs[int(a[0])], 0, c.l);
                break;
            }
            case rust::WorkflowOp::NoticeMessengerTextSize:
                out[0] = double(c.text.size());
                break;
            case rust::WorkflowOp::NoticeMessengerCharacter:
                message(c.h, WM_CHAR, c.text.at(int(a[0])).unicode());
                break;
            case rust::WorkflowOp::NoticeMessengerCapture:
                c.frame =
                    notice_messenger_test::boundedDesktopCapture([h = c.h] { return capture(h); }, cancel_);
                break;
            case rust::WorkflowOp::NoticeMessengerOcr:
                c.lines = ocr_.read(c.frame, c.region.isEmpty() ? std::optional<QRect>{} : c.region, .1);
                break;
            case rust::WorkflowOp::NoticeMessengerNameCount:
                out[0] = double(c.names.size());
                break;
            case rust::WorkflowOp::NoticeMessengerFoldName:
                c.names[int(a[0])] = c.names.at(int(a[0])).trimmed().toCaseFolded();
                break;
            case rust::WorkflowOp::NoticeMessengerRead:
                c.lines = read(c.h, c.region);
                break;
            case rust::WorkflowOp::NoticeMessengerLineCount:
                out[0] = double(c.lines.size());
                break;
            case rust::WorkflowOp::NoticeMessengerFoldLine:
                c.folded = c.lines.at(int(a[0])).text.trimmed().toCaseFolded();
                break;
            case rust::WorkflowOp::NoticeMessengerTextSuffix:
                out[0] = c.folded.endsWith(c.names.at(int(a[0])));
                break;
            case rust::WorkflowOp::NoticeMessengerTextEqual:
                out[0] = c.folded == c.names.at(int(a[0]));
                break;
            case rust::WorkflowOp::NoticeMessengerTextContains:
                out[0] = c.folded.contains(c.names.at(int(a[0])));
                break;
            case rust::WorkflowOp::NoticeMessengerFoundLine: {
                const auto &r = c.lines.at(int(a[0])).box;
                c.found = QPoint(r.x() + r.width() / 2, r.y() + r.height() / 2);
                c.lines = {};
                c.folded = {};
                break;
            }
            case rust::WorkflowOp::NoticeMessengerDropLines:
                c.lines = {};
                c.folded = {};
                break;
            case rust::WorkflowOp::NoticeMessengerFindRecipient:
                out[0] = find(root_, {nickname_}, header_, a[0]).has_value();
                break;
            case rust::WorkflowOp::NoticeMessengerControlDown:
                key(c.h, VK_CONTROL, true);
                break;
            case rust::WorkflowOp::NoticeMessengerControlUp:
                key(c.h, VK_CONTROL, false);
                break;
            case rust::WorkflowOp::NoticeMessengerTapA:
                tap(c.h, 'A');
                break;
            case rust::WorkflowOp::NoticeMessengerTapBackspace:
                tap(c.h, VK_BACK);
                break;
            case rust::WorkflowOp::NoticeMessengerTapDelete:
                tap(c.h, VK_DELETE);
                break;
            case rust::WorkflowOp::NoticeMessengerTapEnd:
                tap(c.h, VK_END);
                break;
            case rust::WorkflowOp::NoticeMessengerTapHome:
                tap(c.h, VK_HOME);
                break;
            case rust::WorkflowOp::NoticeMessengerFailure: {
                static const QStringList messages{
                    "桌面通知已取消", "桌面聊天窗口已关闭", "桌面聊天窗口正在使用，通知超时未发送",
                    "桌面聊天控件没有响应", "聊天标题与接收昵称不符，通知没有发送"};
                fail(messages.at(int(a[0])));
            }
            default:
                fail("Rust 桌面聊天宿主无效");
            }
        });
    }
    void check() {
        Call c;
        workflow(rust::WorkflowMethod::NoticeMessengerCheck, c);
    }
    void wait(double seconds) {
        Call c;
        const std::array<double, 1> a{seconds};
        workflow(rust::WorkflowMethod::NoticeMessengerWait, c, a);
    }
    void background() {
        Call c;
        workflow(rust::WorkflowMethod::NoticeMessengerBackground, c);
    }
    void message(HWND h, UINT msg, WPARAM w = 0, LPARAM l = 0) {
        Call c;
        c.h = h;
        c.msg = msg;
        c.w = w;
        c.l = l;
        workflow(rust::WorkflowMethod::NoticeMessengerMessage, c);
    }
    void key(HWND h, int vk, bool down) {
        Call c;
        c.h = h;
        c.vk = vk;
        const std::array<double, 1> a{double(down)};
        workflow(rust::WorkflowMethod::NoticeMessengerKey, c, a);
    }
    void tap(HWND h, int vk) {
        Call c;
        c.h = h;
        c.vk = vk;
        workflow(rust::WorkflowMethod::NoticeMessengerTap, c);
    }
    void click(HWND h, QPoint point, bool right = false, bool surface = false) {
        Call c;
        c.h = h;
        c.point = point;
        const std::array<double, 2> a{double(right), double(surface)};
        workflow(rust::WorkflowMethod::NoticeMessengerClick, c, a);
    }
    void type(HWND h, QString text) {
        Call c;
        c.h = h;
        c.text = std::move(text);
        workflow(rust::WorkflowMethod::NoticeMessengerType, c);
    }
    QVector<TextLine> read(HWND h, QRect region = {}) {
        Call c;
        c.h = h;
        c.region = region;
        workflow(rust::WorkflowMethod::NoticeMessengerRead, c);
        return c.lines;
    }
    std::optional<QPoint> find(HWND h, QStringList names, QRect region, double timeout, bool exact = true,
                               bool suffix = false) {
        Call c;
        c.h = h;
        c.names = std::move(names);
        c.region = region;
        const std::array<double, 3> a{timeout, double(exact), double(suffix)};
        workflow(rust::WorkflowMethod::NoticeMessengerFind, c, a);
        return c.found;
    }
    void confirmRecipient() {
        Call c;
        workflow(rust::WorkflowMethod::NoticeMessengerConfirmRecipient, c);
    }
    void clear(HWND h, bool search) {
        Call c;
        c.h = h;
        const std::array<double, 1> a{double(search)};
        workflow(rust::WorkflowMethod::NoticeMessengerClear, c, a);
    }
};
template <class T> QJsonObject outcome(int scene, int method) {
    State state(scene);
    active = &state;
    T m;
    QJsonObject result;
    try {
        switch (method) {
        case 0:
            m.check();
            break;
        case 1:
            m.wait(scene % 7 ? 0.2 : std::numeric_limits<double>::quiet_NaN());
            break;
        case 2:
            m.background();
            break;
        case 3:
            m.message(handle(3), WM_CHAR, 'Q', 123);
            break;
        case 4:
            m.key(handle(3), VK_CONTROL, scene % 2);
            break;
        case 5:
            m.tap(handle(3), 'A');
            break;
        case 6:
            m.click(handle(3), {17, 29}, scene % 2, scene % 3);
            break;
        case 7:
            m.type(handle(3), QString::fromUtf8("A中文😀"));
            break;
        case 8: {
            auto lines = m.read(handle(3), scene % 2 ? QRect{} : QRect(1, 2, 10, 12));
            QJsonArray text;
            for (const auto &line : lines)
                text.append(line.text);
            result["lines"] = text;
            break;
        }
        case 9: {
            auto point =
                m.find(handle(3), {"target", "paste", "send"}, {1, 2, 3, 4},
                       scene % 7 ? 0.3 : std::numeric_limits<double>::quiet_NaN(), scene % 2, scene % 3 == 0);
            if (point)
                result["point"] = QJsonArray{point->x(), point->y()};
            break;
        }
        case 10:
            m.confirmRecipient();
            break;
        case 11:
            m.clear(handle(3), scene % 2);
            break;
        }
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    } catch (...) {
        result["error"] = "unknown";
    }
    result["trace"] = state.trace;
    return result;
}
} // namespace okww::runtime::notice_messenger_test
