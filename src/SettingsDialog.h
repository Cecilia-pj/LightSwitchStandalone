// LightSwitch Standalone - settings dialog.
//
// A plain Win32 modal dialog (no WPF/WinUI, no Windows App SDK) so the whole
// component stays at "Windows SDK + standard library".
//
// Two controls deserve a note:
//   * SysDateTimePick32 for the time pickers - it is part of comctl32 and
//     requires ICC_DATE_CLASSES to be initialised.
//   * msctls_hotkey32 for the shortcut. That control cannot represent the Win
//     modifier at all, which is why there is a separate "Include Win key"
//     checkbox next to it. The combined value is stored in the same
//     {"win","ctrl","alt","shift","code"} shape PowerToys uses.
#pragma once

#include <windows.h>

namespace ls
{

// Implemented by App so the dialog can trigger side effects.
struct ISettingsHost
{
    virtual ~ISettingsHost() = default;

    // The user pressed "Toggle now".
    virtual void OnToggleNow() = 0;

    // Ask Windows Explorer to restart so shell surfaces pick up the theme.
    virtual void OnRestartExplorer() = 0;

    // Settings were written to disk.
    virtual void OnSettingsSaved() = 0;
};

// Shows the dialog modally. Returns true when the user pressed Save.
bool ShowSettingsDialog(HWND owner, ISettingsHost& host);

} // namespace ls
