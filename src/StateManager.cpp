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

namespace
{

// A failed apply is retried half an hour later, not every minute: the usual
// causes (theme file moved, theme service disabled) do not fix themselves
// within a tick, and each retry costs a wallpaper swap attempt.
constexpr int kApplyRetryMinutes = 30;

} // namespace

LightSwitchState StateManager::GetState() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

void StateManager::AdoptCurrentIdentity(const theme::ThemeIdentity& identity)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_state.currentTheme = identity;
}

void StateManager::MarkApplyFailedLocked(int retryAfterMinutes)
{
    m_state.lastApplyFailed = true;
    m_state.nextRetryTick = GetTickCount64() + static_cast<ULONGLONG>(retryAfterMinutes) * 60ULL * 1000ULL;
}

void StateManager::MarkApplyFailed(int retryAfterMinutes)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    MarkApplyFailedLocked(retryAfterMinutes);
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

    m_state.currentTheme = theme::ReadCurrentIdentity();
    m_state.lastAppliedTheme = theme::ThemeIdentity{};
    m_state.lastApplyTick = 0;
    m_state.isNightLightActive = IsNightLightEnabled();

    LOG_INFO(L"[StateManager] Initial theme: \"" << m_state.currentTheme.path << L"\" (id "
                                                 << (m_state.currentTheme.themeId.empty() ? L"none" : m_state.currentTheme.themeId)
                                                 << L"), nightLight=" << (m_state.isNightLightActive ? L"on" : L"off") << L".");

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

    // The user may have pointed a side at a different theme, so "we already
    // applied that one" no longer means anything.
    m_state.lastAppliedTheme = theme::ThemeIdentity{};
    m_state.nextRetryTick = 0;

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
    m_state.currentTheme = theme::ReadCurrentIdentity();

    // Whatever we applied last is no longer what is on screen, so the
    // "already tried this one" shortcut must not swallow the next apply after
    // the override is released at the following boundary.
    m_state.lastAppliedTheme = theme::ThemeIdentity{};
    m_state.nextRetryTick = 0;

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

    const bool shouldBeLight = (config.scheduleMode == ScheduleMode::FollowNightLight)
                                   ? !m_state.isNightLightActive
                                   : ShouldBeLight(now, m_state.effectiveLightMinutes, m_state.effectiveDarkMinutes);

    const std::wstring& targetPath = shouldBeLight ? config.themeA : config.themeB;
    if (targetPath.empty())
    {
        if (!m_state.warnedMissingTheme)
        {
            m_state.warnedMissingTheme = true;
            LOG_WARN(L"[StateManager] Theme " << (shouldBeLight ? L"A (light side)" : L"B (dark side)")
                                              << L" is not configured; automatic switching stays idle.");
        }
        m_state.lastTickMinutes = now;
        return;
    }
    m_state.warnedMissingTheme = false;

    const ThemeTarget target = MakeTarget(targetPath);

    // Three reasons to leave the desktop alone. "Matches current" is the normal
    // case; "already tried" covers the degraded path where the pack could not
    // be applied and only the registry mode changed, so the identity will never
    // match; the retry deadline covers a target that failed outright.
    const bool matchesCurrent = theme::SameIdentity(m_state.currentTheme, target.identity);
    const bool alreadyTried = theme::SameIdentity(m_state.lastAppliedTheme, target.identity);
    if (matchesCurrent || alreadyTried)
    {
        m_state.lastTickMinutes = now;
        return;
    }

    if (GetTickCount64() < m_state.nextRetryTick)
    {
        m_state.lastTickMinutes = now;
        return;
    }

    LOG_INFO(L"[StateManager] Applying theme " << (shouldBeLight ? L"A" : L"B") << L": \"" << target.path << L"\".");
    const ApplyReport report = ApplyThemeTarget(target);

    if (IsSuccess(report))
    {
        m_state.currentTheme = (report.status == ApplyStatus::Ok) ? report.observed : target.identity;
        m_state.lastAppliedTheme = target.identity;
        m_state.lastApplyTick = GetTickCount64();
        m_state.nextRetryTick = 0;
        m_state.lastApplyFailed = false;
        LOG_INFO(L"[StateManager] " << ApplyStatusName(report.status) << L": " << report.detail << L".");
    }
    else
    {
        MarkApplyFailedLocked(kApplyRetryMinutes);
        LOG_WARN(L"[StateManager] Could not apply \"" << target.path << L"\" (" << ApplyStatusName(report.status)
                                                      << L": " << report.detail << L"); retrying in "
                                                      << kApplyRetryMinutes << L" minutes.");
    }

    m_state.lastTickMinutes = now;
}

} // namespace ls
