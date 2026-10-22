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

// Anything observed this soon after our own successful apply is attributed to
// that apply, not to the user. Windows rewrites CurrentTheme to Custom.theme
// whenever a colour, wallpaper mode or accent is touched, and the rewrite has a
// different ThemeId and a different path - so no amount of identity comparison
// survives it. Without this window the program would detect its own work as a
// manual override and then sit out the rest of the day.
constexpr ULONGLONG kPostApplyGraceMs = 30000;

constexpr int kRetryMinutes = 30;

// Which side of the pair the schedule currently wants. Mirrors the arithmetic
// in StateManager::EvaluateAndApplyIfNeeded - kept separate because the
// detection path must not touch the state it is about to correct.
bool ScheduleWantsDark(StateManager& stateManager)
{
    const Config config = SettingsStore::Instance().Get();
    if (config.scheduleMode == ScheduleMode::FollowNightLight)
        return IsNightLightEnabled();

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

    return !ShouldBeLight(nowMinutes, effectiveLight, effectiveDark);
}

// Mirrors upstream DetectAndHandleExternalThemeChange():
// if the user changed the theme from Windows Settings and it no longer matches
// the schedule, treat it as a manual override so we stop fighting the user
// until the next schedule boundary.
//
// Since 1.4.0 the comparison is an identity comparison against the configured
// pack, with three escape hatches in front of it - see the comments inline.
void DetectAndHandleExternalThemeChange(StateManager& stateManager)
{
    const Config config = SettingsStore::Instance().Get();
    if (config.scheduleMode == ScheduleMode::Off)
        return;

    const std::wstring targetPath = ScheduleWantsDark(stateManager) ? config.themeB : config.themeA;
    if (targetPath.empty())
        return;

    const theme::ThemeIdentity current = theme::ReadCurrentIdentity();
    const LightSwitchState state = stateManager.GetState();

    // Already standing down: just keep the cache fresh.
    if (state.isManualOverride)
    {
        stateManager.AdoptCurrentIdentity(current);
        return;
    }

    const ThemeTarget target = MakeTarget(targetPath);

    // On schedule. The common case, and the one that must stay silent.
    if (theme::SameIdentity(current, target.identity))
    {
        stateManager.AdoptCurrentIdentity(current);
        return;
    }

    // Our own apply, possibly rewritten by Windows on the way out.
    if (state.lastApplyTick != 0 && GetTickCount64() - state.lastApplyTick < kPostApplyGraceMs)
    {
        LOG_INFO(L"[Scheduler] Theme identity differs inside the post-apply grace window; "
                 L"treating it as our own result (current=\"" << current.path << L"\").");
        stateManager.AdoptCurrentIdentity(current);
        return;
    }

    // Still the theme we last wrote: Windows merely re-spelled the path or the
    // user re-saved the same pack. Not an external change.
    if (theme::SameIdentity(current, state.lastAppliedTheme))
    {
        stateManager.AdoptCurrentIdentity(current);
        return;
    }

    // The target itself is broken. That is a configuration error, not the user
    // taking over, so back off instead of entering override.
    if (theme::DescribePath(targetPath).fileMissing)
    {
        if (!state.lastApplyFailed)
        {
            LOG_WARN(L"[Scheduler] Scheduled theme file is missing: \"" << targetPath
                                                                       << L"\". Backing off, not overriding.");
            stateManager.MarkApplyFailed(kRetryMinutes);
        }
        return;
    }

    LOG_INFO(L"[Scheduler] External theme change detected: current=\"" << current.path
                                                                      << L"\" (" << (current.themeId.empty() ? L"no id" : current.themeId)
                                                                      << L"), schedule wants \"" << targetPath
                                                                      << L"\". Entering manual override.");
    stateManager.OnManualOverride();
}

// Applies one named side. Deliberately runs on the worker thread.
ApplyReport ApplySide(bool darkSide)
{
    const Config config = SettingsStore::Instance().Get();
    const std::wstring path = darkSide ? config.themeB : config.themeA;

    ApplyReport report;
    report.requestedPath = path;

    if (path.empty())
    {
        LOG_WARN(L"[Scheduler] Asked for the " << (darkSide ? L"dark" : L"light")
                                              << L" theme, but that side is not configured.");
        report.status = ApplyStatus::FileMissing;
        report.detail = L"that side has no theme configured";
        return report;
    }

    LOG_INFO(L"[Scheduler] Applying the " << (darkSide ? L"dark" : L"light") << L" theme: \"" << path << L"\".");
    return ApplyThemeTarget(MakeTarget(path));
}

// Flips to the other side of the pair, exactly the way the PowerToys module
// interface did from its hotkey handler.
ApplyReport FlipThemeByHand()
{
    const Config config = SettingsStore::Instance().Get();

    if (config.themeA.empty() || config.themeB.empty())
    {
        LOG_WARN(L"[Scheduler] Toggle requested, but both themes must be chosen first "
                 L"(themeA=" << (config.themeA.empty() ? L"unset" : L"set")
                 << L", themeB=" << (config.themeB.empty() ? L"unset" : L"set") << L").");
        ApplyReport report;
        report.status = ApplyStatus::FileMissing;
        report.detail = L"one of the two themes is not configured";
        return report;
    }

    const theme::ThemeIdentity current = theme::ReadCurrentIdentity();
    const bool currentlyDark = theme::SameIdentity(current, MakeTarget(config.themeB).identity);

    return ApplySide(!currentlyDark);
}

} // namespace

Scheduler::Scheduler()
{
    m_state = std::make_unique<StateManager>();

    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_tickEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_manualEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_sideEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_nightLightEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

Scheduler::~Scheduler()
{
    Stop();

    EnsureNightLightWatcher(false);

    for (HANDLE* handle : { &m_stopEvent, &m_tickEvent, &m_manualEvent, &m_sideEvent, &m_nightLightEvent })
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

void Scheduler::RequestApplySide(bool darkSide)
{
    m_pendingSide = darkSide ? 1 : 0;
    if (m_sideEvent)
        SetEvent(m_sideEvent);
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
        HANDLE waits[6];
        DWORD count = 0;
        waits[count++] = m_stopEvent;
        waits[count++] = m_tickEvent;
        waits[count++] = m_manualEvent;
        waits[count++] = m_sideEvent;
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
            const ApplyReport report = FlipThemeByHand();
            if (IsSuccess(report))
                m_state->OnManualOverride();
            else
                LOG_WARN(L"[Scheduler] Manual toggle did nothing (" << ApplyStatusName(report.status)
                                                                   << L": " << report.detail << L").");
            continue;
        }

        if (wait == WAIT_OBJECT_0 + 3)
        {
            const bool darkSide = m_pendingSide.load() != 0;
            const ApplyReport report = ApplySide(darkSide);
            if (IsSuccess(report))
                m_state->OnManualOverride();
            else
                LOG_WARN(L"[Scheduler] Applying the " << (darkSide ? L"dark" : L"light") << L" theme failed ("
                                                      << ApplyStatusName(report.status) << L": " << report.detail << L").");
            continue;
        }

        if (wait == WAIT_OBJECT_0 + 4)
        {
            m_state->OnNightLightChange();
            continue;
        }

        if (wait == WAIT_OBJECT_0 + 5)
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
