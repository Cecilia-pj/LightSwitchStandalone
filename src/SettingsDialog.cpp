// LightSwitch Standalone - settings dialog (implementation).
#include "SettingsDialog.h"

#include "AutoStart.h"
#include "Config.h"
#include "L10n.h"
#include "Logging.h"
#include "ThemeCatalog.h"
#include "dark/DarkApp.h"
#include "dark/darkui.h"
#include "resource.h"
#include "vendor/LightSwitchUtils.h"
#include "vendor/ThemeHelper.h"
#include "vendor/ThemeScheduler.h"

// <commdlg.h> for GetOpenFileNameW - the project builds with
// WIN32_LEAN_AND_MEAN, which takes the common dialogs out of <windows.h>.
#include <commctrl.h>
#include <commdlg.h>
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

// Theme combo order. Kept as two parallel arrays rather than one table of
// {label, value} pairs so both stay usable in a constexpr context on every
// compiler this project supports. kThemeValues holds the exact strings
// Config::NormalizeUiTheme() accepts, so a dropdown entry can never resolve to
// something settings.json would not round-trip.
constexpr Str kThemeComboTexts[] = {
    Str::ThemeAuto,
    Str::ThemeLight,
    Str::ThemeDark,
};

constexpr const wchar_t* kThemeValues[] = {
    L"auto",
    L"light",
    L"dark",
};

static_assert(sizeof(kThemeComboTexts) / sizeof(kThemeComboTexts[0]) ==
                  sizeof(kThemeValues) / sizeof(kThemeValues[0]),
              "kThemeComboTexts and kThemeValues must stay in step.");

// One row of a scheduled-theme dropdown. The entry is kept as a whole rather
// than as a bare path because the identity is what decides "same theme" - two
// files may carry the same DisplayName, and one theme may be installed twice.
struct ThemeEntry
{
    std::wstring path;
    std::wstring label;
    theme::ThemeIdentity identity;
};

// Item data of the two non-theme rows at the bottom of a dropdown.
constexpr LPARAM kUnsetEntry = -2;
constexpr LPARAM kBrowseEntry = -1;

struct DialogContext
{
    ISettingsHost* host = nullptr;
    Config config;
    HFONT font = nullptr; // owned; deleted once DialogBoxParamW returns

    // Filled by PopulateThemePairCombo. The combo boxes store indexes into
    // these vectors as item data, so a duplicate label can never make the code
    // pick the wrong file.
    std::vector<ThemeEntry> lightEntries;
    std::vector<ThemeEntry> darkEntries;

    // Last selected row of each dropdown. Only used to roll the selection back
    // when the file dialog behind "Browse..." is cancelled.
    int lastSelLight = -1;
    int lastSelDark = -1;
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
    { IDC_STATIC_GROUP_THEMES, Str::DialogGroupThemes },
    { IDC_STATIC_LBL_THEME_LIGHT, Str::DialogLabelLightTheme },
    { IDC_STATIC_LBL_THEME_DARK, Str::DialogLabelDarkTheme },
    { IDC_STATIC_GROUP_SUN, Str::DialogGroupSun },
    { IDC_STATIC_LBL_LAT, Str::DialogLabelLatitude },
    { IDC_STATIC_LBL_LON, Str::DialogLabelLongitude },
    { IDC_STATIC_LBL_SUNRISE_OFF, Str::DialogLabelSunriseOffset },
    { IDC_STATIC_LBL_SUNSET_OFF, Str::DialogLabelSunsetOffset },
    { IDC_STATIC_HINT_OFFSETS, Str::DialogSunOffsetHint },
    { IDC_STATIC_GROUP_SHORTCUT, Str::DialogGroupShortcut },
    { IDC_CHK_WIN, Str::DialogCheckWinKey },
    { IDC_STATIC_HINT_HOTKEY, Str::DialogHotkeyHint },
    { IDC_CHK_TRAY_CLICK, Str::DialogCheckTrayClick },
    { IDC_STATIC_GROUP_STARTUP, Str::DialogGroupStartup },
    { IDC_CHK_STARTUP, Str::DialogCheckStartup },
    { IDC_STATIC_GROUP_LANGUAGE, Str::DialogGroupLanguage },
    { IDC_STATIC_LBL_LANGUAGE, Str::DialogLabelLanguage },
    { IDC_STATIC_GROUP_THEME, Str::DialogGroupTheme },
    { IDC_STATIC_LBL_THEME, Str::DialogLabelTheme },
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
// Theme combo
// --------------------------------------------------------------------------
//
// The values that travel to and from settings.json are the same three strings
// Config::NormalizeUiTheme() knows, so the round trip is closed: whatever the
// combo produces, the file will read back unchanged on the next launch.

int ThemeComboIndex(const std::wstring& configured)
{
    constexpr int kCount = static_cast<int>(sizeof(kThemeValues) / sizeof(kThemeValues[0]));

    // Normalise first: "DARK", " dark " and "dark" all land on the same entry,
    // and a typo in the file falls back to "auto" - the same degradation the
    // config layer applies when it loads.
    const std::wstring normalized = NormalizeUiTheme(configured);
    for (int i = 0; i < kCount; ++i)
    {
        if (normalized == kThemeValues[i])
            return i;
    }
    return 0;
}

std::wstring ThemeFromCombo(HWND dialog)
{
    constexpr int kCount = static_cast<int>(sizeof(kThemeValues) / sizeof(kThemeValues[0]));

    const LRESULT index = SendDlgItemMessageW(dialog, IDC_CMB_THEME, CB_GETCURSEL, 0, 0);
    if (index < 0 || index >= kCount)
        return L"auto";
    return kThemeValues[index];
}

void PopulateThemeCombo(HWND dialog, const std::wstring& configured)
{
    SendDlgItemMessageW(dialog, IDC_CMB_THEME, CB_RESETCONTENT, 0, 0);
    for (Str label : kThemeComboTexts)
        SendDlgItemMessageW(dialog, IDC_CMB_THEME, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(L10n::T(label)));
    SendDlgItemMessageW(dialog, IDC_CMB_THEME, CB_SETCURSEL,
                        static_cast<WPARAM>(ThemeComboIndex(configured)), 0);
}

// --------------------------------------------------------------------------
// Scheduled theme pair
// --------------------------------------------------------------------------
//
// Unlike the interface-theme combo above, these two are not a fixed list. The
// rows come from the .theme files Windows can see in its two theme folders,
// plus the path the configuration already points at (which may live anywhere),
// plus a trailing "Browse..." row that opens a file dialog.
//
// The values that travel to settings.json are absolute file paths, not the
// display names shown here: the name is only a hint for the user, while the
// path is what actually gets applied.

std::vector<ThemeEntry> BuildThemeEntries(const std::wstring& selected)
{
    const std::wstring wanted = theme::NormalizePath(selected);

    theme::EnumerateOptions options;
    if (!wanted.empty())
        options.extraPaths.push_back(wanted);

    std::vector<ThemeEntry> entries;
    for (const theme::ThemeInfo& info : theme::Enumerate(options))
    {
        ThemeEntry entry;
        entry.path = info.path;
        entry.label = info.displayName;
        entry.identity = theme::ThemeIdentity{ info.themeId, info.path };
        if (info.fileMissing)
            entry.label += L10n::T(Str::DialogThemeMissingSuffix);
        entries.push_back(std::move(entry));
    }

    // Enumerate() de-duplicates by identity, so a configured path can be
    // swallowed by an identical file that happens to share its ThemeId. The
    // entry the user actually chose must stay selectable even then.
    if (!wanted.empty())
    {
        const theme::ThemeIdentity identity = theme::IdentifyPath(wanted);
        const bool present = std::any_of(entries.begin(), entries.end(),
                                         [&](const ThemeEntry& entry) {
                                             return theme::SameIdentity(entry.identity, identity);
                                         });
        if (!present)
        {
            const theme::ThemeInfo info = theme::DescribePath(wanted);
            ThemeEntry entry;
            entry.path = wanted;
            entry.label = info.displayName;
            entry.identity = theme::ThemeIdentity{ info.themeId, wanted };
            if (info.fileMissing)
                entry.label += L10n::T(Str::DialogThemeMissingSuffix);
            entries.push_back(std::move(entry));
        }
    }

    return entries;
}

void PopulateThemePairCombo(HWND dialog, int controlId, const std::wstring& selected,
                            std::vector<ThemeEntry>& entries, int& lastSelection)
{
    const HWND combo = GetDlgItem(dialog, controlId);
    if (!combo)
        return;

    entries = BuildThemeEntries(selected);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);

    // A fresh profile (or a 1.3.x upgrade, which can only seed the light side)
    // has nothing to show. An empty closed combo box reads like a bug, so an
    // explicit "(not set)" row takes the place of the missing selection.
    int selection = -1;
    if (selected.empty())
    {
        const LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0,
                                           reinterpret_cast<LPARAM>(L10n::T(Str::CurrentThemeUnset)));
        if (index >= 0)
        {
            SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(index),
                         static_cast<LPARAM>(kUnsetEntry));
            selection = static_cast<int>(index);
        }
    }

    const theme::ThemeIdentity wanted = selected.empty() ? theme::ThemeIdentity{}
                                                        : theme::IdentifyPath(selected);
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0,
                                           reinterpret_cast<LPARAM>(entries[i].label.c_str()));
        if (index < 0)
            continue;

        SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(index), static_cast<LPARAM>(i));
        if (selection < 0 && theme::SameIdentity(entries[i].identity, wanted))
            selection = static_cast<int>(index);
    }

    const LRESULT browseIndex = SendMessageW(combo, CB_ADDSTRING, 0,
                                             reinterpret_cast<LPARAM>(L10n::T(Str::DialogThemeBrowse)));
    if (browseIndex >= 0)
        SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(browseIndex),
                     static_cast<LPARAM>(kBrowseEntry));

    lastSelection = (selection >= 0) ? selection : 0;
    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(lastSelection), 0);
}

// Path behind a dropdown row, or an empty string for "(not set)" and
// "Browse...". Both sentinels are negative, and CB_ERR (-1) means the same
// thing here, so one comparison covers all three cases.
std::wstring ThemePathFromCombo(HWND dialog, int controlId, const std::vector<ThemeEntry>& entries)
{
    const HWND combo = GetDlgItem(dialog, controlId);
    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index < 0)
        return std::wstring();

    const LPARAM data = SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(index), 0);
    if (data < 0)
        return std::wstring();

    const size_t position = static_cast<size_t>(data);
    if (position >= entries.size())
        return std::wstring();
    return entries[position].path;
}

bool BrowseForThemeFile(HWND dialog, std::wstring& picked)
{
    wchar_t buffer[32768] = {};
    const std::wstring initial = theme::UserThemesDirectory();

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = dialog;
    ofn.lpstrFilter = L"Theme packs (*.theme)\0*.theme\0All files (*.*)\0*.*\0\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = ARRAYSIZE(buffer);
    ofn.lpstrTitle = L10n::T(Str::DialogThemeBrowseTitle);
    ofn.lpstrInitialDir = initial.empty() ? nullptr : initial.c_str();
    // OFN_NOCHANGEDIR: the process keeps a working directory for the rest of
    // its life, and a file dialog must not move it behind our back.
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn))
        return false;

    picked = theme::NormalizePath(buffer);
    return !picked.empty();
}

// A selection change on one of the two dropdowns. The only case that needs
// work is "Browse...": it is a command dressed as a list row, so it has to be
// undone when the user cancels the file dialog.
void HandleThemePairSelection(HWND dialog, DialogContext& context, bool lightSide)
{
    const int controlId = lightSide ? IDC_CMB_THEME_LIGHT : IDC_CMB_THEME_DARK;
    std::vector<ThemeEntry>& entries = lightSide ? context.lightEntries : context.darkEntries;
    int& lastSelection = lightSide ? context.lastSelLight : context.lastSelDark;

    const HWND combo = GetDlgItem(dialog, controlId);
    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index < 0)
        return;

    if (SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(index), 0) != kBrowseEntry)
    {
        lastSelection = static_cast<int>(index);
        return;
    }

    std::wstring picked;
    if (!BrowseForThemeFile(dialog, picked))
    {
        // Cancelled. Leaving "Browse..." selected would look like a choice the
        // next Save would then try to apply.
        SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(lastSelection), 0);
        return;
    }

    // The file was just picked, so it can only be a few seconds old at most -
    // well inside the catalogue's cache window.
    theme::InvalidateCache();
    PopulateThemePairCombo(dialog, controlId, picked, entries, lastSelection);
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

// Which side of the schedule is active at this minute. Reads the values the
// dialog currently shows rather than the settings file, so the status line
// follows the dropdowns and the time pickers while they are being edited.
//
// This mirrors Scheduler::ScheduleWantsDark() on purpose. The dialog holds no
// scheduler handle - the worker thread owns that state, and reading it from a
// repaint would only add a lock to the message loop - while the rules
// themselves are four lines of arithmetic that cannot drift silently.
bool DialogTargetsDark(HWND dialog)
{
    const ScheduleMode mode = SelectedMode(dialog);

    if (mode == ScheduleMode::FollowNightLight)
        return IsNightLightEnabled();

    int light = GetTimeControl(dialog, IDC_DTP_LIGHT);
    int dark = GetTimeControl(dialog, IDC_DTP_DARK);

    if (mode == ScheduleMode::SunsetToSunrise)
    {
        double latitude = 0.0;
        double longitude = 0.0;
        int sunriseOffset = 0;
        int sunsetOffset = 0;

        const bool usable =
            ParseDouble(GetEditText(dialog, IDC_EDT_LAT), latitude) &&
            ParseDouble(GetEditText(dialog, IDC_EDT_LON), longitude) &&
            ParseInt(GetEditText(dialog, IDC_EDT_SUNRISE_OFF), sunriseOffset) &&
            ParseInt(GetEditText(dialog, IDC_EDT_SUNSET_OFF), sunsetOffset) &&
            !(latitude == 0.0 && longitude == 0.0) &&
            latitude >= -90.0 && latitude <= 90.0 &&
            longitude >= -180.0 && longitude <= 180.0;

        if (usable)
        {
            SYSTEMTIME today{};
            GetLocalTime(&today);
            const SunTimes times = CalculateSunriseSunset(latitude, longitude,
                                                          today.wYear, today.wMonth, today.wDay);
            light = times.sunriseHour * 60 + times.sunriseMinute + sunriseOffset;
            dark = times.sunsetHour * 60 + times.sunsetMinute + sunsetOffset;
        }
        // Unusable coordinates fall back to the two time pickers, exactly like
        // the scheduler does while it waits for a valid location.
    }

    return !ShouldBeLight(GetNowMinutes(), light, dark);
}

void RefreshCurrentStateText(HWND dialog, DialogContext* context)
{
    // Line 1: what Windows is running right now. Read from CurrentTheme rather
    // than from the scheduler's cached identity, because the user may have
    // switched themes from Windows Settings while this dialog was open.
    const theme::ThemeIdentity current = theme::ReadCurrentIdentity();
    const std::wstring nowText = current.path.empty()
                                     ? std::wstring(L10n::T(Str::CurrentThemeUnset))
                                     : theme::DisplayNameOfPath(current.path);

    // Line 2: the pack the schedule is asking for. With the schedule off there
    // is no such thing, so the pair is read the other way round: it names
    // whichever of the two packs is on screen, which is the side a manual
    // toggle would flip away from.
    std::wstring targetText = L10n::T(Str::CurrentThemeUnset);
    if (context)
    {
        bool darkSide = false;
        if (SelectedMode(dialog) == ScheduleMode::Off)
        {
            const std::wstring darkPath = ThemePathFromCombo(dialog, IDC_CMB_THEME_DARK,
                                                            context->darkEntries);
            darkSide = !darkPath.empty() &&
                       theme::SameIdentity(current, theme::IdentifyPath(darkPath));
        }
        else
        {
            darkSide = DialogTargetsDark(dialog);
        }

        const int controlId = darkSide ? IDC_CMB_THEME_DARK : IDC_CMB_THEME_LIGHT;
        const std::vector<ThemeEntry>& entries = darkSide ? context->darkEntries
                                                          : context->lightEntries;
        const std::wstring targetPath = ThemePathFromCombo(dialog, controlId, entries);
        if (!targetPath.empty())
            targetText = theme::DisplayNameOfPath(targetPath);
    }

    const std::wstring text = L10n::F(Str::CurrentThemePair, nowText, targetText);
    SetWindowTextW(GetDlgItem(dialog, IDC_STATIC_CURRENT), text.c_str());
}

void RefreshStartupText(HWND dialog)
{
    SetWindowTextW(GetDlgItem(dialog, IDC_STATIC_STARTUP), AutoStart::StatusText().c_str());
}

// --------------------------------------------------------------------------
// Populate / harvest
// --------------------------------------------------------------------------

void Populate(HWND dialog, DialogContext& context, const Config& config)
{
    SendDlgItemMessageW(dialog, IDC_CMB_MODE, CB_RESETCONTENT, 0, 0);
    for (Str label : kModeComboTexts)
        SendDlgItemMessageW(dialog, IDC_CMB_MODE, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(L10n::T(label)));
    SendDlgItemMessageW(dialog, IDC_CMB_MODE, CB_SETCURSEL, static_cast<WPARAM>(config.scheduleMode), 0);

    SetTimeControl(dialog, IDC_DTP_LIGHT, config.lightTime);
    SetTimeControl(dialog, IDC_DTP_DARK, config.darkTime);

    PopulateThemePairCombo(dialog, IDC_CMB_THEME_LIGHT, config.themeA,
                           context.lightEntries, context.lastSelLight);
    PopulateThemePairCombo(dialog, IDC_CMB_THEME_DARK, config.themeB,
                           context.darkEntries, context.lastSelDark);

    SetEditText(dialog, IDC_EDT_LAT, config.latitude);
    SetEditText(dialog, IDC_EDT_LON, config.longitude);
    SetEditText(dialog, IDC_EDT_SUNRISE_OFF, std::to_wstring(config.sunriseOffset));
    SetEditText(dialog, IDC_EDT_SUNSET_OFF, std::to_wstring(config.sunsetOffset));

    SetHotkeyControl(dialog, config.hotkey);
    CheckDlgButton(dialog, IDC_CHK_STARTUP, config.startOnLogin ? BST_CHECKED : BST_UNCHECKED);

    PopulateLanguageCombo(dialog, config.language);
    PopulateThemeCombo(dialog, config.uiTheme);
    CheckDlgButton(dialog, IDC_CHK_TRAY_CLICK, config.trayClickTogglesTheme ? BST_CHECKED : BST_UNCHECKED);

    RefreshEnabledState(dialog);
    RefreshSunInfo(dialog);
    RefreshCurrentStateText(dialog, &context);
    RefreshStartupText(dialog);
}

// Returns false and shows a message box when the form is not usable.
bool Harvest(HWND dialog, DialogContext& context, Config& config)
{
    Config result = config;

    result.scheduleMode = SelectedMode(dialog);
    result.lightTime = GetTimeControl(dialog, IDC_DTP_LIGHT);
    result.darkTime = GetTimeControl(dialog, IDC_DTP_DARK);
    // changeSystem / changeApps are deliberately not read back: since 1.4.0 the
    // .theme pack carries both modes, so the two historic switches are inert.
    // They keep whatever value the file had, which is what lets a downgrade to
    // 1.3.x still behave the way that file says.

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

    // ---- the two scheduled packs -----------------------------------------
    // A schedule with only one pack would flip the machine to "nothing" every
    // time it crossed the other boundary, so both sides are required - but only
    // while the schedule is on. With it off the pair is reached only through
    // "Toggle now", and refusing to save would lock the rest of the dialog for
    // anyone who just wants the hotkey.
    const std::wstring lightPath = ThemePathFromCombo(dialog, IDC_CMB_THEME_LIGHT, context.lightEntries);
    const std::wstring darkPath = ThemePathFromCombo(dialog, IDC_CMB_THEME_DARK, context.darkEntries);

    if (result.scheduleMode != ScheduleMode::Off && (lightPath.empty() || darkPath.empty()))
    {
        MessageBoxW(dialog,
                    L10n::T(Str::MsgNoThemesBody),
                    L10n::T(Str::MsgNoThemesTitle),
                    MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(dialog, lightPath.empty() ? IDC_CMB_THEME_LIGHT : IDC_CMB_THEME_DARK));
        return false;
    }

    // A path that no longer resolves is worth a question rather than a refusal:
    // it may be a theme on a drive that is simply offline right now, and the
    // rest of the configuration is still perfectly valid.
    std::wstring missingPath;
    if (!lightPath.empty() && theme::DescribePath(lightPath).fileMissing)
        missingPath = lightPath;
    else if (!darkPath.empty() && theme::DescribePath(darkPath).fileMissing)
        missingPath = darkPath;

    if (!missingPath.empty())
    {
        const int answer = MessageBoxW(dialog,
                                       L10n::F(Str::MsgThemeMissingBody, missingPath).c_str(),
                                       L10n::T(Str::MsgThemeMissingTitle),
                                       MB_YESNO | MB_ICONWARNING);
        if (answer != IDYES)
            return false;
    }

    if (!lightPath.empty() && !darkPath.empty() &&
        theme::SameIdentity(theme::IdentifyPath(lightPath), theme::IdentifyPath(darkPath)))
    {
        const int answer = MessageBoxW(dialog,
                                       L10n::T(Str::MsgSameThemeBody),
                                       L10n::T(Str::MsgSameThemeTitle),
                                       MB_YESNO | MB_ICONQUESTION);
        if (answer != IDYES)
            return false;
    }

    // Keep the configured spelling when the selection resolves to the same
    // pack. The catalogue de-duplicates by identity, so a theme installed twice
    // can be listed under the copy's path while the file still names the
    // original - saving must not rewrite a path the user never touched.
    auto keepExisting = [](const std::wstring& existing, const std::wstring& selected) {
        if (!existing.empty() && !selected.empty() &&
            theme::SameIdentity(theme::IdentifyPath(existing), theme::IdentifyPath(selected)))
            return existing;
        return selected;
    };
    result.themeA = keepExisting(config.themeA, lightPath);
    result.themeB = keepExisting(config.themeB, darkPath);

    if (latOk)
        result.latitude = latitudeText;
    if (lonOk)
        result.longitude = longitudeText;

    result.sunriseOffset = sunriseOffset;
    result.sunsetOffset = sunsetOffset;

    result.hotkey = GetHotkeyControl(dialog);
    result.startOnLogin = IsDlgButtonChecked(dialog, IDC_CHK_STARTUP) == BST_CHECKED;
    result.language = LanguageFromCombo(dialog);
    result.uiTheme = ThemeFromCombo(dialog);
    result.trayClickTogglesTheme = IsDlgButtonChecked(dialog, IDC_CHK_TRAY_CLICK) == BST_CHECKED;

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
        {
            // A theme imported while the dialog was closed has to show up on
            // the first open, not thirty seconds later.
            theme::InvalidateCache();
            Populate(dialog, *ctx, ctx->config);
        }

        // ---- dark mode ---------------------------------------------------
        // The three steps below are ordered, and the order is not symmetric:
        //   SetFromConfig  - hand the saved "uiTheme" to the module;
        //   InstallControls- subclass the controls (darkui + DarkControls);
        //   ApplyNow       - build the palette and repaint the tree.
        // Installing *after* applying would leave the controls themed on paper
        // but never repainted, which looks exactly like "dark mode does not
        // work" and reports no error anywhere. See src/dark/DarkApp.h.
        if (ctx)
            DarkApp::SetFromConfig(ctx->config.uiTheme);
        DarkApp::InstallControls(dialog);
        DarkApp::ApplyNow(dialog);

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

        case IDC_CMB_THEME:
            if (notification == CBN_SELCHANGE)
            {
                // Live preview. ApplyNow rather than ApplyIfNeeded: this path
                // also has to FlushMenuThemes, so that a tray popup opened
                // later in the session picks up the scheme just chosen. The
                // return value of SetFromConfig is deliberately ignored - even
                // when the resolved theme did not change (auto -> light while
                // Windows is already light) the module still needs the call so
                // its state matches the combo.
                DarkApp::SetFromConfig(ThemeFromCombo(dialog));
                DarkApp::ApplyNow(dialog);
            }
            return TRUE;

        case IDC_CMB_THEME_LIGHT:
        case IDC_CMB_THEME_DARK:
            if (notification == CBN_SELCHANGE && context)
            {
                HandleThemePairSelection(dialog, *context, controlId == IDC_CMB_THEME_LIGHT);
                RefreshCurrentStateText(dialog, context);
            }
            return TRUE;

        case IDC_BTN_TOGGLE:
            if (context && context->host)
                context->host->OnToggleNow();
            RefreshCurrentStateText(dialog, context);
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
        {
            const Config defaults = Config{};
            if (context)
                Populate(dialog, *context, defaults);

            // The theme is previewed live, so restoring the defaults has to
            // move the preview as well - otherwise the combo would read
            // "Follow Windows" while the dialog went on showing the pinned
            // theme that was on screen a moment ago.
            DarkApp::SetFromConfig(defaults.uiTheme);
            DarkApp::ApplyNow(dialog);
            return TRUE;
        }

        case IDOK:
        {
            if (!context)
            {
                EndDialog(dialog, IDCANCEL);
                return TRUE;
            }

            Config updated = context->config;
            if (!Harvest(dialog, *context, updated))
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

            // Re-assert the saved theme and force a full Apply. The live
            // preview has normally left the module on the right scheme already,
            // but Apply is also what calls FlushMenuThemes, and without that a
            // tray popup opened while the dialog was dark would keep the dark
            // menu after switching back to light. ApplyNow, never conditional.
            DarkApp::SetFromConfig(updated.uiTheme);
            DarkApp::ApplyNow(dialog);

            EndDialog(dialog, languageChanged ? kReloadDialog : IDOK);
            return TRUE;
        }

        case IDCANCEL:
            // Undo a live preview. The combo may have been moved without the
            // user saving, and the module would otherwise keep that scheme for
            // the rest of the session - the tray menu in particular.
            if (context)
            {
                DarkApp::SetFromConfig(context->config.uiTheme);
                DarkApp::ApplyNow(dialog);
            }
            EndDialog(dialog, IDCANCEL);
            return TRUE;

        default:
            break;
        }
        return FALSE;
    }

    // ----------------------------------------------------------------------
    // Dark mode plumbing
    // ----------------------------------------------------------------------
    // These messages are simply forwarded to the interface layer; it owns the
    // drawing. Nothing here knows what a dark control looks like.

    case WM_ERASEBKGND:
    {
        // A #32770 dialog would erase itself from the brush returned by
        // WM_CTLCOLORDLG, which is forwarded below and works. Doing the fill
        // here as well costs one FillRect and removes any dependence on
        // DefDlgProc's brush lookup - and this is the message the skill's own
        // checklist asks a host to handle.
        HBRUSH background = DarkApp::BgBrush();
        if (!background)
            return TRUE;

        HDC dc = reinterpret_cast<HDC>(wParam);
        RECT client{};
        GetClientRect(dialog, &client);
        FillRect(dc, &client, background);
        return TRUE;
    }

    case WM_DRAWITEM:
    {
        LRESULT result = 0;
        if (darkui::HandleDrawItem(lParam, &result))
            return result;
        return FALSE;
    }

    case WM_MEASUREITEM:
    {
        LRESULT result = 0;
        if (darkui::HandleMeasureItem(lParam, &result))
            return result;
        return FALSE;
    }

    case WM_NOTIFY:
    {
        LRESULT result = 0;
        if (darkui::HandleNotify(lParam, &result))
            return result;
        return FALSE;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    {
        // Note this is the *dialog's* copy of the message. A ComboBox sends
        // these to itself instead, which is why the interface layer subclasses
        // the ComboBox and why the two new combos work without anything here.
        LRESULT result = 0;
        if (darkui::HandleCtlColor(message, wParam, &result))
            return result;
        return FALSE;
    }

    // Windows switched between its light and dark app theme while this dialog
    // was open. ApplyIfNeeded is a no-op when the user pinned a theme, so a
    // pinned dialog stays pinned instead of following Windows.
    case WM_SETTINGCHANGE:
        if (DarkApp::IsColorSchemeChange(lParam))
            DarkApp::ApplyIfNeeded(dialog);
        break;

    // Raised by uxtheme when the theme data itself is reloaded - a colour
    // scheme change, a high contrast toggle, a theme being applied. Conditional
    // for the same reason as above: when the resolved theme did not move there
    // is nothing to rebuild, and in particular no stale menu-theme cache to
    // flush, because the cache is keyed on the theme that is still current.
    case WM_THEMECHANGED:
        DarkApp::ApplyIfNeeded(dialog);
        return 0;

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
