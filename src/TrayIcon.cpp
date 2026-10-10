// LightSwitch Standalone - notification area (tray) icon (implementation).
#include "TrayIcon.h"

#include "L10n.h"
#include "Logging.h"
#include "dark/DarkApp.h"

#include <shellapi.h>

#include <cmath>
#include <cstring>
#include <vector>

namespace ls
{

namespace
{

constexpr UINT kNotifyId = 1;
constexpr int kIconSize = 32;

// Straight (non-premultiplied) BGRA because that is what a 32bpp BI_RGB DIB
// expects; Windows treats the fourth byte as alpha for Shell_NotifyIcon icons.
struct Bgra
{
    BYTE b, g, r, a;
};

HICON BuildIcon()
{
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = kIconSize;
    info.bmiHeader.biHeight = -kIconSize; // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* rawPixels = nullptr;
    HBITMAP colorBitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &rawPixels, nullptr, 0);
    if (!colorBitmap || !rawPixels)
        return nullptr;

    auto* pixels = static_cast<Bgra*>(rawPixels);
    std::memset(pixels, 0, sizeof(Bgra) * kIconSize * kIconSize);

    const Bgra outline{ 0xB0, 0x6E, 0x2E, 0xFF }; // #2E6EB0  - ring
    const Bgra lightSide{ 0xF2, 0xF2, 0xF2, 0xFF }; // near-white  - left half
    const Bgra darkSide{ 0x20, 0x20, 0x20, 0xFF };  // near-black  - right half
    const Bgra splitLine{ 0xB0, 0x6E, 0x2E, 0xFF };

    const double center = (kIconSize - 1) / 2.0;
    const double outer = center + 0.4;
    const double inner = center - 1.6;

    for (int y = 0; y < kIconSize; ++y)
    {
        for (int x = 0; x < kIconSize; ++x)
        {
            const double dx = x - center;
            const double dy = y - center;
            const double radius = std::sqrt(dx * dx + dy * dy);

            Bgra color{};
            if (radius > outer)
                color = Bgra{ 0, 0, 0, 0 };
            else if (radius > inner)
                color = outline;
            else if (std::fabs(dx) < 0.75)
                color = splitLine;
            else
                color = (dx < 0) ? lightSide : darkSide;

            pixels[y * kIconSize + x] = color;
        }
    }

    // The mask is required to be all-zero for a 32bpp icon with alpha.
    HBITMAP maskBitmap = CreateBitmap(kIconSize, kIconSize, 1, 1, nullptr);
    if (!maskBitmap)
    {
        DeleteObject(colorBitmap);
        return nullptr;
    }

    ICONINFO iconInfo{};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmColor = colorBitmap;
    iconInfo.hbmMask = maskBitmap;

    HICON icon = CreateIconIndirect(&iconInfo);

    DeleteObject(colorBitmap);
    DeleteObject(maskBitmap);
    return icon;
}

std::wstring ExePath()
{
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;)
    {
        const DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0)
            return {};
        if (len < buffer.size() - 1)
            return std::wstring(buffer.data(), len);
        buffer.resize(buffer.size() * 2);
    }
}

} // namespace

TrayIcon::~TrayIcon()
{
    Remove();
}

HICON TrayIcon::CreateIcon()
{
    HICON icon = BuildIcon();
    if (!icon)
        icon = LoadIconW(nullptr, IDI_APPLICATION);
    return icon;
}

bool TrayIcon::EnsureIcon()
{
    if (m_icon)
        return true;

    m_icon = CreateIcon();
    return m_icon != nullptr;
}

bool TrayIcon::Register()
{
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = m_owner;
    data.uID = kNotifyId;
    data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = kCallbackMessage;
    data.hIcon = m_icon;
    wcsncpy_s(data.szTip, m_tooltip.c_str(), _TRUNCATE);

    if (!Shell_NotifyIconW(NIM_ADD, &data))
        return false;

    // Opt into the modern behaviour so the tooltip uses the current shell style.
    // The shell wants this after every add, not just the first one.
    data.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data);

    m_added = true;
    return true;
}

bool TrayIcon::Modify(const std::wstring& tooltip)
{
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = m_owner;
    data.uID = kNotifyId;
    data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = kCallbackMessage;
    data.hIcon = m_icon;
    wcsncpy_s(data.szTip, tooltip.c_str(), _TRUNCATE);

    return Shell_NotifyIconW(NIM_MODIFY, &data) != FALSE;
}

bool TrayIcon::Add(HWND owner, const std::wstring& tooltip)
{
    m_owner = owner;
    // Recorded before the call, not after: a failed Add() is retried by App,
    // and the retry needs a tooltip to hand to the shell either way.
    m_tooltip = tooltip;

    if (!EnsureIcon())
    {
        LOG_ERROR(L"[Tray] Could not create the tray icon.");
        return false;
    }

    if (!Register())
    {
        LOG_ERROR(L"[Tray] Shell_NotifyIcon(NIM_ADD) failed.");
        return false;
    }

    LOG_INFO(L"[Tray] Icon added.");
    return true;
}

bool TrayIcon::ReAdd()
{
    if (!m_owner)
        return false;

    // The shell that owned the notification area is gone, so the icon went with
    // it. Clearing the flag is what makes the call below issue a full NIM_ADD
    // instead of trusting a registration that no longer exists, and it keeps
    // m_added honest for the NIM_DELETE in Remove().
    m_added = false;

    if (!EnsureIcon())
    {
        LOG_ERROR(L"[Tray] Could not create the tray icon.");
        return false;
    }

    if (Register())
    {
        LOG_INFO(L"[Tray] Icon added.");
        return true;
    }

    // A refused NIM_ADD does not only mean "shell not ready yet". The shell also
    // refuses it when it already holds an icon for this (hWnd, uID) - which is
    // what a redundant re-add looks like, and one shell restart can very well
    // produce two of those, because both the TaskbarCreated broadcast and the
    // timer behind this program's own Restart Explorer react to it. In that case
    // the icon is already in the tray and all that is left to do is put the
    // current tooltip back. NIM_MODIFY only succeeds when the shell really does
    // have the icon, so it cannot be mistaken for a shell that is still starting
    // up: there it fails too, and the caller retries.
    if (Modify(m_tooltip))
    {
        m_added = true;
        LOG_INFO(L"[Tray] This shell already had the icon; refreshed it instead.");
        return true;
    }

    return false;
}

void TrayIcon::Remove()
{
    if (m_added)
    {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = m_owner;
        data.uID = kNotifyId;
        Shell_NotifyIconW(NIM_DELETE, &data);
        m_added = false;
    }

    if (m_icon)
    {
        DestroyIcon(m_icon);
        m_icon = nullptr;
    }
}

void TrayIcon::SetTooltip(const std::wstring& tooltip)
{
    if (!m_added)
        return;

    // Kept so a later shell restart restores the tip that was actually showing,
    // not the product name from startup.
    m_tooltip = tooltip;
    Modify(tooltip);
}

UINT TrayIcon::ShowContextMenu(POINT screenPoint)
{
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return 0;

    // Rebuilt on every right-click, so the menu always reflects the language
    // that is active right now - no need to notify the tray when it changes.
    AppendMenuW(menu, MF_STRING, CmdToggleNow, L10n::T(Str::TrayToggleNow));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CmdSettings, L10n::T(Str::TraySettings));
    AppendMenuW(menu, MF_STRING, CmdReloadConfig, L10n::T(Str::TrayReloadConfig));
    AppendMenuW(menu, MF_STRING, CmdOpenConfigFolder, L10n::T(Str::TrayOpenConfigFolder));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CmdRestartExplorer, L10n::T(Str::TrayRestartExplorer));
    AppendMenuW(menu, MF_STRING, CmdOpenLog, L10n::T(Str::TrayOpenLog));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CmdExit, L10n::T(Str::TrayExit));

    SetMenuDefaultItem(menu, CmdToggleNow, FALSE);

    // The popup itself is drawn by the system from the process-wide app mode,
    // so the only thing to do here is make sure that mode is current. It can be
    // stale if settings.json was edited (or reloaded from the tray menu) after
    // the last Apply. Conditional on purpose: this runs on every right-click,
    // where an unconditional Apply - two rebuilt brushes and a full repaint of
    // every child window - would be pure overhead.
    DarkApp::ApplyIfNeeded(m_owner);

    // Required so the menu closes when the user clicks elsewhere.
    SetForegroundWindow(m_owner);

    const UINT command = TrackPopupMenu(menu,
                                        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                        screenPoint.x,
                                        screenPoint.y,
                                        0,
                                        m_owner,
                                        nullptr);

    DestroyMenu(menu);
    PostMessageW(m_owner, WM_NULL, 0, 0);
    return command;
}

} // namespace ls
