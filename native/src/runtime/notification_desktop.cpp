#include "notification.hpp"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <future>

namespace okww::runtime {
cv::Mat boundedDesktopCapture(std::function<cv::Mat()> capture, const NoticeCancelled &cancelled,
                              int timeoutMs) {
    if (!capture || timeoutMs < 1 || timeoutMs > 30000)
        fail("桌面截图等待参数无效");
    if (cancelled && cancelled())
        fail("桌面通知已取消");
    static const auto occupied = std::make_shared<std::atomic<bool>>(false);
    if (occupied->exchange(true))
        fail("上次桌面截图仍未返回，暂停后续桌面通知");
    auto completion = std::make_shared<std::promise<cv::Mat>>();
    auto result = completion->get_future();
    try {
        std::thread([capture = std::move(capture), completion, busy = occupied] {
            cv::Mat image;
            std::exception_ptr error;
            try {
                image = capture();
            } catch (...) {
                error = std::current_exception();
            }
            busy->store(false);
            if (error)
                completion->set_exception(error);
            else
                completion->set_value(std::move(image));
        }).detach();
    } catch (...) {
        occupied->store(false);
        throw;
    }
    QElapsedTimer elapsed;
    elapsed.start();
    for (;;) {
        if (cancelled && cancelled())
            fail("桌面通知已取消");
        if (result.wait_for(std::chrono::milliseconds(20)) == std::future_status::ready)
            return result.get();
        if (elapsed.elapsed() >= timeoutMs)
            fail("桌面截图超时，通知没有发送");
    }
}
std::optional<QPoint> messengerContact(const QVector<TextLine> &lines, QString nickname, bool popup) {
    nickname = nickname.trimmed().toCaseFolded();
    if (nickname.isEmpty())
        return {};
    const QStringList allowed{"contacts", "contact", "联系人", "聯絡人", "features", "feature", "功能"};
    auto headings = allowed;
    headings << "chat history" << "聊天记录" << "聊天記錄" << "group chats" << "群聊" << "more" << "更多"
             << "internet search results" << "网络搜索结果" << "網路搜尋結果" << "official accounts"
             << "公众号" << "公眾號";
    QVector<QPair<int, int>> sections;
    if (!popup)
        sections.append({0, INT_MAX});
    else
        for (const auto &line : lines)
            if (allowed.contains(line.text.trimmed().toCaseFolded())) {
                const int start = line.box.y() + line.box.height();
                int end = INT_MAX;
                for (const auto &other : lines)
                    if (headings.contains(other.text.trimmed().toCaseFolded()) && other.box.y() >= start)
                        end = std::min(end, other.box.y());
                sections.append({start, end});
            }
    std::optional<QPoint> found;
    for (auto [start, end] : sections)
        for (const auto &line : lines) {
            const QPoint point(line.box.x() + line.box.width() / 2, line.box.y() + line.box.height() / 2);
            if (line.text.trimmed().toCaseFolded() == nickname && point.y() >= start && point.y() < end) {
                if (found && *found != point)
                    return {}; // Duplicate names need a unique contact name.
                found = point;
            }
        }
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
HWND findMessenger(bool wechat) {
    HWND best = nullptr;
    qint64 score = -1;
    for (auto h : windows()) {
        if (!IsWindowVisible(h) || GetParent(h))
            continue;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId(h));
        if (!process)
            continue;
        wchar_t path[32768]{};
        DWORD count = 32768;
        bool ok = QueryFullProcessImageNameW(process, 0, path, &count);
        CloseHandle(process);
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
        if (h == root || !IsWindowVisible(h))
            continue;
        if (contact) {
            if (processId(h) != processId(root) || !windowClass(h).contains("QWindowToolSaveBits"))
                continue;
        } else if ((processId(h) != processId(root) && GetWindow(h, GW_OWNER) != root) ||
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
    EnumChildWindows(
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
    if (IsIconic(h))
        fail("桌面聊天窗口已最小化，无法可靠读取");
    RECT r{};
    if (!GetClientRect(h, &r) || r.right <= 0 || r.bottom <= 0)
        fail("桌面聊天窗口尺寸无效");
    DWORD_PTR response = 0;
    if (!SendMessageTimeoutW(h, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &response))
        fail("桌面聊天窗口没有响应");
    HDC source = GetDC(h);
    if (!source)
        fail("无法读取桌面聊天画面");
    HDC dc = CreateCompatibleDC(source);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = r.right;
    info.bmiHeader.biHeight = -r.bottom;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(source, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !bitmap) {
        if (bitmap)
            DeleteObject(bitmap);
        if (dc)
            DeleteDC(dc);
        ReleaseDC(h, source);
        fail("无法创建桌面通知截图");
    }
    auto previous = SelectObject(dc, bitmap);
    cv::Mat frame;
    const bool ok = PrintWindow(h, dc, 3);
    if (ok)
        cv::cvtColor(cv::Mat(r.bottom, r.right, CV_8UC4, bits), frame, cv::COLOR_BGRA2BGR);
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(h, source);
    if (!ok || frame.empty())
        fail("桌面聊天画面读取失败");
    return frame;
}
class Messenger {
    bool wechat_;
    HWND root_;
    QString nickname_;
    NoticeCancelled cancel_;
    Ocr ocr_;
    QRect search_, contacts_, header_, send_;
    double dpi_ = 1;
    void check() {
        if (cancel_ && cancel_())
            fail("桌面通知已取消");
        if (!IsWindow(root_))
            fail("桌面聊天窗口已关闭");
    }
    void wait(double seconds) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < seconds * 1000) {
            check();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    void background() {
        QElapsedTimer timer;
        timer.start();
        while (GetForegroundWindow() == root_ || processId(GetForegroundWindow()) == processId(root_)) {
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
        if (!SendMessageTimeoutW(h, msg, w, l, SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &result))
            fail("桌面聊天控件没有响应");
    }
    void key(HWND h, int vk, bool down) {
        LPARAM flags = 1 | (MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16);
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
            MapWindowPoints(h, target, &p, 1);
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
        auto frame = boundedDesktopCapture([h] { return capture(h); }, cancel_);
        check();
        auto lines = ocr_.read(frame, region.isEmpty() ? std::optional<QRect>{} : region, .1);
        check();
        return lines;
    }
    std::optional<QPoint> find(HWND h, QStringList names, QRect region, double timeout, bool exact = true,
                               bool suffix = false) {
        for (auto &n : names)
            n = n.trimmed().toCaseFolded();
        QElapsedTimer timer;
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
    class ClipboardImage {
        HWND owner_ = nullptr;
        QByteArray savedText_;
        DWORD sequence_ = 0;

      public:
        explicit ClipboardImage(const QByteArray &png) {
            owner_ = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                     GetModuleHandleW(nullptr), nullptr);
            if (!owner_)
                fail("无法创建通知剪贴板窗口");
            auto image = cv::imdecode(std::vector<uchar>(png.begin(), png.end()), cv::IMREAD_COLOR);
            if (image.empty()) {
                DestroyWindow(owner_);
                fail("通知截图无法解码");
            }
            const int stride = (image.cols * 3 + 3) & ~3;
            const auto bytes = sizeof(BITMAPINFOHEADER) + size_t(stride) * image.rows;
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (!memory) {
                DestroyWindow(owner_);
                fail("无法分配通知截图");
            }
            auto *buffer = static_cast<unsigned char *>(GlobalLock(memory));
            if (!buffer) {
                GlobalFree(memory);
                DestroyWindow(owner_);
                fail("无法锁定通知截图内存");
            }
            memset(buffer, 0, bytes);
            auto *header = reinterpret_cast<BITMAPINFOHEADER *>(buffer);
            header->biSize = sizeof(*header);
            header->biWidth = image.cols;
            header->biHeight = image.rows;
            header->biPlanes = 1;
            header->biBitCount = 24;
            header->biCompression = BI_RGB;
            for (int y = 0; y < image.rows; ++y)
                memcpy(buffer + sizeof(*header) + size_t(stride) * y, image.ptr(image.rows - 1 - y),
                       size_t(image.cols) * 3);
            GlobalUnlock(memory);
            if (!OpenClipboard(owner_)) {
                GlobalFree(memory);
                DestroyWindow(owner_);
                fail("剪贴板正在使用，通知截图未发送");
            }
            auto old = GetClipboardData(CF_UNICODETEXT);
            if (old) {
                auto *value = static_cast<const char *>(GlobalLock(old));
                if (value) {
                    savedText_ = QByteArray(value, int(GlobalSize(old)));
                    GlobalUnlock(old);
                }
            }
            EmptyClipboard();
            const bool set = SetClipboardData(CF_DIB, memory) != nullptr;
            sequence_ = GetClipboardSequenceNumber();
            CloseClipboard();
            if (!set) {
                GlobalFree(memory);
                DestroyWindow(owner_);
                fail("无法写入通知截图");
            }
        }
        ~ClipboardImage() {
            // Preserve a new user copy rather than overwriting it on cleanup.
            if (GetClipboardSequenceNumber() == sequence_ && OpenClipboard(owner_)) {
                EmptyClipboard();
                if (!savedText_.isEmpty()) {
                    auto memory = GlobalAlloc(GMEM_MOVEABLE, size_t(savedText_.size()));
                    if (memory) {
                        auto *data = GlobalLock(memory);
                        if (data) {
                            memcpy(data, savedText_.data(), size_t(savedText_.size()));
                            GlobalUnlock(memory);
                            if (!SetClipboardData(CF_UNICODETEXT, memory))
                                GlobalFree(memory);
                        } else
                            GlobalFree(memory);
                    }
                }
                CloseClipboard();
            }
            DestroyWindow(owner_);
        }
    };
    void image(const QByteArray &png, QPoint composer, QPoint send) {
        confirmRecipient();
        ClipboardImage clipboard(png);
        click(root_, composer, false, true);
        wait(1);
        click(root_, composer, true, true);
        wait(1);
        std::optional<QPoint> paste;
        HWND menu = nullptr;
        QElapsedTimer timer;
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

  public:
    Messenger(bool wechat, QString nickname, QString models, NoticeCancelled cancel)
        : wechat_(wechat), root_(findMessenger(wechat)), nickname_(std::move(nickname)),
          cancel_(std::move(cancel)), ocr_(models) {
        RECT r{};
        GetClientRect(root_, &r);
        dpi_ = GetDpiForWindow(root_) / 96.;
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
        auto contact = messengerContact(read(root_, contacts_), nickname_, false);
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
            QElapsedTimer timer;
            timer.start();
            do {
                auto candidates = popups(root_, true);
                candidates << root_;
                for (auto candidate : candidates) {
                    contact = messengerContact(read(candidate, candidate == root_ ? contacts_ : QRect{}),
                                               nickname_, candidate != root_);
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
            if (!wechat_ && selected != root_ && IsWindowVisible(selected))
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
} // namespace
void sendDesktopNotice(bool wechat, const QJsonObject &config, const Notice &notice, const QString &models,
                       const NoticeCancelled &cancelled) {
    auto nickname = config[wechat ? "WeChat Desktop Nickname" : "QQ Desktop Nickname"].toString().trimmed();
    if (nickname.isEmpty())
        fail("请配置桌面通知接收人的完整昵称");
    Messenger(wechat, nickname, models, cancelled).send(notice);
}
} // namespace okww::runtime
