// LightSwitch Standalone - notification area (tray) icon.
//
// The icon is drawn at runtime with GDI instead of shipping an .ico file:
//   * keeps the distribution to "source + one exe",
//   * guarantees the icon is our own artwork (PowerToys' icon is Microsoft
//     brand collateral and must not be reused - see README "Licensing").
#pragma once

#include <windows.h>

#include <string>

namespace ls
{

class TrayIcon
{
public:
    // Callback message delivered to the owner window.
    static constexpr UINT kCallbackMessage = WM_APP + 0x102;

    enum Command : UINT
    {
        CmdToggleNow = 1,
        CmdSettings = 2,
        CmdRestartExplorer = 3,
        CmdOpenConfigFolder = 4,
        CmdOpenLog = 5,
        CmdReloadConfig = 6,
        CmdExit = 7,
    };

    TrayIcon() = default;
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    bool Add(HWND owner, const std::wstring& tooltip);
    void Remove();
    void SetTooltip(const std::wstring& tooltip);

    // Shows the popup menu at the given screen point. Returns 0 when dismissed.
    UINT ShowContextMenu(POINT screenPoint);

    // Draws the tray icon into a freshly created HICON.
    static HICON CreateIcon();

private:
    HWND m_owner = nullptr;
    HICON m_icon = nullptr;
    bool m_added = false;
};

} // namespace ls
