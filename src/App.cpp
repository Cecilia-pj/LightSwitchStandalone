// LightSwitch Standalone - application object (implementation).
#include "App.h"

#include "AutoStart.h"
#include "Config.h"
#include "L10n.h"
#include "Logging.h"
#include "StateManager.h"
#include "ThemeCatalog.h"
#include "WindowsTheme.h"
#include "dark/DarkApp.h"
#include "vendor/ThemeHelper.h"

#include <shlobj.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <string>
#include <vector>

namespace ls
{

namespace
{

App* g_app = nullptr;

std::wstring FormatMinutes(int minutes)
{
    minutes = ((minutes % 1440) + 1440) % 1440;
    wchar_t buffer[16];
    swprintf_s(buffer, L"%02d:%02d", minutes / 60, minutes % 60);
    return buffer;
}

std::wstring ModeDisplayName(ScheduleMode mode)
{
    switch (mode)
    {
    case ScheduleMode::FixedHours:
        return L10n::T(Str::ModeFixedHours);
    case ScheduleMode::SunsetToSunrise:
        return L10n::T(Str::ModeSunsetToSunrise);
    case ScheduleMode::FollowNightLight:
        return L10n::T(Str::ModeFollowNightLight);
    default:
        return L10n::T(Str::ModeOff);
    }
}

std::wstring DirectoryOf(const std::wstring& path)
{
    const auto pos = path.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? path : path.substr(0, pos);
}

// Terminates every explorer.exe in this session and starts a new one.
// Only ever called after an explicit confirmation from the user.
bool RestartExplorer()
{
    bool killedAny = false;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);

        const DWORD ownPid = GetCurrentProcessId();

        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                if (_wcsicmp(entry.szExeFile, L"explorer.exe") != 0)
                    continue;
                if (entry.th32ProcessID == ownPid)
                    continue;

                HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID);
                if (process)
                {
                    if (TerminateProcess(process, 0))
                        killedAny = true;
                    CloseHandle(process);
                }
            } while (Process32NextW(snapshot, &entry));
        }

        CloseHandle(snapshot);
    }

    if (!killedAny)
    {
        LOG_WARN(L"[App] No explorer.exe instance could be terminated.");
        return false;
    }

    Sleep(800);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};

    wchar_t command[] = L"explorer.exe";
    if (CreateProcessW(nullptr, command, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &info))
    {
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
        LOG_INFO(L"[App] Explorer restarted.");
        return true;
    }

    LOG_ERROR(L"[App] Failed to start explorer.exe (error " << GetLastError() << L").");
    return false;
}

} // namespace

App& App::Instance()
{
    static App instance;
    return instance;
}

App::~App()
{
    Shutdown();
}

bool App::Init(HINSTANCE instance)
{
    m_instance = instance;
    g_app = this;

    // Registered before the window exists so no broadcast can be missed. The
    // shell sends this after it has rebuilt the notification area, which is the
    // only signal that an icon registered with a previous shell has been lost.
    m_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    if (m_taskbarCreatedMessage == 0)
    {
        LOG_WARN(L"[App] RegisterWindowMessage(\"TaskbarCreated\") failed (error "
                 << GetLastError() << L"); the tray icon will not survive a shell restart.");
    }

    if (!CreateMessageWindow(instance))
        return false;

    const Config config = SettingsStore::Instance().Get();

    m_tray.Add(m_window, L10n::T(Str::ProductName));

    ApplyHotkeyFromConfig();

    // Keep the Run key in sync with the stored preference.
    AutoStart::Apply(config.startOnLogin);

    if (!m_scheduler.Start())
    {
        LOG_ERROR(L"[App] Could not start the scheduler worker.");
        return false;
    }

    UpdateTooltip();

    LOG_INFO(L"[App] Started. Settings file: " << SettingsStore::Instance().ConfigFilePath());
    return true;
}

bool App::CreateMessageWindow(HINSTANCE instance)
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &App::WndProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClassName;
    windowClass.hIcon = TrayIcon::CreateIcon();
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.style = 0;

    if (!RegisterClassExW(&windowClass))
    {
        const DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS)
        {
            LOG_ERROR(L"[App] RegisterClassEx failed (error " << error << L").");
            return false;
        }
    }

    // A normal (hidden) top-level window rather than HWND_MESSAGE: the settings
    // dialog needs a real owner so it appears in the taskbar / Alt-Tab
    // correctly, and FindWindow must be able to locate us for the second
    // instance hand-off.
    m_window = CreateWindowExW(0,
                               kWindowClassName,
                               L10n::T(Str::ProductName),
                               WS_OVERLAPPED,
                               0, 0, 0, 0,
                               nullptr,
                               nullptr,
                               instance,
                               nullptr);

    if (!m_window)
    {
        LOG_ERROR(L"[App] CreateWindowEx failed (error " << GetLastError() << L").");
        return false;
    }

    return true;
}

int App::Run()
{
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

void App::Shutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;

    LOG_INFO(L"[App] Shutting down.");

    m_scheduler.Stop();
    m_hotkey.Unregister();
    m_tray.Remove();

    // The scheduler is stopped, so nothing can be inside a theme apply any
    // more; releasing the STA worker here is safe and lets themeui.dll unload
    // instead of being torn down mid-call by process exit.
    ShutdownStaWorker();

    // Join the settings-file watcher thread so nothing keeps running after the
    // message loop ends.
    SettingsStore::Instance().StopWatching();

    // Deliberately NOT clearing g_app here: DestroyWindow() below re-enters
    // WndProc synchronously for WM_DESTROY, and that path needs a valid
    // instance to call PostQuitMessage(). The instance is a function-local
    // static, so it stays valid until the process ends anyway.
}

void App::ApplyHotkeyFromConfig()
{
    const Config config = SettingsStore::Instance().Get();

    if (!m_hotkey.Register(m_window, config.hotkey))
    {
        LOG_WARN(L"[App] The toggle shortcut could not be registered; see the log for details.");
    }
}

void App::BuildTrayMenuState(TrayMenuState& state)
{
    const Config config = SettingsStore::Instance().Get();

    // Read from CurrentTheme rather than from the scheduler's cached identity.
    // The cache is filled by the worker thread, which may not have finished its
    // first sync when this program is a second old - and a menu with no bullet
    // on either row looks like the feature does not work rather than like a
    // race. The registry is the authority anyway; the cache exists so the
    // minute tick does not have to re-read it.
    const theme::ThemeIdentity current = theme::ReadCurrentIdentity();

    // A configured path whose file has since disappeared still gets a row: the
    // user has to be able to see that the pack they picked is gone, and the
    // settings dialog is where that gets fixed. The name falls back to the file
    // name, so the row reads like the one they chose.
    auto describe = [&current](const std::wstring& path) {
        TrayThemeEntry entry;
        if (path.empty())
            return entry;

        entry.name = theme::DisplayNameOfPath(path);
        entry.active = theme::SameIdentity(current, theme::IdentifyPath(path));
        return entry;
    };

    state.light = describe(config.themeA);
    state.dark = describe(config.themeB);
}

void App::UpdateTooltip()
{
    const Config config = SettingsStore::Instance().Get();
    const LightSwitchState state = m_scheduler.State().GetState();

    std::wstring tooltip = L10n::T(Str::ProductName);
    tooltip += L" - ";
    tooltip += ModeDisplayName(config.scheduleMode);

    if (config.scheduleMode == ScheduleMode::FixedHours)
    {
        tooltip += L10n::F(Str::TooltipFixed,
                           FormatMinutes(state.effectiveLightMinutes),
                           FormatMinutes(state.effectiveDarkMinutes));
    }
    else if (config.scheduleMode == ScheduleMode::SunsetToSunrise && state.computedSunriseMinutes >= 0)
    {
        tooltip += L10n::F(Str::TooltipSun,
                           FormatMinutes(state.computedSunriseMinutes + config.sunriseOffset),
                           FormatMinutes(state.computedSunsetMinutes + config.sunsetOffset));
    }

    // The pack that is on screen. With two arbitrary packs the schedule mode
    // says nothing about what the machine looks like, and the names the user
    // gave them are the only label that means anything.
    //
    // CurrentTheme, not the scheduler's cache: this runs once at startup, and
    // the worker may not have completed its first sync yet. A tooltip that only
    // grows the theme name after the next event would look like the feature is
    // missing.
    const theme::ThemeIdentity current = theme::ReadCurrentIdentity();
    if (!current.path.empty())
        tooltip += L10n::F(Str::TooltipTheme, theme::DisplayNameOfPath(current.path));

    if (state.isManualOverride)
        tooltip += L10n::T(Str::TooltipManualOverride);

    // Shell tooltips are limited to 128 characters including the terminator.
    // CJK glyphs are one wchar_t each, so the same cap applies to every
    // language this build ships.
    if (tooltip.size() > 120)
        tooltip.resize(120);

    m_tray.SetTooltip(tooltip);
}

LRESULT CALLBACK App::WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    App* self = g_app;
    if (!self)
    {
        // Never leave WM_DESTROY unhandled: that would strand the message loop.
        if (message == WM_DESTROY)
        {
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    // Rebind if the window was created before g_app was set (defensive).
    if (!self->m_window)
        self->m_window = window;

    return self->HandleMessage(message, wParam, lParam);
}

LRESULT App::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    // A registered message, so it is compared rather than used as a case label:
    // the system hands out its value at runtime and it must not be assumed to
    // sit outside the WM_ range. The zero check covers a failed registration
    // (that would compare equal to WM_NULL).
    if (m_taskbarCreatedMessage != 0 && message == m_taskbarCreatedMessage)
    {
        OnTaskbarCreated();
        return 0;
    }

    switch (message)
    {
    case TrayIcon::kCallbackMessage:
        HandleTrayCallback(lParam);
        return 0;

    case HotkeyManager::kHotkeyMessage:
        OnToggleNow();
        return 0;

    case kShowSettingsMessage:
        OpenSettingsDialog();
        return 0;

    case WM_POWERBROADCAST:
        if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND)
        {
            // A laptop may have slept through several schedule boundaries; ask
            // the worker to re-evaluate as soon as it can.
            LOG_INFO(L"[App] System resumed; requesting an immediate re-evaluation.");
            m_scheduler.RequestTick();
        }
        return TRUE;

    case WM_SETTINGCHANGE:
        // "intl" is broadcast when the user changes the Windows display
        // language. In "auto" mode the interface follows along; when the user
        // pinned a language in the settings dialog nothing happens.
        if (lParam != 0 &&
            _wcsicmp(reinterpret_cast<const wchar_t*>(lParam), L"intl") == 0 &&
            L10n::RefreshFromSystem())
        {
            LOG_INFO(L"[App] Windows display language changed; interface is now " << L10n::CurrentCode() << L".");
            UpdateTooltip();
        }

        // "ImmersiveColorSet" is broadcast when the user switches Windows
        // between its light and dark app theme. The settings dialog handles its
        // own copy; this one matters because it is the only listener when no
        // dialog is open, and the tray icon outlives every dialog. Conditional:
        // when the interface is pinned, nothing about it changed.
        if (DarkApp::IsFollowingSystem() && DarkApp::IsColorSchemeChange(lParam))
        {
            LOG_INFO(L"[App] Windows app theme changed; following it.");
            DarkApp::ApplyIfNeeded(m_window);
        }
        break;

    // uxtheme reloaded its theme data (colour scheme change, high contrast
    // toggle, a theme being applied). Same reasoning as above.
    case WM_THEMECHANGED:
        DarkApp::ApplyIfNeeded(m_window);
        return 0;

    case WM_TIMER:
        if (wParam == kTrayReAddTimer)
        {
            // One-shot: TryReAddTrayIcon re-arms it only if it wants another go.
            KillTimer(m_window, kTrayReAddTimer);
            TryReAddTrayIcon();
            return 0;
        }
        break;

    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_ENDSESSION:
        if (wParam)
        {
            Shutdown();
            PostQuitMessage(0);
        }
        return 0;

    case WM_CLOSE:
        Shutdown();
        DestroyWindow(m_window);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(m_window, message, wParam, lParam);
}

void App::OnTaskbarCreated()
{
    // explorer.exe was restarted - by this program's own "Restart Explorer"
    // command, by the user through Task Manager, or by Windows after a crash.
    // The notification area is explorer's, so the icon died with the old shell:
    // the new one starts with an empty tray and never asks anyone to come back.
    // Registering again is the only way in, and this broadcast is the cue.
    LOG_INFO(L"[App] The shell restarted; registering the tray icon again.");
    m_trayReAddAttempts = 0;
    TryReAddTrayIcon();
}

void App::TryReAddTrayIcon()
{
    if (m_tray.ReAdd())
    {
        // Whichever path got here first wins; drop any retry the other one left
        // armed so the icon is not registered twice for one shell restart.
        KillTimer(m_window, kTrayReAddTimer);

        // The shell keeps its own copy of the tooltip and that copy died with
        // the old shell, so put the current one back. Add() already restored the
        // product name; this replaces it with the live schedule summary.
        UpdateTooltip();
        m_trayReAddAttempts = 0;
        LOG_INFO(L"[App] Tray icon restored.");
        return;
    }

    ++m_trayReAddAttempts;
    if (m_trayReAddAttempts >= kTrayReAddMaxAttempts)
    {
        LOG_ERROR(L"[App] The tray icon could not be registered again after "
                  << kTrayReAddMaxAttempts << L" attempts; restart the program to get it back.");
        m_trayReAddAttempts = 0;
        return;
    }

    // Shell_NotifyIcon refuses NIM_ADD while the new notification area is still
    // being built, which is normal for a moment after the broadcast. Retry a
    // bounded number of times instead of writing the icon off for the session.
    //
    // INFO, not WARN: after this program restarts explorer itself, several
    // refusals in a row are simply how long the new shell takes, and warning on
    // every one of them would teach the reader to ignore warnings. Giving up for
    // good is the ERROR below.
    LOG_INFO(L"[App] Registering the tray icon failed; retrying ("
             << m_trayReAddAttempts << L"/" << kTrayReAddMaxAttempts << L").");
    SetTimer(m_window, kTrayReAddTimer, kTrayReAddIntervalMs, nullptr);
}

void App::HandleTrayCallback(LPARAM lParam)
{
    // With NOTIFYICON_VERSION_4 the low word of lParam is the notification and
    // the high word carries the icon id.
    const UINT notification = LOWORD(lParam);

    switch (notification)
    {
    case WM_LBUTTONDBLCLK:
        OpenSettingsDialog();
        break;

    case WM_LBUTTONUP:
        // A single left click is opt-in. On a touch screen a tap arrives as
        // exactly this message, so the default (settings.json
        // "trayClickTogglesTheme": false) is to do nothing here rather than
        // risk flipping the theme of the whole machine because a finger
        // brushed past the icon. The other three ways to toggle are
        // unaffected - the hotkey, the menu's "Toggle now" and the button of
        // the same name - and a double click still opens the settings dialog.
        if (SettingsStore::Instance().Get().trayClickTogglesTheme)
        {
            OnToggleNow();
        }
        else
        {
            // DEBUG, not INFO: this fires on every stray tap, and someone
            // wondering why clicking the icon does nothing needs the
            // breadcrumb without the log filling up.
            LOG_DEBUG(L"[Tray] Left click ignored; \"trayClickTogglesTheme\" is off.");
        }
        break;

    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
    {
        POINT cursor{};
        GetCursorPos(&cursor);

        // Required so the popup behaves correctly when it is dismissed.
        SetForegroundWindow(m_window);

        TrayMenuState state;
        BuildTrayMenuState(state);

        const UINT command = m_tray.ShowContextMenu(cursor, state);
        if (command)
            HandleTrayCommand(command);
        break;
    }

    default:
        break;
    }
}

void App::HandleTrayCommand(UINT command)
{
    switch (command)
    {
    case TrayIcon::CmdToggleNow:
        OnToggleNow();
        break;

    case TrayIcon::CmdSettings:
        OpenSettingsDialog();
        break;

    case TrayIcon::CmdRestartExplorer:
        OnRestartExplorer();
        break;

    case TrayIcon::CmdOpenConfigFolder:
        OpenConfigFolder();
        break;

    case TrayIcon::CmdOpenLog:
        OpenLogFile();
        break;

    case TrayIcon::CmdReloadConfig:
        LOG_INFO(L"[App] Manual reload requested.");
        SettingsStore::Instance().Load();
        SetEvent(SettingsStore::Instance().ChangedEvent());
        break;

    case TrayIcon::CmdThemeLight:
        m_scheduler.RequestApplySide(false);
        break;

    case TrayIcon::CmdThemeDark:
        m_scheduler.RequestApplySide(true);
        break;

    case TrayIcon::CmdExit:
        Shutdown();
        PostMessageW(m_window, WM_CLOSE, 0, 0);
        break;

    default:
        break;
    }
}

void App::OpenSettingsDialog()
{
    if (m_dialogOpen)
        return;

    m_dialogOpen = true;
    ShowSettingsDialog(m_window, *this);
    m_dialogOpen = false;

    UpdateTooltip();
}

void App::OnToggleNow()
{
    // The actual registry write + broadcast happens on the worker thread.
    m_scheduler.RequestToggle();
}

void App::OnRestartExplorer()
{
    // Cheap, non-destructive first: tell the shell that file associations
    // changed, which makes it re-read several theme-related settings.
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    const int answer = MessageBoxW(m_window,
                                   L10n::T(Str::MsgRestartExplorerBody),
                                   L10n::T(Str::MsgRestartExplorerTitle),
                                   MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);

    if (answer != IDYES)
        return;

    if (!RestartExplorer())
    {
        MessageBoxW(m_window, L10n::T(Str::MsgRestartExplorerFailed), L10n::T(Str::ProductName), MB_OK | MB_ICONERROR);
        return;
    }

    // The icon did not survive that: we terminated the process that owned the
    // notification area. The new shell's "TaskbarCreated" would normally be
    // enough, but this thread was parked inside RestartExplorer() - sleeping,
    // then starting explorer.exe - and Winlogon's AutoRestartShell (on by
    // default) can bring the shell back during that window, in which case the
    // broadcast is sent while this thread is not pumping and is dropped. So do
    // not depend on catching it: ask for the icon back on a timer as well.
    // Registering twice is harmless - same (hWnd, uID) just updates the entry.
    LOG_INFO(L"[App] Explorer restarted; scheduling a tray icon check.");
    m_trayReAddAttempts = 0;
    SetTimer(m_window, kTrayReAddTimer, kTrayReAddIntervalMs, nullptr);
}

void App::OnSettingsSaved()
{
    LOG_INFO(L"[App] Settings saved.");

    // The hotkey may have changed; re-register it. The scheduler picks the new
    // values up through the settings-changed event that SetAndSave() raised.
    ApplyHotkeyFromConfig();
    UpdateTooltip();
}

void App::OpenShellPath(const std::wstring& path)
{
    const HINSTANCE result = ShellExecuteW(m_window, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32)
        LOG_WARN(L"[App] ShellExecute failed for " << path << L".");
}

void App::OpenConfigFolder()
{
    OpenShellPath(DirectoryOf(SettingsStore::Instance().ConfigFilePath()));
}

void App::OpenLogFile()
{
    OpenShellPath(LogFilePath());
}

} // namespace ls
