// LightSwitch Standalone - background scheduler.
//
// Owns the worker thread that replaces PowerToys'
// LightSwitchService.exe worker loop. It waits simultaneously on:
//   * stop          - process shutdown
//   * tickNow       - forced re-evaluation (system resume, tray command)
//   * manualToggle  - the global hotkey / tray "toggle now"
//   * applySide     - tray "light theme" / "dark theme" (a named pack)
//   * nightLight    - the night-light registry observer fired
//   * settings      - SettingsStore saw an external settings.json change
// and otherwise wakes up on the next minute boundary.
//
// Everything that touches the registry and broadcasts WM_SETTINGCHANGE runs on
// this thread, never on the UI thread.
#pragma once

#include <windows.h>

#include <atomic>
#include <memory>

// Vendored as-is from PowerToys, so it lives in the global namespace.
class NightLightRegistryObserver;

namespace ls
{

class StateManager;

class Scheduler
{
public:
    Scheduler();
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    bool Start();
    void Stop();

    // Force the worker to re-evaluate right now (no manual override toggle).
    void RequestTick();

    // Ask the worker to flip the theme by hand and enter manual-override mode.
    // The flip itself happens on the worker thread because applying a theme can
    // block for seconds - doing it here would freeze whichever thread called us
    // (usually the UI thread).
    void RequestToggle();

    // Ask the worker to apply one named side of the pair. Used by the tray
    // menu's "light theme" / "dark theme" entries. Like RequestToggle this
    // enters manual-override mode: the user said "this one, now", so the
    // schedule must not undo it a minute later.
    void RequestApplySide(bool darkSide);

    StateManager& State() { return *m_state; }

private:
    static DWORD WINAPI ThreadEntry(LPVOID param);
    void Run();

    void EnsureNightLightWatcher(bool needed);

    HANDLE m_stopEvent = nullptr;
    HANDLE m_tickEvent = nullptr;
    HANDLE m_manualEvent = nullptr;
    HANDLE m_sideEvent = nullptr;
    HANDLE m_nightLightEvent = nullptr;
    HANDLE m_thread = nullptr;

    // 0 = light side (themeA), 1 = dark side (themeB). Read by the worker after
    // m_sideEvent fires.
    std::atomic<int> m_pendingSide{ 0 };

    std::atomic<bool> m_running{ false };

    std::unique_ptr<StateManager> m_state;
    std::unique_ptr<NightLightRegistryObserver> m_nightLightWatcher;
};

} // namespace ls
