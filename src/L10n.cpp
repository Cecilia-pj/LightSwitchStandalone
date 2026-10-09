// LightSwitch Standalone - user interface localisation (implementation).
//
// This file is the single source of truth for every string the user can read.
// To add a language: append a column to Entry, extend the switch in TextOf(),
// add it to kLanguages and to Lang in L10n.h. Nothing else needs to change.
#include "L10n.h"

#include <algorithm>
#include <atomic>

namespace ls
{

namespace
{

struct Entry
{
    Str id;
    const wchar_t* name; // symbolic name, used by --dump-strings
    const wchar_t* en;
    const wchar_t* zh;
};

// Order MUST match the Str enumeration - enforced by the static_asserts below.
constexpr Entry kStrings[] = {
    { Str::ProductName, L"ProductName", L"Light Switch Standalone", L"LightSwitch 独立版" },

    // ---- settings dialog: static labels ---------------------------------
    { Str::DialogGroupSchedule, L"DialogGroupSchedule", L"Schedule", L"时间计划" },
    { Str::DialogLabelMode, L"DialogLabelMode", L"Mode:", L"模式：" },
    { Str::DialogLabelLightStarts, L"DialogLabelLightStarts", L"Light theme starts at:", L"浅色主题开始时间：" },
    { Str::DialogLabelDarkStarts, L"DialogLabelDarkStarts", L"Dark theme starts at:", L"深色主题开始时间：" },
    { Str::DialogGroupApplyTo, L"DialogGroupApplyTo", L"Apply to", L"生效范围" },
    { Str::DialogCheckSystem, L"DialogCheckSystem", L"Windows system (taskbar, Start, shell)", L"Windows 系统（任务栏、开始菜单、外壳）" },
    { Str::DialogCheckApps, L"DialogCheckApps", L"Applications", L"应用程序" },
    { Str::DialogGroupSun, L"DialogGroupSun", L"Sunrise / sunset", L"日出 / 日落" },
    { Str::DialogLabelLatitude, L"DialogLabelLatitude", L"Latitude:", L"纬度：" },
    { Str::DialogLabelLongitude, L"DialogLabelLongitude", L"Longitude:", L"经度：" },
    { Str::DialogLabelSunriseOffset, L"DialogLabelSunriseOffset", L"Sunrise offset (min):", L"日出偏移（分钟）：" },
    { Str::DialogLabelSunsetOffset, L"DialogLabelSunsetOffset", L"Sunset offset (min):", L"日落偏移（分钟）：" },
    { Str::DialogSunOffsetHint, L"DialogSunOffsetHint",
      L"Offsets may be negative. The times above are ignored while 'Sunset to sunrise' is selected.",
      L"偏移可以是负数。选择「日出到日落」后，上面的时间会被忽略。" },
    { Str::DialogGroupLanguage, L"DialogGroupLanguage", L"Interface language", L"界面语言" },
    { Str::DialogLabelLanguage, L"DialogLabelLanguage", L"Language:", L"语言：" },
    { Str::DialogGroupShortcut, L"DialogGroupShortcut", L"Shortcut", L"快捷键" },
    { Str::DialogCheckWinKey, L"DialogCheckWinKey", L"Include Win key", L"包含 Win 键" },
    { Str::DialogHotkeyHint, L"DialogHotkeyHint",
      L"Win+ combinations are reserved by Windows; a keyboard hook is used instead.",
      L"Win+ 组合键由 Windows 保留；此时改用键盘钩子。" },
    { Str::DialogGroupStartup, L"DialogGroupStartup", L"Startup", L"开机启动" },
    { Str::DialogCheckStartup, L"DialogCheckStartup", L"Start automatically when I sign in", L"登录时自动启动" },
    { Str::DialogButtonToggleNow, L"DialogButtonToggleNow", L"Toggle now", L"立即切换" },
    { Str::DialogButtonRestartExplorer, L"DialogButtonRestartExplorer", L"Restart Explorer", L"重启资源管理器" },
    { Str::DialogButtonOpenFolder, L"DialogButtonOpenFolder", L"Open folder", L"打开目录" },
    { Str::DialogButtonRestoreDefaults, L"DialogButtonRestoreDefaults", L"Restore defaults", L"恢复默认" },
    { Str::DialogButtonSave, L"DialogButtonSave", L"Save", L"保存" },
    { Str::DialogButtonCancel, L"DialogButtonCancel", L"Cancel", L"取消" },
    { Str::LanguageAuto, L"LanguageAuto", L"Follow Windows", L"跟随 Windows" },

    // ---- settings dialog: interface theme --------------------------------
    // ThemeAuto reads the same as LanguageAuto, but keeps its own id: the two
    // dropdowns are free to diverge later, and distinct ids keep the
    // --dump-strings output readable.
    { Str::DialogGroupTheme, L"DialogGroupTheme", L"Theme", L"界面主题" },
    { Str::DialogLabelTheme, L"DialogLabelTheme", L"Theme:", L"主题：" },
    { Str::ThemeAuto, L"ThemeAuto", L"Follow Windows", L"跟随 Windows" },
    { Str::ThemeLight, L"ThemeLight", L"Light", L"浅色" },
    { Str::ThemeDark, L"ThemeDark", L"Dark", L"深色" },

    // ---- schedule mode names --------------------------------------------
    { Str::ModeOff, L"ModeOff", L"Off", L"关闭" },
    { Str::ModeFixedHours, L"ModeFixedHours", L"Fixed hours", L"固定时间" },
    { Str::ModeSunsetToSunrise, L"ModeSunsetToSunrise", L"Sunset to sunrise", L"日出到日落" },
    { Str::ModeFollowNightLight, L"ModeFollowNightLight", L"Follow Night Light", L"跟随夜灯" },

    // ---- settings dialog: live status lines ------------------------------
    { Str::SunInfoToday, L"SunInfoToday",
      L"Today: sunrise %1, sunset %2  (local time, before offsets)",
      L"今天：日出 %1，日落 %2（本地时间，未含偏移）" },
    { Str::SunInfoNeedCoordinates, L"SunInfoNeedCoordinates",
      L"Enter a latitude (-90..90) and longitude (-180..180), excluding 0/0.",
      L"请输入纬度（-90..90）和经度（-180..180），且不能同时为 0。" },
    { Str::SunInfoNightLightOn, L"SunInfoNightLightOn",
      L"Night Light is currently ON -> the theme follows the dark side of the toggle.",
      L"夜灯当前为「开」 -> 主题跟随该开关的深色一侧。" },
    { Str::SunInfoNightLightOff, L"SunInfoNightLightOff",
      L"Night Light is currently OFF.",
      L"夜灯当前为「关」。" },
    { Str::SunInfoFixedHours, L"SunInfoFixedHours",
      L"Light between %1 and %2.",
      L"浅色时段：%1 至 %2。" },
    { Str::SunInfoOff, L"SunInfoOff",
      L"Off: the theme is never changed automatically.",
      L"已关闭：主题不会被自动切换。" },
    { Str::CurrentState, L"CurrentState",
      L"Current: system %1, apps %2\r\nNight Light %3",
      L"当前：系统 %1，应用 %2\r\n夜灯 %3" },
    { Str::WordLight, L"WordLight", L"light", L"浅色" },
    { Str::WordDark, L"WordDark", L"dark", L"深色" },
    { Str::WordOn, L"WordOn", L"on", L"开" },
    { Str::WordOff, L"WordOff", L"off", L"关" },

    // ---- validation message boxes ---------------------------------------
    { Str::MsgInvalidLocationTitle, L"MsgInvalidLocationTitle", L"Invalid location", L"位置无效" },
    { Str::MsgInvalidLocationBody, L"MsgInvalidLocationBody",
      L"Sunset-to-sunrise mode needs a real location.\n\n"
      L"Latitude must be a number between -90 and 90, longitude between -180 and 180, "
      L"and the pair must not be 0/0.",
      L"「日出到日落」模式需要一个真实的位置。\n\n"
      L"纬度必须是 -90 到 90 之间的数字，经度必须是 -180 到 180 之间，"
      L"且两者不能同时为 0。" },
    { Str::MsgInvalidOffsetTitle, L"MsgInvalidOffsetTitle", L"Invalid offset", L"偏移无效" },
    { Str::MsgInvalidOffsetBody, L"MsgInvalidOffsetBody",
      L"Offsets must be whole minutes between -720 and 720.",
      L"偏移必须是 -720 到 720 之间的整数分钟。" },
    { Str::MsgNothingToChangeTitle, L"MsgNothingToChangeTitle", L"Nothing to change", L"没有切换对象" },
    { Str::MsgNothingToChangeBody, L"MsgNothingToChangeBody",
      L"Neither \"Windows system\" nor \"Applications\" is selected, so no theme will ever change.\n\n"
      L"Save anyway?",
      L"「Windows 系统」和「应用程序」都没有勾选，主题将永远不会被切换。\n\n"
      L"仍然保存吗？" },

    // ---- tray menu -------------------------------------------------------
    { Str::TrayToggleNow, L"TrayToggleNow", L"Toggle theme now", L"立即切换主题" },
    { Str::TraySettings, L"TraySettings", L"Settings...", L"设置..." },
    { Str::TrayReloadConfig, L"TrayReloadConfig", L"Reload settings from disk", L"重新载入配置文件" },
    { Str::TrayOpenConfigFolder, L"TrayOpenConfigFolder", L"Open settings folder", L"打开设置目录" },
    { Str::TrayRestartExplorer, L"TrayRestartExplorer", L"Restart Explorer (refresh shell)", L"重启资源管理器（刷新外壳）" },
    { Str::TrayOpenLog, L"TrayOpenLog", L"View log", L"查看日志" },
    { Str::TrayExit, L"TrayExit", L"Exit", L"退出" },

    // ---- tray tooltip ----------------------------------------------------
    { Str::TooltipFixed, L"TooltipFixed", L" (light %1 - %2)", L"（浅色 %1 - %2）" },
    { Str::TooltipSun, L"TooltipSun", L" (sunrise %1, sunset %2)", L"（日出 %1，日落 %2）" },
    { Str::TooltipManualOverride, L"TooltipManualOverride", L" [manual override]", L"［手动覆盖］" },

    // ---- Explorer restart -------------------------------------------------
    { Str::MsgRestartExplorerTitle, L"MsgRestartExplorerTitle",
      L"Restart Windows Explorer", L"重启 Windows 资源管理器" },
    { Str::MsgRestartExplorerBody, L"MsgRestartExplorerBody",
      L"Most shell surfaces (taskbar, Start, the notification area) pick up a theme\n"
      L"change from the WM_SETTINGCHANGE broadcast alone. If yours did not refresh,\n"
      L"restarting Windows Explorer fixes it - at the cost of a brief desktop flash.\n\n"
      L"Restart Windows Explorer now?\n\n"
      L"(Tip: you do not need to restart it every time you toggle the theme.)",
      L"大多数外壳界面（任务栏、开始菜单、通知区域）仅靠 WM_SETTINGCHANGE 广播\n"
      L"就能刷新。如果你的界面没有刷新，重启 Windows 资源管理器可以解决，\n"
      L"代价是桌面会短暂闪一下。\n\n"
      L"现在重启 Windows 资源管理器吗？\n\n"
      L"（提示：不需要每次切换主题都重启它。）" },
    { Str::MsgRestartExplorerFailed, L"MsgRestartExplorerFailed",
      L"Windows Explorer could not be restarted automatically.", L"无法自动重启 Windows 资源管理器。" },

    // ---- "start with Windows" status ---------------------------------------
    { Str::AutoStartNotRegistered, L"AutoStartNotRegistered", L"Not registered", L"未注册" },
    { Str::AutoStartRegistered, L"AutoStartRegistered", L"Registered: %1", L"已注册：%1" },
    { Str::AutoStartElsewhere, L"AutoStartElsewhere",
      L"Registered, but points elsewhere: %1", L"已注册，但指向其他位置：%1" },

    // ---- command line help -------------------------------------------------
    { Str::UsageBody, L"UsageBody",
      L"Light Switch Standalone\n\n"
      L"  (no arguments)  run in the notification area\n"
      L"  --settings      run and open the settings dialog\n"
      L"  --startup       run in the notification area (used by the Run key)\n"
      L"  --toggle        flip the theme once, then exit\n"
      L"  --light         force the light theme once, then exit\n"
      L"  --dark          force the dark theme once, then exit\n"
      L"  --lang CODE     force the interface language for this run only\n"
      L"                  (auto | en-US | zh-CN)\n"
      L"  --dump-strings FILE\n"
      L"                  write every interface string to FILE, then exit\n"
      L"  --help          show this message\n\n"
      L"Settings live next to the executable in settings.json.\n"
      L"Logs are written to lightswitch.log in the same folder.",
      L"LightSwitch 独立版\n\n"
      L"  （无参数）      常驻通知区域运行\n"
      L"  --settings      运行并打开设置对话框\n"
      L"  --startup       常驻通知区域运行（供注册表 Run 项使用）\n"
      L"  --toggle        按当前设置切换一次主题后退出\n"
      L"  --light         强制切到浅色主题后退出\n"
      L"  --dark          强制切到深色主题后退出\n"
      L"  --lang 代码     仅在本次运行强制使用指定界面语言\n"
      L"                  （auto | en-US | zh-CN）\n"
      L"  --dump-strings 文件\n"
      L"                  把所有界面字符串写入该文件后退出\n"
      L"  --help          显示本说明\n\n"
      L"配置文件为 exe 同目录下的 settings.json。\n"
      L"日志写在同目录的 lightswitch.log。" },
};

constexpr size_t kStringCount = sizeof(kStrings) / sizeof(kStrings[0]);
static_assert(kStringCount == static_cast<size_t>(Str::Count),
              "kStrings and the Str enumeration are out of sync");

constexpr bool TableIsOrdered()
{
    for (size_t i = 0; i < kStringCount; ++i)
    {
        if (kStrings[i].id != static_cast<Str>(i))
            return false;
    }
    return true;
}
static_assert(TableIsOrdered(), "kStrings must be ordered exactly like the Str enumeration");

const std::vector<Lang>& LanguageList()
{
    static const std::vector<Lang> languages = { Lang::EnUs, Lang::ZhCn };
    return languages;
}

std::atomic<Lang> g_current{ Lang::EnUs };
std::wstring g_configured = L"auto"; // UI thread only

const wchar_t* TextOf(const Entry& entry, Lang lang)
{
    switch (lang)
    {
    case Lang::ZhCn:
        return entry.zh;
    case Lang::EnUs:
    default:
        return entry.en;
    }
}

const Entry& EntryFor(Str id)
{
    const int index = static_cast<int>(id);
    if (index < 0 || index >= static_cast<int>(kStringCount))
        return kStrings[0]; // defensive; cannot happen
    return kStrings[index];
}

bool EqualsIgnoreCase(const std::wstring& text, const wchar_t* other)
{
    return _wcsicmp(text.c_str(), other) == 0;
}

bool StartsWithIgnoreCase(const std::wstring& text, const wchar_t* prefix)
{
    const size_t length = wcslen(prefix);
    return text.size() >= length && _wcsnicmp(text.c_str(), prefix, length) == 0;
}

std::wstring Trim(const std::wstring& text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && iswspace(text[begin]))
        ++begin;
    while (end > begin && iswspace(text[end - 1]))
        --end;
    return text.substr(begin, end - begin);
}

// The language Windows itself is running in. GetUserDefaultUILanguage() is the
// right source here (not the locale / system default): it is what the user
// picked under Settings > Time & language > Language.
Lang LanguageFromSystem()
{
    const LANGID uiLanguage = GetUserDefaultUILanguage();
    switch (PRIMARYLANGID(uiLanguage))
    {
    case LANG_CHINESE:
        // Simplified Chinese is the only Chinese translation this build ships.
        // Traditional Chinese readers get it rather than English; see README.
        return Lang::ZhCn;
    case LANG_ENGLISH:
        return Lang::EnUs;
    default:
        // English is the source language, so it is the fallback for every
        // language this build does not ship a translation for.
        return Lang::EnUs;
    }
}

// Matches a concrete language code. Returns false when this build has no
// translation for it.
bool TryMatchCode(const std::wstring& trimmed, Lang& matched)
{
    // Prefix match on the primary subtag, so en / en-US / en-GB and
    // zh / zh-CN / zh-Hans / zh-TW all land somewhere sensible.
    if (StartsWithIgnoreCase(trimmed, L"zh"))
    {
        matched = Lang::ZhCn;
        return true;
    }
    if (StartsWithIgnoreCase(trimmed, L"en"))
    {
        matched = Lang::EnUs;
        return true;
    }
    return false;
}

// Several strings legitimately contain newlines (message boxes). The dump is
// line oriented so it can be diffed and parsed, so those have to be escaped -
// otherwise one entry silently becomes two lines and the file stops being
// machine-checkable.
std::wstring EscapeForDump(const std::wstring& text)
{
    std::wstring out;
    out.reserve(text.size());
    for (wchar_t character : text)
    {
        switch (character)
        {
        case L'\\':
            out += L"\\\\";
            break;
        case L'\r':
            out += L"\\r";
            break;
        case L'\n':
            out += L"\\n";
            break;
        case L'\t':
            out += L"\\t";
            break;
        default:
            out.push_back(character);
            break;
        }
    }
    return out;
}

} // namespace

namespace L10n
{

void Init(const std::wstring& code)
{
    g_configured = Normalize(code);
    g_current.store(Resolve(g_configured));
}

bool SetLanguage(Lang lang)
{
    g_configured = CodeOf(lang);
    const Lang previous = g_current.exchange(lang);
    return previous != lang;
}

bool RefreshFromSystem()
{
    if (g_configured != L"auto")
        return false;

    const Lang resolved = LanguageFromSystem();
    const Lang previous = g_current.exchange(resolved);
    return previous != resolved;
}

Lang Current()
{
    return g_current.load();
}

const wchar_t* CurrentCode()
{
    return CodeOf(Current());
}

bool IsAutoConfigured()
{
    return g_configured == L"auto";
}

bool IsAuto(const std::wstring& code)
{
    const std::wstring trimmed = Trim(code);
    return trimmed.empty() || EqualsIgnoreCase(trimmed, L"auto");
}

const std::vector<Lang>& All()
{
    return LanguageList();
}

const wchar_t* CodeOf(Lang lang)
{
    return (lang == Lang::ZhCn) ? L"zh-CN" : L"en-US";
}

const wchar_t* DisplayName(Lang lang)
{
    // Deliberately written in the language it names, which is the convention
    // every language picker follows.
    return (lang == Lang::ZhCn) ? L"简体中文" : L"English";
}

const wchar_t* AutoEntryName()
{
    return T(Str::LanguageAuto);
}

Lang Resolve(const std::wstring& code)
{
    const std::wstring trimmed = Trim(code);
    if (trimmed.empty() || EqualsIgnoreCase(trimmed, L"auto"))
        return LanguageFromSystem();

    Lang matched = Lang::EnUs;
    if (TryMatchCode(trimmed, matched))
        return matched;

    // Unknown code: behave exactly like "auto" rather than silently picking a
    // language the user did not ask for.
    return LanguageFromSystem();
}

std::wstring Normalize(const std::wstring& code)
{
    const std::wstring trimmed = Trim(code);
    if (trimmed.empty() || EqualsIgnoreCase(trimmed, L"auto"))
        return L"auto";

    Lang matched = Lang::EnUs;
    if (TryMatchCode(trimmed, matched))
        return CodeOf(matched);

    // Not a language this build knows: fall back to following Windows instead
    // of pinning a concrete language because of a typo in settings.json.
    return L"auto";
}

const wchar_t* T(Str id)
{
    const wchar_t* text = TextOf(EntryFor(id), Current());
    // Every entry is required to have both columns, so this is belt and braces.
    return text ? text : L"";
}

std::wstring Format(Str id, const std::vector<std::wstring>& args)
{
    const wchar_t* pattern = T(id);
    if (!pattern)
        return {};

    std::wstring out;
    for (const wchar_t* cursor = pattern; *cursor != L'\0'; ++cursor)
    {
        if (*cursor != L'%')
        {
            out.push_back(*cursor);
            continue;
        }

        const wchar_t next = cursor[1];
        if (next == L'%')
        {
            out.push_back(L'%');
            ++cursor;
            continue;
        }

        if (next >= L'1' && next <= L'9')
        {
            const size_t index = static_cast<size_t>(next - L'1');
            if (index < args.size())
            {
                out += args[index];
            }
            else
            {
                // Leave it visible: a missing argument is a bug worth seeing.
                out.push_back(L'%');
                out.push_back(next);
            }
            ++cursor;
            continue;
        }

        out.push_back(L'%');
    }
    return out;
}

const wchar_t* UiFontFace()
{
    // "Segoe UI" has no CJK glyphs. Windows would font-link them in, but the
    // result depends on the installed fonts, so pick the real UI face instead.
    return (Current() == Lang::ZhCn) ? L"Microsoft YaHei UI" : L"Segoe UI";
}

std::wstring DumpAll()
{
    std::wstring out;
    for (Lang lang : All())
    {
        out += L"=== ";
        out += CodeOf(lang);
        out += L" (";
        out += DisplayName(lang);
        out += L") ===\r\n";

        for (size_t i = 0; i < kStringCount; ++i)
        {
            out += kStrings[i].name;
            out += L"\t";
            out += EscapeForDump(TextOf(kStrings[i], lang));
            out += L"\r\n";
        }
        out += L"\r\n";
    }
    return out;
}

} // namespace L10n

} // namespace ls
