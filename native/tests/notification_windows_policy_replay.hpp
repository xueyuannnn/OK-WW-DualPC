#pragma once
#include "runtime/notification.hpp"
#include "runtime/workflow_abi.hpp"
#include <opencv2/imgproc.hpp>
namespace okww::runtime::notice_windows_test {
inline HWND handle(int id) { return reinterpret_cast<HWND>(uintptr_t(id)); }
inline int id(HWND h) { return int(uintptr_t(h)); }
struct State {
    int scene;
    QJsonArray trace;
    QByteArray pixels;
    explicit State(int s) : scene(s) {}
};
inline thread_local State *active = nullptr;
inline QVector<HWND> windows() {
    active->trace.append("windows");
    return {handle(1), handle(2), handle(3), handle(4), handle(5), handle(6), handle(7), handle(8)};
}
inline QString windowText(HWND h) {
    active->trace.append(QJsonObject{{"title", id(h)}});
    const QStringList titles{"QQ", "WeChat", " 微信 ", "Weixin", "QQ secondary", "other", "wechat", ""};
    return titles[(active->scene + id(h)) % 8];
}
inline QString windowClass(HWND h) {
    active->trace.append(QJsonObject{{"class", id(h)}});
    return (active->scene + id(h)) % 3 == 0   ? "Chrome_RenderWidgetHostHWND"
           : (active->scene + id(h)) % 3 == 1 ? "QWindowToolSaveBits fixture"
                                              : "other";
}
inline DWORD processId(HWND h) {
    active->trace.append(QJsonObject{{"pid", id(h)}});
    return DWORD((id(h) + active->scene) % 4 + 1);
}
inline qint64 area(HWND h) {
    active->trace.append(QJsonObject{{"area", id(h)}});
    const auto value = (active->scene + id(h)) % 7;
    return value == 0 ? 0 : value == 1 ? -1 : value == 2 ? (qint64(1) << 54) + id(h) : qint64(value) * 1000;
}
inline BOOL IsWindowVisible(HWND h) {
    active->trace.append(QJsonObject{{"visible", id(h)}});
    return (active->scene + id(h)) % 5 != 0;
}
inline HWND GetParent(HWND h) {
    active->trace.append(QJsonObject{{"parent", id(h)}});
    return (active->scene + id(h)) % 7 == 0 ? handle(9) : nullptr;
}
inline HANDLE OpenProcess(DWORD access, BOOL inherit, DWORD pid) {
    active->trace.append(
        QJsonObject{{"open-process", int(pid)}, {"access", int(access)}, {"inherit", bool(inherit)}});
    return (active->scene + pid) % 7 ? reinterpret_cast<HANDLE>(uintptr_t(pid)) : nullptr;
}
inline BOOL QueryFullProcessImageNameW(HANDLE process, DWORD flags, LPWSTR path, PDWORD count) {
    const int pid = int(uintptr_t(process));
    active->trace.append(QJsonObject{{"path", pid}, {"flags", int(flags)}, {"capacity", int(*count)}});
    if ((active->scene + pid) % 11 == 0)
        return false;
    const QStringList names{"C:/Apps/WeChat.EXE", "C:/Apps/Weixin.exe", "C:/Apps/QQ.exe", "C:/other.exe"};
    const auto name = names[(active->scene + pid) % 4].toStdWString();
    std::copy(name.begin(), name.end(), path);
    *count = DWORD(name.size());
    return true;
}
inline BOOL CloseHandle(HANDLE process) {
    active->trace.append(QJsonObject{{"close-process", double(uintptr_t(process))}});
    return true;
}
inline HWND GetWindow(HWND h, UINT kind) {
    active->trace.append(QJsonObject{{"owner", id(h)}, {"kind", int(kind)}});
    return (active->scene + id(h)) % 2 ? handle(1) : nullptr;
}
template <class Callback> BOOL EnumChildWindows(HWND h, Callback callback, LPARAM p) {
    active->trace.append(QJsonObject{{"children", id(h)}});
    for (int i = 2; i < 6; ++i)
        if (!callback(handle(i), p))
            break;
    return true;
}
inline BOOL IsIconic(HWND h) {
    active->trace.append(QJsonObject{{"iconic", id(h)}});
    return active->scene % 7 == 0;
}
inline BOOL GetClientRect(HWND h, LPRECT rect) {
    active->trace.append(QJsonObject{{"client", id(h)}});
    *rect = {0, 0, 3 + active->scene % 5, 2 + active->scene % 7};
    if (active->scene % 5 == 0)
        rect->right = 0;
    return active->scene % 11 != 0;
}
inline LRESULT SendMessageTimeoutW(HWND h, UINT message, WPARAM w, LPARAM l, UINT flags, UINT timeout,
                                   PDWORD_PTR result) {
    active->trace.append(QJsonObject{{"message", int(message)},
                                     {"h", id(h)},
                                     {"w", double(w)},
                                     {"l", double(l)},
                                     {"flags", int(flags)},
                                     {"timeout", int(timeout)}});
    *result = 0;
    return active->scene % 13 != 0;
}
inline HDC GetDC(HWND h) {
    active->trace.append(QJsonObject{{"get-dc", id(h)}});
    return active->scene % 17 ? reinterpret_cast<HDC>(uintptr_t(1)) : nullptr;
}
inline HDC CreateCompatibleDC(HDC source) {
    active->trace.append(QJsonObject{{"create-dc", double(uintptr_t(source))}});
    return active->scene % 19 ? reinterpret_cast<HDC>(uintptr_t(2)) : nullptr;
}
inline HBITMAP CreateDIBSection(HDC dc, const BITMAPINFO *info, UINT usage, VOID **bits, HANDLE section,
                                DWORD offset) {
    const auto &h = info->bmiHeader;
    active->trace.append(QJsonObject{{"dib", double(uintptr_t(dc))},
                                     {"width", int(h.biWidth)},
                                     {"height", int(h.biHeight)},
                                     {"size", int(h.biSize)},
                                     {"planes", int(h.biPlanes)},
                                     {"bpp", int(h.biBitCount)},
                                     {"compression", int(h.biCompression)},
                                     {"usage", int(usage)},
                                     {"section", double(uintptr_t(section))},
                                     {"offset", int(offset)}});
    if (active->scene % 23 == 0)
        return nullptr;
    active->pixels = QByteArray(h.biWidth * (-h.biHeight) * 4, char(active->scene % 256));
    *bits = active->pixels.data();
    return reinterpret_cast<HBITMAP>(uintptr_t(3));
}
template <class Object> BOOL DeleteObject(Object object) {
    active->trace.append(QJsonObject{{"delete-bitmap", double(uintptr_t(object))}});
    return true;
}
inline BOOL DeleteDC(HDC dc) {
    active->trace.append(QJsonObject{{"delete-dc", double(uintptr_t(dc))}});
    return true;
}
inline int ReleaseDC(HWND h, HDC source) {
    active->trace.append(QJsonObject{{"release-dc", double(uintptr_t(source))}, {"h", id(h)}});
    return 1;
}
template <class Object> HGDIOBJ SelectObject(HDC dc, Object object) {
    active->trace.append(QJsonObject{{"select", double(uintptr_t(object))}, {"dc", double(uintptr_t(dc))}});
    return reinterpret_cast<HGDIOBJ>(uintptr_t(4));
}
inline BOOL PrintWindow(HWND h, HDC dc, UINT flags) {
    active->trace.append(QJsonObject{{"print", id(h)}, {"dc", double(uintptr_t(dc))}, {"flags", int(flags)}});
    return active->scene % 29 != 0;
}
namespace original {
HWND findMessenger(bool wechat) {
    HWND best = nullptr;
    qint64 score = -1;
    for (auto h : windows()) {
        if (!notice_windows_test::IsWindowVisible(h) || notice_windows_test::GetParent(h))
            continue;
        HANDLE process =
            notice_windows_test::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId(h));
        if (!process)
            continue;
        wchar_t path[32768]{};
        DWORD count = 32768;
        bool ok = notice_windows_test::QueryFullProcessImageNameW(process, 0, path, &count);
        notice_windows_test::CloseHandle(process);
        if (!ok)
            continue;
        auto name = QFileInfo(QString::fromWCharArray(path, int(count))).fileName().toLower();
        if (wechat ? !QStringList{"wechat.exe", "weixin.exe"}.contains(name) : name != "qq.exe")
            continue;
        const auto title = windowText(h).trimmed().toCaseFolded();
        const auto exact = wechat ? QStringList{"wechat", "weixin", "微信"}.contains(title) : title == "qq";
        const auto candidate = area(h) + (exact ? (qint64(1) << 40) : 0);
        if (candidate > score) {
            score = candidate;
            best = h;
        }
    }
    if (!best)
        fail("未找到已登录且可见的桌面聊天窗口");
    return best;
}
QVector<HWND> popups(HWND root, bool contact) {
    QVector<HWND> result;
    for (auto h : windows()) {
        if (h == root || !notice_windows_test::IsWindowVisible(h))
            continue;
        if (contact) {
            if (processId(h) != processId(root) || !windowClass(h).contains("QWindowToolSaveBits"))
                continue;
        } else if ((processId(h) != processId(root) && notice_windows_test::GetWindow(h, GW_OWNER) != root) ||
                   area(h) >= area(root) / 2)
            continue;
        if (area(h) > 0)
            result << h;
    }
    std::sort(result.begin(), result.end(),
              [&](HWND a, HWND b) { return contact ? area(a) > area(b) : area(a) < area(b); });
    return result;
}
HWND renderSurface(HWND h) {
    HWND result = nullptr;
    notice_windows_test::EnumChildWindows(
        h,
        [](HWND child, LPARAM out) -> BOOL {
            if (windowClass(child) == "Chrome_RenderWidgetHostHWND") {
                *reinterpret_cast<HWND *>(out) = child;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&result));
    return result ? result : h;
}
cv::Mat capture(HWND h) {
    if (notice_windows_test::IsIconic(h))
        fail("桌面聊天窗口已最小化，无法可靠读取");
    RECT r{};
    if (!notice_windows_test::GetClientRect(h, &r) || r.right <= 0 || r.bottom <= 0)
        fail("桌面聊天窗口尺寸无效");
    DWORD_PTR response = 0;
    if (!notice_windows_test::SendMessageTimeoutW(h, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000,
                                                  &response))
        fail("桌面聊天窗口没有响应");
    HDC source = notice_windows_test::GetDC(h);
    if (!source)
        fail("无法读取桌面聊天画面");
    HDC dc = notice_windows_test::CreateCompatibleDC(source);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = r.right;
    info.bmiHeader.biHeight = -r.bottom;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP bitmap = notice_windows_test::CreateDIBSection(source, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !bitmap) {
        if (bitmap)
            notice_windows_test::DeleteObject(bitmap);
        if (dc)
            notice_windows_test::DeleteDC(dc);
        notice_windows_test::ReleaseDC(h, source);
        fail("无法创建桌面通知截图");
    }
    auto previous = notice_windows_test::SelectObject(dc, bitmap);
    cv::Mat frame;
    const bool ok = notice_windows_test::PrintWindow(h, dc, 3);
    if (ok)
        cv::cvtColor(cv::Mat(r.bottom, r.right, CV_8UC4, bits), frame, cv::COLOR_BGRA2BGR);
    notice_windows_test::SelectObject(dc, previous);
    notice_windows_test::DeleteObject(bitmap);
    notice_windows_test::DeleteDC(dc);
    notice_windows_test::ReleaseDC(h, source);
    if (!ok || frame.empty())
        fail("桌面聊天画面读取失败");
    return frame;
}

} // namespace original
namespace current {
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
            out[0] = notice_windows_test::IsWindowVisible(candidate);
            break;
        case rust::WorkflowOp::NoticeWindowHasParent:
            out[0] = notice_windows_test::GetParent(candidate) != nullptr;
            break;
        case rust::WorkflowOp::NoticeWindowOpenProcess:
            process = notice_windows_test::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                                       processId(candidate));
            out[0] = process != nullptr;
            break;
        case rust::WorkflowOp::NoticeWindowQueryPath:
            path.fill(0);
            count = 32768;
            out[0] = notice_windows_test::QueryFullProcessImageNameW(process, 0, path.data(), &count);
            break;
        case rust::WorkflowOp::NoticeWindowCloseProcess:
            notice_windows_test::CloseHandle(process);
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
            out[0] = notice_windows_test::GetWindow(candidate, GW_OWNER) == root;
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
            notice_windows_test::EnumChildWindows(
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
            out[0] = notice_windows_test::IsIconic(h);
            break;
        case rust::WorkflowOp::NoticeWindowClientRect:
            out[0] = notice_windows_test::GetClientRect(h, &r);
            break;
        case rust::WorkflowOp::NoticeWindowClientWidth:
            out[0] = r.right;
            break;
        case rust::WorkflowOp::NoticeWindowClientHeight:
            out[0] = r.bottom;
            break;
        case rust::WorkflowOp::NoticeWindowResponsive: {
            DWORD_PTR response = 0;
            out[0] = notice_windows_test::SendMessageTimeoutW(h, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK,
                                                              UINT(a[0]), &response) != 0;
            break;
        }
        case rust::WorkflowOp::NoticeWindowGetDc:
            source = notice_windows_test::GetDC(h);
            out[0] = source != nullptr;
            break;
        case rust::WorkflowOp::NoticeWindowCompatibleDc:
            dc = notice_windows_test::CreateCompatibleDC(source);
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
            bitmap = notice_windows_test::CreateDIBSection(source, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            break;
        case rust::WorkflowOp::NoticeWindowHasDc:
            out[0] = dc != nullptr;
            break;
        case rust::WorkflowOp::NoticeWindowHasBitmap:
            out[0] = bitmap != nullptr;
            break;
        case rust::WorkflowOp::NoticeWindowDeleteBitmap:
            notice_windows_test::DeleteObject(bitmap);
            break;
        case rust::WorkflowOp::NoticeWindowDeleteDc:
            notice_windows_test::DeleteDC(dc);
            break;
        case rust::WorkflowOp::NoticeWindowReleaseDc:
            notice_windows_test::ReleaseDC(h, source);
            break;
        case rust::WorkflowOp::NoticeWindowSelectBitmap:
            previous = notice_windows_test::SelectObject(dc, bitmap);
            break;
        case rust::WorkflowOp::NoticeWindowPrint:
            out[0] = notice_windows_test::PrintWindow(h, dc, UINT(a[0]));
            break;
        case rust::WorkflowOp::NoticeWindowConvertImage:
            cv::cvtColor(cv::Mat(r.bottom, r.right, CV_8UC4, bits), frame, cv::COLOR_BGRA2BGR);
            break;
        case rust::WorkflowOp::NoticeWindowRestoreBitmap:
            notice_windows_test::SelectObject(dc, previous);
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

} // namespace current
inline QJsonObject outcome(int scene, int method, bool rust) {
    State state(scene);
    active = &state;
    QJsonObject result;
    try {
        if (method < 2) {
            auto h = rust ? current::findMessenger(method == 1) : original::findMessenger(method == 1);
            result["window"] = id(h);
        } else if (method < 4) {
            auto list =
                rust ? current::popups(handle(1), method == 3) : original::popups(handle(1), method == 3);
            QJsonArray ids;
            for (auto h : list)
                ids.append(id(h));
            result["windows"] = ids;
        } else if (method == 4) {
            auto h = rust ? current::renderSurface(handle(1)) : original::renderSurface(handle(1));
            result["window"] = id(h);
        } else {
            auto frame = rust ? current::capture(handle(1)) : original::capture(handle(1));
            result["size"] = QJsonArray{frame.cols, frame.rows};
            if (!frame.empty())
                result["pixels"] = QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(frame.data),
                                                                  qsizetype(frame.total() * frame.elemSize()))
                                                           .toHex());
        }
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    } catch (...) {
        result["error"] = "unknown";
    }
    result["trace"] = state.trace;
    return result;
}
} // namespace okww::runtime::notice_windows_test
