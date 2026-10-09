// LightSwitch Standalone - application object.
//
// This is the piece that has no upstream equivalent: PowerToys' "Runner" owned
// the tray icon, the message loop, single instance handling and module loading.
// Here it is one process with one message-only-ish window that receives tray
// callbacks, WM_HOTKEY and power notifications, while all theme work happens on
// the Scheduler worker thread.
#pragma once

#include <windows.h>

#include "HotkeyManager.h"
#include "Scheduler.h"
#include "SettingsDialog.h"
#include "TrayIcon.h"

namespace ls
{

class App : public ISettingsHost
{
public:
    // Posted by a second instance to bring up our settings dialog.
    static constexpr UINT kShowSettingsMessage = WM_APP + 0x103;
    static constexpr const wchar_t* kWindowClassName = L"LightSwitchStandalone.MainWindow";

    static App& Instance();

    bool Init(HINSTANCE instance);
    int Run();
    void Shutdown();

    // ISettingsHost
    void OnToggleNow() override;
    void OnRestartExplorer() override;
    void OnSettingsSaved() override;

    HWND Window() const { return m_window; }

private:
    App() = default;
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    static LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    bool CreateMessageWindow(HINSTANCE instance);
    void HandleTrayCallback(LPARAM lParam);
    void HandleTrayCommand(UINT command);
    void OpenSettingsDialog();
    void ApplyHotkeyFromConfig();
    void UpdateTooltip();
    void OpenShellPath(const std::wstring& path);
    void OpenConfigFolder();
    void OpenLogFile();

    HINSTANCE m_instance = nullptr;
    HWND m_window = nullptr;

    TrayIcon m_tray;
    HotkeyManager m_hotkey;
    Scheduler m_scheduler;

    bool m_dialogOpen = false;
    bool m_shuttingDown = false;
};

} // namespace ls
