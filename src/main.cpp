// LightSwitch Standalone - process entry point.
//
// Command line:
//   (no arguments)      run resident: tray icon + scheduler + global hotkey
//   --settings          run resident and open the settings dialog immediately
//   --startup           same as no arguments; written into the Run key
//   --toggle            flip the theme per the current settings and exit
//   --light             force the light theme and exit
//   --dark              force the dark theme and exit
//   --lang CODE         override the interface language for this run only
//   --dump-strings FILE write every interface string to FILE and exit
//   --help              show this text
#include "App.h"

#include "Config.h"
#include "L10n.h"
#include "Logging.h"
#include "Text.h"
#include "ThemeEngine.h"
#include "vendor/ThemeHelper.h"

#include <commctrl.h>
#include <shellapi.h>

#include <string>

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
    ForceLight,
    ForceDark
};

bool ApplyOneShot(OneShot mode)
{
    const ls::Config config = ls::SettingsStore::Instance().Get();

    if (!config.changeSystem && !config.changeApps)
    {
        LOG_WARN(L"[main] Neither the system nor the apps theme is enabled in settings; nothing to do.");
        return false;
    }

    bool target = false;
    switch (mode)
    {
    case OneShot::ForceLight:
        target = true;
        break;
    case OneShot::ForceDark:
        target = false;
        break;
    default:
    {
        const bool currentlyLight = config.changeSystem ? GetCurrentSystemTheme() : GetCurrentAppsTheme();
        target = !currentlyLight;
        break;
    }
    }

    ls::ApplyTheme(config, target);
    return true;
}

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    INITCOMMONCONTROLSEX commonControls{};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_DATE_CLASSES;
    InitCommonControlsEx(&commonControls);

    OneShot oneShot = OneShot::None;
    bool openSettings = false;
    bool showHelp = false;
    std::wstring languageOverride;
    std::wstring dumpPath;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::wstring argument = argv[i];
            if (_wcsicmp(argument.c_str(), L"--toggle") == 0)
                oneShot = OneShot::Toggle;
            else if (_wcsicmp(argument.c_str(), L"--light") == 0)
                oneShot = OneShot::ForceLight;
            else if (_wcsicmp(argument.c_str(), L"--dark") == 0)
                oneShot = OneShot::ForceDark;
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
    std::wstring languageCode = ls::SettingsStore::Instance().Get().language;
    if (!languageOverride.empty())
        languageCode = languageOverride;
    ls::L10n::Init(languageCode);

    if (!dumpPath.empty())
    {
        if (!WriteUtf8File(dumpPath, ls::L10n::DumpAll()))
        {
            LOG_ERROR(L"[main] Could not write the string dump to " << dumpPath << L".");
            return 3;
        }
        return 0;
    }

    if (showHelp)
    {
        ShowUsage();
        return 0;
    }

    if (oneShot != OneShot::None)
        return ApplyOneShot(oneShot) ? 0 : 2;

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
