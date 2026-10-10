// LightSwitch Standalone - configuration store.
//
// Replaces PowerToys' SettingsAPI / FileWatcher. Deliberately keeps the exact
// upstream settings.json key names and the upstream {"value": ...} wrapper so
// that a PowerToys settings.json can be copied over verbatim and compared
// side by side.
//
// Layout written on disk (UTF-8):
// {
//   "changeSystem":     { "value": true },
//   "changeApps":       { "value": true },
//   "scheduleMode":     { "value": "Off" },
//   "lightTime":        { "value": 480 },
//   "darkTime":         { "value": 1200 },
//   "sunrise_offset":   { "value": 0 },
//   "sunset_offset":    { "value": 0 },
//   "latitude":         { "value": "0.0" },
//   "longitude":        { "value": "0.0" },
//   "toggle-theme-hotkey": { "value": { "win": true, "ctrl": true, "alt": false,
//                                       "shift": true, "code": 68 } },
//   "startOnLogin":     { "value": false },    // standalone-only extension
//   "language":         { "value": "auto" },   // standalone-only extension
//   "uiTheme":          { "value": "auto" },   // standalone-only extension
//   "trayClickTogglesTheme": { "value": false } // standalone-only extension
// }
//
// "language" accepts "auto" (follow the Windows UI language) or a concrete
// code such as "en-US" / "zh-CN". It has no PowerToys counterpart, so a
// PowerToys settings.json can still be copied over verbatim - the key simply
// falls back to "auto".
//
// "uiTheme" accepts "auto" (follow the Windows app theme, AppsUseLightTheme),
// "light" or "dark". It has no PowerToys counterpart either. Both keys are
// additive: this program only ever drops keys it does not model, and the keys it
// does model survive a round trip.
//
// "trayClickTogglesTheme" defaults to false, and that default is deliberate: a
// single left click on the tray icon is also what a tap produces on a touch
// screen, so an accidental brush across the icon would otherwise flip the whole
// Windows theme with no confirmation and no undo. Set it to true to get the
// click-to-toggle gesture back. Note this is a plain single click, not a long
// press - the program has no press-duration logic anywhere.
#pragma once

#include <string>
#include <windows.h>

namespace ls
{

enum class ScheduleMode
{
    Off,
    FixedHours,
    SunsetToSunrise,
    FollowNightLight
};

std::wstring ScheduleModeToString(ScheduleMode mode);
ScheduleMode ScheduleModeFromString(const std::wstring& value);

// Canonical form of a settings.json "uiTheme" value: "auto", "light" or "dark".
// Case insensitive and whitespace tolerant. Anything unrecognised - including a
// typo - becomes "auto", so a bad value degrades to following Windows instead of
// pinning the interface to a theme the user did not ask for.
std::wstring NormalizeUiTheme(const std::wstring& value);

struct HotkeyConfig
{
    bool win = true;
    bool ctrl = true;
    bool alt = false;
    bool shift = true;
    unsigned int vk = 0x44; // 'D'
};

struct Config
{
    bool changeSystem = true;
    bool changeApps = true;

    ScheduleMode scheduleMode = ScheduleMode::Off;

    // Minutes since midnight.
    int lightTime = 480;  // 08:00
    int darkTime = 1200;  // 20:00

    int sunriseOffset = 0;
    int sunsetOffset = 0;

    std::wstring latitude = L"0.0";
    std::wstring longitude = L"0.0";

    HotkeyConfig hotkey;

    // Standalone-only: not present in PowerToys settings.json.
    bool startOnLogin = false;

    // Standalone-only: "auto" follows the Windows UI language, otherwise a
    // concrete code such as "en-US" or "zh-CN". See src/L10n.h.
    std::wstring language = L"auto";

    // Standalone-only: interface theme for the settings dialog and the tray
    // menu. "auto" follows the Windows app theme (AppsUseLightTheme, i.e. the
    // same value Light Switch itself writes); "light" and "dark" pin it. See
    // src/dark/DarkApp.h.
    std::wstring uiTheme = L"auto";

    // Standalone-only: whether a single left click on the tray icon toggles the
    // theme. Off by default, because a screen tap arrives as the same message a
    // mouse click does (WM_LBUTTONUP), so any touch user who brushes past the
    // icon would otherwise change the theme of the whole machine by accident.
    //
    // When this is false a single click does nothing at all - deliberately, so
    // that a tap cannot trigger anything. The other three ways to toggle are
    // unaffected: the global hotkey, "Toggle now" in the tray context menu, and
    // the button of the same name in the settings dialog. Double-clicking the
    // icon still opens the settings dialog.
    bool trayClickTogglesTheme = false;

    // Normalised copy with all times wrapped into [0, 1439] -- the scheduler
    // and the dialog should use these instead of the raw values.
    int NormalizedLightTime() const { return (lightTime % 1440 + 1440) % 1440; }
    int NormalizedDarkTime() const { return (darkTime % 1440 + 1440) % 1440; }
};

// Thread-safe singleton. All accessors return copies so callers can never
// observe a half-updated config from another thread.
class SettingsStore
{
public:
    static SettingsStore& Instance();

    Config Get() const;

    // Persist + publish. Signals ChangedEvent() so the scheduler re-evaluates.
    bool SetAndSave(const Config& config);

    // Re-read from disk (used by the file watcher and at startup).
    bool Load();

    // Write the in-memory config back out (atomic replace).
    bool Save();

    HANDLE ChangedEvent() const { return m_changedEvent; }

    // Absolute path of the active settings file.
    std::wstring ConfigFilePath() const;

    void StartWatching();
    void StopWatching();

private:
    SettingsStore();
    ~SettingsStore();

    SettingsStore(const SettingsStore&) = delete;
    SettingsStore& operator=(const SettingsStore&) = delete;

    std::wstring ResolveConfigPath();
    std::string Serialize(const Config& config) const;
    bool Deserialize(const std::string& utf8, Config& config, std::string& error) const;
    void WatchLoop();

    mutable CRITICAL_SECTION m_lock;
    Config m_config;

    std::wstring m_path;
    std::string m_lastRawContent;

    HANDLE m_changedEvent = nullptr;
    HANDLE m_cancelEvent = nullptr;
    HANDLE m_watchThread = nullptr;
};

} // namespace ls
