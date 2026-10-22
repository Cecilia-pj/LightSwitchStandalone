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
#include "ThemeCatalog.h"

#include <mutex>

namespace ls
{

// Runtime-only information (never persisted).
struct LightSwitchState
{
    ScheduleMode lastAppliedMode = ScheduleMode::Off;
    bool isManualOverride = false;
    bool isNightLightActive = false;

    // The theme Windows reports, and the theme this program last put there.
    // Both are identities (ThemeId, or the path when the file has no id), not
    // booleans: a schedule can alternate between any two packs, and Windows
    // rewrites CurrentTheme to Custom.theme whenever the user tweaks a colour.
    theme::ThemeIdentity currentTheme;
    theme::ThemeIdentity lastAppliedTheme;

    // GetTickCount64() of the last successful apply. Everything observed inside
    // the grace window afterwards is attributed to that apply.
    ULONGLONG lastApplyTick = 0;

    // Back-off after a failed apply (missing file, unusable COM), as a
    // GetTickCount64() deadline. A tick counter rather than "minutes since
    // midnight" so it cannot wrap at midnight.
    ULONGLONG nextRetryTick = 0;

    // True while the last apply attempt failed; also used to log a broken
    // configuration once instead of once per tick.
    bool lastApplyFailed = false;

    // Guards against a warning every single minute when a side is unconfigured.
    bool warnedMissingTheme = false;

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

    // Refresh the cached identity from Windows without applying anything or
    // touching the manual-override flag.
    void AdoptCurrentIdentity(const theme::ThemeIdentity& identity);

    // Stop retrying a target that cannot be applied for the next while.
    void MarkApplyFailed(int retryAfterMinutes);

    // Copy of the current state (thread-safe).
    LightSwitchState GetState() const;

private:
    void EvaluateAndApplyIfNeeded();
    static bool CoordinatesAreValid(const std::wstring& latitude, const std::wstring& longitude);

    // Caller must hold m_mutex.
    bool UpdateSunTimesIfNeeded(const Config& config, LightSwitchState& state);
    void MarkApplyFailedLocked(int retryAfterMinutes);

    mutable std::mutex m_mutex;
    LightSwitchState m_state;
};

} // namespace ls
