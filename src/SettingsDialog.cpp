// LightSwitch Standalone - settings dialog (implementation).
#include "SettingsDialog.h"

#include "AutoStart.h"
#include "Config.h"
#include "L10n.h"
#include "Logging.h"
#include "resource.h"
#include "vendor/ThemeHelper.h"
#include "vendor/ThemeScheduler.h"

#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

namespace ls
{

namespace
{

constexpr wchar_t kWindowClassName[] = L"LightSwitchStandalone.SettingsDialog";

// Returned by DialogProc when the save changed the interface language. The
// caller rebuilds the dialog so the switch is visible immediately instead of
// asking the user to close and reopen it.
constexpr INT_PTR kReloadDialog = 0x4C53;

// Button row metrics in pixels. The row is measured and packed at runtime: the
// hand-placed row this replaced had "Restore defaults" overlapping "Save", and
// translated labels have different widths anyway.
constexpr int kButtonMargin = 8;
constexpr int kButtonGap = 6;
constexpr int kButtonTextPadding = 18;
constexpr int kButtonMinWidth = 44;

// Mode combo order. Index N corresponds to static_cast<int>(ScheduleMode) == N.
constexpr Str kModeComboTexts[] = {
    Str::ModeOff,
    Str::ModeFixedHours,
    Str::ModeSunsetToSunrise,
    Str::ModeFollowNightLight,
};

struct DialogContext
{
    ISettingsHost* host = nullptr;
    Config config;
    HFONT font = nullptr; // owned; deleted once DialogBoxParamW returns
};

// Every control whose caption comes from the L10n table.
struct ControlText
{
    int controlId;
    Str text;
};

constexpr ControlText kControlTexts[] = {
    { IDC_STATIC_GROUP_SCHEDULE, Str::DialogGroupSchedule },
    { IDC_STATIC_LBL_MODE, Str::DialogLabelMode },
    { IDC_STATIC_LBL_LIGHT, Str::DialogLabelLightStarts },
    { IDC_STATIC_LBL_DARK, Str::DialogLabelDarkStarts },
    { IDC_STATIC_GROUP_APPLY, Str::DialogGroupApplyTo },
    { IDC_CHK_SYSTEM, Str::DialogCheckSystem },
    { IDC_CHK_APPS, Str::DialogCheckApps },
    { IDC_STATIC_GROUP_SUN, Str::DialogGroupSun },
    { IDC_STATIC_LBL_LAT, Str::DialogLabelLatitude },
    { IDC_STATIC_LBL_LON, Str::DialogLabelLongitude },
    { IDC_STATIC_LBL_SUNRISE_OFF, Str::DialogLabelSunriseOffset },
    { IDC_STATIC_LBL_SUNSET_OFF, Str::DialogLabelSunsetOffset },
    { IDC_STATIC_HINT_OFFSETS, Str::DialogSunOffsetHint },
    { IDC_STATIC_GROUP_SHORTCUT, Str::DialogGroupShortcut },
    { IDC_CHK_WIN, Str::DialogCheckWinKey },
    { IDC_STATIC_HINT_HOTKEY, Str::DialogHotkeyHint },
    { IDC_STATIC_GROUP_STARTUP, Str::DialogGroupStartup },
    { IDC_CHK_STARTUP, Str::DialogCheckStartup },
    { IDC_STATIC_GROUP_LANGUAGE, Str::DialogGroupLanguage },
    { IDC_STATIC_LBL_LANGUAGE, Str::DialogLabelLanguage },
    { IDC_BTN_TOGGLE, Str::DialogButtonToggleNow },
    { IDC_BTN_RESTART_EXPLORER, Str::DialogButtonRestartExplorer },
    { IDC_BTN_OPEN_FOLDER, Str::DialogButtonOpenFolder },
    { IDC_BTN_RESET, Str::DialogButtonRestoreDefaults },
    { IDOK, Str::DialogButtonSave },
    { IDCANCEL, Str::DialogButtonCancel },
};

// The button row, right to left so Cancel stays in the corner users expect.
constexpr int kButtonRow[] = {
    IDCANCEL,
    IDOK,
    IDC_BTN_RESET,
    IDC_BTN_OPEN_FOLDER,
    IDC_BTN_RESTART_EXPLORER,
    IDC_BTN_TOGGLE,
};

DialogContext* ContextOf(HWND dialog)
{
    return reinterpret_cast<DialogContext*>(GetWindowLongPtrW(dialog, DWLP_USER));
}

// --------------------------------------------------------------------------
// Font
// --------------------------------------------------------------------------

// The template asks for Segoe UI 9pt, which has no CJK glyphs. Windows would
// font-link them in, but the result depends on what is installed, so build the
// font explicitly from L10n and scale it to the dialog's monitor.
HFONT CreateDialogFont(HWND dialog)
{
    HDC dc = GetDC(dialog);
    if (!dc)
        return nullptr;
    const int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(dialog, dc);
    if (dpi <= 0)
        return nullptr;

    LOGFONTW font{};
    font.lfHeight = -MulDiv(9, dpi, 72);
    font.lfWeight = FW_NORMAL;
    font.lfCharSet = DEFAULT_CHARSET;
    font.lfOutPrecision = OUT_TT_PRECIS;
    font.lfQuality = CLEARTYPE_QUALITY;
    font.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    wcsncpy_s(font.lfFaceName, L10n::UiFontFace(), _TRUNCATE);

    return CreateFontIndirectW(&font);
}

void ApplyFont(HWND dialog, HFONT font)
{
    if (!font)
        return;

    SendMessageW(dialog, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    // DefDlgProc is documented to forward WM_SETFONT to the child controls, but
    // doing it explicitly costs nothing and removes the doubt.
    for (HWND child = GetWindow(dialog, GW_CHILD); child != nullptr; child = GetWindow(child, GW_HWNDNEXT))
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

// --------------------------------------------------------------------------
// Localised caption pass
// --------------------------------------------------------------------------

void ApplyLocalizedText(HWND dialog)
{
    SetWindowTextW(dialog, L10n::T(Str::ProductName));

    for (const ControlText& entry : kControlTexts)
    {
        HWND control = GetDlgItem(dialog, entry.controlId);
        if (control)
            SetWindowTextW(control, L10n::T(entry.text));
    }
}

// --------------------------------------------------------------------------
// Text measurement
// --------------------------------------------------------------------------

int MeasureControlText(HWND control)
{
    const int length = GetWindowTextLengthW(control);
    if (length <= 0)
        return 0;

    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));

    HDC dc = GetDC(control);
    if (!dc)
        return 0;

    HGDIOBJ previous = nullptr;
    const HFONT font = reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0));
    if (font)
        previous = SelectObject(dc, font);

    SIZE size{};
    const BOOL measured = GetTextExtentPoint32W(dc, text.c_str(), length, &size);

    if (previous)
        SelectObject(dc, previous);
    ReleaseDC(control, dc);

    return measured ? static_cast<int>(size.cx) : 0;
}

// Height of one line of the control's font, in pixels.
int LineHeight(HWND control)
{
    HDC dc = GetDC(control);
    if (!dc)
        return 0;

    HGDIOBJ previous = nullptr;
    const HFONT font = reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0));
    if (font)
        previous = SelectObject(dc, font);

    TEXTMETRICW metrics{};
    const BOOL measured = GetTextMetricsW(dc, &metrics);

    if (previous)
        SelectObject(dc, previous);
    ReleaseDC(control, dc);

    return measured ? static_cast<int>(metrics.tmHeight) : 0;
}

bool IsStaticControl(HWND control)
{
    wchar_t className[16] = {};
    if (GetClassNameW(control, className, 15) == 0)
        return false;
    return _wcsicmp(className, L"Static") == 0;
}

// A full-width CJK glyph is about one em wide, so a translated label can be
// wider than the box that was sized for English - "浅色主题开始时间：" needs more
// room than "Light theme starts at:" despite being half the character count.
// Rather than hand-tune every box for every language, widen the ones that do
// not fit, stopping short of the next control. Wrapping hints (boxes tall
// enough for two lines) are left alone: widening them would only move the wrap.
void FitLabels(HWND dialog)
{
    RECT client{};
    GetClientRect(dialog, &client);
    const int outerLimit = client.right - kButtonMargin;

    for (HWND child = GetWindow(dialog, GW_CHILD); child != nullptr; child = GetWindow(child, GW_HWNDNEXT))
    {
        if (!IsStaticControl(child))
            continue;

        const int lineHeight = LineHeight(child);
        if (lineHeight <= 0)
            continue;

        RECT rect{};
        GetWindowRect(child, &rect);
        MapWindowPoints(HWND_DESKTOP, dialog, reinterpret_cast<POINT*>(&rect), 2);

        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        if (height >= 2 * lineHeight - 2)
            continue;

        const int needed = MeasureControlText(child) + 2;
        if (needed <= width)
            continue;

        int boundary = outerLimit;
        for (HWND other = GetWindow(dialog, GW_CHILD); other != nullptr; other = GetWindow(other, GW_HWNDNEXT))
        {
            if (other == child)
                continue;

            RECT otherRect{};
            GetWindowRect(other, &otherRect);
            MapWindowPoints(HWND_DESKTOP, dialog, reinterpret_cast<POINT*>(&otherRect), 2);

            // Only a control that actually shares this row can be run into.
            // Without the vertical test the button row (which sits far below
            // but further right) would clamp every label to a few pixels.
            const bool sharesRow = otherRect.bottom > rect.top && otherRect.top < rect.bottom;
            if (!sharesRow)
                continue;

            if (otherRect.left >= rect.right && otherRect.left < boundary)
                boundary = otherRect.left;
        }

        const int target = std::min(needed, static_cast<int>(boundary - rect.left) - 2);
        if (target <= width)
            continue;

        LOG_DEBUG(L"[Settings] Widened a label from " << width << L" to " << target
                                                      << L" px so the translated text is not clipped.");
        SetWindowPos(child, nullptr, rect.left, rect.top, target, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

// --------------------------------------------------------------------------
// Button row layout
// --------------------------------------------------------------------------

void LayoutButtonRow(HWND dialog)
{
    constexpr int kCount = static_cast<int>(sizeof(kButtonRow) / sizeof(kButtonRow[0]));

    RECT client{};
    GetClientRect(dialog, &client);

    int widths[kCount] = {};
    int total = 0;
    for (int i = 0; i < kCount; ++i)
    {
        HWND control = GetDlgItem(dialog, kButtonRow[i]);
        if (!control)
            continue;

        widths[i] = std::max(kButtonMinWidth, MeasureControlText(control) + kButtonTextPadding);
        total += widths[i];
    }
    total += kButtonGap * (kCount - 1);

    const int available = client.right - 2 * kButtonMargin;
    if (total > available)
    {
        // A translation is wider than the template allows. Grow the dialog
        // rather than let the left-most button escape the client area.
        RECT window{};
        GetWindowRect(dialog, &window);
        SetWindowPos(dialog,
                     nullptr,
                     0,
                     0,
                     (window.right - window.left) + (total - available),
                     window.bottom - window.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        GetClientRect(dialog, &client);
    }

    int right = client.right - kButtonMargin;
    for (int i = 0; i < kCount; ++i)
    {
        HWND control = GetDlgItem(dialog, kButtonRow[i]);
        if (!control)
            continue;

        RECT rect{};
        GetWindowRect(control, &rect);
        MapWindowPoints(HWND_DESKTOP, dialog, reinterpret_cast<POINT*>(&rect), 2);

        const int width = widths[i];
        const int height = rect.bottom - rect.top;
        SetWindowPos(control,
                     nullptr,
                     right - width,
                     rect.top,
                     width,
                     height,
                     SWP_NOZORDER | SWP_NOACTIVATE);

        right -= width + kButtonGap;
    }
}

// --------------------------------------------------------------------------
// Time pickers
// --------------------------------------------------------------------------

void SetTimeControl(HWND dialog, int controlId, int minutes)
{
    minutes = ((minutes % 1440) + 1440) % 1440;

    SYSTEMTIME st{};
    st.wYear = 1601;
    st.wMonth = 1;
    st.wDay = 1;
    st.wHour = static_cast<WORD>(minutes / 60);
    st.wMinute = static_cast<WORD>(minutes % 60);

    DateTime_SetSystemtime(GetDlgItem(dialog, controlId), GDT_VALID, &st);
}

int GetTimeControl(HWND dialog, int controlId)
{
    SYSTEMTIME st{};
    if (DateTime_GetSystemtime(GetDlgItem(dialog, controlId), &st) != GDT_VALID)
        return 0;
    return static_cast<int>(st.wHour) * 60 + static_cast<int>(st.wMinute);
}

// --------------------------------------------------------------------------
// Hotkey control
// --------------------------------------------------------------------------

void SetHotkeyControl(HWND dialog, const HotkeyConfig& hotkey)
{
    BYTE modifiers = 0;
    if (hotkey.ctrl)
        modifiers |= HOTKEYF_CONTROL;
    if (hotkey.alt)
        modifiers |= HOTKEYF_ALT;
    if (hotkey.shift)
        modifiers |= HOTKEYF_SHIFT;

    const WORD packed = MAKEWORD(static_cast<BYTE>(hotkey.vk), modifiers);
    SendMessageW(GetDlgItem(dialog, IDC_HOTKEY), HKM_SETHOTKEY, packed, 0);
    CheckDlgButton(dialog, IDC_CHK_WIN, hotkey.win ? BST_CHECKED : BST_UNCHECKED);
}

HotkeyConfig GetHotkeyControl(HWND dialog)
{
    HotkeyConfig hotkey;
    const WORD packed = static_cast<WORD>(SendMessageW(GetDlgItem(dialog, IDC_HOTKEY), HKM_GETHOTKEY, 0, 0));

    const BYTE vk = LOBYTE(packed);
    const BYTE modifiers = HIBYTE(packed);

    hotkey.vk = (vk != 0) ? vk : 0x44;
    hotkey.ctrl = (modifiers & HOTKEYF_CONTROL) != 0;
    hotkey.alt = (modifiers & HOTKEYF_ALT) != 0;
    hotkey.shift = (modifiers & HOTKEYF_SHIFT) != 0;
    hotkey.win = IsDlgButtonChecked(dialog, IDC_CHK_WIN) == BST_CHECKED;
    return hotkey;
}

// --------------------------------------------------------------------------
// Language combo
// --------------------------------------------------------------------------

// Index 0 is "Follow Windows"; the concrete languages follow in L10n::All()
// order.
int LanguageComboIndex(const std::wstring& configured)
{
    if (L10n::IsAuto(configured))
        return 0;

    const Lang wanted = L10n::Resolve(configured);
    const std::vector<Lang>& languages = L10n::All();
    for (size_t i = 0; i < languages.size(); ++i)
    {
        if (languages[i] == wanted)
            return static_cast<int>(i) + 1;
    }
    return 0;
}

std::wstring LanguageFromCombo(HWND dialog)
{
    const LRESULT index = SendDlgItemMessageW(dialog, IDC_CMB_LANG, CB_GETCURSEL, 0, 0);
    if (index <= 0)
        return L"auto";

    const std::vector<Lang>& languages = L10n::All();
    const size_t position = static_cast<size_t>(index - 1);
    if (position >= languages.size())
        return L"auto";
    return L10n::CodeOf(languages[position]);
}

void PopulateLanguageCombo(HWND dialog, const std::wstring& configured)
{
    SendDlgItemMessageW(dialog, IDC_CMB_LANG, CB_RESETCONTENT, 0, 0);
    SendDlgItemMessageW(dialog, IDC_CMB_LANG, CB_ADDSTRING, 0,
                        reinterpret_cast<LPARAM>(L10n::AutoEntryName()));
    for (Lang lang : L10n::All())
        SendDlgItemMessageW(dialog, IDC_CMB_LANG, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(L10n::DisplayName(lang)));
    SendDlgItemMessageW(dialog, IDC_CMB_LANG, CB_SETCURSEL,
                        static_cast<WPARAM>(LanguageComboIndex(configured)), 0);
}

// --------------------------------------------------------------------------
// Text helpers
// --------------------------------------------------------------------------

std::wstring GetEditText(HWND dialog, int controlId)
{
    const HWND control = GetDlgItem(dialog, controlId);
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

void SetEditText(HWND dialog, int controlId, const std::wstring& text)
{
    SetWindowTextW(GetDlgItem(dialog, controlId), text.c_str());
}

bool ParseDouble(const std::wstring& text, double& value)
{
    if (text.empty())
        return false;
    wchar_t* end = nullptr;
    const double parsed = std::wcstod(text.c_str(), &end);
    if (end == text.c_str())
        return false;
    while (end && *end != L'\0' && iswspace(*end))
        ++end;
    if (end && *end != L'\0')
        return false;
    value = parsed;
    return true;
}

bool ParseInt(const std::wstring& text, int& value)
{
    if (text.empty())
    {
        value = 0;
        return true;
    }
    wchar_t* end = nullptr;
    const long parsed = std::wcstol(text.c_str(), &end, 10);
    if (end == text.c_str())
        return false;
    while (end && *end != L'\0' && iswspace(*end))
        ++end;
    if (end && *end != L'\0')
        return false;
    value = static_cast<int>(parsed);
    return true;
}

std::wstring FormatMinutes(int minutes)
{
    minutes = ((minutes % 1440) + 1440) % 1440;
    wchar_t buffer[16];
    swprintf_s(buffer, L"%02d:%02d", minutes / 60, minutes % 60);
    return buffer;
}

// --------------------------------------------------------------------------
// Derived UI state
// --------------------------------------------------------------------------

ScheduleMode SelectedMode(HWND dialog)
{
    const LRESULT index = SendDlgItemMessageW(dialog, IDC_CMB_MODE, CB_GETCURSEL, 0, 0);
    switch (index)
    {
    case 1:
        return ScheduleMode::FixedHours;
    case 2:
        return ScheduleMode::SunsetToSunrise;
    case 3:
        return ScheduleMode::FollowNightLight;
    default:
        return ScheduleMode::Off;
    }
}

void RefreshEnabledState(HWND dialog)
{
    const ScheduleMode mode = SelectedMode(dialog);
    const bool fixedHours = (mode == ScheduleMode::FixedHours);
    const bool sunMode = (mode == ScheduleMode::SunsetToSunrise);

    EnableWindow(GetDlgItem(dialog, IDC_DTP_LIGHT), fixedHours);
    EnableWindow(GetDlgItem(dialog, IDC_DTP_DARK), fixedHours);

    EnableWindow(GetDlgItem(dialog, IDC_EDT_LAT), sunMode);
    EnableWindow(GetDlgItem(dialog, IDC_EDT_LON), sunMode);
    EnableWindow(GetDlgItem(dialog, IDC_EDT_SUNRISE_OFF), sunMode);
    EnableWindow(GetDlgItem(dialog, IDC_EDT_SUNSET_OFF), sunMode);
}

void RefreshSunInfo(HWND dialog)
{
    const ScheduleMode mode = SelectedMode(dialog);

    std::wstring text;
    if (mode == ScheduleMode::SunsetToSunrise)
    {
        double latitude = 0.0;
        double longitude = 0.0;
        const bool valid = ParseDouble(GetEditText(dialog, IDC_EDT_LAT), latitude) &&
                           ParseDouble(GetEditText(dialog, IDC_EDT_LON), longitude) &&
                           !(latitude == 0.0 && longitude == 0.0) &&
                           latitude >= -90.0 && latitude <= 90.0 &&
                           longitude >= -180.0 && longitude <= 180.0;

        if (valid)
        {
            SYSTEMTIME st{};
            GetLocalTime(&st);
            const SunTimes times = CalculateSunriseSunset(latitude, longitude, st.wYear, st.wMonth, st.wDay);
            text = L10n::F(Str::SunInfoToday,
                           FormatMinutes(times.sunriseHour * 60 + times.sunriseMinute),
                           FormatMinutes(times.sunsetHour * 60 + times.sunsetMinute));
        }
        else
        {
            text = L10n::T(Str::SunInfoNeedCoordinates);
        }
    }
    else if (mode == ScheduleMode::FollowNightLight)
    {
        text = L10n::T(IsNightLightEnabled() ? Str::SunInfoNightLightOn : Str::SunInfoNightLightOff);
    }
    else if (mode == ScheduleMode::FixedHours)
    {
        text = L10n::F(Str::SunInfoFixedHours,
                       FormatMinutes(GetTimeControl(dialog, IDC_DTP_LIGHT)),
                       FormatMinutes(GetTimeControl(dialog, IDC_DTP_DARK)));
    }
    else
    {
        text = L10n::T(Str::SunInfoOff);
    }

    SetWindowTextW(GetDlgItem(dialog, IDC_STATIC_SUN), text.c_str());
}

void RefreshCurrentStateText(HWND dialog)
{
    const std::wstring text = L10n::F(Str::CurrentState,
                                      L10n::T(GetCurrentSystemTheme() ? Str::WordLight : Str::WordDark),
                                      L10n::T(GetCurrentAppsTheme() ? Str::WordLight : Str::WordDark),
                                      L10n::T(IsNightLightEnabled() ? Str::WordOn : Str::WordOff));
    SetWindowTextW(GetDlgItem(dialog, IDC_STATIC_CURRENT), text.c_str());
}

void RefreshStartupText(HWND dialog)
{
    SetWindowTextW(GetDlgItem(dialog, IDC_STATIC_STARTUP), AutoStart::StatusText().c_str());
}

// --------------------------------------------------------------------------
// Populate / harvest
// --------------------------------------------------------------------------

void Populate(HWND dialog, const Config& config)
{
    SendDlgItemMessageW(dialog, IDC_CMB_MODE, CB_RESETCONTENT, 0, 0);
    for (Str label : kModeComboTexts)
        SendDlgItemMessageW(dialog, IDC_CMB_MODE, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(L10n::T(label)));
    SendDlgItemMessageW(dialog, IDC_CMB_MODE, CB_SETCURSEL, static_cast<WPARAM>(config.scheduleMode), 0);

    SetTimeControl(dialog, IDC_DTP_LIGHT, config.lightTime);
    SetTimeControl(dialog, IDC_DTP_DARK, config.darkTime);

    CheckDlgButton(dialog, IDC_CHK_SYSTEM, config.changeSystem ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dialog, IDC_CHK_APPS, config.changeApps ? BST_CHECKED : BST_UNCHECKED);

    SetEditText(dialog, IDC_EDT_LAT, config.latitude);
    SetEditText(dialog, IDC_EDT_LON, config.longitude);
    SetEditText(dialog, IDC_EDT_SUNRISE_OFF, std::to_wstring(config.sunriseOffset));
    SetEditText(dialog, IDC_EDT_SUNSET_OFF, std::to_wstring(config.sunsetOffset));

    SetHotkeyControl(dialog, config.hotkey);
    CheckDlgButton(dialog, IDC_CHK_STARTUP, config.startOnLogin ? BST_CHECKED : BST_UNCHECKED);

    PopulateLanguageCombo(dialog, config.language);

    RefreshEnabledState(dialog);
    RefreshSunInfo(dialog);
    RefreshCurrentStateText(dialog);
    RefreshStartupText(dialog);
}

// Returns false and shows a message box when the form is not usable.
bool Harvest(HWND dialog, Config& config)
{
    Config result = config;

    result.scheduleMode = SelectedMode(dialog);
    result.lightTime = GetTimeControl(dialog, IDC_DTP_LIGHT);
    result.darkTime = GetTimeControl(dialog, IDC_DTP_DARK);
    result.changeSystem = IsDlgButtonChecked(dialog, IDC_CHK_SYSTEM) == BST_CHECKED;
    result.changeApps = IsDlgButtonChecked(dialog, IDC_CHK_APPS) == BST_CHECKED;

    const std::wstring latitudeText = GetEditText(dialog, IDC_EDT_LAT);
    const std::wstring longitudeText = GetEditText(dialog, IDC_EDT_LON);
    double latitude = 0.0;
    double longitude = 0.0;
    const bool latOk = ParseDouble(latitudeText, latitude);
    const bool lonOk = ParseDouble(longitudeText, longitude);

    int sunriseOffset = 0;
    int sunsetOffset = 0;
    const bool sunriseOk = ParseInt(GetEditText(dialog, IDC_EDT_SUNRISE_OFF), sunriseOffset);
    const bool sunsetOk = ParseInt(GetEditText(dialog, IDC_EDT_SUNSET_OFF), sunsetOffset);

    const bool needsCoordinates = (result.scheduleMode == ScheduleMode::SunsetToSunrise);
    const bool coordinatesUsable = latOk && lonOk &&
                                   !(latitude == 0.0 && longitude == 0.0) &&
                                   latitude >= -90.0 && latitude <= 90.0 &&
                                   longitude >= -180.0 && longitude <= 180.0;

    if (needsCoordinates && !coordinatesUsable)
    {
        MessageBoxW(dialog,
                    L10n::T(Str::MsgInvalidLocationBody),
                    L10n::T(Str::MsgInvalidLocationTitle),
                    MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(dialog, IDC_EDT_LAT));
        return false;
    }

    if (!sunriseOk || !sunsetOk || sunriseOffset < -720 || sunriseOffset > 720 || sunsetOffset < -720 || sunsetOffset > 720)
    {
        MessageBoxW(dialog,
                    L10n::T(Str::MsgInvalidOffsetBody),
                    L10n::T(Str::MsgInvalidOffsetTitle),
                    MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(dialog, IDC_EDT_SUNRISE_OFF));
        return false;
    }

    if (!result.changeSystem && !result.changeApps)
    {
        const int answer = MessageBoxW(dialog,
                                       L10n::T(Str::MsgNothingToChangeBody),
                                       L10n::T(Str::MsgNothingToChangeTitle),
                                       MB_YESNO | MB_ICONQUESTION);
        if (answer != IDYES)
            return false;
    }

    if (latOk)
        result.latitude = latitudeText;
    if (lonOk)
        result.longitude = longitudeText;

    result.sunriseOffset = sunriseOffset;
    result.sunsetOffset = sunsetOffset;

    result.hotkey = GetHotkeyControl(dialog);
    result.startOnLogin = IsDlgButtonChecked(dialog, IDC_CHK_STARTUP) == BST_CHECKED;
    result.language = LanguageFromCombo(dialog);

    config = result;
    return true;
}

// --------------------------------------------------------------------------
// Dialog procedure
// --------------------------------------------------------------------------

INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    DialogContext* context = ContextOf(dialog);

    switch (message)
    {
    case WM_INITDIALOG:
    {
        auto* ctx = reinterpret_cast<DialogContext*>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(ctx));

        SendMessageW(dialog, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(LoadIconW(nullptr, IDI_APPLICATION)));

        // Order matters: the font decides how wide every label measures, so it
        // has to be in place before the button row is packed.
        if (ctx)
        {
            ctx->font = CreateDialogFont(dialog);
            ApplyFont(dialog, ctx->font);
        }

        ApplyLocalizedText(dialog);
        FitLabels(dialog);
        LayoutButtonRow(dialog);

        if (ctx)
            Populate(dialog, ctx->config);
        return TRUE;
    }

    case WM_COMMAND:
    {
        const int controlId = LOWORD(wParam);
        const int notification = HIWORD(wParam);

        switch (controlId)
        {
        case IDC_CMB_MODE:
            if (notification == CBN_SELCHANGE)
            {
                RefreshEnabledState(dialog);
                RefreshSunInfo(dialog);
            }
            return TRUE;

        case IDC_EDT_LAT:
        case IDC_EDT_LON:
            if (notification == EN_CHANGE)
                RefreshSunInfo(dialog);
            return TRUE;

        case IDC_DTP_LIGHT:
        case IDC_DTP_DARK:
            if (notification == DTN_DATETIMECHANGE)
                RefreshSunInfo(dialog);
            return TRUE;

        case IDC_BTN_TOGGLE:
            if (context && context->host)
                context->host->OnToggleNow();
            RefreshCurrentStateText(dialog);
            return TRUE;

        case IDC_BTN_RESTART_EXPLORER:
            if (context && context->host)
                context->host->OnRestartExplorer();
            return TRUE;

        case IDC_BTN_OPEN_FOLDER:
        {
            const std::wstring path = SettingsStore::Instance().ConfigFilePath();
            const auto pos = path.find_last_of(L"\\/");
            const std::wstring folder = (pos == std::wstring::npos) ? path : path.substr(0, pos);
            ShellExecuteW(dialog, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        }

        case IDC_BTN_RESET:
            Populate(dialog, Config{});
            return TRUE;

        case IDOK:
        {
            if (!context)
            {
                EndDialog(dialog, IDCANCEL);
                return TRUE;
            }

            Config updated = context->config;
            if (!Harvest(dialog, updated))
                return TRUE;

            // Compare the *resolved* languages so switching "Follow Windows"
            // to the language Windows is already using does not cause a
            // pointless rebuild.
            const bool languageChanged =
                L10n::Resolve(updated.language) != L10n::Resolve(context->config.language);

            context->config = updated;

            SettingsStore::Instance().SetAndSave(updated);
            AutoStart::Apply(updated.startOnLogin);

            if (languageChanged)
                L10n::Init(updated.language);

            if (context->host)
                context->host->OnSettingsSaved();

            EndDialog(dialog, languageChanged ? kReloadDialog : IDOK);
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;

        default:
            break;
        }
        return FALSE;
    }

    default:
        break;
    }

    return FALSE;
}

} // namespace

bool ShowSettingsDialog(HWND owner, ISettingsHost& host)
{
    // Saving a different interface language ends the dialog with kReloadDialog
    // so it can be rebuilt in the new language straight away. Looping here keeps
    // the modality and the single-instance hand-off intact.
    for (;;)
    {
        DialogContext context;
        context.host = &host;
        context.config = SettingsStore::Instance().Get();

        const INT_PTR result = DialogBoxParamW(GetModuleHandleW(nullptr),
                                              MAKEINTRESOURCEW(IDD_SETTINGS),
                                              owner,
                                              &DialogProc,
                                              reinterpret_cast<LPARAM>(&context));

        if (context.font)
            DeleteObject(context.font);

        if (result != kReloadDialog)
            return result == IDOK;
    }
}

} // namespace ls
