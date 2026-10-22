// LightSwitch Standalone - process entry point.
//
// Command line:
//   (no arguments)      run resident: tray icon + scheduler + global hotkey
//   --settings          run resident and open the settings dialog immediately
//   --startup           same as no arguments; written into the Run key
//   --toggle            apply the other of the two themes once and exit
//   --light / --theme-a apply theme A once and exit
//   --dark  / --theme-b apply theme B once and exit
//   --theme FILE        apply an arbitrary .theme file once and exit
//   --lang CODE         override the interface language for this run only
//   --dump-strings FILE write every interface string to FILE and exit
//   --dump-themes FILE  write every .theme found to FILE as TSV and exit
//   --theme-check FILE  probe IThemeManager2 and write the verdict to FILE
//   --help              show this text
#include "App.h"

#include "Config.h"
#include "L10n.h"
#include "Logging.h"
#include "Text.h"
#include "ThemeCatalog.h"
#include "ThemeEngine.h"
#include "WindowsTheme.h"
#include "dark/DarkApp.h"
#include "vendor/ThemeHelper.h"

#include <commctrl.h>
#include <shellapi.h>

#include <string>
#include <vector>

namespace
{

constexpr wchar_t kSingleInstanceMutex[] = L"Local\\LightSwitchStandalone.SingleInstance";

void ShowUsage()
{
    MessageBoxW(nullptr,
                ls::L10n::T(ls::Str::UsageBody),
                ls::L10n::T(ls::Str::ProductName),
                MB_OK | MB_ICONINFORMATION);
}

// --dump-strings writes UTF-8 with a BOM, so Notepad and every scripting
// language detect the encoding without being told.
bool WriteUtf8File(const std::wstring& path, const std::wstring& text)
{
    const std::string utf8 = "\xEF\xBB\xBF" + ls::WideToUtf8(text);

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    DWORD written = 0;
    const BOOL ok = WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == utf8.size();
}

enum class OneShot
{
    None,
    Toggle,
    ApplyLight,
    ApplyDark
};

// Exit codes, stable so a script can branch on them:
//   0 applied (or already active)   2 nothing configured
//   3 theme file missing            4 the theme could not be applied
int ApplyOneShot(OneShot mode, const std::wstring& explicitPath)
{
    const ls::Config config = ls::SettingsStore::Instance().Get();

    ls::ThemeTarget target;
    if (!explicitPath.empty())
    {
        target = ls::MakeTarget(explicitPath);
    }
    else if (mode == OneShot::ApplyLight)
    {
        target = ls::MakeTarget(config.themeA);
    }
    else if (mode == OneShot::ApplyDark)
    {
        target = ls::MakeTarget(config.themeB);
    }
    else
    {
        if (config.themeA.empty() || config.themeB.empty())
        {
            LOG_WARN(L"[main] Toggle requested, but both themes must be chosen first (themeA="
                     << (config.themeA.empty() ? L"unset" : L"set") << L", themeB="
                     << (config.themeB.empty() ? L"unset" : L"set") << L").");
            return 2;
        }

        const bool currentlyDark = ls::theme::SameIdentity(
            ls::theme::ReadCurrentIdentity(), ls::MakeTarget(config.themeB).identity);
        target = ls::MakeTarget(currentlyDark ? config.themeA : config.themeB);
    }

    if (target.Empty())
    {
        LOG_WARN(L"[main] No theme to apply; set themeA and themeB in settings first.");
        return 2;
    }

    LOG_INFO(L"[main] Applying \"" << target.path << L"\".");
    const ls::ApplyReport report = ls::ApplyThemeTarget(target);
    LOG_INFO(L"[main] " << ls::ApplyStatusName(report.status) << L": " << report.detail << L".");
    ls::ShutdownStaWorker();

    if (ls::IsSuccess(report))
        return 0;
    return report.status == ls::ApplyStatus::FileMissing ? 3 : 4;
}

// One row per theme: name, the two mode hints, the identity and the path.
// Written as a file rather than to stdout because this is a windowed
// process - it has no console attached to print to.
std::wstring BuildThemeDump()
{
    ls::theme::EnumerateOptions options;
    const std::vector<ls::theme::ThemeInfo> themes = ls::theme::Enumerate(options);
    const ls::theme::ThemeIdentity current = ls::theme::ReadCurrentIdentity();

    std::wstring text;
    text.reserve(themes.size() * 160 + 128);
    text += L"name\tsystem\tapp\tcurrent\tmissing\tthemeId\tpath\r\n";
    for (const ls::theme::ThemeInfo& entry : themes)
    {
        const bool isCurrent = ls::theme::SameIdentity(ls::theme::ThemeIdentity{ entry.themeId, entry.path }, current);
        text += entry.displayName;
        text += L'\t';
        text += entry.systemMode;
        text += L'\t';
        text += entry.appMode;
        text += L'\t';
        text += isCurrent ? L"yes" : L"no";
        text += L'\t';
        text += entry.fileMissing ? L"yes" : L"no";
        text += L'\t';
        text += entry.themeId;
        text += L'\t';
        text += entry.path;
        text += L"\r\n";
    }
    return text;
}

// The one diagnostic that answers "can this machine apply a theme at all?"
// without changing the desktop: create the manager, initialise it, and read
// the theme count - the cheapest proof the declared vtable matches.
std::wstring BuildThemeCheckReport()
{
    const ls::Config config = ls::SettingsStore::Instance().Get();
    const bool ready = ls::ProbeThemeManager();
    const ls::theme::ThemeIdentity current = ls::theme::ReadCurrentIdentity();

    std::wstring text;
    text += L"com = ";
    text += ready ? L"ok" : L"unavailable";
    text += L"\r\nthemes = ";
    text += std::to_wstring(ls::ThemeManagerThemeCount());
    text += L"\r\ncurrent = ";
    text += current.path.empty() ? std::wstring(L"(none)") : current.path;
    text += L"\r\ncurrent id = ";
    text += current.themeId.empty() ? std::wstring(L"(none)") : current.themeId;
    text += L"\r\nschedule = ";
    text += ls::ScheduleModeToString(config.scheduleMode);
    text += L"\r\nthemeA = ";
    text += config.themeA.empty() ? std::wstring(L"(unset)") : config.themeA;
    text += L"\r\nthemeB = ";
    text += config.themeB.empty() ? std::wstring(L"(unset)") : config.themeB;
    text += L"\r\n";

    ls::ShutdownStaWorker();
    return text;
}

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    // Before InitCommonControlsEx and before any window exists: uxtheme has to
    // have the preferred app mode set before the first window is created, or
    // that first frame is painted light and then corrected. This also registers
    // the interface-layer callback and builds the palette.
    ls::DarkApp::Startup();

    INITCOMMONCONTROLSEX commonControls{};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_DATE_CLASSES;
    InitCommonControlsEx(&commonControls);

    OneShot oneShot = OneShot::None;
    bool openSettings = false;
    bool showHelp = false;
    std::wstring languageOverride;
    std::wstring dumpPath;
    std::wstring themeDumpPath;
    std::wstring themeCheckPath;
    std::wstring explicitTheme;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::wstring argument = argv[i];
            if (_wcsicmp(argument.c_str(), L"--toggle") == 0)
                oneShot = OneShot::Toggle;
            else if (_wcsicmp(argument.c_str(), L"--light") == 0 || _wcsicmp(argument.c_str(), L"--theme-a") == 0)
                oneShot = OneShot::ApplyLight;
            else if (_wcsicmp(argument.c_str(), L"--dark") == 0 || _wcsicmp(argument.c_str(), L"--theme-b") == 0)
                oneShot = OneShot::ApplyDark;
            else if (_wcsicmp(argument.c_str(), L"--settings") == 0)
                openSettings = true;
            else if (_wcsicmp(argument.c_str(), L"--startup") == 0)
            {
                // Default behaviour; accepted so the Run key value is explicit.
            }
            else if (_wcsicmp(argument.c_str(), L"--lang") == 0 && i + 1 < argc)
            {
                languageOverride = argv[++i];
            }
            else if (_wcsicmp(argument.c_str(), L"--dump-strings") == 0 && i + 1 < argc)
            {
                dumpPath = argv[++i];
            }
            else if (_wcsicmp(argument.c_str(), L"--dump-themes") == 0 && i + 1 < argc)
            {
                themeDumpPath = argv[++i];
            }
            else if (_wcsicmp(argument.c_str(), L"--theme-check") == 0 && i + 1 < argc)
            {
                themeCheckPath = argv[++i];
            }
            else if (_wcsicmp(argument.c_str(), L"--theme") == 0 && i + 1 < argc)
            {
                // An arbitrary .theme file, applied once. Handy for testing a
                // pack before choosing it, and for scripts.
                explicitTheme = argv[++i];
            }
            else if (_wcsicmp(argument.c_str(), L"--help") == 0 || _wcsicmp(argument.c_str(), L"/?") == 0)
            {
                showHelp = true;
            }
        }
        LocalFree(argv);
    }

    // Resolve the interface language before anything can display text. The
    // configured value ("auto" by default) lives in settings.json; --lang
    // overrides it for this process without touching the file.
    const ls::Config startupConfig = ls::SettingsStore::Instance().Get();
    std::wstring languageCode = startupConfig.language;
    if (!languageOverride.empty())
        languageCode = languageOverride;
    ls::L10n::Init(languageCode);

    // Push the saved interface theme into the dark-mode module, then re-apply.
    // Startup() primed the module from its own registry value, but settings.json
    // is the authoritative store - so a hand-edited file wins, and doing it here
    // means the first frame this process paints is already in the right scheme
    // rather than flashing the registry's value and correcting a moment later.
    ls::DarkApp::SetFromConfig(startupConfig.uiTheme);
    ls::DarkApp::ApplyNow(nullptr);

    // One line that answers "why is my window light?" without a debugger: what
    // the file said, and what the module decided to do about it.
    LOG_INFO(L"[main] Interface theme: " << startupConfig.uiTheme << L", resolved to "
                                         << (ls::DarkApp::DesiredDark() ? L"dark" : L"light") << L".");
    if (ls::DarkApp::IsFollowingSystem())
        LOG_INFO(L"[main] The interface theme follows Windows (AppsUseLightTheme).");

    if (!dumpPath.empty())
    {
        if (!WriteUtf8File(dumpPath, ls::L10n::DumpAll()))
        {
            LOG_ERROR(L"[main] Could not write the string dump to " << dumpPath << L".");
            return 3;
        }
        return 0;
    }

    if (!themeDumpPath.empty())
    {
        if (!WriteUtf8File(themeDumpPath, BuildThemeDump()))
        {
            LOG_ERROR(L"[main] Could not write the theme dump to " << themeDumpPath << L".");
            return 3;
        }
        return 0;
    }

    if (!themeCheckPath.empty())
    {
        const std::wstring report = BuildThemeCheckReport();
        if (!WriteUtf8File(themeCheckPath, report))
        {
            LOG_ERROR(L"[main] Could not write the theme check to " << themeCheckPath << L".");
            return 3;
        }
        return report.find(L"com = ok") != std::wstring::npos ? 0 : 4;
    }

    if (showHelp)
    {
        ShowUsage();
        return 0;
    }

    if (oneShot != OneShot::None || !explicitTheme.empty())
        return ApplyOneShot(oneShot, explicitTheme);

    // ---- Single instance -------------------------------------------------
    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, kSingleInstanceMutex);
    const bool alreadyRunning = (instanceMutex != nullptr) && (GetLastError() == ERROR_ALREADY_EXISTS);

    if (alreadyRunning)
    {
        LOG_INFO(L"[main] Another instance is already running; handing over.");
        const HWND existing = FindWindowW(ls::App::kWindowClassName, nullptr);
        if (existing)
            PostMessageW(existing, ls::App::kShowSettingsMessage, 0, 0);
        if (instanceMutex)
            CloseHandle(instanceMutex);
        return 0;
    }

    ls::App& app = ls::App::Instance();
    if (!app.Init(instance))
    {
        LOG_ERROR(L"[main] Initialisation failed.");
        if (instanceMutex)
            CloseHandle(instanceMutex);
        return 1;
    }

    if (openSettings)
        PostMessageW(app.Window(), ls::App::kShowSettingsMessage, 0, 0);

    const int result = app.Run();

    app.Shutdown();
    if (instanceMutex)
        CloseHandle(instanceMutex);

    LOG_INFO(L"[main] Exiting with code " << result << L".");
    return result;
}
