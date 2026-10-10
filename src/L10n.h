// LightSwitch Standalone - user interface localisation.
//
// Design notes
// ------------
//   * Every user-visible string lives in ONE table in L10n.cpp, keyed by the
//     Str enumeration and carrying a symbolic name, the English text and the
//     Simplified Chinese text. Nothing else in the project hard-codes a
//     user-facing string. A static_assert in L10n.cpp keeps the enumeration and
//     the table in lockstep.
//   * The table is compiled into the binary. There is no resource lookup, no
//     resource language to resolve and nothing that can fail at runtime.
//     English is the source language and the hard fallback for every id.
//   * The dialog template in LightSwitch.rc deliberately carries geometry and
//     control ids only - not text. That keeps a single source of truth and
//     avoids putting non-ASCII literals in a .rc file, where the resource
//     compiler's code page handling is a classic source of mojibake. (This
//     project compiles every translation unit with /utf-8, so wide literals in
//     a .cpp are unambiguous.)
//   * The log file is NOT translated on purpose: it is a developer artefact and
//     keeping it in one language makes two bug reports comparable.
#pragma once

#include <windows.h>

#include <string>
#include <utility>
#include <vector>

namespace ls
{

enum class Lang
{
    EnUs = 0,
    ZhCn = 1
};

// One id per user-visible string. Order must match kStrings in L10n.cpp.
enum class Str
{
    ProductName = 0,

    // ---- settings dialog: static labels --------------------------------
    DialogGroupSchedule,
    DialogLabelMode,
    DialogLabelLightStarts,
    DialogLabelDarkStarts,
    DialogGroupApplyTo,
    DialogCheckSystem,
    DialogCheckApps,
    DialogGroupSun,
    DialogLabelLatitude,
    DialogLabelLongitude,
    DialogLabelSunriseOffset,
    DialogLabelSunsetOffset,
    DialogSunOffsetHint,
    DialogGroupLanguage,
    DialogLabelLanguage,
    DialogGroupShortcut,
    DialogCheckWinKey,
    DialogHotkeyHint,
    DialogCheckTrayClick,
    DialogGroupStartup,
    DialogCheckStartup,
    DialogButtonToggleNow,
    DialogButtonRestartExplorer,
    DialogButtonOpenFolder,
    DialogButtonRestoreDefaults,
    DialogButtonSave,
    DialogButtonCancel,
    LanguageAuto,

    // ---- settings dialog: interface theme ------------------------------
    DialogGroupTheme,
    DialogLabelTheme,
    ThemeAuto,
    ThemeLight,
    ThemeDark,

    // ---- schedule mode names ------------------------------------------
    ModeOff,
    ModeFixedHours,
    ModeSunsetToSunrise,
    ModeFollowNightLight,

    // ---- settings dialog: live status lines ---------------------------
    SunInfoToday,
    SunInfoNeedCoordinates,
    SunInfoNightLightOn,
    SunInfoNightLightOff,
    SunInfoFixedHours,
    SunInfoOff,
    CurrentState,
    WordLight,
    WordDark,
    WordOn,
    WordOff,

    // ---- validation message boxes -------------------------------------
    MsgInvalidLocationTitle,
    MsgInvalidLocationBody,
    MsgInvalidOffsetTitle,
    MsgInvalidOffsetBody,
    MsgNothingToChangeTitle,
    MsgNothingToChangeBody,

    // ---- tray menu -----------------------------------------------------
    TrayToggleNow,
    TraySettings,
    TrayReloadConfig,
    TrayOpenConfigFolder,
    TrayRestartExplorer,
    TrayOpenLog,
    TrayExit,

    // ---- tray tooltip --------------------------------------------------
    TooltipFixed,
    TooltipSun,
    TooltipManualOverride,

    // ---- Explorer restart ----------------------------------------------
    MsgRestartExplorerTitle,
    MsgRestartExplorerBody,
    MsgRestartExplorerFailed,

    // ---- "start with Windows" status ------------------------------------
    AutoStartNotRegistered,
    AutoStartRegistered,
    AutoStartElsewhere,

    // ---- command line help ----------------------------------------------
    UsageBody,

    Count
};

namespace L10n
{
// Activate the language named by a settings.json / command line value.
// Accepts "auto" (follow the Windows UI language), "en-US", "en", "zh-CN",
// "zh", "zh-Hans" - case insensitive. Anything unrecognised behaves like
// "auto". Also records the *configured* value, so RefreshFromSystem() knows
// whether the user asked to follow Windows.
void Init(const std::wstring& code);

// Force one specific language. Returns true when it actually changed.
bool SetLanguage(Lang lang);

// Re-resolve "auto" against the current Windows UI language. Returns true when
// the active language changed as a result. Does nothing when the user picked a
// language explicitly.
bool RefreshFromSystem();

Lang Current();
const wchar_t* CurrentCode();

// True when the configured value is "auto" (or unset).
bool IsAutoConfigured();

// True when `code` means "follow Windows".
bool IsAuto(const std::wstring& code);

// Every language this build ships, in dropdown order.
const std::vector<Lang>& All();
const wchar_t* CodeOf(Lang lang);

// Name of a language written in that language ("English", "简体中文").
const wchar_t* DisplayName(Lang lang);

// "Follow Windows" / "跟随 Windows", written in the *active* language.
const wchar_t* AutoEntryName();

// Resolve a settings.json value to a concrete language.
Lang Resolve(const std::wstring& code);

// Canonical code for a settings.json value ("auto" stays "auto").
std::wstring Normalize(const std::wstring& code);

// The string for `id` in the active language. Never null.
const wchar_t* T(Str id);

// ---- argument collection for the variadic F() below ---------------------
// Declared before F() because unqualified lookup in a template happens at the
// point of definition, not at the point of instantiation.
inline std::wstring ToWString(const std::wstring& value) { return value; }
inline std::wstring ToWString(const wchar_t* value) { return value ? std::wstring(value) : std::wstring(); }
inline std::wstring ToWString(int value) { return std::to_wstring(value); }
inline std::wstring ToWString(unsigned int value) { return std::to_wstring(value); }
inline std::wstring ToWString(long value) { return std::to_wstring(value); }
inline std::wstring ToWString(long long value) { return std::to_wstring(value); }

inline void Collect(std::vector<std::wstring>&) {}

template <typename First, typename... Rest>
void Collect(std::vector<std::wstring>& out, First&& first, Rest&&... rest)
{
    out.push_back(ToWString(std::forward<First>(first)));
    Collect(out, std::forward<Rest>(rest)...);
}

// T() with %1..%9 substitution. A placeholder with no matching argument is left
// verbatim, so a translation mistake is visible instead of silently eaten.
//
// Format() is the implementation and is deliberately NOT called F(): a variadic
// F() forwarding to an overload of itself would lose to its own forwarding
// reference during overload resolution, which is a hard error rather than a
// silent one only because both are templates.
std::wstring Format(Str id, const std::vector<std::wstring>& args);

template <typename... Args>
std::wstring F(Str id, Args&&... args)
{
    std::vector<std::wstring> collected;
    collected.reserve(sizeof...(Args));
    Collect(collected, std::forward<Args>(args)...);
    return Format(id, collected);
}

// Face name the settings dialog should use, so CJK text is rendered by a font
// that actually has the glyphs instead of relying on font linking.
const wchar_t* UiFontFace();

// Whole table, every language, in a text form - used by --dump-strings so the
// translation can be diffed and machine-checked. One "name<TAB>value" line per
// string, with \, \r, \n and \t escaped so a multi-line message box string
// cannot break the line-oriented format.
std::wstring DumpAll();
} // namespace L10n

} // namespace ls
