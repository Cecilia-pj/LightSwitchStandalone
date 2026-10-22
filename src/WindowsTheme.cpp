// LightSwitch Standalone - Windows theme packs via IThemeManager2 (implementation).
#include "WindowsTheme.h"

#include "Logging.h"

#include <objbase.h>
#include <objidl.h>

#include <atomic>
#include <mutex>

namespace ls
{

namespace
{

// ---------------------------------------------------------------------------
// The interface, declared once.
//
// theme-switch.ps1 enumerates the same 24 methods in the same order against the
// same IID, which is how this list was verified. Declaring the vtable by hand
// is the only option - themeui.dll ships no header - so the list is the single
// source of truth for both the declarations and the slot numbers, and the
// static_asserts below catch an edit that shifts a method.
//
// If this ever needs to change, change this macro and nothing else.
// ---------------------------------------------------------------------------
#define LS_TM2_METHODS(X)                                        \
    X(Init, (int))                                               \
    X(InitAsync, (HWND, int))                                     \
    X(Refresh, ())                                                \
    X(RefreshAsync, (HWND, int))                                  \
    X(RefreshComplete, ())                                        \
    X(GetThemeCount, (int*))                                      \
    X(GetTheme, (int, IUnknown**))                                \
    X(IsThemeDisabled, (int, int*))                               \
    X(GetCurrentTheme, (int*))                                    \
    X(SetCurrentTheme, (HWND, int, int, int, int))                \
    X(GetCustomTheme, (int*))                                     \
    X(GetDefaultTheme, (int*))                                    \
    X(CreateThemePack, (HWND, LPCWSTR, int))                      \
    X(CloneAndSetCurrentTheme, (HWND, LPCWSTR, LPWSTR*))          \
    X(InstallThemePack, (HWND, LPCWSTR, int, int, LPWSTR*, IUnknown**)) \
    X(DeleteTheme, (LPCWSTR))                                     \
    X(OpenTheme, (HWND, LPCWSTR, int))                            \
    X(AddAndSelectTheme, (HWND, LPCWSTR, int, int))               \
    X(SQMCurrentTheme, ())                                        \
    X(ExportRoamingThemeToStream, (IStream*, int))                \
    X(ImportRoamingThemeFromStream, (IStream*, int))              \
    X(UpdateColorSettingsForLogonUI, ())                          \
    X(GetDefaultThemeId, (GUID*))                                 \
    X(UpdateCustomTheme, ())

struct __declspec(uuid("c1e8c83e-845d-4d95-81db-e283fdffc000")) IThemeManager2 : IUnknown
{
#define LS_TM2_DECLARE(name, params) virtual HRESULT STDMETHODCALLTYPE name params = 0;
    LS_TM2_METHODS(LS_TM2_DECLARE)
#undef LS_TM2_DECLARE
};

enum : int
{
    kSlot_QueryInterface = 0,
    kSlot_AddRef = 1,
    kSlot_Release = 2,
#define LS_TM2_SLOT(name, params) kSlot_##name,
    LS_TM2_METHODS(LS_TM2_SLOT)
#undef LS_TM2_SLOT
    kSlot_Count
};

static_assert(kSlot_Init == 3, "Init must be the first method after IUnknown.");
static_assert(kSlot_OpenTheme == 19, "OpenTheme must sit at vtable slot 19.");
static_assert(kSlot_Count == 27, "IUnknown (3) plus 24 IThemeManager2 methods.");

const CLSID kClsidThemeManager2 = { 0x9324da94, 0x50ec, 0x4a14,
                                    { 0xa7, 0x70, 0xe9, 0x0c, 0xa0, 0x3e, 0x7c, 0x8f } };

// InitFlags.NoFlags, PackFlags.Silent (no hourglass, no dialog, no theme sound).
constexpr int kInitNoFlags = 0;
constexpr int kPackFlagsSilent = 4;

// The wallpaper swap is asynchronous; CurrentTheme follows within a few hundred
// milliseconds on this machine. Two seconds is generous without being so long
// that a broken call hides behind it.
constexpr int kReadBackAttempts = 20;
constexpr DWORD kReadBackIntervalMs = 100;

struct Worker
{
    HANDLE thread = nullptr;
    HANDLE jobEvent = nullptr;   // auto-reset: "there is work in the slot"
    HANDLE doneEvent = nullptr;  // auto-reset: "the slot finished"
    HANDLE quitEvent = nullptr;  // manual-reset
    HANDLE readyEvent = nullptr; // manual-reset: init finished, successfully or not

    std::mutex lock;
    std::wstring pendingPath;
    unsigned long long pendingId = 0;
    unsigned long long nextId = 0;
    unsigned long long completedId = 0;
    bool hasPending = false;

    ApplyReport lastReport;

    HRESULT initHresult = E_FAIL;
    int themeCount = 0;
    std::wstring initDetail;

    std::atomic<bool> attempted{ false };
};

Worker g_worker;
std::mutex g_startLock;
bool g_shutDown = false;

void CloseHandles(Worker& worker)
{
    HANDLE* handles[] = { &worker.jobEvent, &worker.doneEvent, &worker.quitEvent, &worker.readyEvent };
    for (HANDLE* handle : handles)
    {
        if (*handle != nullptr)
        {
            CloseHandle(*handle);
            *handle = nullptr;
        }
    }
}

// Runs on the STA worker only.
ApplyReport ApplyOnWorker(IThemeManager2* manager, const std::wstring& path)
{
    ApplyReport report;
    report.requestedPath = path;

    if (manager == nullptr)
    {
        report.status = ApplyStatus::Disabled;
        report.hresult = g_worker.initHresult;
        report.detail = g_worker.initDetail;
        return report;
    }

    const theme::ThemeInfo info = theme::DescribePath(path);
    if (info.fileMissing)
    {
        report.status = ApplyStatus::FileMissing;
        report.detail = L"theme file does not exist";
        report.observed = theme::ReadCurrentIdentity();
        return report;
    }

    const theme::ThemeIdentity target{ info.themeId, info.path };
    if (theme::SameIdentity(theme::ReadCurrentIdentity(), target))
    {
        report.status = ApplyStatus::AlreadyCurrent;
        report.observed = target;
        report.detail = L"already active";
        return report;
    }

    const HRESULT opened = manager->OpenTheme(nullptr, info.path.c_str(), kPackFlagsSilent);
    report.hresult = opened;
    if (FAILED(opened))
    {
        report.status = ApplyStatus::Failed;
        report.detail = L"OpenTheme failed";
        report.observed = theme::ReadCurrentIdentity();
        return report;
    }

    for (int attempt = 0; attempt < kReadBackAttempts; ++attempt)
    {
        Sleep(kReadBackIntervalMs);

        const theme::ThemeIdentity now = theme::ReadCurrentIdentity();
        report.observed = now;
        if (theme::SameIdentity(now, target))
        {
            report.status = ApplyStatus::Ok;
            report.detail = L"applied";
            return report;
        }
    }

    // The call succeeded but the theme never became current. In practice this
    // is the policy-disabled case: themeui.dll answers S_OK and does nothing.
    report.status = ApplyStatus::Failed;
    report.detail = L"OpenTheme returned S_OK but CurrentTheme never matched";
    return report;
}

DWORD WINAPI WorkerEntry(LPVOID)
{
    // No message pump on purpose. These are in-process calls with no
    // cross-apartment marshalling, and the prototype this module was ported
    // from (theme-switch.ps1) ran the same way. Adding a pump would only
    // create a second place for a deadlock to live.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    IThemeManager2* manager = nullptr;
    HRESULT result = com;

    if (SUCCEEDED(com))
    {
        result = CoCreateInstance(kClsidThemeManager2, nullptr, CLSCTX_ALL,
                                  __uuidof(IThemeManager2), reinterpret_cast<void**>(&manager));
        if (SUCCEEDED(result) && manager != nullptr)
        {
            result = manager->Init(kInitNoFlags);
            if (SUCCEEDED(result))
            {
                int count = 0;
                result = manager->GetThemeCount(&count);
                if (SUCCEEDED(result))
                {
                    // A wrong vtable slot usually shows up here as an absurd
                    // number rather than as a crash, which is much easier to
                    // act on than an access violation inside themeui.
                    if (count <= 0 || count > 4096)
                    {
                        result = E_UNEXPECTED;
                        g_worker.initDetail = L"theme count out of range - vtable layout suspect";
                        LOG_ERROR(L"[WindowsTheme] GetThemeCount returned " << count
                                                                           << L"; refusing to use IThemeManager2.");
                    }
                    else
                    {
                        g_worker.themeCount = count;
                    }
                }
            }
        }
    }

    if (FAILED(result) || manager == nullptr)
    {
        g_worker.initHresult = FAILED(result) ? result : E_NOINTERFACE;
        if (g_worker.initDetail.empty())
            g_worker.initDetail = L"IThemeManager2 is unavailable";
        LOG_WARN(L"[WindowsTheme] Theme manager unavailable (hr=0x" << std::hex
                                                                   << static_cast<unsigned long>(g_worker.initHresult)
                                                                   << std::dec << L", " << g_worker.initDetail << L").");
        if (manager != nullptr)
        {
            manager->Release();
            manager = nullptr;
        }
    }
    else
    {
        g_worker.initHresult = S_OK;
        LOG_INFO(L"[WindowsTheme] IThemeManager2 ready, " << g_worker.themeCount << L" themes.");
    }

    SetEvent(g_worker.readyEvent);

    for (;;)
    {
        HANDLE waits[2] = { g_worker.quitEvent, g_worker.jobEvent };
        const DWORD signaled = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (signaled != WAIT_OBJECT_0 + 1)
            break;

        std::wstring path;
        unsigned long long id = 0;
        {
            std::lock_guard<std::mutex> lock(g_worker.lock);
            if (!g_worker.hasPending)
                continue;
            path = g_worker.pendingPath;
            id = g_worker.pendingId;
            g_worker.hasPending = false;
        }

        const ApplyReport report = ApplyOnWorker(manager, path);

        {
            std::lock_guard<std::mutex> lock(g_worker.lock);
            g_worker.lastReport = report;
            g_worker.completedId = id;
        }
        SetEvent(g_worker.doneEvent);
    }

    if (manager != nullptr)
        manager->Release();
    if (SUCCEEDED(com))
        CoUninitialize();

    LOG_INFO(L"[WindowsTheme] STA worker exiting.");
    return 0;
}

bool TestHookDisablesCom()
{
    wchar_t value[8] = {};
    const DWORD length = GetEnvironmentVariableW(L"LS_DISABLE_THEME_COM", value, ARRAYSIZE(value));
    return length > 0 && value[0] != L'0' && value[0] != L'\0';
}

} // namespace

const wchar_t* ApplyStatusName(ApplyStatus status)
{
    switch (status)
    {
    case ApplyStatus::Ok:
        return L"ok";
    case ApplyStatus::AlreadyCurrent:
        return L"already-current";
    case ApplyStatus::FileMissing:
        return L"file-missing";
    case ApplyStatus::Disabled:
        return L"disabled";
    case ApplyStatus::TimedOut:
        return L"timed-out";
    case ApplyStatus::Failed:
        break;
    }
    return L"failed";
}

bool EnsureStaWorker()
{
    std::lock_guard<std::mutex> startLock(g_startLock);

    if (g_shutDown)
        return false;

    if (!g_worker.attempted.load())
    {
        g_worker.attempted = true;

        if (TestHookDisablesCom())
        {
            g_worker.initHresult = E_NOTIMPL;
            g_worker.initDetail = L"disabled by LS_DISABLE_THEME_COM";
            LOG_WARN(L"[WindowsTheme] COM path disabled by the LS_DISABLE_THEME_COM test hook.");
            return false;
        }

        g_worker.jobEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        g_worker.doneEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        g_worker.quitEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        g_worker.readyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        if (g_worker.jobEvent == nullptr || g_worker.doneEvent == nullptr ||
            g_worker.quitEvent == nullptr || g_worker.readyEvent == nullptr)
        {
            g_worker.initHresult = HRESULT_FROM_WIN32(GetLastError());
            g_worker.initDetail = L"could not create the worker events";
            LOG_ERROR(L"[WindowsTheme] " << g_worker.initDetail << L".");
            CloseHandles(g_worker);
            return false;
        }

        g_worker.thread = CreateThread(nullptr, 0, &WorkerEntry, nullptr, 0, nullptr);
        if (g_worker.thread == nullptr)
        {
            g_worker.initHresult = HRESULT_FROM_WIN32(GetLastError());
            g_worker.initDetail = L"could not create the STA worker thread";
            LOG_ERROR(L"[WindowsTheme] " << g_worker.initDetail << L".");
            CloseHandles(g_worker);
            return false;
        }

        // CoCreateInstance on a cold start is fast; 15 s only matters when
        // something pathological is holding the theme service.
        if (WaitForSingleObject(g_worker.readyEvent, 15000) != WAIT_OBJECT_0)
        {
            g_worker.initHresult = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            g_worker.initDetail = L"the STA worker did not become ready in time";
            LOG_ERROR(L"[WindowsTheme] " << g_worker.initDetail << L".");
        }
    }

    return g_worker.thread != nullptr && SUCCEEDED(g_worker.initHresult);
}

bool ProbeThemeManager()
{
    const bool ready = EnsureStaWorker();
    if (!ready)
    {
        LOG_ERROR(L"[WindowsTheme] Probe failed: " << g_worker.initDetail
                                                   << L" (hr=0x" << std::hex
                                                   << static_cast<unsigned long>(g_worker.initHresult) << std::dec << L").");
        return false;
    }

    LOG_INFO(L"[WindowsTheme] Probe ok: " << g_worker.themeCount << L" themes visible.");
    return true;
}

int ThemeManagerThemeCount()
{
    return g_worker.themeCount;
}

ApplyReport ApplyThemeFile(const std::wstring& absolutePath, DWORD timeoutMs)
{
    ApplyReport report;
    report.requestedPath = absolutePath;

    if (!EnsureStaWorker())
    {
        report.status = ApplyStatus::Disabled;
        report.hresult = g_worker.initHresult;
        report.detail = g_worker.initDetail;
        report.observed = theme::ReadCurrentIdentity();
        return report;
    }

    unsigned long long id = 0;
    {
        std::lock_guard<std::mutex> lock(g_worker.lock);
        id = ++g_worker.nextId;
        g_worker.pendingId = id;
        g_worker.pendingPath = absolutePath;
        g_worker.hasPending = true;
    }
    SetEvent(g_worker.jobEvent);

    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (;;)
    {
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline)
        {
            report.status = ApplyStatus::TimedOut;
            report.detail = L"the STA worker did not finish in time";
            report.observed = theme::ReadCurrentIdentity();
            LOG_WARN(L"[WindowsTheme] Timed out after " << timeoutMs << L" ms applying \"" << absolutePath << L"\".");
            return report;
        }

        const DWORD signaled = WaitForSingleObject(g_worker.doneEvent, static_cast<DWORD>(deadline - now));
        if (signaled == WAIT_TIMEOUT)
            continue;

        std::lock_guard<std::mutex> lock(g_worker.lock);
        if (g_worker.completedId == id)
            return g_worker.lastReport;
        if (g_worker.completedId > id)
        {
            // A newer request overtook this one; its result is not ours to use.
            report.status = ApplyStatus::TimedOut;
            report.detail = L"superseded by a newer request";
            report.observed = theme::ReadCurrentIdentity();
            return report;
        }
        // Somebody else's completion woke us up; keep waiting for ours.
    }
}

void ShutdownStaWorker()
{
    std::lock_guard<std::mutex> startLock(g_startLock);

    g_shutDown = true;

    if (g_worker.thread == nullptr)
    {
        CloseHandles(g_worker);
        return;
    }

    SetEvent(g_worker.quitEvent);

    if (WaitForSingleObject(g_worker.thread, 10000) == WAIT_OBJECT_0)
    {
        CloseHandle(g_worker.thread);
        g_worker.thread = nullptr;
        CloseHandles(g_worker);
        LOG_INFO(L"[WindowsTheme] STA worker stopped.");
        return;
    }

    // The worker is still inside OpenTheme. Leaking it is the lesser evil: the
    // process is on its way out, and closing the handles underneath a running
    // thread would turn a slow exit into a crash.
    LOG_WARN(L"[WindowsTheme] STA worker did not stop in 10 s; leaking the thread and exiting anyway.");
    g_worker.thread = nullptr;
}

} // namespace ls
