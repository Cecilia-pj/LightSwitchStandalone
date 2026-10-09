// LightSwitch Standalone - application object (implementation).
#include "App.h"

#include "AutoStart.h"
#include "Config.h"
#include "L10n.h"
#include "Logging.h"
#include "StateManager.h"
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
        OnToggleNow();
        break;

    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
    {
        POINT cursor{};
        GetCursorPos(&cursor);

        // Required so the popup behaves correctly when it is dismissed.
        SetForegroundWindow(m_window);

        const UINT command = m_tray.ShowContextMenu(cursor);
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
        MessageBoxW(m_window, L10n::T(Str::MsgRestartExplorerFailed), L10n::T(Str::ProductName), MB_OK | MB_ICONERROR);
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
