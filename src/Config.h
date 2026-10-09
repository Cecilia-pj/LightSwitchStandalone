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
//   "language":         { "value": "auto" }    // standalone-only extension
// }
//
// "language" accepts "auto" (follow the Windows UI language) or a concrete
// code such as "en-US" / "zh-CN". It has no PowerToys counterpart, so a
// PowerToys settings.json can still be copied over verbatim - the key simply
// falls back to "auto".
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
