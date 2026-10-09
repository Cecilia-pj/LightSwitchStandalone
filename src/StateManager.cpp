// LightSwitch Standalone - scheduler state machine (implementation).
#include "StateManager.h"

#include "Config.h"
#include "Logging.h"
#include "ThemeEngine.h"
#include "vendor/LightSwitchUtils.h"
#include "vendor/ThemeHelper.h"
#include "vendor/ThemeScheduler.h"

#include <cstdlib>
#include <stdexcept>

namespace ls
{

LightSwitchState StateManager::GetState() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

bool StateManager::CoordinatesAreValid(const std::wstring& latitude, const std::wstring& longitude)
{
    try
    {
        const double latVal = std::stod(latitude);
        const double lonVal = std::stod(longitude);
        return !(latVal == 0 && lonVal == 0) && (latVal >= -90.0 && latVal <= 90.0) && (lonVal >= -180.0 && lonVal <= 180.0);
    }
    catch (...)
    {
        return false;
    }
}

bool StateManager::UpdateSunTimesIfNeeded(const Config& config, LightSwitchState& state)
{
    SYSTEMTIME st;
    GetLocalTime(&st);

    const bool newDay = (state.lastEvaluatedDay != st.wDay);
    const bool modeChangedToSun = (state.lastAppliedMode != ScheduleMode::SunsetToSunrise);

    if (!newDay && !modeChangedToSun)
        return false;

    double latitude = 0.0;
    double longitude = 0.0;
    try
    {
        latitude = std::stod(config.latitude);
        longitude = std::stod(config.longitude);
    }
    catch (...)
    {
        LOG_WARN(L"[StateManager] Invalid coordinates; keeping previous sun times.");
        return false;
    }

    const SunTimes times = CalculateSunriseSunset(latitude, longitude, st.wYear, st.wMonth, st.wDay);

    state.computedSunriseMinutes = times.sunriseHour * 60 + times.sunriseMinute;
    state.computedSunsetMinutes = times.sunsetHour * 60 + times.sunsetMinute;
    state.lastEvaluatedDay = st.wDay;

    LOG_INFO(L"[StateManager] Sun times for " << st.wYear << L"-" << st.wMonth << L"-" << st.wDay
                                              << L" -> sunrise " << (times.sunriseHour) << L":" << (times.sunriseMinute)
                                              << L", sunset " << (times.sunsetHour) << L":" << (times.sunsetMinute));
    return true;
}

void StateManager::SyncInitialThemeState()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_state.isSystemLightActive = GetCurrentSystemTheme();
    m_state.isAppsLightActive = GetCurrentAppsTheme();
    m_state.isNightLightActive = IsNightLightEnabled();

    LOG_INFO(L"[StateManager] Initial state: system=" << (m_state.isSystemLightActive ? L"light" : L"dark")
                                                      << L", apps=" << (m_state.isAppsLightActive ? L"light" : L"dark")
                                                      << L", nightLight=" << (m_state.isNightLightActive ? L"on" : L"off"));

    EvaluateAndApplyIfNeeded();
}

void StateManager::OnSettingsChanged()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // If manual override was active, clear it so new settings take effect.
    if (m_state.isManualOverride)
    {
        LOG_INFO(L"[StateManager] Settings changed; clearing manual override.");
        m_state.isManualOverride = false;
    }

    EvaluateAndApplyIfNeeded();
}

void StateManager::OnTick()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state.lastAppliedMode != ScheduleMode::FollowNightLight)
    {
        EvaluateAndApplyIfNeeded();
    }
}

void StateManager::OnManualOverride()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_state.isManualOverride = !m_state.isManualOverride;
    LOG_INFO(L"[StateManager] Manual override is now " << (m_state.isManualOverride ? L"ON" : L"OFF"));

    // The caller has already flipped the Windows theme, regardless of which
    // direction isManualOverride just toggled. Sync the cached state so the
    // scheduler compares against the actual current theme on the next
    // evaluation.
    m_state.isSystemLightActive = GetCurrentSystemTheme();
    m_state.isAppsLightActive = GetCurrentAppsTheme();

    EvaluateAndApplyIfNeeded();
}

void StateManager::OnNightLightChange()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const bool newNightLightState = IsNightLightEnabled();

    // In Follow Night Light mode, treat a Night Light toggle as a boundary.
    if (m_state.lastAppliedMode == ScheduleMode::FollowNightLight && m_state.isManualOverride)
    {
        LOG_INFO(L"[StateManager] Night Light changed while manual override active; treating as a boundary and clearing manual override.");
        m_state.isManualOverride = false;
    }

    if (newNightLightState != m_state.isNightLightActive)
    {
        LOG_INFO(L"[StateManager] Night Light toggled to " << (newNightLightState ? L"ON" : L"OFF"));
        m_state.isNightLightActive = newNightLightState;
    }

    EvaluateAndApplyIfNeeded();
}

void StateManager::EvaluateAndApplyIfNeeded()
{
    const Config config = SettingsStore::Instance().Get();
    const int now = GetNowMinutes();

    // Early exit: OFF mode just pauses activity.
    if (config.scheduleMode == ScheduleMode::Off)
    {
        m_state.lastAppliedMode = ScheduleMode::Off;
        m_state.lastTickMinutes = now;
        return;
    }

    const bool coordsValid = CoordinatesAreValid(config.latitude, config.longitude);

    // Handle Sun Mode recalculation.
    if (config.scheduleMode == ScheduleMode::SunsetToSunrise && coordsValid)
    {
        if (UpdateSunTimesIfNeeded(config, m_state))
        {
            m_state.effectiveLightMinutes = m_state.computedSunriseMinutes + config.sunriseOffset;
            m_state.effectiveDarkMinutes = m_state.computedSunsetMinutes + config.sunsetOffset;
        }
        else if (m_state.computedSunriseMinutes >= 0 && m_state.computedSunsetMinutes >= 0)
        {
            m_state.effectiveLightMinutes = m_state.computedSunriseMinutes + config.sunriseOffset;
            m_state.effectiveDarkMinutes = m_state.computedSunsetMinutes + config.sunsetOffset;
        }
        else
        {
            // No valid sun times available yet (e.g. polar day/night):
            // fall back to the stored fixed times so we never wedge.
            m_state.effectiveLightMinutes = config.lightTime;
            m_state.effectiveDarkMinutes = config.darkTime;
        }
    }
    else if (config.scheduleMode == ScheduleMode::FixedHours)
    {
        m_state.effectiveLightMinutes = config.lightTime;
        m_state.effectiveDarkMinutes = config.darkTime;
    }

    // Handle manual override logic.
    if (m_state.isManualOverride)
    {
        bool crossedBoundary = false;
        if (m_state.lastTickMinutes != -1)
        {
            const int prev = m_state.lastTickMinutes;

            // Handle midnight wraparound safely.
            if (now < prev)
            {
                crossedBoundary =
                    (prev <= m_state.effectiveLightMinutes || now >= m_state.effectiveLightMinutes) ||
                    (prev <= m_state.effectiveDarkMinutes || now >= m_state.effectiveDarkMinutes);
            }
            else
            {
                crossedBoundary =
                    (prev < m_state.effectiveLightMinutes && now >= m_state.effectiveLightMinutes) ||
                    (prev < m_state.effectiveDarkMinutes && now >= m_state.effectiveDarkMinutes);
            }
        }

        if (crossedBoundary)
        {
            LOG_INFO(L"[StateManager] Boundary crossed; releasing manual override.");
            m_state.isManualOverride = false;
        }
        else
        {
            m_state.lastTickMinutes = now;
            return;
        }
    }

    m_state.lastAppliedMode = config.scheduleMode;

    bool shouldBeLight = false;
    if (config.scheduleMode == ScheduleMode::FollowNightLight)
    {
        shouldBeLight = !m_state.isNightLightActive;
    }
    else
    {
        shouldBeLight = ShouldBeLight(now, m_state.effectiveLightMinutes, m_state.effectiveDarkMinutes);
    }

    const bool appsNeedsToChange = config.changeApps && (m_state.isAppsLightActive != shouldBeLight);
    const bool systemNeedsToChange = config.changeSystem && (m_state.isSystemLightActive != shouldBeLight);

    // Only apply the theme if there is a change and no override is active.
    if (!m_state.isManualOverride && (appsNeedsToChange || systemNeedsToChange))
    {
        LOG_INFO(L"[StateManager] Applying " << (shouldBeLight ? L"light" : L"dark") << L" theme.");
        ApplyTheme(config, shouldBeLight);

        m_state.isSystemLightActive = GetCurrentSystemTheme();
        m_state.isAppsLightActive = GetCurrentAppsTheme();
    }

    m_state.lastTickMinutes = now;
}

} // namespace ls
