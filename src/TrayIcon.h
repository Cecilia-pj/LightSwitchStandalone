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

    // Puts the icon back after the shell restarted. The notification area
    // belongs to explorer.exe and has no memory of who registered what, so
    // every application has to re-register itself; nothing is restored for us.
    // Safe to call when the icon is already present - NIM_ADD with the same
    // (hWnd, uID) updates the existing entry rather than adding a second one.
    bool ReAdd();

    void Remove();
    void SetTooltip(const std::wstring& tooltip);

    // Shows the popup menu at the given screen point. Returns 0 when dismissed.
    UINT ShowContextMenu(POINT screenPoint);

    // Draws the tray icon into a freshly created HICON.
    static HICON CreateIcon();

private:
    // Creates m_icon on first use. False only if GDI refused to make one.
    bool EnsureIcon();

    // One NIM_ADD followed by the NIM_SETVERSION the shell requires after every
    // add. Silent about failures: the caller decides whether a refusal is fatal
    // (Add, at startup) or expected (ReAdd, when the shell already has the icon).
    bool Register();

    // One NIM_MODIFY carrying the icon, the callback message and the tooltip.
    // Returns false when the shell holds no icon for this (hWnd, uID), which is
    // also how ReAdd() tells "already registered" from "shell not ready yet".
    bool Modify(const std::wstring& tooltip);

    HWND m_owner = nullptr;
    HICON m_icon = nullptr;
    bool m_added = false;

    // Remembered so ReAdd() can restore the tooltip too. The shell keeps the
    // tip it was last given, and that copy dies with the shell.
    std::wstring m_tooltip;
};

} // namespace ls
