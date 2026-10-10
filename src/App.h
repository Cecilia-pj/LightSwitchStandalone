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

    // Paces the attempts to re-register the tray icon after the shell restarted.
    // NIM_ADD can be refused while the new notification area is still coming up,
    // so the first refusal is treated as "not yet" rather than as a failure.
    // The id is arbitrary; nothing else in this program sets a timer.
    //
    // 20 x 500 ms is 10 s of cover. Measured: after this program restarts
    // explorer itself, the new shell took about 4 s to broadcast, and six of
    // these attempts were used up waiting for it. Stopping at ten would have
    // given a second of headroom on a faster machine and a false "could not
    // register" on a slower one.
    static constexpr UINT_PTR kTrayReAddTimer = 0x4C53; // 'LS'
    static constexpr UINT kTrayReAddIntervalMs = 500;
    static constexpr int kTrayReAddMaxAttempts = 20;

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
    void OnTaskbarCreated();
    void TryReAddTrayIcon();
    void OpenSettingsDialog();
    void ApplyHotkeyFromConfig();
    void UpdateTooltip();
    void OpenShellPath(const std::wstring& path);
    void OpenConfigFolder();
    void OpenLogFile();

    HINSTANCE m_instance = nullptr;
    HWND m_window = nullptr;

    // "TaskbarCreated", registered in Init(). The shell broadcasts it after
    // building a fresh notification area, which is what tells us an icon it
    // used to hold has to be registered again. A registered message gets a
    // value the system picks at runtime, so it can only be compared against a
    // variable - never used as a case label.
    UINT m_taskbarCreatedMessage = 0;

    // Failed re-registration attempts for the current shell restart, and the
    // timer that paces the retries. Reset on every restart.
    int m_trayReAddAttempts = 0;

    TrayIcon m_tray;
    HotkeyManager m_hotkey;
    Scheduler m_scheduler;

    bool m_dialogOpen = false;
    bool m_shuttingDown = false;
};

} // namespace ls
