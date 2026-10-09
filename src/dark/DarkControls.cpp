// DarkControls.cpp - see DarkControls.h for the rationale.
#include "DarkControls.h"

#include "darkmode.h"

#include <commctrl.h> // HKM_GETHOTKEY, DTM_GETSYSTEMTIME, HOTKEYF_*

#include <cstdio>
#include <string>

namespace ls::DarkControls
{

namespace
{

// Subclass identifiers. Any value works as long as it is unique per window.
constexpr UINT_PTR kSubDatePicker = 1;
constexpr UINT_PTR kSubHotkey = 2;

// --------------------------------------------------------------------------
// Metrics
// --------------------------------------------------------------------------

using FnGetDpiForWindow = UINT(WINAPI*)(HWND);

int DpiOf(HWND window)
{
    // Resolved dynamically rather than called directly: GetDpiForWindow needs a
    // _WIN32_WINNT the project does not pin, and this costs one lookup.
    static FnGetDpiForWindow getDpiForWindow = reinterpret_cast<FnGetDpiForWindow>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));

    if (getDpiForWindow && window)
    {
        const UINT dpi = getDpiForWindow(window);
        if (dpi >= 72)
            return static_cast<int>(dpi);
    }
    return 96;
}

int Scaled(HWND window, int pixels)
{
    return MulDiv(pixels, DpiOf(window), 96);
}

// --------------------------------------------------------------------------
// Palette access
// --------------------------------------------------------------------------

struct FieldLook
{
    COLORREF fill;
    COLORREF ink;
    COLORREF frame;
};

FieldLook Look(HWND control)
{
    const dm::Palette& palette = dm::Colors();

    FieldLook look;
    look.fill = palette.field;
    look.ink = (IsWindowEnabled(control) != FALSE) ? palette.text : palette.textDim;
    look.frame = palette.border;
    return look;
}

// --------------------------------------------------------------------------
// Painting
// --------------------------------------------------------------------------

// The non-client frame. IDC_HOTKEY carries WS_BORDER (a non-client border) and the
// system would paint it in COLOR_WINDOWFRAME, which stays light in dark mode. The
// date pickers have no border of their own, so for them this is a no-op in
// practice.
void PaintNonClientFrame(HWND control)
{
    HDC dc = GetWindowDC(control);
    if (!dc)
        return;

    RECT rect;
    GetWindowRect(control, &rect);
    OffsetRect(&rect, -rect.left, -rect.top); // the window DC is window-relative

    HBRUSH brush = CreateSolidBrush(Look(control).frame);
    if (brush)
    {
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
    }

    ReleaseDC(control, dc);
}

void PaintSpinner(HWND control, HDC dc, const RECT& client, const FieldLook& look)
{
    // Scaled against the control, not against null: on a HiDPI monitor a
    // 96-DPI arrow width would put the two triangles somewhere other than over
    // the region the control itself hit-tests for the up/down clicks.
    const int arrowWidth = Scaled(control, GetSystemMetrics(SM_CXVSCROLL));
    if (arrowWidth <= 0 || arrowWidth >= client.right - client.left - 4)
        return;

    RECT area = client;
    area.left = client.right - 1 - arrowWidth;
    area.right = client.right - 1;
    area.top = client.top + 1;
    area.bottom = client.bottom - 1;
    if (area.left <= area.right || area.top >= area.bottom)
        return;

    // Separation line between the text and the buttons.
    HBRUSH divider = CreateSolidBrush(look.frame);
    if (divider)
    {
        const RECT line = { area.left - 1, area.top, area.left, area.bottom };
        FillRect(dc, &line, divider);
        DeleteObject(divider);
    }

    const int centreX = (area.left + area.right) / 2;
    const int middleY = (area.top + area.bottom) / 2;
    const int halfWidth = Scaled(control, 3);
    const int gap = Scaled(control, 2);

    const POINT up[3] = {
        { centreX, middleY - gap - halfWidth },
        { centreX - halfWidth, middleY - gap + halfWidth },
        { centreX + halfWidth, middleY - gap + halfWidth },
    };
    const POINT down[3] = {
        { centreX, middleY + gap + halfWidth },
        { centreX - halfWidth, middleY + gap - halfWidth },
        { centreX + halfWidth, middleY + gap - halfWidth },
    };

    HBRUSH fill = CreateSolidBrush(look.ink);
    if (!fill)
        return;

    HGDIOBJ previousBrush = SelectObject(dc, fill);
    HGDIOBJ previousPen = SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc, up, 3);
    Polygon(dc, down, 3);
    SelectObject(dc, previousPen);
    SelectObject(dc, previousBrush);
    DeleteObject(fill);
}

// `spinner` is true for the date-time pickers (which are created with DTS_UPDOWN,
// so their spinner is the up/down pair) and false for the hotkey box.
void PaintField(HWND control, const std::wstring& text, bool spinner)
{
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(control, &paint);
    if (!dc)
        return;

    RECT client;
    GetClientRect(control, &client);

    const FieldLook look = Look(control);

    HBRUSH background = CreateSolidBrush(look.fill);
    if (background)
    {
        FillRect(dc, &client, background);
        DeleteObject(background);
    }

    HBRUSH frame = CreateSolidBrush(look.frame);
    if (frame)
    {
        FrameRect(dc, &client, frame);
        DeleteObject(frame);
    }

    const int arrowWidth =
        spinner ? Scaled(control, GetSystemMetrics(SM_CXVSCROLL)) : 0;
    const int padding = Scaled(control, 5);

    RECT textRect = client;
    textRect.left += 1 + padding;
    textRect.right -= 1 + ((arrowWidth > 0) ? arrowWidth + 2 : padding);

    if (textRect.left < textRect.right)
    {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, look.ink);

        // Use the control's own font rather than a generic one: the dialog sets
        // Microsoft YaHei UI for Chinese, and the field must match.
        const HFONT font = reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0));
        HGDIOBJ previousFont = font ? SelectObject(dc, font) : nullptr;

        DrawTextW(dc,
                  text.c_str(),
                  static_cast<int>(text.size()),
                  &textRect,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);

        if (previousFont)
            SelectObject(dc, previousFont);
    }

    if (spinner)
        PaintSpinner(control, dc, client, look);

    if (GetFocus() == control)
    {
        RECT focus = client;
        focus.left += 1;
        focus.right -= 1 + (arrowWidth > 0 ? arrowWidth : 0);
        focus.top += 1;
        focus.bottom -= 1;
        if (focus.left < focus.right && focus.top < focus.bottom)
            DrawFocusRect(dc, &focus);
    }

    EndPaint(control, &paint);
}

// --------------------------------------------------------------------------
// Content
// --------------------------------------------------------------------------

// The text the control would have drawn itself. Asking the control is better than
// formatting the time here: the display honours the user's clock format (24-hour
// vs 12-hour, and its separators), which we would otherwise have to reimplement.
std::wstring DatePickerText(HWND control)
{
    wchar_t buffer[128] = { 0 };
    if (GetWindowTextW(control, buffer, 128) > 0)
        return buffer;

    // Only reached if WM_GETTEXT goes unanswered, which should not happen.
    SYSTEMTIME time{};
    if (SendMessageW(control, DTM_GETSYSTEMTIME, 0, reinterpret_cast<LPARAM>(&time)) == GDT_VALID)
    {
        wchar_t text[16] = { 0 };
        swprintf_s(text, L"%02u:%02u", time.wHour, time.wMinute);
        return text;
    }
    return std::wstring();
}

std::wstring KeyName(BYTE virtualKey)
{
    const UINT scanCode = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC);
    LONG keyData = static_cast<LONG>(scanCode) << 16;

    // Keys in the dedicated navigation block need the extended flag, otherwise
    // GetKeyNameTextW answers with the numeric-keypad legend instead.
    switch (virtualKey)
    {
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_DIVIDE:
    case VK_NUMLOCK:
        keyData |= 1 << 24;
        break;
    default:
        break;
    }

    wchar_t name[64] = { 0 };
    if (GetKeyNameTextW(keyData, name, 64) > 0)
        return name;

    wchar_t fallback[16] = { 0 };
    swprintf_s(fallback, L"0x%02X", static_cast<unsigned>(virtualKey));
    return fallback;
}

std::wstring HotkeyText(HWND control)
{
    // HKM_GETHOTKEY packs the virtual key into the LOW byte and the modifier
    // flags into the HIGH byte (HKM_SETHOTKEY takes the same layout, which is
    // why SettingsDialog.cpp builds it with MAKEWORD(vk, modifiers)). Getting
    // this the wrong way round is not a visible crash but a silent misread:
    // with Ctrl+Shift+D stored as 0x0344 it renders "Alt + Scroll Lock".
    const WORD hotkey = static_cast<WORD>(SendMessageW(control, HKM_GETHOTKEY, 0, 0));
    const BYTE virtualKey = LOBYTE(hotkey);
    const BYTE modifiers = HIBYTE(hotkey);

    if (virtualKey == 0)
        return std::wstring();

    std::wstring text;
    if (modifiers & HOTKEYF_CONTROL)
        text += L"Ctrl + ";
    if (modifiers & HOTKEYF_ALT)
        text += L"Alt + ";
    if (modifiers & HOTKEYF_SHIFT)
        text += L"Shift + ";
    // msctls_hotkey32 cannot represent MOD_WIN at all - which is exactly why the
    // dialog carries a separate "Include Win key" checkbox. The Win key is
    // therefore applied outside this control and deliberately not shown here.

    text += KeyName(virtualKey);
    return text;
}

// --------------------------------------------------------------------------
// Subclass plumbing
// --------------------------------------------------------------------------

bool AlreadySubclassed(HWND control, SUBCLASSPROC procedure, UINT_PTR id)
{
    DWORD_PTR reference = 0;
    return GetWindowSubclass(control, procedure, id, &reference) != FALSE;
}

void SubOnce(HWND control, SUBCLASSPROC procedure, UINT_PTR id)
{
    if (!AlreadySubclassed(control, procedure, id))
        SetWindowSubclass(control, procedure, id, 0);
}

bool HandleEraseBackground(HWND control, WPARAM wParam)
{
    RECT client;
    GetClientRect(control, &client);

    HBRUSH brush = CreateSolidBrush(Look(control).fill);
    if (!brush)
        return true;

    FillRect(reinterpret_cast<HDC>(wParam), &client, brush);
    DeleteObject(brush);
    return true;
}

LRESULT CALLBACK DatePickerProc(HWND control, UINT message, WPARAM wParam, LPARAM lParam,
                                UINT_PTR id, DWORD_PTR)
{
    switch (message)
    {
    case WM_ERASEBKGND:
        return HandleEraseBackground(control, wParam);

    case WM_PAINT:
        PaintField(control, DatePickerText(control), /*spinner=*/true);
        return 0;

    // A full takeover means the control must also paint its own border.
    case WM_NCPAINT:
        PaintNonClientFrame(control);
        return 0;

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
        InvalidateRect(control, nullptr, TRUE);
        break;

    case WM_NCDESTROY:
        RemoveWindowSubclass(control, DatePickerProc, id);
        break;

    default:
        break;
    }

    return DefSubclassProc(control, message, wParam, lParam);
}

LRESULT CALLBACK HotkeyProc(HWND control, UINT message, WPARAM wParam, LPARAM lParam,
                            UINT_PTR id, DWORD_PTR)
{
    switch (message)
    {
    case WM_ERASEBKGND:
        return HandleEraseBackground(control, wParam);

    case WM_PAINT:
        PaintField(control, HotkeyText(control), /*spinner=*/false);
        return 0;

    case WM_NCPAINT:
        PaintNonClientFrame(control);
        return 0;

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
        InvalidateRect(control, nullptr, TRUE);
        break;

    case WM_NCDESTROY:
        RemoveWindowSubclass(control, HotkeyProc, id);
        break;

    default:
        break;
    }

    return DefSubclassProc(control, message, wParam, lParam);
}

} // namespace

void Install(HWND top)
{
    if (!top)
        return;

    for (HWND child = GetWindow(top, GW_CHILD); child != nullptr;
         child = GetWindow(child, GW_HWNDNEXT))
    {
        wchar_t className[64] = { 0 };
        if (!GetClassNameW(child, className, 64))
            continue;

        if (_wcsicmp(className, L"SysDateTimePick32") == 0)
            SubOnce(child, DatePickerProc, kSubDatePicker);
        else if (_wcsicmp(className, L"msctls_hotkey32") == 0)
            SubOnce(child, HotkeyProc, kSubHotkey);
    }
}

} // namespace ls::DarkControls
