#include "notification.hpp"
#include "workflow_abi.hpp"
#include <future>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace okww::runtime {

namespace {
[[noreturn]] void desktopNoticeFailure(int code) {
    static const QStringList messages{"桌面截图等待参数无效", "桌面通知已取消",
                                      "上次桌面截图仍未返回，暂停后续桌面通知", "桌面截图超时，通知没有发送"};
    fail(messages.at(code));
}
struct BoundedNoticeCaptureSdk {
    std::function<cv::Mat()> &capture;
    const NoticeCancelled &cancelled;
    std::shared_ptr<std::atomic<bool>> occupied;
    std::shared_ptr<std::promise<cv::Mat>> completion;
    std::optional<std::future<cv::Mat>> result;
    QElapsedTimer elapsed;
    cv::Mat image;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::NoticeDesktopHasCapture:
            out[0] = bool(capture);
            break;
        case rust::WorkflowOp::NoticeDesktopHasCancellation:
            out[0] = bool(cancelled);
            break;
        case rust::WorkflowOp::NoticeDesktopCancelled:
            out[0] = cancelled();
            break;
        case rust::WorkflowOp::NoticeDesktopClaimCapture: {
            static const auto slot = std::make_shared<std::atomic<bool>>(false);
            occupied = slot;
            out[0] = occupied->exchange(true);
            break;
        }
        case rust::WorkflowOp::NoticeDesktopCreatePromise:
            completion = std::make_shared<std::promise<cv::Mat>>();
            break;
        case rust::WorkflowOp::NoticeDesktopGetFuture:
            result.emplace(completion->get_future());
            break;
        case rust::WorkflowOp::NoticeDesktopSpawnCapture:
            std::thread([capture = std::move(capture), completion = completion, busy = occupied] {
                cv::Mat image;
                std::exception_ptr error;
                rust::workflowRun(
                    rust::WorkflowMethod::NoticeDesktopCaptureWorker, {},
                    [&](rust::WorkflowOp op, const double *, double *out, const std::exception_ptr &cause) {
                        switch (op) {
                        case rust::WorkflowOp::NoticeDesktopExecuteCapture:
                            image = capture();
                            break;
                        case rust::WorkflowOp::NoticeDesktopRememberException:
                            error = cause;
                            break;
                        case rust::WorkflowOp::NoticeDesktopReleaseCapture:
                            busy->store(false);
                            break;
                        case rust::WorkflowOp::NoticeDesktopCaptureFailed:
                            out[0] = bool(error);
                            break;
                        case rust::WorkflowOp::NoticeDesktopPromiseException:
                            completion->set_exception(error);
                            break;
                        case rust::WorkflowOp::NoticeDesktopPromiseValue:
                            completion->set_value(std::move(image));
                            break;
                        default:
                            fail("Rust 桌面截图工作宿主无效");
                        }
                    });
            }).detach();
            break;
        case rust::WorkflowOp::NoticeDesktopReleaseCapture:
            occupied->store(false);
            break;
        case rust::WorkflowOp::NoticeDesktopTimerStart:
            elapsed.start();
            break;
        case rust::WorkflowOp::NoticeDesktopTimerElapsed:
            out[0] = double(elapsed.elapsed());
            break;
        case rust::WorkflowOp::NoticeDesktopFutureReady:
            out[0] = result->wait_for(std::chrono::milliseconds(int(a[0]))) == std::future_status::ready;
            break;
        case rust::WorkflowOp::NoticeDesktopFutureGet:
            image = result->get();
            break;
        case rust::WorkflowOp::NoticeDesktopFailure:
            desktopNoticeFailure(int(a[0]));
            break;
        default:
            fail("Rust 桌面截图宿主无效");
        }
    }
};
} // namespace
cv::Mat boundedDesktopCapture(std::function<cv::Mat()> capture, const NoticeCancelled &cancelled,
                              int timeoutMs) {
    BoundedNoticeCaptureSdk sdk{capture, cancelled};
    const std::array<double, 1> args{double(timeoutMs)};
    rust::workflowRun(rust::WorkflowMethod::NoticeDesktopBoundedCapture, args, sdk);
    return sdk.image;
}
std::optional<QPoint> messengerContact(const QVector<TextLine> &lines, QString nickname, bool popup) {
    QStringList allowed, headings;
    QVector<QPair<int, int>> sections;
    QPoint point;
    std::optional<QPoint> found;
    const std::array<double, 1> args{double(popup)};
    rust::workflowRun(
        rust::WorkflowMethod::NoticeDesktopContact, args,
        [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::NoticeDesktopFoldNickname:
                nickname = nickname.trimmed().toCaseFolded();
                break;
            case rust::WorkflowOp::NoticeDesktopNicknameEmpty:
                out[0] = nickname.isEmpty();
                break;
            case rust::WorkflowOp::NoticeDesktopContactHeadings:
                allowed = {"contacts", "contact", "联系人", "聯絡人", "features", "feature", "功能"};
                headings = allowed;
                headings << "chat history" << "聊天记录" << "聊天記錄" << "group chats" << "群聊" << "more"
                         << "更多" << "internet search results" << "网络搜索结果" << "網路搜尋結果"
                         << "official accounts" << "公众号" << "公眾號";
                break;
            case rust::WorkflowOp::NoticeDesktopContactSection:
                sections.append({int(a[0]), int(a[1])});
                break;
            case rust::WorkflowOp::NoticeDesktopLineCount:
                out[0] = double(lines.size());
                break;
            case rust::WorkflowOp::NoticeDesktopAllowedHeading:
                out[0] = allowed.contains(lines.at(int(a[0])).text.trimmed().toCaseFolded());
                break;
            case rust::WorkflowOp::NoticeDesktopLineBottom: {
                const auto &r = lines.at(int(a[0])).box;
                out[0] = r.y() + r.height();
                break;
            }
            case rust::WorkflowOp::NoticeDesktopAnyHeading:
                out[0] = headings.contains(lines.at(int(a[0])).text.trimmed().toCaseFolded());
                break;
            case rust::WorkflowOp::NoticeDesktopLineY:
                out[0] = lines.at(int(a[0])).box.y();
                break;
            case rust::WorkflowOp::NoticeDesktopSectionCount:
                out[0] = double(sections.size());
                break;
            case rust::WorkflowOp::NoticeDesktopSectionAt: {
                auto [start, end] = sections.at(int(a[0]));
                out[0] = start;
                out[1] = end;
                break;
            }
            case rust::WorkflowOp::NoticeDesktopLineCenter: {
                const auto &r = lines.at(int(a[0])).box;
                point = {r.x() + r.width() / 2, r.y() + r.height() / 2};
                break;
            }
            case rust::WorkflowOp::NoticeDesktopNicknameMatches:
                out[0] = lines.at(int(a[0])).text.trimmed().toCaseFolded() == nickname;
                break;
            case rust::WorkflowOp::NoticeDesktopCenterY:
                out[0] = point.y();
                break;
            case rust::WorkflowOp::NoticeDesktopContactFound:
                out[0] = found.has_value();
                break;
            case rust::WorkflowOp::NoticeDesktopSameContactPoint:
                out[0] = *found == point;
                break;
            case rust::WorkflowOp::NoticeDesktopClearContact:
                found.reset();
                break;
            case rust::WorkflowOp::NoticeDesktopUseContactPoint:
                found = point;
                break;
            default:
                fail("Rust 桌面联系人宿主无效");
            }
        });
    return found;
}
namespace {
QString windowText(HWND h) {
    wchar_t value[1024]{};
    GetWindowTextW(h, value, 1024);
    return QString::fromWCharArray(value);
}
QString windowClass(HWND h) {
    wchar_t value[256]{};
    GetClassNameW(h, value, 256);
    return QString::fromWCharArray(value);
}
DWORD processId(HWND h) {
    DWORD id = 0;
    GetWindowThreadProcessId(h, &id);
    return id;
}
qint64 area(HWND h) {
    RECT r{};
    GetWindowRect(h, &r);
    return qint64(std::max<LONG>(0, r.right - r.left)) * std::max<LONG>(0, r.bottom - r.top);
}
QVector<HWND> windows() {
    QVector<HWND> out;
    EnumWindows(
        [](HWND h, LPARAM ptr) -> BOOL {
            reinterpret_cast<QVector<HWND> *>(ptr)->append(h);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&out));
    return out;
}

[[noreturn]] void noticeWindowFailure(int code) {
    static const QStringList errors{"未找到已登录且可见的桌面聊天窗口",
                                    "桌面聊天窗口已最小化，无法可靠读取",
                                    "桌面聊天窗口尺寸无效",
                                    "桌面聊天窗口没有响应",
                                    "无法读取桌面聊天画面",
                                    "无法创建桌面通知截图",
                                    "桌面聊天画面读取失败"};
    fail(errors.at(code));
}
struct NoticeWindowSdk {
    HWND root = nullptr, candidate = nullptr, best = nullptr, render = nullptr, child = nullptr;
    QVector<HWND> all, result;
    HANDLE process = nullptr;
    std::array<wchar_t, 32768> path{};
    DWORD count = 32768;
    QString name, title;
    qint64 score = -1, candidateScore = 0;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::NoticeWindowEnumerate:
            all = windows();
            break;
        case rust::WorkflowOp::NoticeWindowCount:
            out[0] = double(all.size());
            break;
        case rust::WorkflowOp::NoticeWindowCandidate:
            candidate = all.at(qsizetype(a[0]));
            break;
        case rust::WorkflowOp::NoticeWindowVisible:
            out[0] = IsWindowVisible(candidate);
            break;
        case rust::WorkflowOp::NoticeWindowHasParent:
            out[0] = GetParent(candidate) != nullptr;
            break;
        case rust::WorkflowOp::NoticeWindowOpenProcess:
            process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId(candidate));
            out[0] = process != nullptr;
            break;
        case rust::WorkflowOp::NoticeWindowQueryPath:
            path.fill(0);
            count = 32768;
            out[0] = QueryFullProcessImageNameW(process, 0, path.data(), &count);
            break;
        case rust::WorkflowOp::NoticeWindowCloseProcess:
            CloseHandle(process);
            break;
        case rust::WorkflowOp::NoticeWindowProcessName:
            name = QFileInfo(QString::fromWCharArray(path.data(), int(count))).fileName().toLower();
            break;
        case rust::WorkflowOp::NoticeWindowWechatName:
            out[0] = QStringList{"wechat.exe", "weixin.exe"}.contains(name);
            break;
        case rust::WorkflowOp::NoticeWindowQqName:
            out[0] = name == "qq.exe";
            break;
        case rust::WorkflowOp::NoticeWindowTitle:
            title = windowText(candidate).trimmed().toCaseFolded();
            break;
        case rust::WorkflowOp::NoticeWindowWechatTitle:
            out[0] = QStringList{"wechat", "weixin", "微信"}.contains(title);
            break;
        case rust::WorkflowOp::NoticeWindowQqTitle:
            out[0] = title == "qq";
            break;
        case rust::WorkflowOp::NoticeWindowScore:
            candidateScore = area(candidate) + (bool(a[0]) ? (qint64(1) << 40) : 0);
            break;
        case rust::WorkflowOp::NoticeWindowBetterScore:
            out[0] = candidateScore > score;
            break;
        case rust::WorkflowOp::NoticeWindowChooseBest:
            score = candidateScore;
            best = candidate;
            break;
        case rust::WorkflowOp::NoticeWindowHasBest:
            out[0] = best != nullptr;
            break;
        case rust::WorkflowOp::NoticeWindowCandidateIsRoot:
            out[0] = candidate == root;
            break;
        case rust::WorkflowOp::NoticeWindowSameProcess:
            out[0] = processId(candidate) == processId(root);
            break;
        case rust::WorkflowOp::NoticeWindowContactClass:
            out[0] = windowClass(candidate).contains("QWindowToolSaveBits");
            break;
        case rust::WorkflowOp::NoticeWindowOwnerIsRoot:
            out[0] = GetWindow(candidate, GW_OWNER) == root;
            break;
        case rust::WorkflowOp::NoticeWindowAreaTooLarge: {
            const auto rootArea = area(root);
            out[0] = area(candidate) >= rootArea / 2;
            break;
        }
        case rust::WorkflowOp::NoticeWindowPositiveArea:
            out[0] = area(candidate) > 0;
            break;
        case rust::WorkflowOp::NoticeWindowAppendPopup:
            result << candidate;
            break;
        case rust::WorkflowOp::NoticeWindowSortPopups: {
            const std::array<double, 1> args{a[0]};
            std::sort(result.begin(), result.end(), [&](HWND x, HWND y) {
                return rust::workflowRun(rust::WorkflowMethod::NoticeWindowPopupCompare, args,
                                         [&](rust::WorkflowOp op, const double *, double *out) {
                                             switch (op) {
                                             case rust::WorkflowOp::NoticeWindowAreaGreater:
                                                 out[0] = area(x) > area(y);
                                                 break;
                                             case rust::WorkflowOp::NoticeWindowAreaLess:
                                                 out[0] = area(x) < area(y);
                                                 break;
                                             default:
                                                 fail("Rust 聊天窗口排序宿主无效");
                                             }
                                         })[0] != 0;
            });
            break;
        }
        case rust::WorkflowOp::NoticeWindowEnumerateChildren:
            EnumChildWindows(
                root,
                [](HWND h, LPARAM p) -> BOOL {
                    auto &sdk = *reinterpret_cast<NoticeWindowSdk *>(p);
                    sdk.child = h;
                    return BOOL(rust::workflowRun(rust::WorkflowMethod::NoticeWindowRenderChild, {}, sdk)[0]);
                },
                reinterpret_cast<LPARAM>(this));
            break;
        case rust::WorkflowOp::NoticeWindowHasRender:
            out[0] = render != nullptr;
            break;
        case rust::WorkflowOp::NoticeWindowUseRootRender:
            render = root;
            break;
        case rust::WorkflowOp::NoticeWindowChromeClass:
            out[0] = windowClass(child) == "Chrome_RenderWidgetHostHWND";
            break;
        case rust::WorkflowOp::NoticeWindowUseChildRender:
            render = child;
            break;
        case rust::WorkflowOp::NoticeWindowFailure:
            noticeWindowFailure(int(a[0]));
            break;
        default:
            fail("Rust 聊天窗口宿主无效");
        }
    }
};
HWND findMessenger(bool wechat) {
    NoticeWindowSdk sdk;
    const std::array<double, 1> args{double(wechat)};
    rust::workflowRun(rust::WorkflowMethod::NoticeWindowFindMessenger, args, sdk);
    return sdk.best;
}
QVector<HWND> popups(HWND root, bool contact) {
    NoticeWindowSdk sdk;
    sdk.root = root;
    const std::array<double, 1> args{double(contact)};
    rust::workflowRun(rust::WorkflowMethod::NoticeWindowPopups, args, sdk);
    return sdk.result;
}
HWND renderSurface(HWND h) {
    NoticeWindowSdk sdk;
    sdk.root = h;
    rust::workflowRun(rust::WorkflowMethod::NoticeWindowRender, {}, sdk);
    return sdk.render;
}
struct NoticeWindowCaptureSdk {
    HWND h;
    RECT r{};
    HDC source = nullptr, dc = nullptr;
    BITMAPINFO info{};
    void *bits = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    cv::Mat frame;
    void operator()(rust::WorkflowOp op, const double *a, double *out) {
        switch (op) {
        case rust::WorkflowOp::NoticeWindowIconic:
            out[0] = IsIconic(h);
            break;
        case rust::WorkflowOp::NoticeWindowClientRect:
            out[0] = GetClientRect(h, &r);
            break;
        case rust::WorkflowOp::NoticeWindowClientWidth:
            out[0] = r.right;
            break;
        case rust::WorkflowOp::NoticeWindowClientHeight:
            out[0] = r.bottom;
            break;
        case rust::WorkflowOp::NoticeWindowResponsive: {
            DWORD_PTR response = 0;
            out[0] = SendMessageTimeoutW(h, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, UINT(a[0]),
                                         &response) != 0;
            break;
        }
        case rust::WorkflowOp::NoticeWindowGetDc:
            source = GetDC(h);
            out[0] = source != nullptr;
            break;
        case rust::WorkflowOp::NoticeWindowCompatibleDc:
            dc = CreateCompatibleDC(source);
            break;
        case rust::WorkflowOp::NoticeWindowBitmapInfo:
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = r.right;
            info.bmiHeader.biHeight = -r.bottom;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            break;
        case rust::WorkflowOp::NoticeWindowCreateBitmap:
            bitmap = CreateDIBSection(source, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            break;
        case rust::WorkflowOp::NoticeWindowHasDc:
            out[0] = dc != nullptr;
            break;
        case rust::WorkflowOp::NoticeWindowHasBitmap:
            out[0] = bitmap != nullptr;
            break;
        case rust::WorkflowOp::NoticeWindowDeleteBitmap:
            DeleteObject(bitmap);
            break;
        case rust::WorkflowOp::NoticeWindowDeleteDc:
            DeleteDC(dc);
            break;
        case rust::WorkflowOp::NoticeWindowReleaseDc:
            ReleaseDC(h, source);
            break;
        case rust::WorkflowOp::NoticeWindowSelectBitmap:
            previous = SelectObject(dc, bitmap);
            break;
        case rust::WorkflowOp::NoticeWindowPrint:
            out[0] = PrintWindow(h, dc, UINT(a[0]));
            break;
        case rust::WorkflowOp::NoticeWindowConvertImage:
            cv::cvtColor(cv::Mat(r.bottom, r.right, CV_8UC4, bits), frame, cv::COLOR_BGRA2BGR);
            break;
        case rust::WorkflowOp::NoticeWindowRestoreBitmap:
            SelectObject(dc, previous);
            break;
        case rust::WorkflowOp::NoticeWindowFrameEmpty:
            out[0] = frame.empty();
            break;
        case rust::WorkflowOp::NoticeWindowFailure:
            noticeWindowFailure(int(a[0]));
            break;
        default:
            fail("Rust 聊天窗口截图宿主无效");
        }
    }
};
cv::Mat capture(HWND h) {
    NoticeWindowCaptureSdk sdk{h};
    rust::workflowRun(rust::WorkflowMethod::NoticeWindowCapture, {}, sdk);
    return sdk.frame;
}
class Messenger {
    bool wechat_;
    HWND root_;
    QString nickname_;
    NoticeCancelled cancel_;
    Ocr ocr_;
    QRect search_, contacts_, header_, send_;
    double dpi_ = 1;

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
        QElapsedTimer timer;
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
                out[0] = IsWindow(root_);
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
                std::this_thread::sleep_for(std::chrono::milliseconds(int(a[0])));
                break;
            case rust::WorkflowOp::NoticeMessengerForegroundRoot:
                out[0] = GetForegroundWindow() == root_;
                break;
            case rust::WorkflowOp::NoticeMessengerForegroundProcess:
                out[0] = processId(GetForegroundWindow()) == processId(root_);
                break;
            case rust::WorkflowOp::NoticeMessengerWait:
                wait(a[0]);
                break;
            case rust::WorkflowOp::NoticeMessengerBackground:
                background();
                break;
            case rust::WorkflowOp::NoticeMessengerSendMessage: {
                DWORD_PTR result = 0;
                out[0] = SendMessageTimeoutW(c.h, c.msg, c.w, c.l, SMTO_ABORTIFHUNG | SMTO_BLOCK, UINT(a[0]),
                                             &result) != 0;
                break;
            }
            case rust::WorkflowOp::NoticeMessengerKeyScanFlags:
                out[0] = 1 | (MapVirtualKeyW(c.vk, MAPVK_VK_TO_VSC) << 16);
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
                MapWindowPoints(c.h, c.target, &p, 1);
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
                c.frame = boundedDesktopCapture([h = c.h] { return capture(h); }, cancel_);
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

    class ClipboardImage {
        HWND owner_ = nullptr;
        QByteArray savedText_;
        DWORD sequence_ = 0;
        void workflow(rust::WorkflowMethod method, const QByteArray *png = nullptr) {
            cv::Mat image;
            int stride = 0;
            size_t bytes = 0;
            HGLOBAL memory = nullptr, old = nullptr;
            unsigned char *buffer = nullptr;
            const char *oldText = nullptr;
            void *saved = nullptr;
            rust::workflowRun(method, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
                switch (op) {
                case rust::WorkflowOp::NoticeClipboardCreateOwner:
                    owner_ = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                             GetModuleHandleW(nullptr), nullptr);
                    out[0] = owner_ != nullptr;
                    break;
                case rust::WorkflowOp::NoticeClipboardDecode:
                    image = cv::imdecode(std::vector<uchar>(png->begin(), png->end()), cv::IMREAD_COLOR);
                    break;
                case rust::WorkflowOp::NoticeClipboardImageEmpty:
                    out[0] = image.empty();
                    break;
                case rust::WorkflowOp::NoticeClipboardDestroyOwner:
                    DestroyWindow(owner_);
                    break;
                case rust::WorkflowOp::NoticeClipboardImageCols:
                    out[0] = image.cols;
                    break;
                case rust::WorkflowOp::NoticeClipboardImageRows:
                    out[0] = image.rows;
                    break;
                case rust::WorkflowOp::NoticeClipboardStride:
                    stride = int(a[0]);
                    bytes = sizeof(BITMAPINFOHEADER) + size_t(stride) * image.rows;
                    break;
                case rust::WorkflowOp::NoticeClipboardAllocateImage:
                    memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
                    out[0] = memory != nullptr;
                    break;
                case rust::WorkflowOp::NoticeClipboardLockImage:
                    buffer = static_cast<unsigned char *>(GlobalLock(memory));
                    out[0] = buffer != nullptr;
                    break;
                case rust::WorkflowOp::NoticeClipboardFreeMemory:
                    GlobalFree(memory);
                    break;
                case rust::WorkflowOp::NoticeClipboardImageHeader: {
                    memset(buffer, 0, bytes);
                    auto *header = reinterpret_cast<BITMAPINFOHEADER *>(buffer);
                    header->biSize = sizeof(*header);
                    header->biWidth = image.cols;
                    header->biHeight = image.rows;
                    header->biPlanes = 1;
                    header->biBitCount = 24;
                    header->biCompression = BI_RGB;
                    break;
                }
                case rust::WorkflowOp::NoticeClipboardCopyRow: {
                    const int y = int(a[0]);
                    memcpy(buffer + sizeof(BITMAPINFOHEADER) + size_t(stride) * y,
                           image.ptr(image.rows - 1 - y), size_t(image.cols) * 3);
                    break;
                }
                case rust::WorkflowOp::NoticeClipboardUnlockMemory:
                    GlobalUnlock(memory);
                    break;
                case rust::WorkflowOp::NoticeClipboardOpen:
                    out[0] = OpenClipboard(owner_);
                    break;
                case rust::WorkflowOp::NoticeClipboardOldText:
                    old = GetClipboardData(CF_UNICODETEXT);
                    out[0] = old != nullptr;
                    break;
                case rust::WorkflowOp::NoticeClipboardLockOld:
                    oldText = static_cast<const char *>(GlobalLock(old));
                    out[0] = oldText != nullptr;
                    break;
                case rust::WorkflowOp::NoticeClipboardSaveOld:
                    savedText_ = QByteArray(oldText, int(GlobalSize(old)));
                    break;
                case rust::WorkflowOp::NoticeClipboardUnlockOld:
                    GlobalUnlock(old);
                    break;
                case rust::WorkflowOp::NoticeClipboardEmpty:
                    EmptyClipboard();
                    break;
                case rust::WorkflowOp::NoticeClipboardSetImage:
                    out[0] = SetClipboardData(CF_DIB, memory) != nullptr;
                    break;
                case rust::WorkflowOp::NoticeClipboardSaveSequence:
                    sequence_ = GetClipboardSequenceNumber();
                    break;
                case rust::WorkflowOp::NoticeClipboardClose:
                    CloseClipboard();
                    break;
                case rust::WorkflowOp::NoticeClipboardSequenceMatches:
                    out[0] = GetClipboardSequenceNumber() == sequence_;
                    break;
                case rust::WorkflowOp::NoticeClipboardSavedEmpty:
                    out[0] = savedText_.isEmpty();
                    break;
                case rust::WorkflowOp::NoticeClipboardAllocateSaved:
                    memory = GlobalAlloc(GMEM_MOVEABLE, size_t(savedText_.size()));
                    out[0] = memory != nullptr;
                    break;
                case rust::WorkflowOp::NoticeClipboardLockSaved:
                    saved = GlobalLock(memory);
                    out[0] = saved != nullptr;
                    break;
                case rust::WorkflowOp::NoticeClipboardCopySaved:
                    memcpy(saved, savedText_.data(), size_t(savedText_.size()));
                    break;
                case rust::WorkflowOp::NoticeClipboardSetText:
                    out[0] = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
                    break;
                case rust::WorkflowOp::NoticeClipboardFailure: {
                    static const QStringList errors{"无法创建通知剪贴板窗口",
                                                    "通知截图无法解码",
                                                    "无法分配通知截图",
                                                    "无法锁定通知截图内存",
                                                    "剪贴板正在使用，通知截图未发送",
                                                    "无法写入通知截图"};
                    fail(errors.at(int(a[0])));
                }
                default:
                    fail("Rust 通知剪贴板宿主无效");
                }
            });
        }

      public:
        explicit ClipboardImage(const QByteArray &png) {
            workflow(rust::WorkflowMethod::NoticeClipboardCreate, &png);
        }
        ~ClipboardImage() { workflow(rust::WorkflowMethod::NoticeClipboardRestore); }
    };
    struct DeliveryCall {
        std::optional<ClipboardImage> clipboard;
        const Notice *notice = nullptr;
        const QByteArray *png = nullptr;
        QPoint composer, send;
        std::optional<QPoint> contact, search, paste;
        HWND menu = nullptr, selected = nullptr, candidate = nullptr, render = nullptr;
        QVector<HWND> candidates;
        QElapsedTimer timer;
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
                GetClientRect(root_, &r);
                out[0] = r.right;
                out[1] = r.bottom;
                break;
            }
            case rust::WorkflowOp::NoticeDeliveryQueryDpi:
                dpi_ = GetDpiForWindow(root_) / 96.;
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
                c.contact = messengerContact(read(root_, contacts_), nickname_, false);
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
                c.contact = messengerContact(read(c.candidate, bool(a[0]) ? contacts_ : QRect{}), nickname_,
                                             !bool(a[0]));
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
                out[0] = IsWindowVisible(c.selected);
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

  public:
    Messenger(bool wechat, QString nickname, QString models, NoticeCancelled cancel)
        : wechat_(wechat), root_(findMessenger(wechat)), nickname_(std::move(nickname)),
          cancel_(std::move(cancel)), ocr_(models) {
        DeliveryCall c;
        const std::array<double, 1> a{double(wechat)};
        delivery(rust::WorkflowMethod::NoticeDeliveryInitialize, c, a);
    }
    void send(const Notice &notice) {
        DeliveryCall c;
        c.notice = &notice;
        delivery(rust::WorkflowMethod::NoticeDeliverySend, c);
    }
};
} // namespace

void sendDesktopNotice(bool wechat, const QJsonObject &config, const Notice &notice, const QString &models,
                       const NoticeCancelled &cancelled) {
    QString nickname;
    const std::array<double, 1> args{double(wechat)};
    rust::workflowRun(rust::WorkflowMethod::NoticeDeliveryEntry, args,
                      [&](rust::WorkflowOp op, const double *, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::NoticeDeliveryReadWechatNickname:
                              nickname = config["WeChat Desktop Nickname"].toString().trimmed();
                              break;
                          case rust::WorkflowOp::NoticeDeliveryReadQqNickname:
                              nickname = config["QQ Desktop Nickname"].toString().trimmed();
                              break;
                          case rust::WorkflowOp::NoticeDeliveryNicknameEmpty:
                              out[0] = nickname.isEmpty();
                              break;
                          case rust::WorkflowOp::NoticeDeliveryConstructAndSend:
                              Messenger(wechat, nickname, models, cancelled).send(notice);
                              break;
                          case rust::WorkflowOp::NoticeDeliveryFailure:
                              fail("请配置桌面通知接收人的完整昵称");
                              break;
                          default:
                              fail("Rust 桌面通知入口宿主无效");
                          }
                      });
}
} // namespace okww::runtime
