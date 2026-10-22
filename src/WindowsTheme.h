// LightSwitch Standalone - Windows theme packs via IThemeManager2 (declarations).
//
// Applying a .theme file is what the Settings > Personalisation page does, and
// the only supported way to do it programmatically is the COM class that
// themeui.dll registers as "Windows Theme Manager 2 API":
//
//   CLSID {9324DA94-50EC-4A14-A770-E90CA03E7C8F}
//   IID   {c1e8c83e-845d-4d95-81db-e283fdffc000}
//
// Double-clicking a .theme file does not work on current builds: the shell
// association passes /Action:OpenTheme, which desk.cpl does not understand (it
// only parses /Action:OpenMSTheme) and the launch silently does nothing.
//
// Threading: the interface is apartment-threaded and the apply call blocks for
// seconds while the wallpaper is swapped. Everything here therefore runs on one
// private STA thread owned by this module, and ApplyThemeFile() is the only door
// in. Never call it from the UI thread.
#pragma once

#include <windows.h>

#include <string>

#include "ThemeCatalog.h"

namespace ls
{

enum class ApplyStatus
{
    Ok,              // COM accepted it and CurrentTheme reads back as requested
    AlreadyCurrent,  // the theme was already active; COM was never touched
    FileMissing,     // the .theme file does not exist; COM was never touched
    Disabled,        // no COM: disabled by policy, by the test hook, or by a failed probe
    TimedOut,        // the STA worker did not finish within the caller's budget
    Failed           // the HRESULT was a failure, or the result never showed up
};

struct ApplyReport
{
    ApplyStatus status = ApplyStatus::Failed;
    long hresult = 0;
    std::wstring requestedPath;
    theme::ThemeIdentity observed;
    std::wstring detail;
};

const wchar_t* ApplyStatusName(ApplyStatus status);

// Starts the STA worker if needed and reports whether the theme manager could
// be created and initialised. Sets the process-wide "COM is unusable" flag when
// it could not, so later calls fail fast instead of retrying forever.
bool EnsureStaWorker();

// Diagnostic used by --theme-check: creates the manager, initialises it and
// sanity-checks the theme count. The count is the cheapest proof that the
// vtable layout this file declares matches the one themeui.dll exports - a
// single slot of drift tends to produce an absurd number or an access
// violation, both of which land here.
bool ProbeThemeManager();

// Theme count reported by the manager at probe time, 0 when COM is unusable.
int ThemeManagerThemeCount();

// Applies one .theme file. Blocks the calling thread for up to timeoutMs, so
// keep it on the scheduler worker (or a one-shot CLI process). timeoutMs must
// stay below Scheduler::Stop()'s 15 s grace period.
ApplyReport ApplyThemeFile(const std::wstring& absolutePath, DWORD timeoutMs = 10000);

// Stops the STA worker. Called from App::Shutdown after the scheduler stopped,
// and from the one-shot CLI path before returning.
void ShutdownStaWorker();

} // namespace ls
