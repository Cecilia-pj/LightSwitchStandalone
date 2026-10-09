// LightSwitch Standalone - background scheduler (implementation).
#include "Scheduler.h"

#include "Config.h"
#include "Logging.h"
#include "StateManager.h"
#include "ThemeEngine.h"
#include "vendor/LightSwitchUtils.h"
#include "vendor/NightLightRegistryObserver.h"
#include "vendor/ThemeHelper.h"

namespace ls
{

namespace
{

// Mirrors upstream DetectAndHandleExternalThemeChange():
// if the user changed the theme from Windows Settings and it no longer matches
// the schedule, treat it as a manual override so we stop fighting the user
// until the next schedule boundary.
void DetectAndHandleExternalThemeChange(StateManager& stateManager)
{
    const Config config = SettingsStore::Instance().Get();
    if (config.scheduleMode == ScheduleMode::Off)
        return;

    const int nowMinutes = GetNowMinutes();

    int effectiveLight = config.lightTime;
    int effectiveDark = config.darkTime;

    if (config.scheduleMode == ScheduleMode::SunsetToSunrise)
    {
        const LightSwitchState snapshot = stateManager.GetState();
        if (snapshot.computedSunriseMinutes >= 0 && snapshot.computedSunsetMinutes >= 0)
        {
            effectiveLight = snapshot.computedSunriseMinutes + config.sunriseOffset;
            effectiveDark = snapshot.computedSunsetMinutes + config.sunsetOffset;
        }
    }

    bool shouldBeLight = false;
    if (config.scheduleMode == ScheduleMode::FollowNightLight)
    {
        shouldBeLight = !IsNightLightEnabled();
    }
    else
    {
        shouldBeLight = ShouldBeLight(nowMinutes, effectiveLight, effectiveDark);
    }

    const bool currentSystemLight = GetCurrentSystemTheme();
    const bool currentAppsLight = GetCurrentAppsTheme();

    const bool systemMismatch = config.changeSystem && (currentSystemLight != shouldBeLight);
    const bool appsMismatch = config.changeApps && (currentAppsLight != shouldBeLight);

    if ((systemMismatch || appsMismatch) && !stateManager.GetState().isManualOverride)
    {
        LOG_INFO(L"[Scheduler] External theme change detected; entering manual override mode.");
        stateManager.OnManualOverride();
    }
}

// Flips the theme by hand, exactly the way the PowerToys module interface did
// from its hotkey handler. Deliberately runs on the worker thread.
bool FlipThemeByHand()
{
    const Config config = SettingsStore::Instance().Get();

    if (!config.changeSystem && !config.changeApps)
    {
        LOG_WARN(L"[Scheduler] Toggle requested, but neither \"Windows system\" nor \"Applications\" is enabled in settings.");
        return false;
    }

    const bool currentlyLight = config.changeSystem ? GetCurrentSystemTheme() : GetCurrentAppsTheme();
    ApplyTheme(config, !currentlyLight);
    return true;
}

} // namespace

Scheduler::Scheduler()
{
    m_state = std::make_unique<StateManager>();

    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_tickEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_manualEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_nightLightEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

Scheduler::~Scheduler()
{
    Stop();

    EnsureNightLightWatcher(false);

    for (HANDLE* handle : { &m_stopEvent, &m_tickEvent, &m_manualEvent, &m_nightLightEvent })
    {
        if (*handle)
        {
            CloseHandle(*handle);
            *handle = nullptr;
        }
    }
}

bool Scheduler::Start()
{
    if (m_running)
        return true;

    m_running = true;
    ResetEvent(m_stopEvent);

    m_thread = CreateThread(nullptr, 0, &Scheduler::ThreadEntry, this, 0, nullptr);
    if (!m_thread)
    {
        m_running = false;
        LOG_ERROR(L"[Scheduler] CreateThread failed (error " << GetLastError() << L").");
        return false;
    }

    return true;
}

void Scheduler::Stop()
{
    if (!m_running.exchange(false))
    {
        if (m_thread)
        {
            WaitForSingleObject(m_thread, 15000);
            CloseHandle(m_thread);
            m_thread = nullptr;
        }
        return;
    }

    SetEvent(m_stopEvent);

    if (m_thread)
    {
        // The worker may be blocked inside a WM_SETTINGCHANGE broadcast for up
        // to 5s, so allow generously more than that before giving up.
        WaitForSingleObject(m_thread, 15000);
        CloseHandle(m_thread);
        m_thread = nullptr;
    }
}

void Scheduler::RequestTick()
{
    if (m_tickEvent)
        SetEvent(m_tickEvent);
}

void Scheduler::RequestToggle()
{
    if (m_manualEvent)
        SetEvent(m_manualEvent);
}

void Scheduler::EnsureNightLightWatcher(bool needed)
{
    if (needed && !m_nightLightWatcher)
    {
        LOG_INFO(L"[Scheduler] Starting Night Light registry watcher.");
        m_nightLightWatcher = std::make_unique<NightLightRegistryObserver>(
            HKEY_CURRENT_USER,
            NIGHT_LIGHT_REGISTRY_PATH,
            [this]() {
                if (m_nightLightEvent)
                    SetEvent(m_nightLightEvent);
            });
    }
    else if (!needed && m_nightLightWatcher)
    {
        LOG_INFO(L"[Scheduler] Stopping Night Light registry watcher.");
        m_nightLightWatcher->Stop();
        m_nightLightWatcher.reset();
    }
}

DWORD WINAPI Scheduler::ThreadEntry(LPVOID param)
{
    static_cast<Scheduler*>(param)->Run();
    return 0;
}

void Scheduler::Run()
{
    LOG_INFO(L"[Scheduler] Worker thread starting.");

    SettingsStore::Instance().StartWatching();

    {
        const Config config = SettingsStore::Instance().Get();
        // Note: this performs an initial theme application, which may block for
        // a few seconds inside the first WM_SETTINGCHANGE broadcast.
        m_state->SyncInitialThemeState();
        EnsureNightLightWatcher(config.scheduleMode == ScheduleMode::FollowNightLight);
    }

    const HANDLE settingsChanged = SettingsStore::Instance().ChangedEvent();

    while (m_running)
    {
        HANDLE waits[5];
        DWORD count = 0;
        waits[count++] = m_stopEvent;
        waits[count++] = m_tickEvent;
        waits[count++] = m_manualEvent;
        waits[count++] = m_nightLightEvent;
        waits[count++] = settingsChanged;

        // Sleep until the top of the next minute so the tick aligns with wall
        // clock minutes the way upstream does.
        SYSTEMTIME st;
        GetLocalTime(&st);
        int msToNextMinute = (60 - st.wSecond) * 1000 - st.wMilliseconds;
        if (msToNextMinute < 50)
            msToNextMinute = 50;

        const DWORD wait = WaitForMultipleObjects(count, waits, FALSE, static_cast<DWORD>(msToNextMinute));

        if (wait == WAIT_TIMEOUT)
        {
            DetectAndHandleExternalThemeChange(*m_state);
            m_state->OnTick();
            continue;
        }

        if (wait == WAIT_OBJECT_0)
        {
            LOG_INFO(L"[Scheduler] Stop event signaled; exiting worker.");
            break;
        }

        if (wait == WAIT_OBJECT_0 + 1)
        {
            ResetEvent(m_tickEvent);
            DetectAndHandleExternalThemeChange(*m_state);
            m_state->OnTick();
            continue;
        }

        if (wait == WAIT_OBJECT_0 + 2)
        {
            LOG_INFO(L"[Scheduler] Manual toggle requested.");
            if (FlipThemeByHand())
                m_state->OnManualOverride();
            continue;
        }

        if (wait == WAIT_OBJECT_0 + 3)
        {
            m_state->OnNightLightChange();
            continue;
        }

        if (wait == WAIT_OBJECT_0 + 4)
        {
            ResetEvent(settingsChanged);
            LOG_INFO(L"[Scheduler] Settings changed; re-evaluating.");

            const Config config = SettingsStore::Instance().Get();
            EnsureNightLightWatcher(config.scheduleMode == ScheduleMode::FollowNightLight);
            m_state->OnSettingsChanged();
            continue;
        }
    }

    EnsureNightLightWatcher(false);
    LOG_INFO(L"[Scheduler] Worker thread exiting cleanly.");
}

} // namespace ls
