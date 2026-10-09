// LightSwitch Standalone - "start with Windows" management.
//
// Uses HKCU\Software\Microsoft\Windows\CurrentVersion\Run:
//   * no administrator rights required,
//   * shows up in Task Manager's Startup tab,
//   * consistent with "uninstall = delete the folder".
//
// A scheduled task (what PowerToys itself uses) is deliberately avoided: it can
// only be created with elevation and would not survive a portable relocation.
#pragma once

#include <string>

namespace ls
{

class AutoStart
{
public:
    static bool IsEnabled();

    // Writes the current executable path into HKCU\...\Run.
    static bool Enable();

    static bool Disable();

    // Applies the desired state; returns true when the registry matches it.
    static bool Apply(bool enabled);

    // Human readable summary used by the settings dialog.
    static std::wstring StatusText();

    // HKCU\Software\Microsoft\Windows\CurrentVersion\Run
    static std::wstring RegistryKeyPath();

private:
    static constexpr const wchar_t* kValueName = L"LightSwitchStandalone";
};

} // namespace ls
