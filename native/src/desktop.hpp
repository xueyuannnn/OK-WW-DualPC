#pragma once
#include "common.hpp"
#include <QRect>

namespace okww {
QJsonArray desktopTargets(bool windowsOnly = false);
QJsonObject dxgiDisplay(const Config &config);
QJsonObject desktopCaptureTarget(const Config &config);
double displayRefreshRate(const QString &device);
QRect foregroundClientRect();
// Resolve saved identities once per connection; never follow a reused HWND.
class DesktopTarget {
    QJsonObject identity_;
    HWND window_ = nullptr;
    HMONITOR monitor_ = nullptr;
    DWORD process_ = 0;
  public:
    explicit DesktopTarget(QJsonObject identity);
    QRect rect() const;
    bool foreground() const;
    HWND window() const { return window_; }
};
QPoint desktopAbsolute(QPoint point, QRect virtualDesktop);
QRect virtualDesktopRect();
INPUT hidInput(int usage, bool down);
inline constexpr ULONG_PTR localInputTag = 0x4F4B5757;
// Installed on a dedicated message thread. Script-generated F is not physical F.
class PhysicalKeys {
    std::thread thread_;
    std::atomic<DWORD> threadId_{0};
    std::atomic<bool> ready_{false}, failed_{false};
  public:
    PhysicalKeys();
    ~PhysicalKeys();
    bool down(int vk) const;
    bool physicalHotkey(int vk) const;
};
} // namespace okww
