#pragma once
#include "runtime/notification.hpp"
#include <future>
namespace okww::runtime::notice_desktop_reference {
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

} // namespace okww::runtime::notice_desktop_reference
