// LightSwitch Standalone - scheduler state machine.
//
// Faithful port of PowerToys'
//   src/modules/LightSwitch/LightSwitchService/LightSwitchStateManager.{h,cpp}
//
// Deliberate deviations from upstream (documented in README.md / UPSTREAM.md):
//   1. No NotifyPowerDisplayThemeChanged(): that was a cross-module hook into
//      the Power Display module which does not exist here.
//   2. Sunset/sunrise results are no longer written back into settings.json.
//      Writing them back, combined with a file watcher, creates a feedback loop
//      that repeatedly re-enters the state machine. The computed times are kept
//      in memory and exposed through GetState() so the settings dialog can show
//      them.
//   3. Settings are read from the SettingsStore singleton instead of
//      LightSwitchSettings::instance().
// Everything else - decision order, manual-override semantics, boundary
// crossing detection, ColorPrevalence handling - is unchanged.
#pragma once

#include "Config.h"

#include <mutex>

namespace ls
{

// Runtime-only information (never persisted).
struct LightSwitchState
{
    ScheduleMode lastAppliedMode = ScheduleMode::Off;
    bool isManualOverride = false;
    bool isSystemLightActive = false;
    bool isAppsLightActive = false;
    bool isNightLightActive = false;
    int lastEvaluatedDay = -1;
    int lastTickMinutes = -1;

    // Derived, runtime-resolved boundaries (minutes since midnight).
    int effectiveLightMinutes = 0;
    int effectiveDarkMinutes = 0;

    // Informational: last computed sunrise/sunset (-1 when polar day/night).
    int computedSunriseMinutes = -1;
    int computedSunsetMinutes = -1;
};

class StateManager
{
public:
    StateManager() = default;

    // Initial sync at startup: align internal state with the real Windows theme.
    void SyncInitialThemeState();

    // Called every minute by the scheduler worker.
    void OnTick();

    // Called when settings.json changed (or was first loaded).
    void OnSettingsChanged();

    // Called when the toggle hotkey / tray command flipped the theme by hand.
    void OnManualOverride();

    // Called when the night-light registry value changed.
    void OnNightLightChange();

    // Copy of the current state (thread-safe).
    LightSwitchState GetState() const;

private:
    void EvaluateAndApplyIfNeeded();
    static bool CoordinatesAreValid(const std::wstring& latitude, const std::wstring& longitude);

    // Caller must hold m_mutex.
    bool UpdateSunTimesIfNeeded(const Config& config, LightSwitchState& state);

    mutable std::mutex m_mutex;
    LightSwitchState m_state;
};

} // namespace ls
